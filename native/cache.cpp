#include "yamaps.h"

// Two-level map cache:
//  - up to CACHE_MEM decoded images in memory (least recently used is evicted);
//  - raw downloaded files in <exe>\Cache\<z>\<bx>_<by>\*.img (see CachePath), kept across reboots.
// The disk size is kept in g_cfg.cacheKb (saved in the ini), so the card is scanned only
// when the limit is exceeded or the size is unknown.

static CacheEntry s_mem[CACHE_MEM];
static int        s_count;

bool SameKey(const MapRequest& a, const MapRequest& b)
{
    return a.z == b.z && a.gx == b.gx && a.gy == b.gy && a.layer == b.layer && a.traffic == b.traffic;
}

static LONG FileKb(DWORD bytes)
{
    return (LONG)((bytes + 1023) / 1024);
}

// Called by the download thread after a file was written.
void CacheCountSaved(int newBytes, int oldBytes)
{
    if (g_cfg.cacheKb >= 0)
        InterlockedExchangeAdd((LONG*)&g_cfg.cacheKb, FileKb(newBytes) - FileKb(oldBytes));
}

// ---------------------------------------------------------------- disk scan & prune

struct FileRec {
    MapRequest req;
    LONG       kb;
    FILETIME   time;
};

static FileRec* s_recs;
static int      s_recCount, s_recCap;

// "<layer>_<traffic>_<gx>_<gy>.img"
static bool ParseName(const wchar_t* s, int* v)
{
    for (int i = 0; i < 4; i++) {
        wchar_t* end;
        v[i] = wcstol(s, &end, 10);
        if (end == s)
            return false;
        s = end;
        if (i < 3) {
            if (*s != L'_')
                return false;
            s++;
        }
    }
    return !wcscmp(s, L".img");
}

static void AddRec(int z, const WIN32_FIND_DATA& fd)
{
    int v[4];
    if (!ParseName(fd.cFileName, v))
        return;
    if (s_recCount == s_recCap) {
        int cap = s_recCap ? s_recCap * 2 : 1024;
        FileRec* r = (FileRec*)realloc(s_recs, cap * sizeof(FileRec));
        if (!r)
            return;
        s_recs = r;
        s_recCap = cap;
    }
    FileRec& r = s_recs[s_recCount++];
    r.req.z = z;
    r.req.layer = v[0];
    r.req.traffic = v[1];
    r.req.gx = v[2];
    r.req.gy = v[3];
    r.kb = FileKb(fd.nFileSizeLow);
    r.time = fd.ftLastWriteTime;
}

// Traffic maps first (stale anyway), then the oldest.
static int CompareRec(const void* a, const void* b)
{
    const FileRec* x = (const FileRec*)a;
    const FileRec* y = (const FileRec*)b;
    if (x->req.traffic != y->req.traffic)
        return y->req.traffic - x->req.traffic;
    return CompareFileTime(&x->time, &y->time);
}

