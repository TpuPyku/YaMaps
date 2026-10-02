#include "yamaps.h"

// ---------------------------------------------------------------- UI layout
// Button positions match the old SystemInformation layout (Main.ini), 800x480 screen.

enum { B_SETTINGS, B_EXIT, B_TRF, B_LAYERS, B_GPS, B_LEFT, B_RIGHT, B_UP, B_DOWN, B_MINUS, B_PLUS, B_COUNT };

static const int kBtnSize = 50;

struct Button {
    int            x, y;
    const wchar_t* name[3];   // image base names per state (toggle buttons have 2-3)
    Image          img[3][2]; // [state][pressed]
};

static Button s_btn[B_COUNT] = {
    { 610,  28, { L"Bset" } },
    { 740,  28, { L"Bexit" } },
    { 610,  93, { L"Btrf_on", L"Btrf_off" } },
    { 675,  93, { L"Blayers" } },
    { 740,  93, { L"Bgps_on", L"Bgps_off", L"Bgps_none" } },
    { 615, 290, { L"Bleft" } },
    { 735, 290, { L"Bright" } },
    { 675, 230, { L"Bup" } },
    { 675, 350, { L"Bdown" } },
    { 640, 420, { L"Bminus" } },
    { 710, 420, { L"Bplus" } },
};

static const wchar_t* kWeekDays[7] = { L"Вс", L"Пн", L"Вт", L"Ср", L"Чт", L"Пт", L"Сб" };
static const wchar_t* kLayerTitle[LAYER_COUNT] = { L"схема", L"спутник", L"гибрид" };

static const COLORREF kText    = RGB(255, 255, 255);
static const COLORREF kTextDim = RGB(170, 170, 170);
static const COLORREF kTextErr = RGB(255, 120, 120);

enum { TIMER_TICK = 1, TIMER_USER = 2 };
static const UINT kUserDelayMs = 900;    // wait for the user to stop pressing buttons

// GPS button modes (g_cfg.follow)
enum { FOLLOW_OFF, FOLLOW_NORTH, FOLLOW_HEADING };
static const double kCarAhead       = 0.3;  // heading-up: view center this part of map height ahead of the car
static const double kHeadingMinSpeed = 5;   // km/h: below it the course is noise, keep the last heading
static const int    kCanvas         = 768;  // heading-up: unrotated canvas >= map diagonal (750)
static const int    kMaxKeys        = 12;   // map images one view can need

// Small ring of map keys: "not on disk" / "nothing to build a fallback from" memos,
// so the card is not searched again every second.
struct KeyRing {
    MapRequest k[16];
    int        n, next;
};

static bool RingHas(const KeyRing& r, const MapRequest& k)
{
    for (int i = 0; i < r.n; i++)
        if (SameKey(r.k[i], k))
            return true;
    return false;
}

static void RingAdd(KeyRing& r, const MapRequest& k)
{
    r.k[r.next] = k;
    r.next = (r.next + 1) % 16;
    if (r.n < 16)
        r.n++;
}

// ---------------------------------------------------------------- state

static HWND       s_wnd;
static HWND       s_taskbar;
static HBITMAP    s_back;
static int        s_cw, s_ch;
static Image      s_bg;
static HFONT      s_fontBig, s_fontSmall;

static bool       s_inflight;
static DWORD      s_inflightSeq, s_inflightTick;
static MapRequest s_inflightReq;
static int        s_lastErr, s_lastHttp;
static DWORD      s_lastErrTick;
static bool       s_limitHit;
static KeyRing    s_missKeys;           // keys not found on disk
static KeyRing    s_noFallbackKeys;     // keys with nothing cached around them
static bool       s_userPending;        // user is still pressing buttons (TIMER_USER running)
static double     s_heading;            // heading-up: map rotation, degrees
static Image      s_canvas, s_rotated;  // heading-up buffers
static unsigned char* s_canvasBits;
static unsigned char* s_rotatedBits;
static DWORD      s_lastSaveTick;

static int        s_pressed = -1;
static bool       s_pressedInside;
static bool       s_dragging, s_dragMoved;
static POINT      s_dragStart;
static double     s_dragWx, s_dragWy;

static GpsState   s_gps;
static bool       s_gpsRunning;

// Settings screen (GPS port, cache size), drawn over the map
static bool       s_settings;
static int        s_setPort;        // 1..9 -> COMn:
static int        s_setBaud;        // index in kSetBauds
static int        s_setCache;       // index in kSetCacheMb
static int        s_setPressed = -1;
static const DWORD kSetBauds[] = { 0, 4800, 9600, 19200, 38400, 57600, 115200 };   // 0 = leave as is
static const int   kSetBaudCount = sizeof(kSetBauds) / sizeof(kSetBauds[0]);
static const int   kSetCacheMb[] = { 250, 500, 1000, 2000 };
static const int   kSetCacheCount = sizeof(kSetCacheMb) / sizeof(kSetCacheMb[0]);

enum { S_PORT_PREV, S_PORT_NEXT, S_BAUD_PREV, S_BAUD_NEXT, S_CACHE_PREV, S_CACHE_NEXT,
       S_APPLY, S_SCAN, S_CLOSE, S_COUNT };
static const RECT kSetCtl[S_COUNT] = {
    { 180,  75, 230, 125 }, { 410,  75, 460, 125 },
    { 180, 133, 230, 183 }, { 410, 133, 460, 183 },
    { 180, 191, 230, 241 }, { 410, 191, 460, 241 },
    {  22, 251, 192, 296 }, { 212, 251, 382, 296 }, { 402, 251, 572, 296 },
};
static const wchar_t* kSetCaption[S_COUNT] = { L"<", L">", L"<", L">", L"<", L">",
                                               L"Применить", L"Поиск GPS", L"Закрыть" };

// ---------------------------------------------------------------- view & grid

static void ViewWorld(double* x, double* y)
{
    GeoToWorld(g_cfg.lon, g_cfg.lat, g_cfg.z, x, y);
}

static MapRequest KeyAt(double wx, double wy)
{
    MapRequest r;
    r.z = g_cfg.z;
    r.gx = (int)floor(wx / GRID_X + 0.5);
    r.gy = (int)floor(wy / GRID_Y + 0.5);
    r.layer = g_cfg.layer;
    r.traffic = g_cfg.traffic;
    return r;
}

static MapRequest CurrentKey()
{
    double x, y;
    ViewWorld(&x, &y);
    return KeyAt(x, y);
}

// Moves the view center to the nearest grid point.
static void SnapView()
{
    MapRequest k = CurrentKey();
    GridCenter(k, &g_cfg.lon, &g_cfg.lat);
}

