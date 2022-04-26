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

#include "GLProducer.h"
#include "Console.h"
#include "CudaUtils.h"

GLProducer::GLProducer(const TestFormat& format, size_t simulatedProcessing)
    : Producer(format, simulatedProcessing)
    , m_monitor(nullptr)
    , m_window(nullptr)
    , m_cudaBuffer(nullptr)
{
    glfwInit();
}

GLProducer::~GLProducer()
{
    Close();

    glfwTerminate();
}

void GLProducer::ShowSupportedVideoModes(GLFWmonitor* monitor)
{
    Log(std::endl << "Available modes for display '" << glfwGetMonitorName(monitor) << "':");
    int modeCount;
    const GLFWvidmode* modes = glfwGetVideoModes(monitor, &modeCount);
    for (int i = 0; i < modeCount; i++)
    {
        Log("    " << modes[i].width << "x" << modes[i].height << " @ " << modes[i].refreshRate << "Hz");
    }
    Log("");
}

bool GLProducer::Initialize()
{
    int monitorCount;
    glfwGetMonitors(&monitorCount);
    if (monitorCount > 1)
    {
        Error("More than one display is connected. GLFW currently has troubles" << std::endl <<
              "       initializing the display correctly when more than one display is" << std::endl <<
              "       connected. Please disconnect the other display(s) and try again.");
        return false;
    }

    m_monitor = glfwGetPrimaryMonitor();
    if (!m_monitor)
    {
        Error("Failed to get a handle to the display (is the DISPLAY environment variable set?");
        return false;
    }

    glfwWindowHint(GLFW_REFRESH_RATE, m_format.frameRate);
    m_window = glfwCreateWindow(m_format.width, m_format.height, "GLRenderer", m_monitor, nullptr);
    const GLFWvidmode* mode = glfwGetVideoMode(m_monitor);
    int windowWidth, windowHeight;
    glfwGetWindowSize(m_window, &windowWidth, &windowHeight);
    if (!m_window || !mode ||
        mode->width != m_format.width ||
        mode->height != m_format.height ||
        mode->refreshRate != m_format.frameRate ||
        windowWidth != m_format.width ||
        windowHeight != m_format.height)
    {
        if (mode && mode->refreshRate == 0)
        {
            Error("Failed to get the monitor mode (is the display cable attached?)");
        }
        else
        {
            Error("Failed to create a " << m_format.width << "x" << m_format.height << " @ " <<
                  m_format.frameRate << "Hz fullscreen window.");
            ShowSupportedVideoModes(m_monitor);
            Warning("If the requested format is in the list of supported" << std::endl <<
                    "         formats above, try resetting the current display mode with" << std::endl <<
                    "         the xrandr tool using the following command:" << std::endl << std::endl <<
                    "           $ xrandr --output " << glfwGetMonitorName(m_monitor) <<
                    " --mode " << m_format.width << "x" << m_format.height <<
                    " --panning " << m_format.width << "x" << m_format.height <<
                    " --rate " << m_format.frameRate << std::endl);
        }
        return false;
    }

    // Allocate the scratch CUDA buffer.
    m_cudaBuffer = CudaAlloc(m_format.totalBytes);
    if (!m_cudaBuffer)
    {
        Error("Failed to allocate CUDA memory.");
        return false;
    }

    return true;
}

void GLProducer::Close()
{
    if (IsStreaming())
        StopStreaming();

    if (m_cudaBuffer)
        CudaFree(m_cudaBuffer);
    m_cudaBuffer = nullptr;

    if (m_window)
        glfwDestroyWindow(m_window);
    m_window = nullptr;
}

std::ostream& GLProducer::Dump(std::ostream& o) const
{
    o << "OpenGL" << std::endl
      << "    RDMA: 1 (Always enabled, outputs directly from GPU)" << std::endl;
    return o;
}

void GLProducer::StreamThread()
{
    glfwMakeContextCurrent(m_window);

    glfwSwapInterval(1);

    while (IsStreaming())
    {
        auto frame = StartFrame();

        frame->RecordProcessingStart();

        // Simulate processing time.
        size_t elementCount = m_format.width * m_format.height;
        CudaSimulateProcessing((uint32_t*)m_cudaBuffer, elementCount, m_simulatedProcessing);

        frame->RecordRenderStart();

        // Render the frame.
        glClearColor(frame->R() / 255.0f, frame->G() / 255.0f, frame->B() / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();

        frame->RecordRenderEnd();
        frame->RecordCopiedFromGPU();
        frame->RecordWriteEnd();

        // Present the frame and wait for scanout to start
        // Note: The glFinish here is essentially blocking until the back buffer
        //       for the next frame is available for rendering (implying that
        //       scanout of the front buffer has begun).
        glfwSwapBuffers(m_window);
        glFinish();

        frame->RecordScanoutStart();
    }

    glfwMakeContextCurrent(nullptr);
}
