// G4-step3: iOS-native PNG decoder. Mirrors upstream's per-platform pattern -
// each console plugs its own decoder into RenderManager.LoadTextureData;
// iOS plugs CGImageSource. PNG bytes in, RGBA bytes out, free with the
// matching mcle_png_decode_free().

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include <stdlib.h>
#include <string.h>

extern "C" int mcle_png_decode_rgba8(const void* data, unsigned long length,
                                       unsigned char** out_rgba,
                                       int* out_w, int* out_h) {
    if (!data || length == 0 || !out_rgba || !out_w || !out_h) return 0;
    *out_rgba = NULL; *out_w = 0; *out_h = 0;

    CFDataRef cfdata = CFDataCreate(NULL, (const UInt8*)data, (CFIndex)length);
    if (!cfdata) return 0;

    CGImageSourceRef src = CGImageSourceCreateWithData(cfdata, NULL);
    CFRelease(cfdata);
    if (!src) return 0;

    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    if (!img) return 0;

    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);
    if (w == 0 || h == 0) {
        CGImageRelease(img);
        return 0;
    }

    unsigned char* pixels = (unsigned char*)malloc(w * h * 4);
    if (!pixels) {
        CGImageRelease(img);
        return 0;
    }
    memset(pixels, 0, w * h * 4);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    if (!cs) {
        free(pixels);
        CGImageRelease(img);
        return 0;
    }

    // RGBA8, premultiplied alpha (matches Metal's MTLPixelFormatRGBA8Unorm
    // expectations when sampling).
    CGContextRef ctx = CGBitmapContextCreate(pixels, w, h, 8, w * 4, cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CFRelease(cs);
    if (!ctx) {
        free(pixels);
        CGImageRelease(img);
        return 0;
    }

    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    *out_rgba = pixels;
    *out_w    = (int)w;
    *out_h    = (int)h;
    return 1;
}

extern "C" void mcle_png_decode_free(unsigned char* pixels) {
    free(pixels);
}

extern "C" const char *ios_documents_dir(void);

extern "C" int mcle_log_msg(const char *msg) {
    if (!msg) return 0;
    NSLog(@"[MCLE/up] %s", msg);

    // Write to TWO files:
    //   1. Documents/crash_log.txt - the legacy interleaved-with-ruffle
    //      file that 3uTools / Files app users already pull. Kept so
    //      existing tooling keeps working.
    //   2. Documents/mcle_log.txt  - mcle-only, dedicated. Ruffle does
    //      NOT open this path, so our lines never get overwritten by
    //      a non-O_APPEND writer racing on the same file. Use THIS
    //      file for crash diagnosis - the legacy crash_log.txt loses
    //      early-game-init log lines to the Ruffle write race.
    static FILE *legacy = nullptr;
    static FILE *clean  = nullptr;
    if (!legacy || !clean) {
        const char *root = ios_documents_dir();
        if (root && *root) {
            char path[1024];
            if (!legacy) {
                snprintf(path, sizeof(path), "%s/crash_log.txt", root);
                legacy = fopen(path, "a");
            }
            if (!clean) {
                snprintf(path, sizeof(path), "%s/mcle_log.txt", root);
                clean = fopen(path, "a");
            }
        }
    }
    if (legacy) {
        fprintf(legacy, "[mcle] %s\n", msg);
        fflush(legacy);
    }
    if (clean) {
        fprintf(clean, "[mcle] %s\n", msg);
        fflush(clean);
    }
    return 1;
}