// Map rotation for drawing: heading-up mode only.
static double ViewAngle()
{
    return g_cfg.follow == FOLLOW_HEADING ? s_heading : 0;
}

// North-up: the car is in the center. Heading-up: the car is near the bottom edge,
// the view center is ahead of it along the course.
static void FollowGps()
{
    if (g_cfg.follow == FOLLOW_OFF || !s_gps.valid)
        return;
    if (s_gps.speedKmh > kHeadingMinSpeed)
        s_heading = s_gps.course;
    double x, y;
    GeoToWorld(s_gps.lon, s_gps.lat, g_cfg.z, &x, &y);
    if (g_cfg.follow == FOLLOW_HEADING) {
        double a = s_heading * M_PI / 180.0, ahead = g_cfg.mapH * kCarAhead;
        x += sin(a) * ahead;
        y -= cos(a) * ahead;
    }
    WorldToGeo(x, y, g_cfg.z, &g_cfg.lon, &g_cfg.lat);
}

// Map images the view needs, nearest to the car (or view center) first.
// Static view: the one grid image it is snapped to. Following GPS the view is not aligned
// to the grid, so it is covered by whole-image tiles (even grid points: they don't overlap)
// that touch the map window, rotated in heading-up mode.
static int NeededKeys(MapRequest* out, int max)
{
    if (g_cfg.follow == FOLLOW_OFF) {
        out[0] = CurrentKey();
        return 1;
    }
    double vx, vy;
    ViewWorld(&vx, &vy);
    double a = ViewAngle() * M_PI / 180.0, c = cos(a), s = sin(a);
    double hw = g_cfg.mapW / 2.0, hh = g_cfg.mapH / 2.0;
    // world bounding box of the (rotated) window
    double ex = hw * fabs(c) + hh * fabs(s), ey = hw * fabs(s) + hh * fabs(c);
    double fx = vx, fy = vy;
    if (s_gps.fixTick)
        GeoToWorld(s_gps.lon, s_gps.lat, g_cfg.z, &fx, &fy);

    double dist[kMaxKeys];
    int n = 0;
    int i0 = (int)floor((vx - ex) / REQ_W), i1 = (int)ceil((vx + ex) / REQ_W);
    int j0 = (int)floor((vy - ey) / REQ_H), j1 = (int)ceil((vy + ey) / REQ_H);
    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            double tx = (double)i * REQ_W, ty = (double)j * REQ_H;
            double dx = tx - vx, dy = ty - vy;
            // separating axes: world x/y, then the window's own axes
            if (fabs(dx) >= ex + REQ_W / 2.0 || fabs(dy) >= ey + REQ_H / 2.0)
                continue;
            if (fabs(dx * c + dy * s) >= hw + REQ_W / 2.0 * fabs(c) + REQ_H / 2.0 * fabs(s))
                continue;
            if (fabs(-dx * s + dy * c) >= hh + REQ_W / 2.0 * fabs(s) + REQ_H / 2.0 * fabs(c))
                continue;
            if (n == max)
                continue;
            MapRequest k = { g_cfg.z, 2 * i, 2 * j, g_cfg.layer, g_cfg.traffic };
            double d = (tx - fx) * (tx - fx) + (ty - fy) * (ty - fy);
            int p = n++;
            for (; p > 0 && dist[p - 1] > d; p--) {
                out[p] = out[p - 1];
                dist[p] = dist[p - 1];
            }
            out[p] = k;
            dist[p] = d;
        }
    }
    return n;
}

// ---------------------------------------------------------------- request budget

static int Today()
{
    if (s_gps.date)
        return s_gps.date;
    SYSTEMTIME t;
    GetLocalTime(&t);
    if (t.wYear < 2024)
        return 0;   // clock was reset, can't trust it
    return t.wYear * 10000 + t.wMonth * 100 + t.wDay;
}

static void CheckDay()
{
    int d = Today();
    if (d > g_cfg.reqDay) {   // never go back: a wrong clock must not reset the counter
        Log("budget: new day %d (yesterday %d requests)", d, g_cfg.reqCount);
        g_cfg.reqDay = d;
        g_cfg.reqCount = 0;
        ConfigSave();
    }
}

// ---------------------------------------------------------------- map requests

enum { REQ_AUTO, REQ_USER, REQ_FORCE };

static bool IsFresh(const CacheEntry* e)
{
    if (!e)
        return false;
    if (e->fallback)
        return false;
    if (!e->req.traffic)
        return true;   // plain map/satellite never expires
    return e->fetchTick && GetTickCount() - e->fetchTick < (DWORD)g_cfg.trafficTtlMin * 60000;
}

// Downloads a map image of the view if really needed. One download at a time: the next
// image is requested when the answer comes (OnMapResult).
// Returns true if the image is fresh in the cache or will be downloaded.
//  REQ_AUTO  - timer/GPS: uses the budget minus the reserve, waits after errors;
//  REQ_USER  - user stopped on a view: full budget;
//  REQ_FORCE - tap on the map: refresh even a fresh image (but not more than once a minute).
static bool Request(int mode, const MapRequest& k, CacheEntry* e)
{
    DWORD now = GetTickCount();
    if (mode == REQ_FORCE) {
        if (e && e->fetchTick && now - e->fetchTick < 60000)
            return true;
    } else if (IsFresh(e)) {
        return true;
    }

    if (s_inflight && now - s_inflightTick > 40000)
        s_inflight = false;   // answer lost
    if (s_inflight)
        return true;
    if (mode == REQ_AUTO && s_lastErr && now - s_lastErrTick < 30000)
        return false;

    CheckDay();
    int limit = g_cfg.dailyLimit - (mode == REQ_AUTO ? g_cfg.autoReserve : 0);
    if (g_cfg.reqCount >= limit) {
        if (!s_limitHit)
            Log("budget: limit reached (%d/%d), mode %d", g_cfg.reqCount, g_cfg.dailyLimit, mode);
        s_limitHit = true;
        return false;
    }
    s_limitHit = false;

    s_inflightSeq = NetRequest(k);
    s_inflight = true;
    s_inflightReq = k;
    s_inflightTick = now;
    g_cfg.reqCount++;
    ConfigSave();   // the counter must survive a sudden power loss
    s_lastSaveTick = now;
    return true;
}

// No image for the view and none coming (limit, no network): compose one from cached
// images of the same layer. Priority: same zoom with the other traffic state, then more
// detailed levels, then coarser ones. Low priority is drawn first, so the most detailed
// data ends up on top and coarse images fill the holes.
static const int kFallbackLevels[] = { 0, 1, 2, -1, -2, -3, -4, -5, -6 };
static const int kFallbackMaxLoads = 12;   // each decode takes ~0.1-0.3 s on the unit
static const int kFallbackMaxDetailed = 9; // leave loads for a coarse background under the holes

