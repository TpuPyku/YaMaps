#include "yamaps.h"

// Map downloader: one worker thread, "latest request wins".
// While a download is running new requests overwrite the pending one,
// so fast button presses never queue up more than one extra download.

static const int   kConnectTimeoutSec = 10;
static const int   kReadTimeoutSec    = 15;
static const int   kMaxResponse       = 2 * 1024 * 1024;

static CRITICAL_SECTION s_cs;
static HANDLE           s_wake;
static HANDLE           s_thread;
static HWND             s_notify;
static volatile bool    s_stop;
static MapRequest       s_pending;
static bool             s_hasPending;
static DWORD            s_seq;
static unsigned long    s_hostAddr;     // cached host address, network order

static const char* kLayerParam[LAYER_COUNT] = { "map", "sat", "sat,skl" };

const wchar_t* NetErrorText(int err)
{
    switch (err) {
    case NET_OK:      return L"OK";
    case NET_DNS:     return L"нет сети (DNS)";
    case NET_CONNECT: return L"нет соединения";
    case NET_TIMEOUT: return L"таймаут";
    case NET_HTTP:    return L"ошибка сервера";
    case NET_DECODE:  return L"битая картинка";
    case NET_MEMORY:  return L"мало памяти";
    }
    return L"?";
}

static bool WaitSocket(SOCKET s, bool write, int sec)
{
    fd_set set, err;
    FD_ZERO(&set);
    FD_ZERO(&err);
    FD_SET(s, &set);
    FD_SET(s, &err);
    timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    int r = write ? select(0, NULL, &set, &err, &tv) : select(0, &set, NULL, &err, &tv);
    return r > 0 && FD_ISSET(s, &set);
}

// Plain HTTP/1.0 GET. Returns body in *body (malloc'ed). HTTP/1.0 + Connection: close
// guarantees no chunked encoding and the server closes the socket at the end.
static int HttpGet(const char* host, const char* path, unsigned char** body, int* bodyLen, int* status)
{
    *status = 0;
    if (!s_hostAddr) {
        hostent* he = gethostbyname(host);
        if (!he) {
            Log("net: gethostbyname(%s) failed (%d)", host, WSAGetLastError());
            return NET_DNS;
        }
        memcpy(&s_hostAddr, he->h_addr_list[0], 4);
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return NET_CONNECT;

    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    sa.sin_addr.s_addr = s_hostAddr;

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        Log("net: connect failed (%d)", WSAGetLastError());
        closesocket(s);
        s_hostAddr = 0;
        return NET_CONNECT;
    }
    if (!WaitSocket(s, true, kConnectTimeoutSec)) {
        Log("net: connect timeout");
        closesocket(s);
        s_hostAddr = 0;
        return NET_CONNECT;
    }
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);

    char req[512];
    int reqLen = _snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: YaMapsCE/1.0\r\nConnection: close\r\n\r\n",
        path, host);
    if (send(s, req, reqLen, 0) != reqLen) {
        closesocket(s);
        return NET_CONNECT;
    }

    int cap = 128 * 1024, len = 0;
    char* buf = (char*)malloc(cap + 1);
    if (!buf) {
        closesocket(s);
        return NET_MEMORY;
    }
    int result = NET_OK;
    for (;;) {
        if (s_stop || !WaitSocket(s, false, kReadTimeoutSec)) {
            result = NET_TIMEOUT;
            break;
        }
        if (len == cap) {
            if (cap >= kMaxResponse) {
                result = NET_MEMORY;
                break;
            }
            cap *= 2;
            char* nbuf = (char*)realloc(buf, cap + 1);
            if (!nbuf) {
                result = NET_MEMORY;
                break;
            }
            buf = nbuf;
        }
        int n = recv(s, buf + len, cap - len, 0);
        if (n < 0) {
            result = NET_TIMEOUT;
            break;
        }
        if (n == 0)
            break;
        len += n;
    }
    closesocket(s);
    if (result != NET_OK) {
        free(buf);
        return result;
    }
    buf[len] = 0;

    // status line + headers
    char* hdrEnd = strstr(buf, "\r\n\r\n");
    if (strncmp(buf, "HTTP/1.", 7) != 0 || !hdrEnd || len < 12) {
        free(buf);
        return NET_HTTP;
    }
    *status = atoi(buf + 9);
    *hdrEnd = 0;
    char* bodyStart = hdrEnd + 4;
    int blen = len - (int)(bodyStart - buf);

    int contentLength = -1;
    for (char* h = buf; (h = strstr(h, "\r\n")) != NULL; ) {
        h += 2;
        if (_strnicmp(h, "Content-Length:", 15) == 0)
            contentLength = atoi(h + 15);
    }
    if (*status != 200) {
        Log("net: HTTP %d: %.200s", *status, bodyStart);
        free(buf);
        return NET_HTTP;
    }
    if (contentLength >= 0 && blen < contentLength) {
        Log("net: truncated body %d of %d", blen, contentLength);
        free(buf);
        return NET_TIMEOUT;
    }

    memmove(buf, bodyStart, blen);
    *body = (unsigned char*)buf;
    *bodyLen = blen;
    return NET_OK;
}

