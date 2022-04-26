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

#include <fstream>
#include <iomanip>

#include "Console.h"

#ifdef ENABLE_AJA
#include "AJAProducer.h"
#endif
#include "GLProducer.h"
#include "GStreamerProducer.h"

#ifdef ENABLE_AJA
#include "AJAConsumer.h"
#endif
#include "GStreamerConsumer.h"
#include "V4L2Consumer.h"

#include "CudaUtils.h"

constexpr TestFormat DEFAULT_FORMAT = FORMAT_1080_RGBA_60;
constexpr size_t DEFAULT_NUM_FRAMES = 600;
constexpr size_t DEFAULT_WARMUP_FRAMES = 60;
constexpr size_t DEFAULT_PRODUCER_TIME = 10;
constexpr size_t DEFAULT_SIMULATED_PROCESSING = 0;
constexpr int    DEFAULT_USE_RDMA = 1;

enum ProducerType
{
    PRODUCER_UNKNOWN,
    PRODUCER_GL,
    PRODUCER_AJA,
    PRODUCER_GSTREAMER,
};

enum ConsumerType
{
    CONSUMER_UNKNOWN,
    CONSUMER_V4L2,
    CONSUMER_AJA,
    CONSUMER_GSTREAMER,
    CONSUMER_NONE
};

struct ProgramOptions
{
    ProgramOptions()
        : producerType(PRODUCER_UNKNOWN)
        , consumerType(CONSUMER_UNKNOWN)
        , format(DEFAULT_FORMAT)
        , numFrames(DEFAULT_NUM_FRAMES)
        , warmupFrames(DEFAULT_WARMUP_FRAMES)
        , simulatedProcessing(DEFAULT_SIMULATED_PROCESSING)
        , producerRDMA(DEFAULT_USE_RDMA)
        , producerTime(DEFAULT_PRODUCER_TIME)
        , consumerRDMA(DEFAULT_USE_RDMA)
    {}

    ProducerType producerType;
    ConsumerType consumerType;
    TestFormat format;
    size_t numFrames;
    size_t warmupFrames;
    size_t simulatedProcessing;
    std::string outputFilename;

    std::string producerDevice;
    std::string producerChannel;
    bool producerRDMA;
    size_t producerTime;

    std::string consumerDevice;
    std::string consumerChannel;
    bool consumerRDMA;
};