static bool IsSubdir(const WIN32_FIND_DATA& fd)
{
    return (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.';
}

// Files right in Cache\: the first version kept "<layer>_<traffic>_<z>_<gx>_<gy>.img" there.
// Such files are moved into the subfolders, anything else (temp file) is deleted.
static void MigrateOld(const wchar_t* file)
{
    wchar_t name[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    _snwprintf(name, MAX_PATH, L"Cache\\%s", file);
    name[MAX_PATH - 1] = 0;
    PathInDir(from, name);

    int v[5];
    const wchar_t* s = file;
    bool ok = true;
    for (int i = 0; i < 5 && ok; i++) {
        wchar_t* end;
        v[i] = wcstol(s, &end, 10);
        ok = end != s && *end == (i < 4 ? L'_' : L'.');
        s = end + 1;
    }
    if (!ok || wcscmp(s - 1, L".img") != 0) {
        DeleteFile(from);
        return;
    }
    MapRequest r = { v[2], v[3], v[4], v[0], v[1] };
    _snwprintf(name, MAX_PATH, L"Cache\\%d", r.z);
    PathInDir(to, name);
    CreateDirectory(to, NULL);
    _snwprintf(name, MAX_PATH, L"Cache\\%d\\%d_%d", r.z, r.gx / CACHE_BLOCK, r.gy / CACHE_BLOCK);
    PathInDir(to, name);
    CreateDirectory(to, NULL);
    CachePath(to, r);
    if (!MoveFile(from, to))
        DeleteFile(from);
}

static void Scan()
{
    wchar_t name[MAX_PATH], mask[MAX_PATH];
    WIN32_FIND_DATA zd, bd, fd;
    PathInDir(mask, L"Cache\\*");
    HANDLE hz = FindFirstFile(mask, &zd);
    if (hz != INVALID_HANDLE_VALUE) {
        do {
            if (!(zd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                MigrateOld(zd.cFileName);
        } while (FindNextFile(hz, &zd));
        FindClose(hz);
    }

    hz = FindFirstFile(mask, &zd);
    if (hz == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!IsSubdir(zd))
            continue;
        int z = (int)wcstol(zd.cFileName, NULL, 10);
        _snwprintf(name, MAX_PATH, L"Cache\\%s\\*", zd.cFileName);
        name[MAX_PATH - 1] = 0;
        PathInDir(mask, name);
        HANDLE hb = FindFirstFile(mask, &bd);
        if (hb == INVALID_HANDLE_VALUE)
            continue;
        do {
            if (!IsSubdir(bd))
                continue;
            _snwprintf(name, MAX_PATH, L"Cache\\%s\\%s\\*.img", zd.cFileName, bd.cFileName);
            name[MAX_PATH - 1] = 0;
            PathInDir(mask, name);
            HANDLE hf = FindFirstFile(mask, &fd);
            if (hf == INVALID_HANDLE_VALUE)
                continue;
            do AddRec(z, fd); while (FindNextFile(hf, &fd));
            FindClose(hf);
        } while (FindNextFile(hb, &bd));
        FindClose(hb);
    } while (FindNextFile(hz, &zd));
    FindClose(hz);
}

void CachePrune()
{
    DWORD t0 = GetTickCount();
    s_recCount = 0;
    Scan();
    LONG total = 0;
    for (int i = 0; i < s_recCount; i++)
        total += s_recs[i].kb;

    LONG limit = (LONG)g_cfg.cacheMb * 1024;
    int deleted = 0;
    if (total > limit) {
        qsort(s_recs, s_recCount, sizeof(FileRec), CompareRec);
        LONG target = limit / 10 * 9;
        for (int i = 0; i < s_recCount && total > target; i++) {
            wchar_t path[MAX_PATH];
            CachePath(path, s_recs[i].req);
            if (DeleteFile(path)) {
                total -= s_recs[i].kb;
                deleted++;
            }
        }
    }
    g_cfg.cacheKb = total;
    Log("cache: %d files, %ld MB of %d MB, %d deleted, %lu ms",
        s_recCount - deleted, total / 1024, g_cfg.cacheMb, deleted, GetTickCount() - t0);
    free(s_recs);
    s_recs = NULL;
    s_recCap = 0;
    ConfigSave();
}

void CacheInit()
{
    wchar_t dir[MAX_PATH];
    PathInDir(dir, L"Cache");
    CreateDirectory(dir, NULL);
    if (g_cfg.cacheKb < 0 || g_cfg.cacheKb > (LONG)g_cfg.cacheMb * 1024)
        CachePrune();
}

CacheEntry* CacheAdd(const MapRequest& r, Image img, DWORD fetchTick)
{
    CacheEntry* e = NULL;
    for (int i = 0; i < s_count && !e; i++)
        if (SameKey(s_mem[i].req, r))
            e = &s_mem[i];
    if (!e && s_count < CACHE_MEM)
        e = &s_mem[s_count++];
    if (!e) {
        e = &s_mem[0];
        for (int i = 1; i < s_count; i++)
            if (s_mem[i].useTick < e->useTick)
                e = &s_mem[i];
    }
    ImageFree(&e->img);
    e->req = r;
    e->img = img;
    e->fetchTick = fetchTick;
    e->fileDay = 0;
    e->useTick = GetTickCount();
    e->fallback = 0;
    return e;
}

CacheEntry* CacheFind(const MapRequest& r, bool loadFromDisk)
{
    for (int i = 0; i < s_count; i++) {
        if (SameKey(s_mem[i].req, r)) {
            s_mem[i].useTick = GetTickCount();
            return &s_mem[i];
        }
    }
    if (!loadFromDisk)
        return NULL;
    wchar_t path[MAX_PATH];
    CachePath(path, r);
    Image img;
    if (!ImageFromFile(path, &img))
        return NULL;
    CacheEntry* e = CacheAdd(r, img, 0);
    e->fileDay = FileDay(path);
    return e;
}

int CacheList(CacheEntry** out, int max)
{
    int n = 0;
    for (int i = 0; i < s_count && n < max; i++)
        out[n++] = &s_mem[i];
    return n;
}
