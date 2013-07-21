/*
 * High dynamic range color library.
 *
 * This is a high dynamic range (48-bit) color data type,
 * and a temporal dithering implementation that's compatible
 * with the OctoWS2811 LED driver.
 * 
 * Copyright (c) 2013 Micah Elizabeth Scott
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once
#include <stdint.h>
#include <algorithm>

class OctoWS2811;

/// Basic data type for a high-dynamic-range color.
struct HColor {
    uint16_t r, g, b;
};

/// Constructor for 16-bit colors
static inline HColor HColor16(uint16_t r, uint16_t g, uint16_t b) {
    HColor c = { r, g, b };
    return c;
}

/// Constructor for 8-bit colors
static inline HColor HColor8(uint8_t r, uint8_t g, uint8_t b) {
    HColor c = {
        uint16_t(r | (unsigned(r) << 8)),
        uint16_t(g | (unsigned(g) << 8)),
        uint16_t(b | (unsigned(b) << 8)),
    };
    return c;
}

/// Constructor for 8-bit colors packed into a 24-bit word
static inline HColor HColor8(uint32_t rgb) {
    return HColor8( (rgb & 0xFF0000) >> 16, (rgb & 0x00FF00) >> 8, rgb & 0x0000FF );
}

/// Constructor for float colors, with clamping.
static inline HColor HColorF(float r, float g, float b) {
    HColor c = {
        uint16_t(std::min<int>(0xffff, std::max<int>(0, r * 65535.0f + 0.5f))),
        uint16_t(std::min<int>(0xffff, std::max<int>(0, g * 65535.0f + 0.5f))),
        uint16_t(std::min<int>(0xffff, std::max<int>(0, b * 65535.0f + 0.5f))),
    };
    return c;
}

/// Add two colors, with saturation
static inline HColor operator + (HColor a, HColor b) {
    HColor c = {
        uint16_t(std::min<int>(0xffff, unsigned(a.r) + unsigned(b.r))),
        uint16_t(std::min<int>(0xffff, unsigned(a.g) + unsigned(b.g))),
        uint16_t(std::min<int>(0xffff, unsigned(a.b) + unsigned(b.b))),
    };
    return c;
}

/**
 * Linear interpolation between two colors. "Alpha" is in 8-bit fixed point.
 * Returns c1 if alpha==0, or c2 if alpha==0x100. Values outside this range will extrapolate.
 */
static inline HColor lerp8(HColor c1, HColor c2, int alpha) {
    int invA = 0x100 - alpha;
    HColor c = {
        uint16_t((c1.r * invA + c2.r * alpha) >> 8),
        uint16_t((c1.g * invA + c2.g * alpha) >> 8),
        uint16_t((c1.b * invA + c2.b * alpha) >> 8),
    };
    return c;
}

/// Floating point linear interpolation, with clamping.
static inline HColor lerp(HColor c1, HColor c2, float alpha) {
    float invA = 1.0f - alpha;
    HColor c = {
        uint16_t(std::min<int>(0xffff, std::max<int>(0, c1.r * invA + c2.r * alpha))),
        uint16_t(std::min<int>(0xffff, std::max<int>(0, c1.g * invA + c2.g * alpha))),
        uint16_t(std::min<int>(0xffff, std::max<int>(0, c1.b * invA + c2.b * alpha))),
    };
    return c;
}

/// Data type for one display pixel
struct HPixel {
    HColor color;
    int16_t residual[3];

    /// Temporal dithering algorithm. Returns a 24-bit RGB color.
    uint32_t dither() {

        // Incorporate the residual from last frame
        int r16 = color.r + residual[0];
        int g16 = color.g + residual[1];
        int b16 = color.b + residual[2];

        // Round to the nearest 8-bit value
        int r8 = std::min<int>(0xff, std::max<int>(0, (r16 + 0x80) >> 8));
        int g8 = std::min<int>(0xff, std::max<int>(0, (g16 + 0x80) >> 8));
        int b8 = std::min<int>(0xff, std::max<int>(0, (b16 + 0x80) >> 8));

        // Compute the error, after expanding the 8-bit value back to 16-bit.
        residual[0] = r16 - (r8 * 257);
        residual[1] = g16 - (g8 * 257);
        residual[2] = b16 - (b8 * 257);

        return (r8 << 16) | (g8 << 8) | b8;
    }
};

/// Data type for a framebuffer of pixels
template <unsigned tCount>
struct HPixelBuffer {
    HPixel pixels[tCount];

    /// Update the entire frame
    void show(OctoWS2811 &leds) {
        for (unsigned i = 0; i < tCount; ++i) {
            leds.setPixel(i, pixels[i].dither());
        }
        leds.show();
    }
};