static bool FindFallbackTile(MapRequest* t)
{
    wchar_t path[MAX_PATH];
    CachePath(path, *t);
    if (GetFileAttributes(path) != 0xFFFFFFFF)
        return true;
    t->traffic = !t->traffic;
    CachePath(path, *t);
    return GetFileAttributes(path) != 0xFFFFFFFF;
}

static void BuildFallback(const MapRequest& k)
{
    MapRequest tiles[kFallbackMaxLoads];
    int n = 0;
    for (int li = 0; li < (int)(sizeof(kFallbackLevels) / sizeof(kFallbackLevels[0])) && n < kFallbackMaxLoads; li++) {
        int dz = kFallbackLevels[li];
        int z = k.z + dz;
        if (z < 2 || z > 19)
            continue;
        if (dz == 0) {
            MapRequest t = k;
            t.traffic = !k.traffic;
            if (FindFallbackTile(&t) && t.traffic != k.traffic)
                tiles[n++] = t;
            continue;
        }
        // grid images at level z that overlap the view
        double f = pow(2.0, dz);
        double cx = k.gx * GRID_X * f, cy = k.gy * GRID_Y * f;
        double hw = (REQ_W * f + REQ_W) / 2, hh = (REQ_H * f + REQ_H) / 2;
        int gx0 = (int)ceil((cx - hw) / GRID_X), gx1 = (int)floor((cx + hw) / GRID_X);
        int gy0 = (int)ceil((cy - hh) / GRID_Y), gy1 = (int)floor((cy + hh) / GRID_Y);
        int cap = dz > 0 ? kFallbackMaxDetailed : kFallbackMaxLoads;
        // even grid points first: they don't overlap each other, fewer loads for the same area
        for (int pass = 0; pass < 2; pass++)
            for (int gy = gy0; gy <= gy1 && n < cap; gy++)
                for (int gx = gx0; gx <= gx1 && n < cap; gx++) {
                    bool even = (gx % 2 == 0) && (gy % 2 == 0);
                    if (even != (pass == 0))
                        continue;
                    MapRequest t = { z, gx, gy, k.layer, k.traffic };
                    if (FindFallbackTile(&t))
                        tiles[n++] = t;
                }
    }
    if (!n)
        return;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = REQ_W;
    bi.bmiHeader.biHeight = REQ_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits;
    HDC screen = GetDC(NULL);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dst = CreateCompatibleDC(screen);
    HDC src = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!bmp) {
        DeleteDC(dst);
        DeleteDC(src);
        return;
    }
    HGDIOBJ oldDst = SelectObject(dst, bmp);
    RECT all = { 0, 0, REQ_W, REQ_H };
    HBRUSH bg = CreateSolidBrush(RGB(228, 226, 222));
    FillRect(dst, &all, bg);
    DeleteObject(bg);

    int drawn = 0;
    for (int i = n - 1; i >= 0; i--) {
        wchar_t path[MAX_PATH];
        CachePath(path, tiles[i]);
        Image im;
        if (!ImageFromFile(path, &im))
            continue;
        // tile center at its own zoom -> view pixels
        double f = pow(2.0, tiles[i].z - k.z);
        double x = REQ_W / 2.0 + (tiles[i].gx * GRID_X - k.gx * GRID_X * f) / f;
        double y = REQ_H / 2.0 + (tiles[i].gy * GRID_Y - k.gy * GRID_Y * f) / f;
        int w = (int)floor(im.w / f + 0.5), h = (int)floor(im.h / f + 0.5);
        HGDIOBJ oldSrc = SelectObject(src, im.bmp);
        StretchBlt(dst, (int)floor(x - w / 2.0 + 0.5), (int)floor(y - h / 2.0 + 0.5), w, h,
                   src, 0, 0, im.w, im.h, SRCCOPY);
        SelectObject(src, oldSrc);
        ImageFree(&im);
        drawn++;
    }
    SelectObject(dst, oldDst);
    DeleteDC(dst);
    DeleteDC(src);

    Image img = { bmp, REQ_W, REQ_H };
    CacheEntry* e = CacheAdd(k, img, 0);
    e->fallback = 1;
    Log("fallback: z%d from %d cached images", k.z, drawn);
}

// Brings every image of the view into memory (disk, download or fallback), nearest first.
// Disk decodes and fallback builds are limited per call: each costs ~0.1-0.3 s on the unit.
static void Ensure(int mode)
{
    MapRequest keys[kMaxKeys];
    int n = NeededKeys(keys, kMaxKeys);
    int diskLoads = 0;
    bool fallbackBuilt = false;
    for (int i = 0; i < n; i++) {
        const MapRequest& k = keys[i];
        CacheEntry* e = CacheFind(k, false);
        if (!e && diskLoads < 2 && !RingHas(s_missKeys, k)) {
            diskLoads++;
            e = CacheFind(k, true);
            if (!e)
                RingAdd(s_missKeys, k);
        }
        if (!Request(i == 0 ? mode : (mode == REQ_FORCE ? REQ_AUTO : mode), k, e) && !e &&
            !s_userPending && !fallbackBuilt && !RingHas(s_noFallbackKeys, k)) {
            BuildFallback(k);
            fallbackBuilt = true;
            if (!CacheFind(k, false))
                RingAdd(s_noFallbackKeys, k);
        }
    }
}

static void OnMapResult(MapResult* res)
{
    if (res->seq == s_inflightSeq)
        s_inflight = false;
    if (res->err == NET_OK) {
        CacheAdd(res->req, res->img, GetTickCount());
        s_missKeys.n = 0;
        s_noFallbackKeys.n = 0;   // new data on disk
        s_lastErr = 0;
    } else {
        s_lastErr = res->err;
        s_lastHttp = res->httpStatus;
        s_lastErrTick = GetTickCount();
    }
    free(res);
    // next image of the view; after an error this waits and shows a fallback right away
    Ensure(REQ_AUTO);
    InvalidateRect(s_wnd, NULL, FALSE);
}

// Called after every user change: show what the cache has now, download later.
static void UserChanged()
{
    s_missKeys.n = 0;
    MapRequest keys[kMaxKeys];
    if (NeededKeys(keys, kMaxKeys))
        CacheFind(keys[0], true);
    ConfigSave();
    s_lastSaveTick = GetTickCount();
    SetTimer(s_wnd, TIMER_USER, kUserDelayMs, NULL);
    s_userPending = true;
    InvalidateRect(s_wnd, NULL, FALSE);
}

// ---------------------------------------------------------------- GPS settings

