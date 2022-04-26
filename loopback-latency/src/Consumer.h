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

#include "Producer.h"

class Consumer
{
public:

    virtual ~Consumer() {}

    virtual bool Initialize() = 0;
    virtual void Close() = 0;
    virtual bool StartStreaming() = 0;
    virtual void StopStreaming() = 0;
    virtual bool CaptureFrames(size_t numFrames, size_t warmupFrames) = 0;

    virtual std::ostream& Dump(std::ostream& o) const = 0;

    const std::vector<std::shared_ptr<Frame>>& GetReceivedFrames() const { return m_frames; }

protected:

    Consumer(std::shared_ptr<Producer> producer)
        : m_producer(producer)
    {}

    std::shared_ptr<Producer> m_producer;

    std::vector<std::shared_ptr<Frame>> m_frames;
};

inline std::ostream& operator<<(std::ostream& o, const Consumer& c)
{
    return c.Dump(o);
}
