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

#include <unistd.h>
#include <gst/app/gstappsink.h>

#include "GStreamerConsumer.h"
#include "Console.h"
#include "CudaUtils.h"

GStreamerConsumer::GStreamerConsumer(std::shared_ptr<Producer> producer,
                                     int* argc, char** argv[],
                                     const std::string& device)
    : Consumer(producer)
    , m_device(device.size() == 0 ? "/dev/video0" : device)
    , m_loop(nullptr)
    , m_pipeline(nullptr)
    , m_source(nullptr)
    , m_sink(nullptr)
    , m_caps(nullptr)
    , m_busWatchID(0)
    , m_cudaBuffer(nullptr)
    , m_warmupFramesRemaining(0)
    , m_framesRemaining(0)
{
    gst_init(argc, argv);
}

GStreamerConsumer::~GStreamerConsumer()
{
    Close();
}

bool GStreamerConsumer::Initialize()
{
    // Create the GStreamer elements.
    m_pipeline = gst_pipeline_new("v4l2-consumer");
    m_source = gst_element_factory_make("v4l2src", "v4l2-camera-src");
    m_sink = gst_element_factory_make("appsink", "app-sink");
    if (!m_pipeline || !m_source || !m_sink)
    {
        Error("Failed to create a required GStreamer element.");
        return false;
    }

    // Set the V4L2 device.
    g_object_set(G_OBJECT(m_source), "device", m_device.c_str(), NULL);

    // Set the format caps.
    m_caps = gst_caps_new_empty();
    GstStructure* s = gst_structure_new("video/x-raw",
        "format", G_TYPE_STRING, GetCapsFormat(m_producer->Format().pixelFormat).c_str(),
        "width", G_TYPE_INT, m_producer->Format().width,
        "height", G_TYPE_INT, m_producer->Format().height,
        "framerate", GST_TYPE_FRACTION, m_producer->Format().frameRate, 1, NULL);
    gst_caps_append_structure(m_caps, s);
    gst_app_sink_set_caps(GST_APP_SINK(m_sink), m_caps);

    // Configure the buffer callback.
    g_object_set(m_sink, "emit-signals", TRUE, NULL);
    g_signal_connect(m_sink, "new_sample", G_CALLBACK(BufferCallbackStatic), this);

    // Add the bus watcher callback to handle events.
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    m_busWatchID = gst_bus_add_watch(bus, BusCallbackStatic, this);
    gst_object_unref(bus);

    // Add the elements and link the pipeline.
    gst_bin_add_many(GST_BIN(m_pipeline), m_source, m_sink, NULL);
    if (!gst_element_link_many(m_source, m_sink, NULL))
    {
        Error("Failed to link GStreamer elements.");
        return false;
    }

    // Create the main loop to handle GLib events.
    m_loop = g_main_loop_new(NULL, FALSE);

    // Allocate the CUDA buffer.
    m_cudaBuffer = CudaAlloc(m_producer->Format().totalBytes);
    if (!m_cudaBuffer)
    {
        Error("Failed to allocate CUDA memory.");
        return false;
    }

    return true;
}

void GStreamerConsumer::Close()
{
    if (m_cudaBuffer)
        CudaFree(m_cudaBuffer);
    m_cudaBuffer = nullptr;

    if (m_busWatchID)
        g_source_remove(m_busWatchID);
    m_busWatchID = 0;

    if (m_loop)
        g_main_loop_unref(m_loop);
    m_loop = nullptr;

    if (m_caps)
        gst_caps_unref(m_caps);
    m_caps = nullptr;

    if (m_pipeline)
        gst_object_unref(GST_OBJECT(m_pipeline));
    m_pipeline = nullptr;
}

bool GStreamerConsumer::StartStreaming()
{
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    return true;
}

void GStreamerConsumer::StopStreaming()
{
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
}

