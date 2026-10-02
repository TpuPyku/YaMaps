#include "yamaps.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"

// Decodes PNG/JPEG into a 24bpp DIB section (the format every CE display driver can blit).
bool ImageFromMemory(const unsigned char* data, int len, Image* out)
{
    int w, h, comp;
    unsigned char* rgb = stbi_load_from_memory(data, len, &w, &h, &comp, 3);
    if (!rgb) {
        Log("image: decode failed: %s", stbi_failure_reason());
        return false;
    }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;          // bottom-up
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HDC screen = GetDC(NULL);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!bmp || !bits) {
        Log("image: CreateDIBSection %dx%d failed (%lu)", w, h, GetLastError());
        stbi_image_free(rgb);
        return false;
    }

    int stride = (w * 3 + 3) & ~3;
    for (int y = 0; y < h; y++) {
        const unsigned char* src = rgb + y * w * 3;
        unsigned char* dst = (unsigned char*)bits + (h - 1 - y) * stride;
        for (int x = 0; x < w; x++) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            src += 3;
            dst += 3;
        }
    }
    stbi_image_free(rgb);

    out->bmp = bmp;
    out->w = w;
    out->h = h;
    return true;
}

bool ImageFromFile(const wchar_t* path, Image* out)
{
    unsigned char* data;
    int len;
    if (!ReadWholeFile(path, &data, &len))
        return false;
    bool ok = ImageFromMemory(data, len, out);
    free(data);
    return ok;
}

void ImageFree(Image* im)
{
    if (im->bmp)
        DeleteObject(im->bmp);
    im->bmp = NULL;
    im->w = im->h = 0;
}
