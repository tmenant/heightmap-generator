#pragma once

#include <cstdint>

struct Color
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;

    constexpr Color()
    {
    }

    constexpr Color(int r, int g, int b)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = 255;
    }

    constexpr Color(int r, int g, int b, int a)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }
};