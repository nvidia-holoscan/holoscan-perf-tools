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

#include <gst/gst.h>

#include "Consumer.h"

class GStreamerConsumer : public Consumer
{
public:
    GStreamerConsumer(std::shared_ptr<Producer> producer, int* argc, char** argv[], const std::string& device);
    virtual ~GStreamerConsumer();

    virtual bool Initialize();
    virtual void Close();
    virtual bool StartStreaming();
    virtual void StopStreaming();
    virtual bool CaptureFrames(size_t numFrames, size_t warmupFrames);

    virtual std::ostream& Dump(std::ostream& o) const;

private:

    GstFlowReturn BufferCallback(GstElement* sink);
    static GstFlowReturn BufferCallbackStatic(GstElement* sink, GStreamerConsumer* consumer);

    static gboolean BusCallbackStatic(GstBus* bus, GstMessage* msg, gpointer data);

    static std::string GetCapsFormat(PixelFormat format);

    std::string m_device;

    GMainLoop* m_loop;
    GstElement* m_pipeline;
    GstElement* m_source;
    GstElement* m_sink;
    GstCaps* m_caps;
    guint m_busWatchID;

    void* m_cudaBuffer;

    std::mutex m_frameCountMutex;
    bool m_eos;
    size_t m_numFrames;
    size_t m_warmupFramesRemaining;
    size_t m_framesRemaining;
};
