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

#include "AJAConsumer.h"
#include "Console.h"
#include "CudaUtils.h"

AJAConsumer::AJAConsumer(std::shared_ptr<Producer> producer,
                         const std::string& device,
                         const std::string& channel,
                         bool useRDMA)
    : Consumer(producer)
    , AJABase(producer->Format(), device, channel.size() == 0 ? "2" : channel, useRDMA)
{
}

AJAConsumer::~AJAConsumer()
{
}

AJAStatus AJAConsumer::SetupVideo()
{
    NTV2InputSourceKinds inputKind = m_konaHDMI ? NTV2_INPUTSOURCES_HDMI : NTV2_INPUTSOURCES_SDI;
    NTV2InputSource inputSrc(NTV2ChannelToInputSource(m_channel, inputKind));
    NTV2Channel tsiChannel = (NTV2Channel)(m_channel + 1);

    if (!IsRGBFormat(m_pixelFormat))
    {
        Error("YUV formats not yet supported");
        return AJA_STATUS_UNSUPPORTED;
    }

    // Detect if the source is YUV or RGB (if CSC is required or not).
    bool isInputRGB(false);
    if (inputKind == NTV2_INPUTSOURCES_HDMI)
    {
        NTV2LHIHDMIColorSpace inputColor;
        m_device.GetHDMIInputColor(inputColor, m_channel);
        isInputRGB = (inputColor == NTV2_LHIHDMIColorSpaceRGB);
    }

    // Setup the input routing.
    m_device.EnableChannel(m_channel);
    if (m_useTSI)
    {
        m_device.SetTsiFrameEnable(true, m_channel);
        m_device.EnableChannel(tsiChannel);
    }
    m_device.SetMode(m_channel, NTV2_MODE_CAPTURE);
    if (NTV2DeviceHasBiDirectionalSDI(m_deviceID) && NTV2_INPUT_SOURCE_IS_SDI(inputSrc))
    {
        m_device.SetSDITransmitEnable(m_channel, false);
    }
    m_device.SetVideoFormat(m_videoFormat, false, false, m_channel);
    m_device.SetFrameBufferFormat(m_channel, m_pixelFormat);
    if (m_useTSI)
    {
        m_device.SetFrameBufferFormat(tsiChannel, m_pixelFormat);
    }
    m_device.EnableInputInterrupt(m_channel);
    m_device.SubscribeInputVerticalEvent(m_channel);

    NTV2OutputXptID inputOutputXpt(GetInputSourceOutputXpt(inputSrc, /*DS2*/ false, isInputRGB, /*Quadrant*/ 0));
    NTV2InputXptID fbInputXpt(GetFrameBufferInputXptFromChannel(m_channel));
    if (m_useTSI)
    {
        if (!isInputRGB)
        {
            if (NTV2DeviceGetNumCSCs(m_deviceID) < 4)
            {
                Error("CSCs not available for TSI input.");
                return AJA_STATUS_UNSUPPORTED;
            }
            m_device.Connect(NTV2_XptFrameBuffer1Input, NTV2_Xpt425Mux1ARGB);
            m_device.Connect(NTV2_XptFrameBuffer1BInput, NTV2_Xpt425Mux1BRGB);
            m_device.Connect(NTV2_XptFrameBuffer2Input, NTV2_Xpt425Mux2ARGB);
            m_device.Connect(NTV2_XptFrameBuffer2BInput, NTV2_Xpt425Mux2BRGB);
            m_device.Connect(NTV2_Xpt425Mux1AInput, NTV2_XptCSC1VidRGB);
            m_device.Connect(NTV2_Xpt425Mux1BInput, NTV2_XptCSC2VidRGB);
            m_device.Connect(NTV2_Xpt425Mux2AInput, NTV2_XptCSC3VidRGB);
            m_device.Connect(NTV2_Xpt425Mux2BInput, NTV2_XptCSC4VidRGB);
            m_device.Connect(NTV2_XptCSC1VidInput, NTV2_XptHDMIIn1);
            m_device.Connect(NTV2_XptCSC2VidInput, NTV2_XptHDMIIn1Q2);
            m_device.Connect(NTV2_XptCSC3VidInput, NTV2_XptHDMIIn1Q3);
            m_device.Connect(NTV2_XptCSC4VidInput, NTV2_XptHDMIIn1Q4);
        }
        else
        {
            m_device.Connect(NTV2_XptFrameBuffer1Input, NTV2_Xpt425Mux1ARGB);
            m_device.Connect(NTV2_XptFrameBuffer1BInput, NTV2_Xpt425Mux1BRGB);
            m_device.Connect(NTV2_XptFrameBuffer2Input, NTV2_Xpt425Mux2ARGB);
            m_device.Connect(NTV2_XptFrameBuffer2BInput, NTV2_Xpt425Mux2BRGB);
            m_device.Connect(NTV2_Xpt425Mux1AInput, NTV2_XptHDMIIn1RGB);
            m_device.Connect(NTV2_Xpt425Mux1BInput, NTV2_XptHDMIIn1Q2RGB);
            m_device.Connect(NTV2_Xpt425Mux2AInput, NTV2_XptHDMIIn1Q3RGB);
            m_device.Connect(NTV2_Xpt425Mux2BInput, NTV2_XptHDMIIn1Q4RGB);
        }
    }
    else if (!isInputRGB)
    {
        if (NTV2DeviceGetNumCSCs(m_deviceID) <= (int)m_channel)
        {
            Error("No CSC available for NTV2_CHANNEL" << (m_channel + 1));
            return AJA_STATUS_UNSUPPORTED;
        }
        m_device.Connect(fbInputXpt, GetCSCOutputXptFromChannel(m_channel, /*inIsKey*/ false, /*inIsRGB*/ true));
        m_device.Connect(GetCSCInputXptFromChannel(m_channel), inputOutputXpt);
    }
    else
    {
        m_device.Connect(fbInputXpt, inputOutputXpt);
    }

    return AJA_STATUS_SUCCESS;
}

