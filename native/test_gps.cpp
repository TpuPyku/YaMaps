// Desktop test of the NMEA parser on a mixed stream (NMEA + binary data, like the
// Vesta shell port). Build/run: native\test.bat
#include "gps.cpp"

static int s_fail;

static void Check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        s_fail++;
}

static void Reset()
{
    memset(&s_state, 0, sizeof(s_state));
    s_lineLen = 0;
}

int main()
{
    InitializeCriticalSection(&s_cs);

    // 1. clean sentences with checksum
    Reset();
    const char* clean =
        "$GPGGA,123519,5549.3932,N,03752.8059,E,1,08,0.9,545.4,M,46.9,M,,*44\r\n"
        "$GPRMC,123519,A,5549.3932,N,03752.8059,E,022.4,084.4,011026,003.1,W*62\r\n";
    Feed(clean, (DWORD)strlen(clean));
    Check(s_state.valid == 1, "RMC with checksum gives a fix");
    Check(fabs(s_state.lat - 55.823220) < 1e-5 && fabs(s_state.lon - 37.880098) < 1e-5, "coordinates 55.823220, 37.880098");
    Check(fabs(s_state.speedKmh - 22.4 * 1.852) < 1e-6 && s_state.course == 84.4, "speed and course");
    Check(s_state.date == 20261001, "date from RMC");
    Check(s_state.sats == 8, "satellites from GGA");

    // 2. binary garbage between and inside sentences, bad checksum, cut sentence
    Reset();
    char mixed[512];
    int n = 0;
    const unsigned char junk[] = { 0x55, 0xAA, 0x01, 0x80, 0xFF, 0x00, 0x13 };
    memcpy(mixed + n, junk, sizeof(junk)); n += sizeof(junk);
    const char* bad = "$GPRMC,123519,A,5549.3932,N,03752.8059,E,022.4,084.4,011026,003.1,W*00\r\n";
    memcpy(mixed + n, bad, strlen(bad)); n += (int)strlen(bad);
    const char* cut = "$GPRMC,123520,A,5549.39";
    memcpy(mixed + n, cut, strlen(cut)); n += (int)strlen(cut);
    memcpy(mixed + n, junk, sizeof(junk)); n += sizeof(junk);
    mixed[n++] = '\n';
    const char* good = "$GNRMC,123521,A,5549.3932,S,03752.8059,W,0.0,,011026,,,A*4C\r\n";
    memcpy(mixed + n, good, strlen(good)); n += (int)strlen(good);
    Feed(mixed, n);
    Check(s_state.badLines == 2, "bad checksum and garbage-cut line rejected");
    Check(s_state.lines == 1 && s_state.valid == 1, "GNRMC accepted after garbage");
    Check(s_state.lat < -55.8 && s_state.lon < -37.8, "S/W hemispheres");

    // 3. no checksum: full sentence accepted, cut one rejected
    Reset();
    const char* nocs = "$GPRMC,123522,A,5549.3932,N,03752.8059,E,010.0,090.0,011026,,,A\r\n";
    Feed(nocs, (DWORD)strlen(nocs));
    Check(s_state.valid == 1 && s_state.speedKmh > 18, "RMC without checksum accepted");
    Reset();
    const char* nocsCut = "$GPRMC,123522,A,5549.3932,N,0375\r\n";
    Feed(nocsCut, (DWORD)strlen(nocsCut));
    Check(s_state.valid == 0, "cut RMC without checksum rejected");

    // 4. void fix
    Reset();
    const char* v = "$GPRMC,123523,V,,,,,,,011026,,,N*53\r\n";
    Feed(v, (DWORD)strlen(v));
    Check(s_state.lines == 1 && s_state.valid == 0, "status V = no fix");

    printf(s_fail ? "%d FAILED\n" : "all passed\n", s_fail);
    return s_fail;
}