void Usage()
{
    Log("Usage:" << std::endl << std::endl <<
        "  loopback-latency -p {producer} -c {consumer} [other options]" << std::endl << std::endl <<
        "    This standard usage is the standard usage that specifies a producer and" << std::endl <<
        "    consumer to test, and causes the tool to measure and estimate latency" << std::endl <<
        "    results using the given parameters." << std::endl << std::endl <<
        "  loopback-latency -s {loops} -f {format}" << std::endl << std::endl <<
        "    This second usage is used to simply run the simulation CUDA kernel for" << std::endl <<
        "    the given number of loops such that the baseline latency for the" << std::endl <<
        "    simulated CUDA workload can be measured. The idea being that this number" << std::endl <<
        "    would be adjusted until the measured time of the simlated workload" << std::endl <<
        "    matches that of the real processing workload that is going to be applied." << std::endl <<
        "    This value can then be used to launch the latency test as usual in order" << std::endl <<
        "    to measure the expected total latency using a similar GPU workload." << std::endl <<
        "    Note that the provided format should match in both cases, too." << std::endl << std::endl <<
        "Options:" << std::endl <<
        "  -p | --producer  The producer type. Options include:" << std::endl <<
        "                     gl:   OpenGL to dGPU display (DP/HDMI)" << std::endl <<
        "                     gst:  GStreamer to dGPU display (DP/HDMI)" << std::endl <<
#ifdef ENABLE_AJA
        "                     aja:  AJA playback device" << std::endl <<
#endif
        "  -c | --consumer  The consumer type. Options include:" << std::endl <<
        "                     v4l2: V4L2 consumer (e.g. CSI HDMI input)" << std::endl <<
        "                     gst:  GStreamer V4L2-based consumer (e.g. CSI HDMI input)" << std::endl <<
#ifdef ENABLE_AJA
        "                     aja:  AJA capture device" << std::endl <<
#endif
        "                     none: Don't consume frames. This allows the application" << std::endl <<
        "                           to just render the produced frames, which can be" << std::endl <<
        "                           useful for debugging." << std::endl <<
        "  -f | --format    The format to use. Options include:" << std::endl <<
        "                     720:    " << FORMAT_720_RGBA_60 << std::endl <<
        "                     1080:   " << FORMAT_1080_RGBA_60 << std::endl <<
        "                     uhd-24: " << FORMAT_UHD_RGBA_24 << std::endl <<
        "                     uhd:    " << FORMAT_UHD_RGBA_60 << std::endl <<
        "                     4k-24:  " << FORMAT_4K_RGBA_24 << std::endl <<
        "                     4k:     " << FORMAT_4K_RGBA_60 << std::endl <<
        "                     (Default: " << DEFAULT_FORMAT << ")" << std::endl <<
        "  -n {frames}      The number of frames to measure (default: " << DEFAULT_NUM_FRAMES << ")" << std::endl <<
        "  -w {frames}      The number of warmup frames to skip (default: " << DEFAULT_WARMUP_FRAMES << ")" << std::endl <<
        "  -s {loops}       The amount of simulated processing to add each frame (default: " << DEFAULT_SIMULATED_PROCESSING << ")" << std::endl <<
        "                   This value corresponds directly to a loop counter that is used in" << std::endl <<
        "                   a CUDA kernel to add some amount of GPU processing to each frame" << std::endl <<
        "                   before the actual frame color is written." << std::endl <<
        "  -o {filename}    The path to write the output results as a CSV file." << std::endl <<
        std::endl << "Producer options:" << std::endl <<
        "  -p.device {x}    The device to use" << std::endl <<
        "  -p.channel {x}   The channel to use" << std::endl <<
        "  -p.rdma {x}      Whether to use RDMA (default: " << DEFAULT_USE_RDMA << ")" << std::endl <<
        "  -p.time {x}      The amount of time to produce frames" << std::endl <<
        "                   (only used when consumer = none)" << std::endl <<
        std::endl << "Consumer options:" << std::endl <<
        "  -c.device {x}    The device to use" << std::endl <<
        "  -c.channel {x}   The channel to use" << std::endl <<
        "  -c.rdma {x}      Whether to use RDMA (default: " << DEFAULT_USE_RDMA << ")" << std::endl);
}

#define USAGE_ERROR(x) \
{ \
    Error(x); \
    exit(1); \
}

void ParseArguments(int argc, char* argv[], ProgramOptions* opts)
{
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            Usage();
            exit(0);
        }
        else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--producer"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -p (producer) option.")
            if (!strcmp(argv[i], "gl") || !strcmp(argv[i], "opengl"))
                opts->producerType = PRODUCER_GL;
            else if (!strcmp(argv[i], "aja"))
#ifdef ENABLE_AJA
                opts->producerType = PRODUCER_AJA;
#else
                USAGE_ERROR("AJA producer not supported (requires NTV2_SDK build option).")
#endif
            else if (!strcmp(argv[i], "gst") || !strcmp(argv[i], "gstreamer"))
                opts->producerType = PRODUCER_GSTREAMER;
            else
                USAGE_ERROR("Invalid value for -p (producer) option: " << argv[i])
        }
        else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--consumer"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -c (consumer) option.")
            if (!strcmp(argv[i], "v4l2"))
                opts->consumerType = CONSUMER_V4L2;
            else if (!strcmp(argv[i], "aja"))
#ifdef ENABLE_AJA
                opts->consumerType = CONSUMER_AJA;
#else
                USAGE_ERROR("AJA consumer not supported (requires NTV2_SDK build option).")
