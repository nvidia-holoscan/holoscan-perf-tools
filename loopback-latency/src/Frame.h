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

#include "DurationList.h"

class Frame
{
public:

    Frame(uint32_t number)
        : m_number(number)
        , m_duplicateReceives(0)
    {
        // This creates a color value that increments (and wraps) one or more of
        // the RGB values by 16 between successive frames. The +8 offset is added
        // to ensure that 0 will never be used (useful to detect invalid frames).
        m_r = ((number & 0xF00) >> 8) * 16 + 8;
        m_g = ((number & 0xF0) >> 4) * 16 + 8;
        m_b = (number & 0xF) * 16 + 8;
    }

    uint32_t Number() const { return m_number; }

    uint8_t R() const { return m_r; }
    uint8_t G() const { return m_g; }
    uint8_t B() const { return m_b; }

    void RecordProcessingStart() { m_processingStart = Clock::now(); }
    void RecordRenderStart() { m_renderStart = Clock::now(); }
    void RecordRenderEnd() { m_renderEnd = Clock::now(); }
    void RecordCopiedFromGPU() { m_copiedFromGPU = Clock::now(); }
    void RecordWriteEnd() { m_writeEnd = Clock::now(); }
    void RecordScanoutStart() { m_scanoutStart = Clock::now(); }
    void RecordFrameReceived() { m_frameReceived = Clock::now(); }
    void RecordReadEnd() { m_readEnd = Clock::now(); }
    void RecordCopiedToGPU() { m_copiedToGPU = Clock::now(); }

    void RecordProcessingStart(TimePoint time) { m_processingStart = time; }
    void RecordRenderStart(TimePoint time) { m_renderStart = time; }
    void RecordRenderEnd(TimePoint time) { m_renderEnd = time; }
    void RecordCopiedFromGPU(TimePoint time) { m_copiedFromGPU = time; }
    void RecordWriteEnd(TimePoint time) { m_writeEnd = time; }
    void RecordScanoutStart(TimePoint time) { m_scanoutStart = time; }
    void RecordFrameReceived(TimePoint time) { m_frameReceived = time; }
    void RecordReadEnd(TimePoint time) { m_readEnd = time; }
    void RecordCopiedToGPU(TimePoint time) { m_copiedToGPU = time; }

    const TimePoint& ProcessingStart() const { return m_processingStart; }
    const TimePoint& RenderStart() const { return m_renderStart; }
    const TimePoint& RenderEnd() const { return m_renderEnd; }
    const TimePoint& CopiedFromGPU() const { return m_copiedFromGPU; }
    const TimePoint& WriteEnd() const { return m_writeEnd; }
    const TimePoint& ScanoutStart() const { return m_scanoutStart; }
    const TimePoint& FrameReceived() const { return m_frameReceived; }
    const TimePoint& ReadEnd() const { return m_readEnd; }
    const TimePoint& CopiedToGPU() const { return m_copiedToGPU; }

    void RecordDuplicateReceive() { m_duplicateReceives++; }
    size_t DuplicateReceives() const { return m_duplicateReceives; }

private:

    uint32_t m_number;

    uint8_t m_r;
    uint8_t m_g;
    uint8_t m_b;

    TimePoint m_processingStart;
    TimePoint m_renderStart;
    TimePoint m_renderEnd;
    TimePoint m_copiedFromGPU;
    TimePoint m_writeEnd;
    TimePoint m_scanoutStart;
    TimePoint m_frameReceived;
    TimePoint m_readEnd;
    TimePoint m_copiedToGPU;

    size_t m_duplicateReceives;
};
