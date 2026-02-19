#include "png_texture.h"

#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include <cstdint>
#include <string>

namespace k2d {

namespace {

struct StbiFreeDeleter {
    void operator()(unsigned char *p) const { stbi_image_free(p); }
};

static bool ReadFileToBuffer(const char *file_path, std::string *out, std::string *out_error) {
    if (!file_path || file_path[0] == '\0') {
        if (out_error) *out_error = "file_path is null/empty";
        return false;
    }

    SDL_IOStream *io = SDL_IOFromFile(file_path, "rb");
    if (!io) {
        if (out_error) *out_error = std::string("SDL_IOFromFile failed: ") + SDL_GetError();
        return false;
    }

    const Sint64 size = SDL_GetIOSize(io);
    if (size <= 0) {
        SDL_CloseIO(io);
        if (out_error) *out_error = "SDL_GetIOSize failed or empty file";
        return false;
    }

    out->resize(static_cast<std::size_t>(size));
    const size_t got = SDL_ReadIO(io, out->data(), out->size());
    SDL_CloseIO(io);

    if (got != out->size()) {
        if (out_error) *out_error = "SDL_ReadIO failed/incomplete read";
        return false;
    }

    return true;
}

}  // namespace

SDL_Texture *LoadPngTexture(SDL_Renderer *renderer,
                           const char *file_path,
                           int *out_width,
                           int *out_height,
                           std::string *out_error) {
    if (out_error) out_error->clear();

    if (!renderer) {
        if (out_error) *out_error = "renderer is null";
        return nullptr;
    }

    std::string file_bytes;
    if (!ReadFileToBuffer(file_path, &file_bytes, out_error)) {
        return nullptr;
    }

    int w = 0;
    int h = 0;
    int comp = 0;

    // Force decode to RGBA (4 channels).
    unsigned char *pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(file_bytes.data()),
                                                  static_cast<int>(file_bytes.size()),
                                                  &w,
                                                  &h,
                                                  &comp,
                                                  4);
    if (!pixels) {
        if (out_error) *out_error = std::string("stbi_load_from_memory failed: ") + stbi_failure_reason();
        return nullptr;
    }

    // Wrap decoded pixels into an SDL_Surface and then create a texture from it.
    // SDL will not take ownership of the pixel buffer, so we must keep it alive until texture creation finishes.
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
    if (!surface) {
        stbi_image_free(pixels);
        if (out_error) *out_error = std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError();
        return nullptr;
    }

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    stbi_image_free(pixels);

    if (!tex) {
        if (out_error) *out_error = std::string("SDL_CreateTextureFromSurface failed: ") + SDL_GetError();
        return nullptr;
    }

    if (out_width) *out_width = w;
    if (out_height) *out_height = h;

    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

}  // namespace k2d
