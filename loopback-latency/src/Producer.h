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

#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include "TestFormat.h"
#include "Frame.h"

class Producer
{
public:

    virtual ~Producer();

    const TestFormat& Format() const;

    virtual bool Initialize() = 0;
    virtual void Close() = 0;
    virtual bool StartStreaming();
    virtual void StopStreaming();

    bool IsStreaming() const;
    std::shared_ptr<Frame> GetFrame(const void* ptr);

protected:

    Producer(const TestFormat& format, size_t simulatedProcessing);

    std::shared_ptr<Frame> StartFrame();

    virtual void StreamThread() = 0;
    virtual std::ostream& Dump(std::ostream& o) const = 0;

    TestFormat m_format;
    size_t m_simulatedProcessing;

private:

    static bool FuzzyMatch(const std::shared_ptr<Frame>& frame,
            uint8_t r, uint8_t g, uint8_t b, uint8_t threshold);

    static void StreamThreadStatic(Producer* producer);

    bool m_streaming;
    std::thread m_streamThread;

    uint32_t m_currentFrame;
    std::list<std::shared_ptr<Frame>> m_frames;
    std::mutex m_framesMutex;

    friend std::ostream& operator<<(std::ostream& o, const Producer& p);
};
