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

#include <ntv2devicescanner.h>

#include "AJABase.h"
#include "Console.h"
#include "CudaUtils.h"

AJABase::AJABase(const TestFormat& format,
                 const std::string& device,
                 const std::string& channel,
                 bool useRDMA)
    : m_deviceSpecifier(device.size() == 0 ? "0" : device)
    , m_videoFormat(GetNTV2VideoFormat(format))
    , m_pixelFormat(GetNTV2PixelFormat(format))
    , m_formatDesc(NTV2FormatDescriptor(m_videoFormat, m_pixelFormat))
    , m_channel(GetNTV2Channel(channel))
    , m_konaHDMI(false)
    , m_useTSI(false)
    , m_useRDMA(useRDMA)
    , m_cudaBuffer(nullptr)
{
}

AJABase::~AJABase()
{
    CudaFree(m_cudaBuffer);
}

AJAStatus AJABase::OpenDevice()
{
    // Get the requested device.
    if (!CNTV2DeviceScanner::GetFirstDeviceFromArgument(m_deviceSpecifier, m_device))
    {
        Error("Device '" << m_deviceSpecifier << "' not found.");
        return AJA_STATUS_OPEN;
    }

    // Check if the device is ready.
    if (!m_device.IsDeviceReady(false))
    {
        Error("Device '" << m_deviceSpecifier << "' not ready.");
        return AJA_STATUS_INITIALIZE;
    }

    // Get the device ID.
    m_deviceID = m_device.GetDeviceID();

    // Detect Kona HDMI device.
    m_konaHDMI = NTV2DeviceGetNumHDMIVideoInputs(m_deviceID) > 1;

    // Check if a TSI 4x format is needed.
    if (m_konaHDMI)
    {
        m_useTSI = GetNTV2VideoFormatTSI(m_videoFormat);
        m_formatDesc = NTV2FormatDescriptor(m_videoFormat, m_pixelFormat);
    }

    // Check device capabilities.
    if (!NTV2DeviceCanDoVideoFormat(m_deviceID, m_videoFormat))
    {
        Error("AJA device does not support requested video format.");
        return AJA_STATUS_UNSUPPORTED;
    }
    if (!NTV2DeviceCanDoFrameBufferFormat(m_deviceID, m_pixelFormat))
    {
        Error("AJA device does not support requested pixel format.");
        return AJA_STATUS_UNSUPPORTED;
    }

    // Allocate the host buffer.
    m_buffer.resize(m_formatDesc.GetTotalBytes());
    if (m_buffer.size() != m_formatDesc.GetTotalBytes())
    {
        Error("Failed to allocate buffer.");
        return AJA_STATUS_INITIALIZE;
    }
    if (!m_useRDMA && !m_device.DMABufferLock((const ULWord*)m_buffer.data(), m_formatDesc.GetTotalBytes(), true))
    {
        Error("AJA device failed to lock CPU buffer.");
        return AJA_STATUS_INITIALIZE;
    }

    // Allocate the CUDA buffer.
    m_cudaBuffer = CudaAlloc(m_formatDesc.GetTotalBytes(), m_useRDMA);
    if (!m_cudaBuffer)
    {
        Error("Failed to allocate CUDA memory.");
        return AJA_STATUS_INITIALIZE;
    }
    if (m_useRDMA && !m_device.DMABufferLock((const ULWord*)m_cudaBuffer, m_formatDesc.GetTotalBytes(), true, true))
    {
        Error("AJA device failed to lock GPU buffer.");
        return AJA_STATUS_INITIALIZE;
    }

    return AJA_STATUS_SUCCESS;
}

NTV2Channel AJABase::GetNTV2Channel(const std::string& channel)
{
    long int idx = strtol(channel.c_str(), nullptr, 10);
    if (idx < 1 || idx > NTV2_MAX_NUM_CHANNELS)
        return NTV2_MAX_NUM_CHANNELS;
    return static_cast<NTV2Channel>(NTV2_CHANNEL1 + (idx - 1));
}

NTV2VideoFormat AJABase::GetNTV2VideoFormat(const TestFormat& format)
{
    if (format == FORMAT_720_RGBA_60)
        return NTV2_FORMAT_720p_6000;
    else if (format == FORMAT_1080_RGBA_60)
        return NTV2_FORMAT_1080p_6000_A;
    else if (format == FORMAT_UHD_RGBA_24)
        return NTV2_FORMAT_3840x2160p_2400;
    else if (format == FORMAT_UHD_RGBA_60)
        return NTV2_FORMAT_3840x2160p_6000;
    else if (format == FORMAT_4K_RGBA_24)
        return NTV2_FORMAT_4096x2160p_2400;
    else if (format == FORMAT_4K_RGBA_60)
        return NTV2_FORMAT_4096x2160p_6000;
    else
        return NTV2_FORMAT_UNKNOWN;
}

NTV2PixelFormat AJABase::GetNTV2PixelFormat(const TestFormat& format)
{
    if (format.pixelFormat == PIXEL_FORMAT_RGBA)
        return NTV2_FBF_ABGR;
    else
        return NTV2_FBF_INVALID;
}

bool AJABase::GetNTV2VideoFormatTSI(NTV2VideoFormat& format)
{
    switch (format)
    {
        case NTV2_FORMAT_3840x2160p_2400:
            format = NTV2_FORMAT_4x1920x1080p_2400;
            return true;
        case NTV2_FORMAT_3840x2160p_6000:
            format = NTV2_FORMAT_4x1920x1080p_6000;
            return true;
        case NTV2_FORMAT_4096x2160p_2400:
            format = NTV2_FORMAT_4x2048x1080p_2400;
            return true;
        case NTV2_FORMAT_4096x2160p_6000:
            format = NTV2_FORMAT_4x2048x1080p_6000;
            return true;
        default:
            return false;
    }
}
