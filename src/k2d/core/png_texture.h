#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace k2d {

// Load a PNG file from disk using stb_image and upload it as an SDL_Texture.
//
// - Decodes to RGBA8 (4 channels).
// - Returns nullptr on failure.
// - On success, the returned texture is owned by the caller (destroy with SDL_DestroyTexture).
// - If out_width/out_height are provided, they receive the decoded image dimensions.
// - If out_error is provided, it receives a human-readable error message on failure.
SDL_Texture *LoadPngTexture(SDL_Renderer *renderer,
                           const char *file_path,
                           int *out_width = nullptr,
                           int *out_height = nullptr,
                           std::string *out_error = nullptr);

}  // namespace k2d
