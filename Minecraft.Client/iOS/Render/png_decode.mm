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

extern "C" int mcle_log_msg(const char *msg) {
    if (!msg) return 0;
    NSLog(@"[MCLE/up] %s", msg);
    return 1;
}
