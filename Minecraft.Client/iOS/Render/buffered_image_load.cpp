// G5-step33: iOS-side body for RenderManager::LoadTextureData. Reads a
// PNG file from disk, decodes it via the existing CGImageSource bridge
// (mcle_png_decode_rgba8 in png_decode.mm), and packs pixels into the
// ARGB int* layout that upstream BufferedImage's `int *data[10]` array
// expects.
//
// Each per-platform 4J render layer plugs its own decoder behind this
// entry point - Win64 uses D3DX9, Xbox 360 uses its own image API, iOS
// uses CGImageSource. Same interface, platform-specific body. C4JRenderStub
// in iOS_stdafx.h forwards LoadTextureData(const char*, D3DXIMAGE_INFO*,
// int**) here.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

extern "C" int mcle_png_decode_rgba8(const void *data, unsigned long length,
                                       unsigned char **out_rgba,
                                       int *out_w, int *out_h);
extern "C" void mcle_png_decode_free(unsigned char *pixels);

// iOS sandboxes everything under Documents. BufferedImage passes paths
// like "Common/res/textures/blocks/dirt.png" relative to upstream's
// Win64 working directory; we prepend the sandbox Documents path.
extern "C" const char *ios_documents_dir(void);

namespace {

bool file_exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Win64 paths use backslashes - convert to forward slashes for POSIX.
std::string normalize_slashes(const std::string &in) {
    std::string out = in;
    for (char &c : out) if (c == '\\') c = '/';
    return out;
}

bool read_file(const std::string &path, std::string &out) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    out.resize((size_t)sz);
    size_t r = std::fread(&out[0], 1, (size_t)sz, f);
    std::fclose(f);
    return r == (size_t)sz;
}

} // namespace

extern "C" int mcle_log_msg(const char *msg);  // bridge to os_log

extern "C" long mcle_buffered_image_load_path(const char *path,
                                                 unsigned int *out_width,
                                                 unsigned int *out_height,
                                                 int **out_data) {
    if (!path || !out_width || !out_height || !out_data) return -1;
    *out_data = nullptr;
    *out_width = 0;
    *out_height = 0;

    std::string rel = normalize_slashes(path);
    std::string full = rel;
    if (!file_exists(full)) {
        // Treat as relative to Documents sandbox.
        const char *root = ios_documents_dir();
        if (root && *root) {
            full = std::string(root) + "/" + rel;
        }
    }
    {
        std::string m = std::string("BIL_CKPT path=") + path + " full=" + full +
            " exists=" + (file_exists(full) ? "1" : "0");
        mcle_log_msg(m.c_str());
    }
    if (!file_exists(full)) return -2;

    std::string bytes;
    if (!read_file(full, bytes)) return -3;

    unsigned char *rgba = nullptr;
    int w = 0, h = 0;
    if (!mcle_png_decode_rgba8(bytes.data(), (unsigned long)bytes.size(),
                                 &rgba, &w, &h)) {
        return -4;
    }

    // RGBA bytes (R,G,B,A in memory order) -> ARGB int (0xAARRGGBB)
    // matches what upstream BufferedImage::data stores.
    int *argb = (int *)std::malloc((size_t)w * (size_t)h * sizeof(int));
    if (!argb) {
        mcle_png_decode_free(rgba);
        return -5;
    }
    const size_t pixels = (size_t)w * (size_t)h;
    for (size_t i = 0; i < pixels; ++i) {
        unsigned int r = rgba[i * 4 + 0];
        unsigned int g = rgba[i * 4 + 1];
        unsigned int b = rgba[i * 4 + 2];
        unsigned int a = rgba[i * 4 + 3];
        argb[i] = (int)((a << 24) | (r << 16) | (g << 8) | b);
    }
    mcle_png_decode_free(rgba);

    *out_width = (unsigned int)w;
    *out_height = (unsigned int)h;
    *out_data = argb;
    return 0;  // ERROR_SUCCESS
}