static void SettingsLoad()
{
    s_setPort = 6;
    const wchar_t* p = g_cfg.gpsPort;
    if (p[0] == L'C' && p[1] == L'O' && p[2] == L'M' && p[3] >= L'1' && p[3] <= L'9')
        s_setPort = p[3] - L'0';
    s_setBaud = kSetBaudCount - 1;
    for (int i = 0; i < kSetBaudCount; i++)
        if (kSetBauds[i] == (DWORD)g_cfg.gpsBaud)
            s_setBaud = i;
    s_setCache = 1;
    for (int i = 0; i < kSetCacheCount; i++)
        if (kSetCacheMb[i] == g_cfg.cacheMb)
            s_setCache = i;
}

static void EnsureGpsRunning()
{
    if (s_gpsRunning)
        return;
    g_cfg.gpsEnabled = 1;
    GpsStart(s_wnd);
    s_gpsRunning = true;
}

static void OnSettingsCtl(int id)
{
    switch (id) {
    case S_PORT_PREV: s_setPort = s_setPort > 1 ? s_setPort - 1 : 9; break;
    case S_PORT_NEXT: s_setPort = s_setPort < 9 ? s_setPort + 1 : 1; break;
    case S_BAUD_PREV: s_setBaud = (s_setBaud + kSetBaudCount - 1) % kSetBaudCount; break;
    case S_BAUD_NEXT: s_setBaud = (s_setBaud + 1) % kSetBaudCount; break;
    case S_CACHE_PREV: if (s_setCache > 0) s_setCache--; break;
    case S_CACHE_NEXT: if (s_setCache < kSetCacheCount - 1) s_setCache++; break;
    case S_APPLY: {
        wchar_t port[16];
        _snwprintf(port, 16, L"COM%d:", s_setPort);
        if (wcscmp(port, g_cfg.gpsPort) != 0 || (DWORD)g_cfg.gpsBaud != kSetBauds[s_setBaud] || !s_gpsRunning) {
            wcscpy(g_cfg.gpsPort, port);
            g_cfg.gpsBaud = kSetBauds[s_setBaud];
            Log("gps: settings %S %d", g_cfg.gpsPort, g_cfg.gpsBaud);
            EnsureGpsRunning();
            GpsConfigure(g_cfg.gpsPort, g_cfg.gpsBaud);
        }
        g_cfg.cacheMb = kSetCacheMb[s_setCache];
        if (g_cfg.cacheKb < 0 || g_cfg.cacheKb > (LONG)g_cfg.cacheMb * 1024)
            CachePrune();
        ConfigSave();
        break;
    }
    case S_SCAN:
        EnsureGpsRunning();
        GpsScan();
        break;
    case S_CLOSE:
        s_settings = false;
        break;
    }
    InvalidateRect(s_wnd, NULL, FALSE);
}

// The GPS thread finished a port search.
static void OnGpsScanDone()
{
    if (s_gps.found != 1)
        return;
    wcscpy(g_cfg.gpsPort, s_gps.port);
    g_cfg.gpsBaud = s_gps.baud;
    ConfigSave();
    SettingsLoad();
}

static int HitSettings(int x, int y)
{
    for (int i = 0; i < S_COUNT; i++)
        if (x >= kSetCtl[i].left && x < kSetCtl[i].right && y >= kSetCtl[i].top && y < kSetCtl[i].bottom)
            return i;
    return -1;
}

// ---------------------------------------------------------------- actions

static void PanBy(double dx, double dy)
{
    double x, y;
    ViewWorld(&x, &y);
    WorldToGeo(x + dx, y + dy, g_cfg.z, &g_cfg.lon, &g_cfg.lat);
    SnapView();
    g_cfg.follow = FOLLOW_OFF;
}

static void Zoom(int dz)
{
    int z = g_cfg.z + dz;
    if (z < 2 || z > 19)
        return;
    double x, y;
    ViewWorld(&x, &y);
    double f = pow(2.0, dz);
    g_cfg.z = z;
    WorldToGeo(x * f, y * f, z, &g_cfg.lon, &g_cfg.lat);
    SnapView();
    FollowGps();
}

// GPS button: off -> north-up (car in the center) -> heading-up (car at the bottom) -> off
static void NextFollowMode()
{
    g_cfg.follow = (g_cfg.follow + 1) % 3;
    if (g_cfg.follow == FOLLOW_OFF)
        SnapView();   // static view: one grid image covers the window
    else
        FollowGps();
}

static void OnButton(int b)
{
    switch (b) {
    case B_SETTINGS:
        s_settings = !s_settings;
        if (s_settings)
            SettingsLoad();
        InvalidateRect(s_wnd, NULL, FALSE);
        return;
    case B_EXIT:   DestroyWindow(s_wnd); return;
    case B_TRF:    g_cfg.traffic = !g_cfg.traffic; break;
    case B_LAYERS: g_cfg.layer = (g_cfg.layer + 1) % LAYER_COUNT; break;
    case B_GPS:    NextFollowMode(); break;
    case B_LEFT:   PanBy(-GRID_X, 0); break;
    case B_RIGHT:  PanBy(GRID_X, 0); break;
    case B_UP:     PanBy(0, -GRID_Y); break;
    case B_DOWN:   PanBy(0, GRID_Y); break;
    case B_MINUS:  Zoom(-1); break;
    case B_PLUS:   Zoom(1); break;
    }
    UserChanged();
}

static int ButtonState(int b)
{
    if (b == B_TRF) return g_cfg.traffic ? 0 : 1;
    if (b == B_GPS) return g_cfg.follow == FOLLOW_HEADING ? 0 : (g_cfg.follow == FOLLOW_NORTH ? 1 : 2);
    return 0;
}

static int HitButton(int x, int y)
{
    for (int i = 0; i < B_COUNT; i++)
        if (x >= s_btn[i].x && x < s_btn[i].x + kBtnSize && y >= s_btn[i].y && y < s_btn[i].y + kBtnSize)
            return i;
    return -1;
}

static bool InMap(int x, int y)
{
    return x >= g_cfg.mapX && x < g_cfg.mapX + g_cfg.mapW && y >= g_cfg.mapY && y < g_cfg.mapY + g_cfg.mapH;
}

// ---------------------------------------------------------------- drawing

static void LoadImages()
{
    wchar_t path[MAX_PATH];
    PathInDir(path, L"Images\\MapGL.png");
    if (!ImageFromFile(path, &s_bg))
        Log("ui: no background %S", path);

    for (int i = 0; i < B_COUNT; i++) {
        for (int state = 0; state < 3; state++) {
            const wchar_t* name = s_btn[i].name[state];
            if (!name)
                continue;
            for (int p = 0; p < 2; p++) {
                wchar_t file[64];
                _snwprintf(file, 64, L"Images\\%s_%c.png", name, p ? L'P' : L'N');
                file[63] = 0;
                PathInDir(path, file);
                if (!ImageFromFile(path, &s_btn[i].img[state][p]))
                    Log("ui: no image %S", path);
            }
        }
    }
}