bool AJAConsumer::Initialize()
{
    AJAStatus status = OpenDevice();
    if (AJA_FAILURE(status))
    {
        Error("Failed to open AJA device '" << m_deviceSpecifier << "'.");
        return false;
    }

    if (!NTV2DeviceCanDoCapture(m_deviceID))
    {
        Error("Device '" << m_deviceSpecifier << "' cannot capture video.");
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

void AJAConsumer::Close()
{
}

bool AJAConsumer::StartStreaming()
{
    return true;
}

void AJAConsumer::StopStreaming()
{
}

bool AJAConsumer::CaptureFrames(size_t numFrames, size_t warmupFrames)
{
    // Set the initial frame and warmup the stream (wait for signal).
    uint32_t currentHwFrame = 2;
    m_device.SetInputFrame(m_channel, currentHwFrame);
    m_device.WaitForInputVerticalInterrupt(m_channel, warmupFrames);

    // If reading the frame exceeds a frame interval then we might encounter a
    // race between the update of the input frame and the interrupt. To avoid this
    // we will insert another interrupt wait if we approach the frame interval.
    const Microseconds frameHeadroom(2000);
    const Microseconds frameInterval(1000000 / m_producer->Format().frameRate);
    const Microseconds maxFrameTime(frameInterval.count() - frameHeadroom.count());

    for (int frameNumber = 0; frameNumber < numFrames; frameNumber++)
    {
        // Update the next output frame for the device and wait until it starts.
        uint32_t nextHwFrame = currentHwFrame ^ 1;
        m_device.SetInputFrame(m_channel, nextHwFrame);
        m_device.WaitForInputVerticalInterrupt(m_channel);

        TimePoint receiveTime = Clock::now();

        // Read the current frame from the device.
        ULWord* dstBuf = (ULWord*)(m_useRDMA ? m_cudaBuffer : m_buffer.data());
        m_device.DMAReadFrame(currentHwFrame, dstBuf, m_formatDesc.GetTotalBytes());

        TimePoint readEnd = Clock::now();

        // If not using RDMA, copy the entire buffer to GPU.
        if (!m_useRDMA)
            CudaMemcpyHtoD(m_cudaBuffer, m_buffer.data(), m_formatDesc.GetTotalBytes());

        TimePoint copiedToGPU = Clock::now();

        // Wait for another frame interrupt if we're approaching an interval to avoid update race.
        auto readTime = std::chrono::duration_cast<Microseconds>(copiedToGPU - receiveTime);
        if (readTime > maxFrameTime)
            m_device.WaitForInputVerticalInterrupt(m_channel);

        // If using RDMA, copy the first pixel used for lookup purposes to host mem.
        // Note that if the lookup method ever changes to use more data then this will
        // also need to change accordingly, but we should minimize the size of the copy
        // to avoid negatively impacting the overall load/latency.
        if (m_useRDMA)
            CudaMemcpyDtoH(m_buffer.data(), m_cudaBuffer, m_producer->Format().bytesPerPixel);

        // Get the frame pointer from the producer.
        auto frame = m_producer->GetFrame(m_buffer.data());
        if (!frame)
        {
            if (m_useRDMA)
            {
                Warning("This error may also occur if RDMA is enabled but the AJA" << std::endl <<
                        "device is not connected to a PCI port that supports RDMA." << std::endl <<
                        "To see if this may be the case, check the 'dmesg' output for" << std::endl <<
                        "'unhandled context faults' from smmu.");
            }
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

        if (frameNumber > 0 && frameNumber % 100 == 0)
        {
            Log(frameNumber << " / " << numFrames);
        }

        currentHwFrame = nextHwFrame;
    }
    Log(numFrames << " / " << numFrames);

    return true;
}

std::ostream& AJAConsumer::Dump(std::ostream& o) const
{
    o << "AJA" << std::endl
      << "    Device: " << m_deviceSpecifier << std::endl
      << "    Channel: NTV2_CHANNEL" << (m_channel + 1) << std::endl
      << "    RDMA: " << m_useRDMA << std::endl;
    return o;
}