void GridCenter(const MapRequest& r, double* lon, double* lat)
{
    WorldToGeo((double)r.gx * GRID_X, (double)r.gy * GRID_Y, r.z, lon, lat);
}

// Cache\<z>\<bx>_<by>\<layer>_<traffic>_<gx>_<gy>.img
void CachePath(wchar_t* out, const MapRequest& r)
{
    wchar_t name[96];
    _snwprintf(name, 96, L"Cache\\%d\\%d_%d\\%d_%d_%d_%d.img", r.z, r.gx / CACHE_BLOCK, r.gy / CACHE_BLOCK,
               r.layer, r.traffic, r.gx, r.gy);
    name[95] = 0;
    PathInDir(out, name);
}

// Raw response goes to the cache via a temp file, so a half-written file is never read.
static void SaveToCache(const MapRequest& r, const unsigned char* data, int len)
{
    wchar_t name[64], dir[MAX_PATH], path[MAX_PATH], tmp[MAX_PATH];
    _snwprintf(name, 64, L"Cache\\%d", r.z);
    PathInDir(dir, name);
    CreateDirectory(dir, NULL);
    _snwprintf(name, 64, L"Cache\\%d\\%d_%d", r.z, r.gx / CACHE_BLOCK, r.gy / CACHE_BLOCK);
    PathInDir(dir, name);
    CreateDirectory(dir, NULL);

    CachePath(path, r);
    PathInDir(tmp, L"Cache\\download.tmp");
    HANDLE h = CreateFile(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD w = 0;
    BOOL ok = WriteFile(h, data, len, &w, NULL) && (int)w == len;
    StampFile(h, g_today);   // the age check (cache_days) counts from this date
    CloseHandle(h);

    int oldBytes = 0;
    WIN32_FIND_DATA fd;
    HANDLE f = FindFirstFile(path, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        oldBytes = (int)fd.nFileSizeLow;
        FindClose(f);
    }
    DeleteFile(path);
    if (!ok || !MoveFile(tmp, path)) {
        Log("cache: cannot save (%lu)", GetLastError());
        DeleteFile(tmp);
        CacheCountSaved(0, oldBytes);
        return;
    }
    CacheCountSaved(len, oldBytes);
}

static void Fetch(const MapRequest& r, MapResult* res)
{
    double lon, lat;
    GridCenter(r, &lon, &lat);
    char path[256];
    _snprintf(path, sizeof(path), "/1.x/?ll=%.6f,%.6f&z=%d&size=%d,%d&l=%s%s&lang=ru_RU",
        lon, lat, r.z, REQ_W, REQ_H, kLayerParam[r.layer], r.traffic ? ",trf,trfe" : "");

    DWORD t0 = GetTickCount();
    unsigned char* body = NULL;
    int bodyLen = 0;
    res->err = HttpGet(g_cfg.host, path, &body, &bodyLen, &res->httpStatus);
    if (res->err == NET_OK) {
        res->bytes = bodyLen;
        if (ImageFromMemory(body, bodyLen, &res->img))
            SaveToCache(r, body, bodyLen);
        else
            res->err = NET_DECODE;
        free(body);
    }
    Log("net: %s -> %d (%d bytes, %lu ms)", path, res->err, res->bytes, GetTickCount() - t0);
}

static DWORD WINAPI NetThread(LPVOID)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    while (!s_stop) {
        WaitForSingleObject(s_wake, INFINITE);
        for (;;) {
            if (s_stop)
                break;
            EnterCriticalSection(&s_cs);
            bool have = s_hasPending;
            MapRequest r = s_pending;
            DWORD seq = s_seq;
            s_hasPending = false;
            LeaveCriticalSection(&s_cs);
            if (!have)
                break;

            MapResult* res = (MapResult*)calloc(1, sizeof(MapResult));
            if (!res)
                break;
            res->req = r;
            res->seq = seq;
            Fetch(r, res);
            if (s_stop || !PostMessage(s_notify, WM_APP_MAP, 0, (LPARAM)res)) {
                ImageFree(&res->img);
                free(res);
            }
        }
    }
    WSACleanup();
    return 0;
}

void NetStart(HWND notify)
{
    InitializeCriticalSection(&s_cs);
    s_notify = notify;
    s_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
    s_thread = CreateThread(NULL, 0, NetThread, NULL, 0, NULL);
}

DWORD NetRequest(const MapRequest& r)
{
    EnterCriticalSection(&s_cs);
    s_pending = r;
    s_hasPending = true;
    DWORD seq = ++s_seq;
    LeaveCriticalSection(&s_cs);
    SetEvent(s_wake);
    return seq;
}

void NetStop()
{
    s_stop = true;
    SetEvent(s_wake);
    // The worker may sit in select() for a while; don't block shutdown on it.
    WaitForSingleObject(s_thread, 1000);
}