static void Blit(HDC dc, const Image* im, int x, int y)
{
    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(src, im->bmp);
    BitBlt(dc, x, y, im->w, im->h, src, 0, 0, SRCCOPY);
    SelectObject(src, old);
    DeleteDC(src);
}

static void Text(HDC dc, HFONT font, COLORREF color, int l, int t, int r, int b, UINT align, const wchar_t* s)
{
    RECT rc = { l, t, r, b };
    SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawText(dc, s, -1, &rc, align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

// Car marker. (cx, cy) - screen point of the view center (vx, vy); angle - map rotation.
static void DrawGpsMarker(HDC dc, double cx, double cy, double vx, double vy, double angle)
{
    if (!s_gps.fixTick)
        return;
    double gx, gy;
    GeoToWorld(s_gps.lon, s_gps.lat, g_cfg.z, &gx, &gy);
    double ra = angle * M_PI / 180.0, rc = cos(ra), rs = sin(ra);
    double dx = gx - vx, dy = gy - vy;
    int px = (int)floor(cx + dx * rc + dy * rs + 0.5);
    int py = (int)floor(cy - dx * rs + dy * rc + 0.5);

    bool fresh = s_gps.valid && GetTickCount() - s_gps.fixTick < 5000;
    HBRUSH brush = CreateSolidBrush(fresh ? RGB(0x20, 0x60, 0xE0) : RGB(140, 140, 140));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ ob = SelectObject(dc, brush);
    HGDIOBJ op = SelectObject(dc, pen);

    if (s_gps.speedKmh > 3 || g_cfg.follow == FOLLOW_HEADING) {
        // arrow pointing along the course (0 = north, clockwise), relative to the map
        static const int shape[4][2] = { { 0, -16 }, { 11, 12 }, { 0, 6 }, { -11, 12 } };
        double course = g_cfg.follow == FOLLOW_HEADING ? s_heading : s_gps.course;
        double a = (course - angle) * M_PI / 180.0, c = cos(a), s = sin(a);
        POINT pts[4];
        for (int i = 0; i < 4; i++) {
            pts[i].x = px + (int)floor(shape[i][0] * c - shape[i][1] * s + 0.5);
            pts[i].y = py + (int)floor(shape[i][0] * s + shape[i][1] * c + 0.5);
        }
        Polygon(dc, pts, 4);
    } else {
        Ellipse(dc, px - 9, py - 9, px + 9, py + 9);
    }

    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
    DeleteObject(brush);
}

// Drawing order: other zoom levels first (scaled), then other traffic state, then older,
// the images the view needs last.
static int DrawScore(const CacheEntry* e, const MapRequest* keys, int nk)
{
    if (e->fallback)
        return 950;   // above other zoom levels, below real neighbours of this zoom
    for (int i = 0; i < nk; i++)
        if (SameKey(e->req, keys[i]))
            return 10000;
    int s = 1000 - abs(e->req.z - g_cfg.z) * 100;
    if (e->req.traffic == g_cfg.traffic)
        s += 10;
    if (IsFresh(e))
        s += 5;
    return s;
}

// Draws a cached image north-up; (cx, cy) - point of dc where the view center (vx, vy) is.
static bool DrawEntry(HDC dc, const CacheEntry* e, double cx, double cy, double vx, double vy, const RECT& clip)
{
    const Image& im = e->img;
    double f = pow(2.0, g_cfg.z - e->req.z);
    double ix = (double)e->req.gx * GRID_X * f, iy = (double)e->req.gy * GRID_Y * f;
    double dw = im.w * f, dh = im.h * f;
    int left = (int)floor(cx + ix - vx - dw / 2 + 0.5);
    int top = (int)floor(cy + iy - vy - dh / 2 + 0.5);
    int w = (int)floor(dw + 0.5), h = (int)floor(dh + 0.5);
    if (left >= clip.right || top >= clip.bottom || left + w <= clip.left || top + h <= clip.top)
        return false;

    HDC src = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(src, im.bmp);
    if (w == im.w && h == im.h)
        BitBlt(dc, left, top, w, h, src, 0, 0, SRCCOPY);
    else
        StretchBlt(dc, left, top, w, h, src, 0, 0, im.w, im.h, SRCCOPY);
    SelectObject(src, old);
    DeleteDC(src);
    return true;
}

// Mosaic of everything cached in memory: neighbours and other zoom levels fill the
// gaps while dragging/zooming, the images the view needs are drawn last (on top).
static int DrawMosaic(HDC dc, double cx, double cy, double vx, double vy, const RECT& clip)
{
    MapRequest keys[kMaxKeys];
    int nk = NeededKeys(keys, kMaxKeys);
    CacheEntry* list[CACHE_MEM];
    int n = CacheList(list, CACHE_MEM);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && DrawScore(list[j], keys, nk) < DrawScore(list[j - 1], keys, nk); j--) {
            CacheEntry* t = list[j];
            list[j] = list[j - 1];
            list[j - 1] = t;
        }
    int drawn = 0;
    for (int i = 0; i < n; i++)
        if (list[i]->req.layer == g_cfg.layer && abs(list[i]->req.z - g_cfg.z) <= 3 &&
            DrawEntry(dc, list[i], cx, cy, vx, vy, clip))
            drawn++;
    return drawn;
}

static unsigned char* MakeDib(int w, int h, Image* out)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;   // bottom-up
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HDC screen = GetDC(NULL);
    out->bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    out->w = w;
    out->h = h;
    return (unsigned char*)bits;
}