#endif
            else if (!strcmp(argv[i], "gst") || !strcmp(argv[i], "gstreamer"))
                opts->consumerType = CONSUMER_GSTREAMER;
            else if (!strcmp(argv[i], "none"))
                opts->consumerType = CONSUMER_NONE;
            else
                USAGE_ERROR("Invalid value for -c (consumer) option: " << argv[i])
        }
        else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--format"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -f (format) option.")
            if (!strcmp(argv[i], "720"))
                opts->format = FORMAT_720_RGBA_60;
            else if (!strcmp(argv[i], "1080"))
                opts->format = FORMAT_1080_RGBA_60;
            else if (!strcmp(argv[i], "uhd-24"))
                opts->format = FORMAT_UHD_RGBA_24;
            else if (!strcmp(argv[i], "uhd"))
                opts->format = FORMAT_UHD_RGBA_60;
            else if (!strcmp(argv[i], "4k-24"))
                opts->format = FORMAT_4K_RGBA_24;
            else if (!strcmp(argv[i], "4k"))
                opts->format = FORMAT_4K_RGBA_60;
            else
                USAGE_ERROR("Invalid value for -f (format) option: " << argv[i])
        }
        else if (!strcmp(argv[i], "-n"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -n (num frames) option.")
            opts->numFrames = strtol(argv[i], nullptr, 10);
        }
        else if (!strcmp(argv[i], "-w"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -w (warmup frames) option.")
            opts->warmupFrames = strtol(argv[i], nullptr, 10);
        }
        else if (!strcmp(argv[i], "-s"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -s (simulated CUDA workload) option.")
            opts->simulatedProcessing = strtol(argv[i], nullptr, 10);
        }
        else if (!strcmp(argv[i], "-o"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -o (output CSV file) option.")
            opts->outputFilename = argv[i];
        }
        else if (!strcmp(argv[i], "-p.device"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -p.device (producer device) option.")
            opts->producerDevice = argv[i];
        }
        else if (!strcmp(argv[i], "-p.channel"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -p.channel (producer channel) option.")
            opts->producerChannel = argv[i];
        }
        else if (!strcmp(argv[i], "-p.rdma"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -p.rdma (producer RDMA) option.")
            opts->producerRDMA = strtol(argv[i], nullptr, 10) != 0;
        }
        else if (!strcmp(argv[i], "-p.time"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -p.time (producer runtime) option.")
            opts->producerTime = strtol(argv[i], nullptr, 10);
        }
        else if (!strcmp(argv[i], "-c.device"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -c.device (consumer device) option.")
            opts->consumerDevice = argv[i];
        }
        else if (!strcmp(argv[i], "-c.channel"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -c.channel (consumer channel) option.")
            opts->consumerChannel = argv[i];
        }
        else if (!strcmp(argv[i], "-c.rdma"))
        {
            if (++i == argc)
                USAGE_ERROR("Missing value for -c.rdma (consumer RDMA) option.")
            opts->consumerRDMA = strtol(argv[i], nullptr, 10) != 0;
        }
    }
}

static int RunSimulatedProcessing(size_t loops, const TestFormat& format)
{
    void* buf = CudaAlloc(format.totalBytes);
    if (!buf)
    {
        Error("Failed to allocate CUDA buffer.");
        return 1;
    }

    constexpr size_t iterations = 500;

    Log("Format: " << format);
    Log("Running simulated workload with " << loops << " loops...");
    DurationList durations;
    for (size_t i = 0; i < iterations; i++)
    {
        auto start = Clock::now();
        CudaSimulateProcessing((uint32_t*)buf, format.width * format.height, loops);
        auto end = Clock::now();
        durations.Append(start, end);
    }
    Log("Done." << std::endl);
    Log("Results:  " << durations.Summary());

    CudaFree(buf);
    return 0;
}

static void PrintLatencyResults(const ProgramOptions& opts, const std::vector<std::shared_ptr<Frame>>& frames)
{
    if (frames.size() == 0)
        return;

    uint32_t expectedFrame = frames[0]->Number();
    size_t skippedFrames = 0;
    size_t duplicateReceives = 0;
    DurationList processingTimes;
    DurationList renderTimes;
    DurationList fromGpuTimes;
    DurationList writeTimes;
    DurationList vsyncTimes;
    DurationList wireTimes;
    DurationList readTimes;
    DurationList toGpuTimes;
    DurationList producerTimes;
    DurationList consumerTimes;
    DurationList totalTimes;
    DurationList estimatedAppTimes;
    for (const auto& f : frames)
    {
        skippedFrames += f->Number() - expectedFrame;
        duplicateReceives += f->DuplicateReceives();
        expectedFrame = f->Number() + 1;

        processingTimes.Append(f->ProcessingStart(), f->RenderStart());
        renderTimes.Append(f->RenderStart(), f->RenderEnd());
        fromGpuTimes.Append(f->RenderEnd(), f->CopiedFromGPU());
        writeTimes.Append(f->CopiedFromGPU(), f->WriteEnd());
        vsyncTimes.Append(f->WriteEnd(), f->ScanoutStart());
        wireTimes.Append(f->ScanoutStart(), f->FrameReceived());
        readTimes.Append(f->FrameReceived(), f->ReadEnd());
        toGpuTimes.Append(f->ReadEnd(), f->CopiedToGPU());
        producerTimes.Append(f->ProcessingStart(), f->WriteEnd());
        consumerTimes.Append(f->FrameReceived(), f->CopiedToGPU());
        totalTimes.Append(f->ProcessingStart(), f->CopiedToGPU());

        Microseconds consumerTime = std::chrono::duration_cast<Microseconds>(f->CopiedToGPU() - f->FrameReceived());
        Microseconds producerTime = std::chrono::duration_cast<Microseconds>(f->WriteEnd() - f->ProcessingStart());
        estimatedAppTimes.Append(consumerTime + producerTime);
    }
    if (skippedFrames || duplicateReceives)
    {
        Warning("Frames were skipped or repeated!" << std::endl <<
                "Frames received: " << frames.size() << std::endl <<
                "Frames skipped:  " << skippedFrames << std::endl <<
                "Frames repeated: " << duplicateReceives << std::endl);
    }

    Log(ProducerColor("CUDA Processing: " << processingTimes.Summary()));
    Log(ProducerColor("Render on GPU:   " << renderTimes.Summary()));
    Log(ProducerColor("Copy To Host:    " << fromGpuTimes.Summary()));
    Log(ProducerColor("Write To HW:     " << writeTimes.Summary()));
    Log("Vsync Wait:      " << vsyncTimes.Summary());
    Log("Wire Time:       " << wireTimes.Summary());
    Log(ConsumerColor("Read From HW:    " << readTimes.Summary()));
    Log(ConsumerColor("Copy To GPU:     " << toGpuTimes.Summary()));
    Log("=========================================================");
    Log("Total:           " << totalTimes.Summary() << std::endl << std::endl);

    Microseconds frameInterval(1000000 / opts.format.frameRate);

    Log(ProducerColor(
        "Producer (Process and Write to HW)" << std::endl <<
        "=========================================================" << std::endl <<
        "   Microseconds: " << producerTimes.Summary() << std::endl <<
        "         Frames: " << producerTimes.SummaryInFrameIntervals(frameInterval) << std::endl));

    Log(ConsumerColor(
        "Consumer (Read from HW and Copy to GPU)" << std::endl <<
        "=========================================================" << std::endl <<
        "   Microseconds: " << consumerTimes.Summary() << std::endl <<
        "         Frames: " << consumerTimes.SummaryInFrameIntervals(frameInterval) << std::endl));

    Log("Estimated Application Times (Read + Process + Write)" << std::endl <<
        "=========================================================" << std::endl <<
        "   Microseconds: " << estimatedAppTimes.Summary() << std::endl <<
        "         Frames: " << estimatedAppTimes.SummaryInFrameIntervals(frameInterval) << std::endl);


    // Estimate the "final" latency based on using the total frame processing time,
    // rounding up to the next vsync, then adding the expected wire time.
    size_t avgFrames = (estimatedAppTimes.Avg() + frameInterval).count() / frameInterval.count();
    size_t minFrames = (estimatedAppTimes.Min() + frameInterval).count() / frameInterval.count();
    size_t maxFrames = (estimatedAppTimes.Max() + frameInterval).count() / frameInterval.count();
    if (opts.producerType == PRODUCER_GSTREAMER)
    {
        // The exact GStreamer producer wire time is unknown, but we know that the nveglglessink
        // component adds a fair amount of latency that is included in the "wire" times, so we'll
        // add that to the processing times to guess the overall latency.
        avgFrames += (wireTimes.Avg() + frameInterval).count() / frameInterval.count();
        minFrames += (wireTimes.Min() + frameInterval).count() / frameInterval.count();
        maxFrames += (wireTimes.Max() + frameInterval).count() / frameInterval.count();
    }
    else
    {
        // All other producers expect the wire time to be one frame interval. Empirical results
        // show this to be true, so we just add one instead of using the measured wire times
        // since the measured times are subject to slight deviations.
        avgFrames++;
        minFrames++;
        maxFrames++;
    }

    std::ostringstream ss;
    ss << "Final Estimated Latencies (Processing + Vsync + Wire)" << std::endl
       << "=========================================================" << std::endl
       << "   Microseconds: "
       << "avg = " << std::setw(6) << (avgFrames * frameInterval.count()) << ", "
       << "min = " << std::setw(6) << (minFrames * frameInterval.count()) << ", "
       << "max = " << std::setw(6) << (maxFrames * frameInterval.count()) << std::endl
       << "         Frames: "
       << "avg = " << std::setw(6) << avgFrames << ", "
       << "min = " << std::setw(6) << minFrames << ", "
       << "max = " << std::setw(6) << maxFrames << std::endl;
    if (skippedFrames || duplicateReceives)
    {
        Log(WarningColor(ss.str()));
        Warning("Frames were skipped or repeated. These times only" << std::endl <<
                "include frames that were actually received, and the times" << std::endl <<
                "include only the first instance each frame was received." << std::endl);
    }
    else
    {
        Log(SuccessColor(ss.str()));
    }

    if (vsyncTimes.Avg() > (frameInterval * 1.5f))
    {
        Warning("The average vsync interval (" << vsyncTimes.Avg().count() << ") exceeded the" << std::endl <<
                "the expected vsync interval (" << frameInterval.count() << ") by a large amount." << std::endl <<
                "This could be due to the producer locking to a lower" << std::endl <<
                "framerate that can't be controlled by the producer API." << std::endl <<
                "Please check the actual vsync interval that was used and" << std::endl <<
                "consider running the test using another format that uses" << std::endl <<
                "the actual frame interval that was used (" << (1000000.0f / vsyncTimes.Avg().count()) << ").");
    }
}

static void WriteLatencyResults(std::ofstream& file, const std::vector<std::shared_ptr<Frame>>& frames)
{
    if (!file.is_open() || frames.size() == 0)
        return;

    file << "Frame,Count,Frame Start Timestamp,Frame Interval,Process,Render,Copy To SYS,"
         << "Write to HW,VSync,Wire,Read from HW,Copy to GPU" << std::endl;

    auto firstFrame = frames[0]->Number();
    auto previousStartTime = frames[0]->ProcessingStart().time_since_epoch();
    for (const auto& f : frames)
    {
        file << (f->Number() - firstFrame) << ","
             << (f->DuplicateReceives() + 1) << ","
             << std::chrono::duration_cast<Microseconds>(f->ProcessingStart().time_since_epoch()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->ProcessingStart().time_since_epoch() - previousStartTime).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->RenderStart() - f->ProcessingStart()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->RenderEnd() - f->RenderStart()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->CopiedFromGPU() - f->RenderEnd()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->WriteEnd() - f->CopiedFromGPU()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->ScanoutStart() - f->WriteEnd()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->FrameReceived() - f->ScanoutStart()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->ReadEnd() - f->FrameReceived()).count() << ","
             << std::chrono::duration_cast<Microseconds>(f->CopiedToGPU() - f->ReadEnd()).count() << std::endl;
        previousStartTime = f->ProcessingStart().time_since_epoch();
    }
}

int main(int argc, char* argv[])
{
    ProgramOptions opts;
    ParseArguments(argc, argv, &opts);

    if (opts.producerType == PRODUCER_UNKNOWN &&
        opts.consumerType == CONSUMER_UNKNOWN &&
        opts.simulatedProcessing > 0)
    {
        return RunSimulatedProcessing(opts.simulatedProcessing, opts.format);
    }

    std::shared_ptr<Producer> producer;
    switch (opts.producerType)
    {
        case PRODUCER_GL:
            producer.reset(new GLProducer(opts.format, opts.simulatedProcessing));
            break;
#ifdef ENABLE_AJA
        case PRODUCER_AJA:
            producer.reset(new AJAProducer(opts.format, opts.producerDevice, opts.producerChannel, opts.simulatedProcessing, opts.producerRDMA));
            break;
#endif
        case PRODUCER_GSTREAMER:
            producer.reset(new GStreamerProducer(&argc, &argv, opts.format, opts.simulatedProcessing, opts.producerRDMA));
            break;
        default:
            Usage();
            Error("Missing required producer (-p) argument.");
            return 1;
    }

    std::shared_ptr<Consumer> consumer;
    switch (opts.consumerType)
    {
        case CONSUMER_V4L2:
            consumer.reset(new V4L2Consumer(producer, opts.consumerDevice));
            break;
#ifdef ENABLE_AJA
        case CONSUMER_AJA:
            consumer.reset(new AJAConsumer(producer, opts.consumerDevice, opts.consumerChannel, opts.consumerRDMA));
            break;
#endif
        case CONSUMER_GSTREAMER:
            consumer.reset(new GStreamerConsumer(producer, &argc, &argv, opts.consumerDevice));
            break;
        case CONSUMER_NONE:
            break;
        default:
            Usage();
            Error("Missing required consumer (-c) argument.");
            return 1;
    }

    std::ofstream outputFile;
    if (opts.outputFilename.size() > 0)
    {
        outputFile.open(opts.outputFilename);
        if (outputFile.fail())
        {
            Error("Could not open file for output: " << opts.outputFilename);
            return 1;
        }
    }

    Log("Format: " << opts.format << std::endl);

    Log(ProducerColor("Producer: " << *producer));
    if (!producer->Initialize())
    {
        Error("Failed to initialize producer.");
        return 1;
    }

    if (!producer->StartStreaming())
    {
        Error("Failed to start producer streaming.");
        return 1;
    }

    if (consumer)
    {
        Log(ConsumerColor("Consumer: " << *consumer));
        if (!consumer->Initialize())
        {
            Error("Failed to initialize consumer.");
            return 1;
        }

        if (!consumer->StartStreaming())
        {
            Error("Failed to start consumer streaming.");
            return 1;
        }

        if (opts.simulatedProcessing > 0)
        {
            Log("Simulating processing with " << opts.simulatedProcessing << " CUDA loops per frame." << std::endl);
        }
        Log("Measuring " << opts.numFrames << " frames...");
        if (!consumer->CaptureFrames(opts.numFrames, opts.warmupFrames))
        {
            Error("Failure occurred during frame capture.");
            return 1;
        }
        Log("Done!" << std::endl);

        consumer->StopStreaming();
        consumer->Close();

        auto frames = consumer->GetReceivedFrames();
        PrintLatencyResults(opts, frames);
        WriteLatencyResults(outputFile, frames);
    }
    else
    {
        Log(ConsumerColor("Consumer: None" << std::endl));
        Log("Producing frames for " << opts.producerTime << " seconds...");
        sleep(opts.producerTime);
        Log("Done!");
    }

    producer->StopStreaming();
    producer->Close();

    if (outputFile.is_open())
    {
        Log("Results written to '" << opts.outputFilename << "'");
        outputFile.close();
    }

    return 0;
}
