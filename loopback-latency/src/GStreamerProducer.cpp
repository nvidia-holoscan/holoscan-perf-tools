/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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

#include <gdk/gdkx.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>

#ifdef ENABLE_DEEPSTREAM
#include <nvbufsurface.h>
#include <gstnvdsbufferpool.h>
#endif

#include "GStreamerProducer.h"
#include "Console.h"
#include "CudaUtils.h"

GStreamerProducer::GStreamerProducer(int* argc, char** argv[],
                                     const TestFormat& format,
                                     size_t simulatedProcessing,
                                     bool useRDMA)
    : Producer(format, simulatedProcessing)
    , m_useRDMA(useRDMA)
    , m_loop(nullptr)
    , m_pipeline(nullptr)
    , m_source(nullptr)
    , m_sink(nullptr)
    , m_caps(nullptr)
    , m_pool(nullptr)
    , m_cudaBuffer(nullptr)
{
    gtk_init(argc, argv);
    gst_init(argc, argv);
}

GStreamerProducer::~GStreamerProducer()
{
    Close();
}

bool GStreamerProducer::Initialize()
{
    // Create the GStreamer elements.
    m_pipeline = gst_pipeline_new("gstreamer-producer");
    m_source = gst_element_factory_make("appsrc", "app-source");
    m_sink = gst_element_factory_make("nveglglessink", "nv-egl-gles-sink");
    if (!m_pipeline || !m_source || !m_sink)
    {
        Error("Failed to create a required GStreamer element.");
        return false;
    }

    // Set the format caps.
    m_caps = gst_caps_new_empty();
    GstStructure* s = gst_structure_new("video/x-raw",
        "format", G_TYPE_STRING, GetCapsFormat(m_format.pixelFormat).c_str(),
        "width", G_TYPE_INT, m_format.width,
        "height", G_TYPE_INT, m_format.height,
        "framerate", GST_TYPE_FRACTION, m_format.frameRate, 1, NULL);
    gst_caps_append_structure(m_caps, s);
    if (m_useRDMA)
    {
#ifdef ENABLE_DEEPSTREAM
        GstCapsFeatures* f = gst_caps_features_new("memory:NVMM", NULL);
        gst_caps_set_features(m_caps, 0, f);
#else
        Error("RDMA support in the GStreamer Producer requires DeepStream.");
        return false;
#endif
    }
    gst_app_src_set_caps(GST_APP_SRC(m_source), m_caps);

    // Set the appsrc to queue (and block) just a single buffer.
    size_t maxBytes = m_format.totalBytes;
#ifdef ENABLE_DEEPSTREAM
    if (m_useRDMA)
    {
        maxBytes = sizeof(NvBufSurface);
    }
#endif
    g_object_set(G_OBJECT(m_source),
        "max-bytes", maxBytes,
        "block", TRUE,
        "format", GST_FORMAT_TIME, NULL);

    // Set the EGL sink to not create a window (we create one for it).
    g_object_set(G_OBJECT(m_sink), "create-window", FALSE, "sync", FALSE, NULL);

    // Add the elements and link the pipeline.
    gst_bin_add_many(GST_BIN(m_pipeline), m_source, m_sink, NULL);
    if (!gst_element_link_many(m_source, m_sink, NULL))
    {
        Error("Failed to link GStreamer elements.");
        return false;
    }

#ifdef ENABLE_DEEPSTREAM
    // Create the NvDsBufferPool for RDMA.
    if (m_useRDMA)
    {
        m_pool = gst_nvds_buffer_pool_new();
        if (!m_pool)
        {
            Error("Failed to create the NvDsBufferPool.");
            return false;
        }

        GstStructure* config = gst_buffer_pool_get_config(m_pool);
        gst_buffer_pool_config_set_params(config, m_caps, sizeof(NvBufSurface), 0, 0);
        gst_structure_set(config,
            "memtype", G_TYPE_UINT, NVBUF_MEM_CUDA_DEVICE,
            "gpu-id", G_TYPE_UINT, 0, NULL);

        if (!gst_buffer_pool_set_config(m_pool, config))
        {
            Error("Failed to set NvDsBufferPool config.");
            return false;
        }

        gst_buffer_pool_set_active(m_pool, true);
    }
#endif

    // Allocate the scratch CUDA buffer.
    m_cudaBuffer = CudaAlloc(m_format.totalBytes);
    if (!m_cudaBuffer)
    {
        Error("Failed to allocate CUDA memory.");
        return false;
    }

    // Check the display configuration.
    GdkDisplay* display = gdk_display_get_default();
    if (!display)
    {
        Error("Failed to get a handle to the display (is the DISPLAY environment variable set?");
        return false;
    }

    if (gdk_display_get_n_monitors(display) > 1)
    {
        Error("More than one display is connected. The GStreamer producer does" << std::endl <<
              "       not work correctly when more than one display is connected." << std::endl <<
              "       Please disconnect the other display(s) and try again.");
        return false;
    }

    GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
    if (!monitor || !gdk_monitor_get_model(monitor))
    {
        Error("Failed to get primary monitor (is the display cable attached?)");
        return false;
    }

    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    int scaleFactor = gdk_monitor_get_scale_factor(monitor);
    int refreshRate = gdk_monitor_get_refresh_rate(monitor) / 1000;
    if (geometry.width * scaleFactor != m_format.width ||
        geometry.height * scaleFactor != m_format.height ||
        refreshRate != m_format.frameRate)
    {
        Error("The requested format (" << m_format.width << "x" << m_format.height << " @ " <<
              m_format.frameRate << "Hz) does not match" << std::endl <<
              "       the current display mode (" << (geometry.width * scaleFactor) << "x" <<
              (geometry.height * scaleFactor) << " @ " << refreshRate << "Hz)" << std::endl <<
              "       Please set the display mode with the xrandr tool using" << std::endl <<
              "       the following comand:" << std::endl << std::endl <<
              "           $ xrandr --output " << gdk_monitor_get_model(monitor) <<
              " --mode " << m_format.width << "x" << m_format.height <<
              " --panning " << m_format.width << "x" << m_format.height <<
              " --rate " << m_format.frameRate << std::endl << std::endl <<
              "       If the mode still does not match after running the above," << std::endl <<
              "       check the output of an 'xrandr' command to ensure that" << std::endl <<
              "       the mode is supported by the devices.");
        return false;
    }

    // Create the window for the rendering overlay.
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget* videoWidget = gtk_drawing_area_new();
    g_signal_connect(videoWidget, "realize", G_CALLBACK(RealizeCallback), this);
    gtk_container_add(GTK_CONTAINER(window), videoWidget);
    gtk_window_set_default_size(GTK_WINDOW(window), 1920, 1080);
    gtk_window_fullscreen(GTK_WINDOW(window));
    gtk_widget_show_all(window);

    // Create the main loop to handle GLib events.
    m_loop = g_main_loop_new(NULL, FALSE);

    return true;
}

