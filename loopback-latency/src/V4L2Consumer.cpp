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

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "V4L2Consumer.h"
#include "Console.h"
#include "CudaUtils.h"

V4L2Consumer::V4L2Consumer(std::shared_ptr<Producer> producer, const std::string& device)
    : Consumer(producer)
    , m_device(device.size() == 0 ? "/dev/video0" : device)
    , m_fd(-1)
    , m_cudaBuffer(nullptr)
{
}

V4L2Consumer::~V4L2Consumer()
{
    Close();

    CudaFree(m_cudaBuffer);
}

bool V4L2Consumer::Initialize()
{
    // Open the device.
    m_fd = open(m_device.c_str(), O_RDWR);
    if (m_fd < 0)
    {
        Error("Failed to open " << m_device);
        return false;
    }

    // Get and check the device capabilities.
    v4l2_capability caps;
    if (ioctl(m_fd, VIDIOC_QUERYCAP, &caps) < 0)
    {
        Error(m_device << " is not a v4l2 device.");
        return false;
    }
    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        Error(m_device << " is not a video capture device.");
        return false;
    }
    if (!(caps.capabilities & V4L2_CAP_STREAMING))
    {
        Error(m_device << " does not support streaming I/O.");
        return false;
    }

    // Set the image format.
    v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = m_producer->Format().width;
    fmt.fmt.pix.height = m_producer->Format().height;
    fmt.fmt.pix.pixelformat = GetV4L2PixelFormat(m_producer->Format().pixelFormat);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0)
    {
        Error("Failed to set the image format on " << m_device << " (" << fmt.fmt.pix.width <<
              "x" << fmt.fmt.pix.height << ", format = " << fmt.fmt.pix.pixelformat << ")");
        return false;
    }
    if (fmt.fmt.pix.width != m_producer->Format().width ||
        fmt.fmt.pix.height != m_producer->Format().height ||
        fmt.fmt.pix.pixelformat != GetV4L2PixelFormat(m_producer->Format().pixelFormat))
    {
        Error("Format not supported by V4L2 consumer.");
        return false;
    }

    // Request buffers.
    v4l2_requestbuffers req = {0};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0)
    {
        Error(m_device << " does not support memory mapping.");
        return false;
    }
    if (req.count < 2)
    {
        Error("Insufficient memory available on " << m_device);
        return false;
    }

    // Retrieve and map the buffers.
    for (int i = 0; i < req.count; i++)
    {
        v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0)
        {
            Error("Failed to query buffer from " << m_device);
            return false;
        }

        void* ptr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
        if (!ptr)
        {
            Error("Failed to map buffer provided by " << m_device);
            return false;
        }

        m_buffers.push_back(Buffer(ptr, buf.length));
    }

    // Allocate the CUDA buffer.
    m_cudaBuffer = CudaAlloc(m_producer->Format().totalBytes);
    if (!m_cudaBuffer)
    {
        Error("Failed to allocate CUDA memory.");
        return false;
    }

    return true;
}

void V4L2Consumer::Close()
{
    // Unmap the buffers.
    for (const auto& buffer : m_buffers)
    {
        if (munmap(buffer.ptr, buffer.length) < 0)
        {
            Error("Failed to unmap buffer from " << m_device);
        }
    }
    m_buffers.clear();

    // Close the device.
    if (m_fd != -1 && close(m_fd) < 0)
    {
        Error("Failed to close " << m_device);
    }
    m_fd = -1;
}

bool V4L2Consumer::StartStreaming()
{
    // Queue all buffers.
    for (int i = 0; i < m_buffers.size(); i++)
    {
        v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0)
        {
            Error("Failed to queue buffer " << i << " on " << m_device);
            return false;
        }
    }

    // Start streaming.
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0)
    {
        Error("Failed to start streaming on " << m_device);
        return false;
    }

    return true;
}

void V4L2Consumer::StopStreaming()
{
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_fd, VIDIOC_STREAMOFF, &type) < 0)
    {
        Error("Failed to stop streaming on " << m_device);
    }
}

bool V4L2Consumer::CaptureFrames(size_t numFrames, size_t warmupFrames)
{
    for (int frame = 0; frame < numFrames + warmupFrames; frame++)
    {
        bool retry = true;
        while (retry)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_fd, &fds);

            timeval timeout;
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;

            const std::string failureMessage(
                "This could be caused by a general V4L2 and/or device error, but it could\n"
                "also be caused by the loopback HDMI cable not being connected properly to\n"
                "the required device ports. Please check the cable connections and try again.");

            int ret = select(m_fd + 1, &fds, nullptr, nullptr, &timeout);
            if (ret < 0)
            {
                Error("Select failure on " << m_device << std::endl << failureMessage);
                return false;
            }
            if (ret == 0)
            {
                Error("Select timeout on " << m_device << std::endl << failureMessage);
                return false;
            }

            if (!ReadFrame(&retry, frame < warmupFrames) && !retry)
            {
                Error("Failed to read frame from " << m_device << std::endl << failureMessage);
                return false;
            }
        }
        if ((frame > warmupFrames) && (frame - warmupFrames) % 100 == 0)
        {
            Log((frame - warmupFrames) << " / " << numFrames);
        }
    }
    Log(numFrames << " / " << numFrames);

    return true;
}

bool V4L2Consumer::ReadFrame(bool* retry, bool warmupFrame)
{
    *retry = false;

    // Dequeue the next available buffer.
    v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0)
    {
        if (errno == EAGAIN)
        {
            *retry = true;
            return false;
        }
        Error("Failed to dequeue buffer from " << m_device);
        return false;
    }

    TimePoint receiveTime = Clock::now();

    if (!warmupFrame)
    {
        Buffer& buffer = m_buffers[buf.index];

        TimePoint readEnd = Clock::now();

        // Copy the buffer to GPU.
        CudaMemcpyHtoD(m_cudaBuffer, buffer.ptr, m_producer->Format().totalBytes);

        TimePoint copiedToGPU = Clock::now();

        // Get the frame pointer from the producer.
        auto frame = m_producer->GetFrame(buffer.ptr);
        if (!frame)
        {
            return false;
        }

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
    }

    // Return (queue) the buffer.
    if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0)
    {
        Error("Failed to queue buffer " << buf.index << " on " << m_device);
        return false;
    }

    return true;
}

std::ostream& V4L2Consumer::Dump(std::ostream& o) const
{
    o << "V4L2" << std::endl
      << "    Device: " << m_device << std::endl
      << "    RDMA: 0 (Not supported)" << std::endl;
    return o;
}

uint32_t V4L2Consumer::GetV4L2PixelFormat(PixelFormat format)
{
    switch (format)
    {
        case PIXEL_FORMAT_RGBA: return V4L2_PIX_FMT_ABGR32;
        default: return 0;
    }
}

V4L2Consumer::Buffer::Buffer(void* _ptr, size_t _length)
    : ptr(_ptr)
    , length(_length)
{
}
