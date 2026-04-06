#pragma once

#define SPNG_STATIC

#include <spng.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <stdexcept>
#include <vector>

#include "types.h"

class RawImage
{
    uint32_t _width = 0;
    uint32_t _height = 0;
    uint32_t _channels = 0;

public:
    RawImage() = default;

    RawImage(int width, int height, int channels = 4)
    {
        resize(width, height, channels);
    }

    std::vector<uint8_t> pixels;

    uint64_t size() const
    {
        return pixels.size();
    }

    uint32_t width() const { return _width; }
    uint32_t height() const { return _height; }

    const uint8_t *data() const
    {
        return pixels.data();
    }

    void resize(int width, int height, int channels = 4)
    {
        this->_width = width;
        this->_height = height;
        this->_channels = channels;

        pixels.assign(width * height * channels, 0);
    }

    void clear()
    {
        pixels.clear();

        _width = 0;
        _height = 0;
        _channels = 0;
    }

    void loadPNG(const BytesBuffer &png, size_t offset, size_t size)
    {
        spng_ctx *ctx = spng_ctx_new(0);

        spng_set_png_buffer(ctx, png.data() + offset, size);

        struct spng_ihdr ihdr;
        spng_get_ihdr(ctx, &ihdr);

        resize(ihdr.width, ihdr.height, 4);

        size_t out_size;
        spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &out_size);

        int ret = spng_decode_image(ctx, pixels.data(), out_size, SPNG_FMT_RGBA8, 0);

        spng_ctx_free(ctx);
    }

    void loadPNG(const BytesBuffer &png)
    {
        loadPNG(png, 0, png.size());
    }

    void saveToPNG(const std::string &filename) const
    {
        FILE *f = fopen(filename.c_str(), "wb");
        if (!f) return;

        spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);

        spng_set_png_file(ctx, f);

        struct spng_ihdr ihdr = { 0 };
        ihdr.width = _width;
        ihdr.height = _height;
        ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
        ihdr.bit_depth = 8;

        spng_set_ihdr(ctx, &ihdr);

        int ret = spng_encode_image(ctx, pixels.data(), this->size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);

        spng_ctx_free(ctx);
        fclose(f);

        if (ret != 0)
        {
            throw std::runtime_error(std::format("Faile saving image, errcode={}", ret));
        }
    }

    bool isEmpty()
    {
        for (int i = 0; i < pixels.size(); i += 4)
        {
            uint8_t r = pixels[i + 0];
            uint8_t g = pixels[i + 1];
            uint8_t b = pixels[i + 2];
            uint8_t a = pixels[i + 3];

            int sum = (r + g + b) * a;

            if (sum > 0)
            {
                return false;
            }
        }

        return true;
    }
};