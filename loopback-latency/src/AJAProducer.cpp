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

#include "AJAProducer.h"
#include "Console.h"
#include "CudaUtils.h"

AJAProducer::AJAProducer(const TestFormat& format,
                         const std::string& device,
                         const std::string& channel,
                         size_t simulatedProcessing,
                         bool useRDMA)
    : Producer(format, simulatedProcessing)
    , AJABase(format, device, channel.size() == 0 ? "1" : channel, useRDMA)
{
}

AJAProducer::~AJAProducer()
{
}

AJAStatus AJAProducer::SetupVideo()
{
    m_device.ClearRouting();
    m_device.SetReference(NTV2_REFERENCE_FREERUN);

    // Setup the output routing.
    m_device.EnableChannel(m_channel);
    m_device.SetMode(m_channel, NTV2_MODE_DISPLAY);
    if (NTV2DeviceHasBiDirectionalSDI(m_deviceID))
    {
        m_device.SetSDITransmitEnable(m_channel, true);
    }
    m_device.SetVideoFormat(m_videoFormat, false, false, m_channel);
    m_device.SetFrameBufferFormat(m_channel, m_pixelFormat);
    m_device.EnableOutputInterrupt(m_channel);
    m_device.SubscribeOutputVerticalEvent(m_channel);

    NTV2OutputXptID fbOutputXpt(GetFrameBufferOutputXptFromChannel(m_channel, IsRGBFormat(m_pixelFormat)));
    NTV2OutputDestination outputDest(NTV2ChannelToOutputDestination(m_channel));
    NTV2InputXptID outputInputXpt(GetOutputDestInputXpt(outputDest));
    if (IsRGBFormat(m_pixelFormat))
    {
        if (NTV2DeviceGetNumCSCs(m_deviceID) <= (int)m_channel)
        {
            Error("No CSC available for NTV2_CHANNEL" << (m_channel + 1));
            return AJA_STATUS_UNSUPPORTED;
        }
        m_device.Connect(outputInputXpt, GetCSCOutputXptFromChannel(m_channel, /*inIsKey*/ false, /*inIsRGB*/ false));
        m_device.Connect(GetCSCInputXptFromChannel(m_channel), fbOutputXpt);
    }
    else
    {
        m_device.Connect(outputInputXpt, fbOutputXpt);
    }

    return AJA_STATUS_SUCCESS;
}

bool AJAProducer::Initialize()
{
    AJAStatus status = OpenDevice();
    if (AJA_FAILURE(status))
    {
        Error("Failed to open AJA device '" << m_deviceSpecifier << "'.");
        return false;
    }

    if (!NTV2DeviceCanDoPlayback(m_deviceID))
    {
        Error("Device '" << m_deviceSpecifier << "' cannot play video.");
        return AJA_STATUS_FEATURE;
    }

    status = SetupVideo();
    if (AJA_FAILURE(status))
    {
        Error("Failed to setup AJA device '" << m_deviceSpecifier << "'.");
        return false;
    }

    return true;
}

void AJAProducer::Close()
{
}

std::ostream& AJAProducer::Dump(std::ostream& o) const
{
    o << "AJA" << std::endl
      << "    Device: " << m_deviceSpecifier << std::endl
      << "    Channel: NTV2_CHANNEL" << (m_channel + 1) << std::endl
      << "    RDMA: " << m_useRDMA << std::endl;
    return o;
}

void AJAProducer::StreamThread()
{
    // Set the initial frame and wait for the next interrupt.
    uint32_t currentHwFrame = 0;
    m_device.SetOutputFrame(m_channel, currentHwFrame);
    m_device.WaitForOutputVerticalInterrupt(m_channel);

    while (IsStreaming())
    {
        auto frame = StartFrame();

        frame->RecordProcessingStart();

        // Simulate processing time.
        size_t elementCount = m_format.width * m_format.height;
        CudaSimulateProcessing((uint32_t*)m_cudaBuffer, elementCount, m_simulatedProcessing);

        frame->RecordRenderStart();

        // Fill the CUDA buffer with the frame color.
        CudaWriteRGBA((uint32_t*)m_cudaBuffer, elementCount, frame->R(), frame->G(), frame->B());

        frame->RecordRenderEnd();

        // If not using RDMA, copy to the host buffer.
        if (!m_useRDMA)
            CudaMemcpyDtoH(m_buffer.data(), m_cudaBuffer, m_formatDesc.GetTotalBytes());

        frame->RecordCopiedFromGPU();

        // Write the frame to the hardware.
        uint32_t nextHwFrame = currentHwFrame ^ 1;
        ULWord* srcBuf = (ULWord*)(m_useRDMA ? m_cudaBuffer : m_buffer.data());
        m_device.DMAWriteFrame(nextHwFrame, srcBuf, m_formatDesc.GetTotalBytes());
        m_device.SetOutputFrame(m_channel, nextHwFrame);

        frame->RecordWriteEnd();

        // Wait for the next frame interrupt.
        m_device.WaitForOutputVerticalInterrupt(m_channel);

        frame->RecordScanoutStart();

        currentHwFrame = nextHwFrame;
    }
}