bool GStreamerConsumer::CaptureFrames(size_t numFrames, size_t warmupFrames)
{
    // Tell the callback how many frames to measure.
    m_frameCountMutex.lock();
    m_numFrames = numFrames;
    m_warmupFramesRemaining = warmupFrames;
    m_framesRemaining = numFrames;
    m_frameCountMutex.unlock();

    // Wait until the callback has measured the requested frames.
    bool done = false;
    m_eos = false;
    while (!done)
    {
        g_main_context_iteration(g_main_loop_get_context(m_loop), FALSE);
        if (m_eos)
        {
            StopStreaming();
            return false;
        }

        m_frameCountMutex.lock();
        done = m_framesRemaining == 0 && m_warmupFramesRemaining == 0;
        m_frameCountMutex.unlock();
        usleep(1000);
    }
    Log(numFrames << " / " << numFrames);

    return true;
}

GstFlowReturn GStreamerConsumer::BufferCallback(GstElement* sink)
{
    // Get the sample and buffer from the app sink.
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample)
    {
        Error("Failed to get GStreamer sample.");
        return GST_FLOW_ERROR;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer)
    {
        Error("Failed to get GStreamer buffer.");
        return GST_FLOW_ERROR;
    }

    TimePoint receiveTime = Clock::now();

    std::lock_guard<std::mutex> lock(m_frameCountMutex);
    if (m_warmupFramesRemaining)
    {
        m_warmupFramesRemaining--;
    }
    else if (m_framesRemaining)
    {
        GstMapInfo map;
        if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        {
            Error("Failed to map GStreamer buffer.");
            return GST_FLOW_ERROR;
        }

        TimePoint readEnd = Clock::now();

        // Copy the buffer to GPU.
        CudaMemcpyHtoD(m_cudaBuffer, map.data, m_producer->Format().totalBytes);

        TimePoint copiedToGPU = Clock::now();

        // Get the frame pointer from the producer.
        auto frame = m_producer->GetFrame(map.data);
        if (!frame)
        {
            return GST_FLOW_ERROR;
        }

        gst_buffer_unmap(buffer, &map);

        if (m_frames.size() && m_frames.back()->Number() == frame->Number())
        {
            // If this frame has already been received, increment the duplicate count.
            frame->RecordDuplicateReceive();
        }
        else
        {
            // Otherwise, record the times and add it to the consumer list.
            frame->RecordFrameReceived(receiveTime);
            frame->RecordReadEnd(readEnd);
            frame->RecordCopiedToGPU(copiedToGPU);
            m_frames.push_back(frame);
        }

        if (m_framesRemaining != m_numFrames && (m_numFrames - m_framesRemaining) % 100 == 0)
        {
            Log((m_numFrames - m_framesRemaining) << " / " << m_numFrames);
        }

        m_framesRemaining--;
    }

    // Release the sample and buffer reference.
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

GstFlowReturn GStreamerConsumer::BufferCallbackStatic(GstElement* sink, GStreamerConsumer* consumer)
{
    return consumer->BufferCallback(sink);
}

std::ostream& GStreamerConsumer::Dump(std::ostream& o) const
{
    o << "GStreamer" << std::endl
      << "    Device: " << m_device << std::endl
      << "    RDMA: 0 (Not supported)" << std::endl;
    return o;
}

std::string GStreamerConsumer::GetCapsFormat(PixelFormat format)
{
    switch (format)
    {
        // Note: The V4L2 GStreamer source uses "BGRA" as the format, even though
        //       the actual input format is RGBA.
        case PIXEL_FORMAT_RGBA: return "BGRA";
        default: return "UNKNOWN";
    }
}

gboolean GStreamerConsumer::BusCallbackStatic(GstBus* bus, GstMessage* msg, gpointer data)
{
    GStreamerConsumer* consumer = static_cast<GStreamerConsumer*>(data);

    gchar* debug;
    GError* error;

    switch (GST_MESSAGE_TYPE(msg))
    {
        case GST_MESSAGE_EOS:
            consumer->m_eos = true;
            break;
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);
            Error("GStreamer Consumer " << error->message << std::endl <<
                  "Note that this may due to an unsupported format being used.");
            g_error_free(error);
            consumer->m_eos = true;
            break;
        default:
            break;
    }

    return TRUE;
}