void GStreamerProducer::Close()
{
    if (IsStreaming())
        StopStreaming();

    if (m_cudaBuffer)
        CudaFree(m_cudaBuffer);
    m_cudaBuffer = nullptr;

    if (m_pool)
    {
        gst_buffer_pool_set_active(m_pool, false);
        gst_object_unref(GST_OBJECT(m_pool));
    }
    m_pool = nullptr;

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

bool GStreamerProducer::StartStreaming()
{
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    return Producer::StartStreaming();
}

void GStreamerProducer::StopStreaming()
{
    gst_element_set_state(m_pipeline, GST_STATE_NULL);

    Producer::StopStreaming();
}

std::ostream& GStreamerProducer::Dump(std::ostream& o) const
{
    o << "GStreamer" << std::endl
      << "    RDMA: " << m_useRDMA << std::endl
      << "    Note: The start of scanout is not known to the GStreamer producer," << std::endl
      << "          since this is handled privatly by the nveglglessink sink." << std::endl
      << "          Because of this, the 'Wire Time' below includes all of the time" << std::endl
      << "          that the frame spends between being passed to the nveglglessink" << std::endl
      << "          and when it is finally received by the consumer." << std::endl;
    return o;
}

void GStreamerProducer::RealizeCallback(GtkWidget* widget, GStreamerProducer* producer)
{
    GdkWindow* window = gtk_widget_get_window(widget);
    if (!gdk_window_ensure_native(window))
    {
        Error("Could not create native window for GStreamer overlay.");
        return;
    }

    guintptr windowHandle = GDK_WINDOW_XID(window);
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(producer->m_sink), windowHandle);
}

void GStreamerProducer::StreamThread()
{
    while (IsStreaming())
    {
        g_main_context_iteration(g_main_loop_get_context(m_loop), FALSE);

        auto frame = StartFrame();

        frame->RecordProcessingStart();

        // Simulate processing time.
        size_t elementCount = m_format.width * m_format.height;
        CudaSimulateProcessing((uint32_t*)m_cudaBuffer, elementCount, m_simulatedProcessing);

        frame->RecordRenderStart();

        GstBuffer* buf(nullptr);
        GstMapInfo map;
#ifdef ENABLE_DEEPSTREAM
        if (m_useRDMA)
        {
            // Acquire and fill a new NvBufSurface directly.
            gst_buffer_pool_acquire_buffer(m_pool, &buf, NULL);

            gst_buffer_map(buf, &map, (GstMapFlags)(GST_MAP_READ | GST_MAP_WRITE));
            NvBufSurface* surf = (NvBufSurface*)map.data;
            CudaWriteRGBA((uint32_t*)surf->surfaceList->dataPtr, elementCount, frame->R(), frame->G(), frame->B());
            gst_buffer_unmap(buf, &map);
        }
        else
#endif
        {
            // Write to the scratch CUDA buffer.
            CudaWriteRGBA((uint32_t*)m_cudaBuffer, elementCount, frame->R(), frame->G(), frame->B());
        }

        frame->RecordRenderEnd();

        if (!m_useRDMA)
        {
            // Copy the scratch CUDA buffer to host memory.
            buf = gst_buffer_new_and_alloc(m_format.totalBytes);
            gst_buffer_map(buf, &map, GST_MAP_WRITE);
            CudaMemcpyDtoH(map.data, m_cudaBuffer, m_format.totalBytes);
            gst_buffer_unmap(buf, &map);
        }

        frame->RecordCopiedFromGPU();

        frame->RecordWriteEnd();

        // Push the buffer to the appsrc.
        gst_app_src_push_buffer(GST_APP_SRC(m_source), buf);

        frame->RecordScanoutStart();
    }
}

std::string GStreamerProducer::GetCapsFormat(PixelFormat format)
{
    switch (format)
    {
        case PIXEL_FORMAT_RGBA: return "RGBA";
        default: return "UNKNOWN";
    }
}
