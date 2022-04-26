/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <cstring>

enum PixelFormat
{
    PIXEL_FORMAT_UNKNOWN,
    PIXEL_FORMAT_RGBA,
};

struct TestFormat
{
    constexpr TestFormat(size_t _width, size_t _height, PixelFormat _pixelFormat, uint32_t _frameRate)
        : width(_width)
        , height(_height)
        , pixelFormat(_pixelFormat)
        , bytesPerPixel(GetBytesPerPixel(pixelFormat))
        , totalBytes(width * height * bytesPerPixel)
        , frameRate(_frameRate) {}

    size_t width;
    size_t height;
    PixelFormat pixelFormat;
    size_t bytesPerPixel;
    size_t totalBytes;
    uint32_t frameRate;

private:

    constexpr size_t GetBytesPerPixel(PixelFormat format)
    {
        switch (pixelFormat)
        {
            case PIXEL_FORMAT_RGBA: return 4;
            default: return 0;
        }
    }

};

inline bool operator==(const TestFormat& a, const TestFormat& b)
{
    return memcmp(&a, &b, sizeof(TestFormat)) == 0;
}

inline std::ostream& operator<<(std::ostream& o, const TestFormat& f)
{
    o << f.width << "x" << f.height << " ";
    switch (f.pixelFormat)
    {
        case PIXEL_FORMAT_RGBA: o << "RGBA"; break;
    }
    o << " @ " << f.frameRate << "Hz";
    return o;
}

constexpr TestFormat FORMAT_UNKNOWN      (   0,    0, PIXEL_FORMAT_UNKNOWN, 0);
constexpr TestFormat FORMAT_720_RGBA_60  (1280,  720, PIXEL_FORMAT_RGBA,   60);
constexpr TestFormat FORMAT_1080_RGBA_60 (1920, 1080, PIXEL_FORMAT_RGBA,   60);
constexpr TestFormat FORMAT_UHD_RGBA_24  (3840, 2160, PIXEL_FORMAT_RGBA,   24);
constexpr TestFormat FORMAT_UHD_RGBA_60  (3840, 2160, PIXEL_FORMAT_RGBA,   60);
constexpr TestFormat FORMAT_4K_RGBA_24   (4096, 2160, PIXEL_FORMAT_RGBA,   24);
constexpr TestFormat FORMAT_4K_RGBA_60   (4096, 2160, PIXEL_FORMAT_RGBA,   60);
