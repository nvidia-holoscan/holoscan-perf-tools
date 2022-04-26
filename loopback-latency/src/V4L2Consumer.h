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

#include "Consumer.h"

class V4L2Consumer : public Consumer
{
public:
    V4L2Consumer(std::shared_ptr<Producer> producer, const std::string& device);
    virtual ~V4L2Consumer();

    virtual bool Initialize();
    virtual void Close();
    virtual bool StartStreaming();
    virtual void StopStreaming();
    virtual bool CaptureFrames(size_t numFrames, size_t warmupFrames);

    virtual std::ostream& Dump(std::ostream& o) const;

private:

    struct Buffer
    {
        Buffer(void* _ptr, size_t _length);

        void* ptr;
        size_t length;
    };

    bool ReadFrame(bool* retry, bool warmupFrame);

    static uint32_t GetV4L2PixelFormat(PixelFormat format);

    std::string m_device;
    int m_fd;
    std::vector<Buffer> m_buffers;

    void* m_cudaBuffer;
};
