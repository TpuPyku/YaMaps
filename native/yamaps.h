// YaMapsCE - native Yandex Static Maps viewer for Windows CE 6.0 (Lada Vesta MMC)
// Also builds as a desktop Win32 app for testing on a PC.
#pragma once

#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#define APP_NAME   L"YaMapsCE"
#define APP_CLASS  L"YaMapsCE_Wnd"

#define WM_APP_MAP (WM_APP + 1)   // lParam = MapResult*, receiver frees it
#define WM_APP_GPS (WM_APP + 2)   // GPS state changed

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { LAYER_MAP = 0, LAYER_SAT, LAYER_HYBRID, LAYER_COUNT };

struct Config {
    char    host[64];
    double  lon, lat;       // view center
    int     z;
    int     layer;
    int     traffic;
    int     follow;         // keep view centered on GPS position
    int     trafficTtlMin;  // traffic image is considered fresh this long
    int     dailyLimit;     // hard cap of requests per day
    int     autoReserve;    // requests kept for manual actions (auto stops earlier)
    int     cacheMb;        // disk cache size limit
    volatile LONG cacheKb;  // disk cache size now (-1 = unknown, scan), kept in ini
    int     reqDay;         // yyyymmdd the counter belongs to
    int     reqCount;       // requests made on reqDay
    int     gpsEnabled;
    wchar_t gpsPort[16];
    int     gpsBaud;
    wchar_t killProcess[32]; // closed when it holds the GPS port (Navitel)
    int     hideTaskbar;
    int     mapX, mapY, mapW, mapH;
};

extern Config  g_cfg;
extern wchar_t g_dir[MAX_PATH];   // exe directory, with trailing backslash

// util.cpp
void PathInDir(wchar_t* out, const wchar_t* name);
bool ReadWholeFile(const wchar_t* path, unsigned char** data, int* len);
void LogInit();
void Log(const char* fmt, ...);
void ConfigDefaults();
bool ConfigLoad();
void ConfigSave();
void GeoToWorld(double lon, double lat, int z, double* x, double* y);
void WorldToGeo(double x, double y, int z, double* lon, double* lat);

// image.cpp
struct Image { HBITMAP bmp; int w, h; };
bool ImageFromMemory(const unsigned char* data, int len, Image* out);
bool ImageFromFile(const wchar_t* path, Image* out);
void ImageFree(Image* im);

// Request geometry. Map centers are snapped to a grid with half-image step,
// so the same place always produces the same request and can be cached.
#define REQ_W 600           // = map window: the Yandex logo must stay visible
#define REQ_H 450
#define GRID_X (REQ_W / 2)
#define GRID_Y (REQ_H / 2)
#define CACHE_BLOCK 64      // grid points per cache subfolder side: FAT is slow on big folders

// net.cpp
enum { NET_OK = 0, NET_DNS, NET_CONNECT, NET_TIMEOUT, NET_HTTP, NET_DECODE, NET_MEMORY };
struct MapRequest { int z, gx, gy, layer, traffic; };   // center = grid point (gx, gy) at zoom z
struct MapResult  { MapRequest req; DWORD seq; int err; int httpStatus; int bytes; Image img; };
void  NetStart(HWND notify);
DWORD NetRequest(const MapRequest& r);   // returns sequence number
void  NetStop();
const wchar_t* NetErrorText(int err);
void  GridCenter(const MapRequest& r, double* lon, double* lat);
void  CachePath(wchar_t* out, const MapRequest& r);

// cache.cpp - decoded images in memory + raw files in <exe>\Cache
struct CacheEntry {
    MapRequest req;
    Image      img;
    DWORD      fetchTick;   // GetTickCount() of download; 0 = loaded from disk, age unknown
    DWORD      useTick;
    int        fallback;    // composed from other zoom levels, never fresh
};
enum { CACHE_MEM = 12 };     // a rotated view may need up to 9 images
void        CacheInit();
void        CachePrune();                 // scan the disk, delete over g_cfg.cacheMb
void        CacheCountSaved(int newBytes, int oldBytes);
CacheEntry* CacheFind(const MapRequest& r, bool loadFromDisk);
CacheEntry* CacheAdd(const MapRequest& r, Image img, DWORD fetchTick);
int         CacheList(CacheEntry** out, int max);
bool        SameKey(const MapRequest& a, const MapRequest& b);

// gps.cpp
enum { GPS_NO_PORT = 0, GPS_BUSY, GPS_OPEN, GPS_SCANNING };
struct GpsState {
    int    status;          // GPS_*
    wchar_t port[16];       // port being used / probed
    DWORD  baud;
    int    lines, badLines; // NMEA sentences with good / bad checksum since (re)configure
    char   last[84];        // last good sentence, for the settings screen
    int    found;           // scan result: 1 found, -1 nothing, 0 no scan
    int    valid;           // last RMC had status 'A'
    double lon, lat;
    double speedKmh, course;
    int    sats;
    DWORD  fixTick;         // GetTickCount() of last valid fix
    DWORD  dataTick;        // GetTickCount() of last NMEA sentence
    int    date;            // yyyymmdd from RMC, 0 = unknown
};
void GpsStart(HWND notify);
void GpsStop();
void GpsGet(GpsState* s);
void GpsConfigure(const wchar_t* port, DWORD baud);   // reopen with new settings
void GpsScan();                                       // look for NMEA on COM1..COM9

// util.cpp
bool KillProcess(const wchar_t* exeName);