// Heading-up: turns the north-up canvas (view center in its middle) by -angle into the
// map-sized buffer. GDI on CE can't rotate, so it is done per pixel in 16.16 fixed point.
static void RotateCanvas(double angle)
{
    const int C = kCanvas, W = s_rotated.w, H = s_rotated.h;
    const int cstride = (C * 3 + 3) & ~3, ostride = (W * 3 + 3) & ~3;
    double a = angle * M_PI / 180.0, ca = cos(a), sa = sin(a);
    long dx = (long)(ca * 65536), dy = (long)(sa * 65536);
    for (int y = 0; y < H; y++) {
        // screen offset s -> canvas offset w = Rot(angle) s
        double sx = -W / 2.0, sy = y - H / 2.0;
        long wx = (long)((sx * ca - sy * sa + C / 2.0) * 65536);
        long wy = (long)((sx * sa + sy * ca + C / 2.0) * 65536);
        unsigned char* d = s_rotatedBits + (H - 1 - y) * ostride;
        for (int x = 0; x < W; x++, d += 3, wx += dx, wy += dy) {
            int px = (int)(wx >> 16), py = (int)(wy >> 16);
            if (px < 0 || py < 0 || px >= C || py >= C) {
                d[0] = 222; d[1] = 226; d[2] = 228;
                continue;
            }
            const unsigned char* s = s_canvasBits + (C - 1 - py) * cstride + px * 3;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
}

static void DrawMap(HDC dc)
{
    RECT mr = { g_cfg.mapX, g_cfg.mapY, g_cfg.mapX + g_cfg.mapW, g_cfg.mapY + g_cfg.mapH };
    HRGN rgn = CreateRectRgn(mr.left, mr.top, mr.right, mr.bottom);
    SelectClipRgn(dc, rgn);
    HBRUSH bg = CreateSolidBrush(RGB(228, 226, 222));
    const wchar_t* empty = s_limitHit ? L"Лимит запросов на сегодня исчерпан"
                         : s_lastErr ? NetErrorText(s_lastErr) : L"Загрузка карты...";

    double vx, vy;
    ViewWorld(&vx, &vy);
    double cx = g_cfg.mapX + g_cfg.mapW / 2.0;
    double cy = g_cfg.mapY + g_cfg.mapH / 2.0;
    double angle = ViewAngle();

    if (g_cfg.follow == FOLLOW_HEADING && s_canvasBits && s_rotatedBits) {
        HDC cdc = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(cdc, s_canvas.bmp);
        RECT all = { 0, 0, kCanvas, kCanvas };
        FillRect(cdc, &all, bg);
        int drawn = DrawMosaic(cdc, kCanvas / 2.0, kCanvas / 2.0, vx, vy, all);
        SelectObject(cdc, old);
        DeleteDC(cdc);
        RotateCanvas(angle);
        Blit(dc, &s_rotated, mr.left, mr.top);
        if (!drawn)
            Text(dc, s_fontSmall, RGB(90, 90, 90), mr.left, mr.top, mr.right, mr.bottom, DT_CENTER, empty);
    } else {
        FillRect(dc, &mr, bg);
        Text(dc, s_fontSmall, RGB(90, 90, 90), mr.left, mr.top, mr.right, mr.bottom, DT_CENTER, empty);
        DrawMosaic(dc, cx, cy, vx, vy, mr);
    }
    DeleteObject(bg);

    DrawGpsMarker(dc, cx, cy, vx, vy, angle);

    SelectClipRgn(dc, NULL);
    DeleteObject(rgn);
}

// White rounded button like the image buttons.
static void DrawCtl(HDC dc, const RECT& r, bool pressed, HFONT font, const wchar_t* caption)
{
    HBRUSH brush = CreateSolidBrush(pressed ? RGB(190, 190, 190) : RGB(255, 255, 255));
    HGDIOBJ ob = SelectObject(dc, brush);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 14, 14);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(brush);
    Text(dc, font, RGB(90, 90, 90), r.left, r.top, r.right, r.bottom, DT_CENTER, caption);
}

static void DrawSettings(HDC dc)
{
    RECT mr = { g_cfg.mapX, g_cfg.mapY, g_cfg.mapX + g_cfg.mapW, g_cfg.mapY + g_cfg.mapH };
    HBRUSH bg = CreateSolidBrush(RGB(32, 36, 44));
    FillRect(dc, &mr, bg);
    DeleteObject(bg);

    wchar_t s[128];
    Text(dc, s_fontBig, kText, 22, 34, 580, 70, DT_LEFT, L"Настройки");
    Text(dc, s_fontSmall, kText, 22, 75, 170, 125, DT_LEFT, L"Порт GPS");
    _snwprintf(s, 128, L"COM%d:", s_setPort);
    Text(dc, s_fontBig, kText, 240, 75, 400, 125, DT_CENTER, s);
    Text(dc, s_fontSmall, kText, 22, 133, 170, 183, DT_LEFT, L"Скорость");
    if (kSetBauds[s_setBaud])
        _snwprintf(s, 128, L"%lu", kSetBauds[s_setBaud]);
    else
        wcscpy(s, L"не менять");
    Text(dc, s_fontBig, kText, 240, 133, 400, 183, DT_CENTER, s);
    Text(dc, s_fontSmall, kText, 22, 191, 170, 241, DT_LEFT, L"Кэш карт");
    int mb = kSetCacheMb[s_setCache];
    if (mb >= 1000)
        _snwprintf(s, 128, L"%d ГБ", mb / 1000);
    else
        _snwprintf(s, 128, L"%d МБ", mb);
    Text(dc, s_fontBig, kText, 240, 191, 400, 241, DT_CENTER, s);
    if (g_cfg.cacheKb >= 0)
        _snwprintf(s, 128, L"занято %ld МБ", g_cfg.cacheKb / 1024);
    else
        wcscpy(s, L"занято ?");
    Text(dc, s_fontSmall, kTextDim, 470, 191, 598, 241, DT_LEFT, s);

    for (int i = 0; i < S_COUNT; i++)
        DrawCtl(dc, kSetCtl[i], s_setPressed == i, i < S_APPLY ? s_fontBig : s_fontSmall, kSetCaption[i]);

    // live state of the receiver
    COLORREF c = kText;
    if (!s_gpsRunning) {
        wcscpy(s, L"GPS выключен (gps=0), нажмите Применить");
        c = kTextDim;
    } else {
        switch (s_gps.status) {
        case GPS_OPEN:     _snwprintf(s, 128, L"Порт %s %lu открыт", s_gps.port, s_gps.baud); break;
        case GPS_BUSY:     _snwprintf(s, 128, L"Порт %s занят другой программой", s_gps.port); c = kTextErr; break;
        case GPS_SCANNING: _snwprintf(s, 128, L"Поиск: %s %lu ...", s_gps.port, s_gps.baud); break;
        default:           _snwprintf(s, 128, L"Порт %s не открывается", s_gps.port); c = kTextErr; break;
        }
    }
    s[127] = 0;
    Text(dc, s_fontSmall, c, 22, 306, 580, 330, DT_LEFT, s);

    _snwprintf(s, 128, L"NMEA: %d строк, с ошибкой %d", s_gps.lines, s_gps.badLines);
    Text(dc, s_fontSmall, s_gps.lines ? kText : kTextDim, 22, 330, 580, 354, DT_LEFT, s);

    if (s_gps.valid)
        _snwprintf(s, 128, L"Координаты: %.5f, %.5f  (%d спутн.)", s_gps.lat, s_gps.lon, s_gps.sats);
    else
        _snwprintf(s, 128, L"Координат пока нет (%d спутн.)", s_gps.sats);
    s[127] = 0;
    Text(dc, s_fontSmall, s_gps.valid ? kText : kTextDim, 22, 354, 580, 378, DT_LEFT, s);

    MultiByteToWideChar(CP_ACP, 0, s_gps.last, -1, s, 128);
    s[127] = 0;
    Text(dc, s_fontSmall, kTextDim, 22, 378, 580, 402, DT_LEFT, s[0] ? s : L"-");

    if (s_gps.found == 1) {
        _snwprintf(s, 128, L"Найден GPS: %s %lu, настройки сохранены", s_gps.port, s_gps.baud);
        Text(dc, s_fontSmall, RGB(120, 220, 120), 22, 404, 580, 428, DT_LEFT, s);
    } else if (s_gps.found == -1) {
        Text(dc, s_fontSmall, kTextErr, 22, 404, 580, 428, DT_LEFT, L"GPS не найден на COM1..COM9");
    } else {
        Text(dc, s_fontSmall, kTextDim, 22, 404, 580, 452, DT_LEFT | DT_WORDBREAK,
             L"Нет строк - не тот порт. Строки с ошибкой - не та скорость.");
    }
}

static void DrawStatus(HDC dc)
{
    wchar_t s[96];
    SYSTEMTIME t;
    GetLocalTime(&t);
    _snwprintf(s, 96, L"%s %02d.%02d.%04d  %02d:%02d:%02d", kWeekDays[t.wDayOfWeek % 7],
               t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute, t.wSecond);
    Text(dc, s_fontBig, kText, 10, 0, 330, 28, DT_LEFT, s);

    COLORREF gc = kText;
    if (!s_gpsRunning) {
        wcscpy(s, L"GPS выкл.");
        gc = kTextDim;
    } else if (s_gps.status == GPS_SCANNING) {
        wcscpy(s, L"GPS: поиск порта...");
    } else if (s_gps.status == GPS_BUSY) {
        _snwprintf(s, 96, L"GPS: порт %s занят", s_gps.port);
        gc = kTextErr;
    } else if (s_gps.status != GPS_OPEN) {
        _snwprintf(s, 96, L"GPS: нет порта %s", s_gps.port);
        gc = kTextErr;
    } else if (!s_gps.lines) {
        wcscpy(s, L"GPS: нет данных");
        gc = kTextErr;
    } else if (!s_gps.valid) {
        _snwprintf(s, 96, L"GPS: поиск (%d спутн.)", s_gps.sats);
        gc = kTextDim;
    } else {
        _snwprintf(s, 96, L"GPS: %d спутн., %d км/ч", s_gps.sats, (int)(s_gps.speedKmh + 0.5));
    }
    s[95] = 0;
    Text(dc, s_fontSmall, gc, 330, 0, 560, 28, DT_LEFT, s);

    // age of the image for the current view
    COLORREF uc = kText;
    MapRequest keys[kMaxKeys];
    CacheEntry* e = NeededKeys(keys, kMaxKeys) ? CacheFind(keys[0], false) : NULL;
    if (s_limitHit) {
        wcscpy(s, L"Лимит запросов!");
        uc = kTextErr;
    } else if (s_lastErr && GetTickCount() - s_lastErrTick < 60000) {
        if (s_lastErr == NET_HTTP)
            _snwprintf(s, 96, L"Ошибка HTTP %d", s_lastHttp);
        else
            _snwprintf(s, 96, L"Ошибка: %s", NetErrorText(s_lastErr));
        uc = kTextErr;
    } else if (s_inflight) {
        wcscpy(s, L"Загрузка...");
    } else if (!e) {
        wcscpy(s, L"Нет карты");
        uc = kTextDim;
    } else if (e->fallback) {
        wcscpy(s, L"Нет карты, показан кэш");
        uc = kTextDim;
    } else if (!e->fetchTick) {
        wcscpy(s, e->req.traffic ? L"Пробки: из кэша" : L"Карта из кэша");
        uc = e->req.traffic ? kTextDim : kText;
    } else {
        int min = (int)((GetTickCount() - e->fetchTick) / 60000);
        _snwprintf(s, 96, L"Обновлено %d мин назад", min);
        uc = IsFresh(e) ? kText : kTextDim;
    }
    s[95] = 0;
    Text(dc, s_fontSmall, uc, 560, 0, 795, 28, DT_RIGHT, s);

    // info block in the right panel
    _snwprintf(s, 96, L"Масштаб: %d", g_cfg.z);
    Text(dc, s_fontSmall, kText, 612, 155, 795, 175, DT_LEFT, s);
    _snwprintf(s, 96, L"Слой: %s", kLayerTitle[g_cfg.layer]);
    Text(dc, s_fontSmall, kText, 612, 175, 795, 195, DT_LEFT, s);
    _snwprintf(s, 96, L"Запросов: %d из %d", g_cfg.reqCount, g_cfg.dailyLimit);
    Text(dc, s_fontSmall, g_cfg.reqCount >= g_cfg.dailyLimit - g_cfg.autoReserve ? kTextErr : kTextDim,
         612, 195, 795, 215, DT_LEFT, s);
}

static void DrawButtons(HDC dc)
{
    for (int i = 0; i < B_COUNT; i++) {
        // the settings button stays highlighted while its screen is open
        int pressed = ((s_pressed == i && s_pressedInside) || (i == B_SETTINGS && s_settings)) ? 1 : 0;
        const Image* im = &s_btn[i].img[ButtonState(i)][pressed];
        if (im->bmp) {
            Blit(dc, im, s_btn[i].x, s_btn[i].y);
        } else {
            RECT r = { s_btn[i].x, s_btn[i].y, s_btn[i].x + kBtnSize, s_btn[i].y + kBtnSize };
            FillRect(dc, &r, (HBRUSH)GetStockObject(pressed ? GRAY_BRUSH : DKGRAY_BRUSH));
            Text(dc, s_fontSmall, kText, r.left, r.top, r.right, r.bottom, DT_CENTER, s_btn[i].name[0] + 1);
        }
    }
}

static void Paint(HDC hdc)
{
    HDC dc = CreateCompatibleDC(hdc);
    HGDIOBJ oldBmp = SelectObject(dc, s_back);
    HGDIOBJ oldFont = SelectObject(dc, s_fontSmall);
    SetBkMode(dc, TRANSPARENT);

    if (s_bg.bmp) {
        Blit(dc, &s_bg, 0, 0);
    } else {
        RECT r = { 0, 0, s_cw, s_ch };
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    DrawMap(dc);
    if (s_settings)
        DrawSettings(dc);
    DrawButtons(dc);
    DrawStatus(dc);

    BitBlt(hdc, 0, 0, s_cw, s_ch, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteDC(dc);
}

static HFONT MakeFont(int height, int weight)
{
    LOGFONT lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -height;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    wcscpy(lf.lfFaceName, L"Tahoma");
    return CreateFontIndirect(&lf);
}

// ---------------------------------------------------------------- window

static void OnCreate(HWND hwnd)
{
    s_wnd = hwnd;
    RECT rc;
    GetClientRect(hwnd, &rc);
    s_cw = rc.right;
    s_ch = rc.bottom;
    HDC hdc = GetDC(hwnd);
    s_back = CreateCompatibleBitmap(hdc, s_cw, s_ch);
    ReleaseDC(hwnd, hdc);

    s_fontBig = MakeFont(22, FW_SEMIBOLD);
    s_fontSmall = MakeFont(15, FW_NORMAL);
    LoadImages();
    s_canvasBits = MakeDib(kCanvas, kCanvas, &s_canvas);
    s_rotatedBits = MakeDib(g_cfg.mapW, g_cfg.mapH, &s_rotated);
    CacheInit();
    SnapView();

#ifdef UNDER_CE
    s_taskbar = FindWindow(L"HHTaskBar", NULL);
    if (s_taskbar && g_cfg.hideTaskbar)
        ShowWindow(s_taskbar, SW_HIDE);
#endif

    NetStart(hwnd);
    if (g_cfg.gpsEnabled)
        EnsureGpsRunning();
    SetTimer(hwnd, TIMER_TICK, 1000, NULL);
    s_lastSaveTick = GetTickCount();
    Ensure(REQ_AUTO);
}

static void OnDestroy(HWND hwnd)
{
    KillTimer(hwnd, TIMER_TICK);
    KillTimer(hwnd, TIMER_USER);
    ConfigSave();
    if (s_taskbar)
        ShowWindow(s_taskbar, SW_SHOWNORMAL);
    GpsStop();
    NetStop();
    Log("exit, %d requests today", g_cfg.reqCount);
    PostQuitMessage(0);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_DESTROY:
        OnDestroy(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == TIMER_USER) {
            KillTimer(hwnd, TIMER_USER);
            s_userPending = false;
            Ensure(REQ_USER);
        } else {
            GpsGet(&s_gps);
            if (!s_dragging)
                Ensure(REQ_AUTO);
            if (g_cfg.cacheKb > (LONG)g_cfg.cacheMb * 1024)
                CachePrune();   // trims to 90% of the limit, so this is rare
            // remember the GPS-driven position now and then: power may vanish any moment
            if (GetTickCount() - s_lastSaveTick > 5 * 60 * 1000) {
                ConfigSave();
                s_lastSaveTick = GetTickCount();
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_GPS:
        GpsGet(&s_gps);
        if (wp == 1)
            OnGpsScanDone();
        if (!s_dragging) {
            FollowGps();
            Ensure(REQ_AUTO);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_MAP:
        OnMapResult((MapResult*)lp);
        return 0;

    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        SetCapture(hwnd);
        s_pressed = HitButton(x, y);
        if (s_pressed >= 0) {
            s_pressedInside = true;
        } else if (s_settings) {
            s_setPressed = HitSettings(x, y);
        } else if (InMap(x, y)) {
            s_dragging = true;
            s_dragMoved = false;
            s_dragStart.x = x;
            s_dragStart.y = y;
            ViewWorld(&s_dragWx, &s_dragWy);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        if (s_pressed >= 0) {
            bool inside = HitButton(x, y) == s_pressed;
            if (inside != s_pressedInside) {
                s_pressedInside = inside;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else if (s_dragging) {
            int dx = x - s_dragStart.x, dy = y - s_dragStart.y;
            if (!s_dragMoved && (abs(dx) > 6 || abs(dy) > 6))
                s_dragMoved = true;
            if (s_dragMoved) {
                WorldToGeo(s_dragWx - dx, s_dragWy - dy, g_cfg.z, &g_cfg.lon, &g_cfg.lat);
                g_cfg.follow = FOLLOW_OFF;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        ReleaseCapture();
        if (s_pressed >= 0) {
            int b = s_pressed;
            bool inside = HitButton(x, y) == b;
            s_pressed = -1;
            if (inside)
                OnButton(b);
        } else if (s_setPressed >= 0) {
            int c = s_setPressed;
            s_setPressed = -1;
            if (HitSettings(x, y) == c)
                OnSettingsCtl(c);
        } else if (s_dragging) {
            s_dragging = false;
            if (s_dragMoved) {
                SnapView();
                UserChanged();
            } else {
                Ensure(REQ_FORCE);   // tap on the map = refresh now
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:     OnButton(B_LEFT); break;
        case VK_RIGHT:    OnButton(B_RIGHT); break;
        case VK_UP:       OnButton(B_UP); break;
        case VK_DOWN:     OnButton(B_DOWN); break;
        case VK_ADD:      OnButton(B_PLUS); break;
        case VK_SUBTRACT: OnButton(B_MINUS); break;
        case 'T':         OnButton(B_TRF); break;
        case 'L':         OnButton(B_LAYERS); break;
        case 'G':         OnButton(B_GPS); break;
        case 'S':         OnButton(B_SETTINGS); break;
        case VK_ESCAPE:   OnButton(B_EXIT); break;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

#ifdef UNDER_CE
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
#else
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
#endif
{
    GetModuleFileName(NULL, g_dir, MAX_PATH);
    wchar_t* slash = wcsrchr(g_dir, L'\\');
    if (slash)
        slash[1] = 0;

    // single instance: bring the running copy to front
    HWND prev = FindWindow(APP_CLASS, NULL);
    if (prev) {
        SetForegroundWindow(prev);
        return 0;
    }

    LogInit();
    Log("YaMapsCE start, dir %S", g_dir);
    if (!ConfigLoad())
        Log("config: not found, using defaults");
    ConfigSave();

    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = APP_CLASS;
#ifndef UNDER_CE
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
#endif
    RegisterClass(&wc);

#ifdef UNDER_CE
    DWORD style = WS_POPUP | WS_VISIBLE;
    int x = 0, y = 0;
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
#else
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    RECT rc = { 0, 0, 800, 480 };
    AdjustWindowRect(&rc, style, FALSE);
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = rc.right - rc.left, h = rc.bottom - rc.top;
#endif
    HWND hwnd = CreateWindowEx(0, APP_CLASS, APP_NAME, style, x, y, w, h, NULL, NULL, inst, NULL);
    if (!hwnd) {
        Log("CreateWindow failed (%lu)", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
