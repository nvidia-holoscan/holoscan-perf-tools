# Loopback Latency Tool

This tool is designed to measure the internal latency of various video I/O
mechanisms that may be used with the Clara Holoscan SDK and associated
hardware.

## Requirements

* CMake 3.10 or newer (https://cmake.org/)
* CUDA 11.1 or newer (https://developer.nvidia.com/cuda-toolkit)
* GLFW 3.2 or newer (https://www.glfw.org/)
* GStreamer 1.14 or newer (https://gstreamer.freedesktop.org/)
* GTK 3.22 or newer (https://www.gtk.org/)
* pkg-config 0.29 or newer (https://www.freedesktop.org/wiki/Software/pkg-config/)

Required for Optional DeepStream Support (for GStreamer Producer RDMA):

* DeepStream 5.1 or newer (https://developer.nvidia.com/deepstream-sdk)

Required for Optional AJA Video Systems Support:

* AJA NTV2 SDK 16.1 or newer (https://www.aja.com/)

## Building

The following steps assume that CUDA (and optionally DeepStream) have already
been installed by the Clara Holoscan SDK. Only the additional packages will be
installed here.

### Install Additional Requirements

```sh
$ sudo apt-get install \
    cmake \
    libglfw3-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgtk-3-dev \
    pkg-config
```

### Default Build (No DeepStream or AJA Support)

```sh
$ mkdir build
$ cd build
$ cmake ..
$ make
```

Note that if the error `No CMAKE_CUDA_COMPILER could be found` is encountered,
make sure that the `nvcc` executable can be found by adding the CUDA runtime
location to your `PATH` variable:

```sh
$ export PATH=$PATH:/usr/local/cuda/bin
```

### Building with DeepStream Support

DeepStream support enables RDMA when using the GStreamer Producer. To build
with DeepStream support, the `DEEPSTREAM_SDK` path must point to the location
of the DeepStream SDK. For example, when building against DeepStream 5.1 then
the following is used to build:

```sh
$ mkdir build
$ cd build
$ cmake -DDEEPSTREAM_SDK=/opt/nvidia/deepstream/deepstream-5.1 ..
$ make
```

### Building with AJA Support

When building with AJA support, the `NTV2_SDK` path must point to the location
of the NTV2 SDK in which both the headers and compiled libraries (i.e.
`libajantv2`) exist. For example, if the NTV2 SDK is in
`/home/nvidia/ntv2sdklinux_16.1.0.3` then the following is used to build:

```sh
$ mkdir build
$ cd build
$ cmake -DNTV2_SDK=/home/nvidia/ntv2sdklinux_16.1.0.3 ..
$ make
```

## Operation Overview

This tool operates by having a producer component generate a sequence of known
video frames that are output and then transferred back to the input consumer
component using a loopback cable. Timestamps are compared throughout the life
of the frame to measure the overall latency that the frame sees during this
process and these results are summarized when the measurement completes.

Each frame that is generated goes through the following steps in order, each of
which is measured and then reported when all of the frames complete:

#### 1. Render on GPU

The time that it takes to generate the frame data using the GPU. This is
expected to be very small (<100us avg), but higher values may be seen if
overall system/GPU load is high.

#### 2. Copy To Host

The time that it takes for the GPU-generated frame to be copied to host memory
for the sake of output. If RDMA is enabled for the producer, this should be
zero.

#### 3. Write To HW

The time that it takes to copy the frame to the hardware for the sake of
output. If the producer outputs directly to the GPU, this should be zero.

#### 4. VSync Wait

The time that the producer hardware blocks waiting for a VSync interval. The
sum of this and all preceeding times is expected to be a multiple of the frame
interval.

#### 5. Wire Time

The time that it takes for the frame to be transferred across the physical
wire. This is expected to be the same as a frame interval.

#### 6. Read From HW

The time that it takes to copy the received frame from the capture hardware to
system or GPU memory (if RDMA is disabled or enabled, respectively). If the
consumer writes received frames directly to system/GPU memory (such as the
onboard HDMI capture card), this should be zero.

#### 7. Copy to GPU

The time that it takes to copy the frame from host memory to the GPU. If RDMA
is enabled for the consumer, this should be zero.

### Interpreting The Results

The individual times that are reported above are also grouped to give the
separate **Producer** (render + copy to host + write to HW) and
**Consumer** (read from HW + copy to GPU) times.

While this tool measures the producer times followed by the consumer times, the
expectation for real-world video processing applications is that this order
would be reversed. That is to say, the expectation for a real-world application
is that it would do the following:

1. Read from HW (**consumer**)
2. Copy to GPU (**consumer**)
3. Process/Render Results to GPU (**producer**)
4. Copy to Host (**producer**)
5. Write to HW (**producer**)

This sum of these five steps are then reported as the **Estimated Application
Times**.

After this total estimated application time, the system would then need to wait
for the next vsync interval before proceeding to scan out across the physical
wire. Using this assumption, the tool provides one final value for the
**Final Estimated Latencies**. This value is estimated using the following:

1. Take the **Estimated Application Time**
2. Round it up to the next VSync interval
3. Add the physical wire time

This final **Final Estimated Latencies** is reported in both absolute time as
well as frame intervals, and provides and estimate for the total amount of
end-to-end latency that would be introduced by the Clara Holoscan system if the
measured components were utilized.

#### Example Output

```
Format: 1920x1080 RGBA @ 60Hz

Producer: AJA
    Device: 0
    Channel: NTV2_CHANNEL1
    RDMA: 1

Consumer: AJA
    Device: 0
    Channel: NTV2_CHANNEL2
    RDMA: 1

Measuring 600 frames...Done!

CUDA Processing: avg =      0, min =      0, max =     60
Render on GPU:   avg =     82, min =     46, max =    221
Copy To Host:    avg =      0, min =      0, max =     31
Write To HW:     avg =   1955, min =   1741, max =   3888
Vsync Wait:      avg =  14609, min =  12651, max =  15501
Wire Time:       avg =  16752, min =  15938, max =  18109
Read From HW:    avg =   1568, min =   1488, max =   3728
Copy To GPU:     avg =      0, min =      0, max =      1
=========================================================
Total:           avg =  34971, min =  34580, max =  37480


Producer (Process and Write to HW)
=========================================================
   Microseconds: avg =   2039, min =   1823, max =   3964
         Frames: avg =  0.122, min =  0.109, max =  0.238

Consumer (Read from HW and Copy to GPU)
=========================================================
   Microseconds: avg =   1568, min =   1488, max =   3729
         Frames: avg = 0.0941, min = 0.0893, max =  0.224

Estimated Application Times (Read + Process + Write)
=========================================================
   Microseconds: avg =   3608, min =   3393, max =   5778
         Frames: avg =  0.216, min =  0.204, max =  0.347

Final Estimated Latencies (Processing + Vsync + Wire)
=========================================================
   Microseconds: avg =  33332, min =  33332, max =  33332
         Frames: avg =      2, min =      2, max =      2
```

### Estimating GPU Processing Workload

By default, the tool measures just the bare minimum that is required for the
video input and output without doing any sort of additional frame processing.
In reality, a Clara Holoscan video processing application would expect to add
some amount of GPU processing between the read (consumer) and write (producer)
stages of this pipeline. Since this GPU processing may compete with GPU
resources that are used by the video I/O itself, this processing may also have
adverse effects on the entire pipeline that isn't measured just by adding the
video I/O and processing times separately.

In order to estimate the total latency when an additional GPU workload is
added to the system, the latency tool has an `-s {count}` option that can be
used to run an arbitrary CUDA loop the specified number of times before the
producer actually generates a frame. The expected usage for this option is
as follows:

1. The per-frame runtime of the *actual* GPU processing algorithm is measured
   outside of the latency measurement tool.
2. The latency tool is repeatedly run with the `-c none -s {count}` options,
   adjusting the `{count}` parameter until the time that it takes to run the
   simulated loop approximately matches the actual processing time that was
   measured in the previous step.
3. The latency tool is run with the full producer (`-p`) and consumer (`-c`)
   options used for the video I/O, along with the `-s {count}` option using
   the loop count that was determined in the previous step.

Doing this will measure and estimate the total overall latency for both the
video I/O *and* the additional GPU processing, and should hopefully be
indicative of the effects that the processing and I/O has on each other due
to the overall system/GPU load.

## Producers

There are currently 3 producer types supported:

### OpenGL GPU Direct Rendering (HDMI)

This producer (`gl`) uses OpenGL to render frames directly on the GPU for
output via the HDMI connectors on the GPU. This is currently expected to be the
lowest latency path for GPU video output.

OpenGL Producer Notes:

 * The video generated by this producer is rendered full-screen to the primary
   display. As of this version, this component has only been tested in a
   display-less environment in which the loopback HDMI cable is the only cable
   attached to the GPU (and thus is the primary display).

 * Since OpenGL renders directly to the GPU, the `p.rdma` flag is not supported
   and RDMA is always considered to be enabled for this producer.

### GStreamer GPU Rendering (HDMI)

This producer (`gst`) uses the `nveglglessink` GStreamer component that is
included with JetPack in order to render frames that originate from a
GStreamer pipeline to the HDMI connectors on the GPU.

GStreamer Producer Notes:

 * The tool must be built with DeepStream support in order for this producer to
   support RDMA (see Building section for details).

 * The video generated by this producer is rendered full-screen to the primary
   display. As of this version, this component has only been tested in a
   display-less environment in which the loopback HDMI cable is the only cable
   attached to the GPU (and thus is the primary display).

 * Since the output of the generated frames is handled internally by the
   `nveglglessink` plugin, the timing of when the frames are output from the
   GPU are not known. Because of this, the 'Wire Time' that is reported by
   this producer includes all of the time that the frame spends between being
   passed to the `nveglglessink` and when it is finally received by the
   consumer.

### AJA Video Systems (SDI and HDMI)

This producer (`aja`) outputs video frames from an AJA Video Systems device
that supports video playback. This can be either an SDI or an HDMI video
source.

AJA Producer Notes:

 * The tool must be built with AJA Video Systems support in order for this
   producer to be available (see Building section for details).

 * The following parameters can be used to configure the AJA device and
   channel that are used to output the frames:

   `-p.device` -- Integer specifying the device index (i.e. `0` or `1`)

   `-p.channel` -- Integer specifying the channel number, starting at 1
   (i.e. `1` specifies `NTV2_CHANNEL_1`).

 * The `p.rdma` flag can be used to enable (`1`) or disable (`0`) the use of
   RDMA with the producer. If RDMA is to be used, the AJA drivers loaded on
   the system must also support RDMA.

## Consumers

There are currently 3 consumer types supported:

### V4L2 API (Onboard HDMI Capture Card)

This consumer (`v4l2`) uses the V4L2 API directly in order to capture frames
using the HDMI capture card that is onboard the Clara AGX Developer Kit.

V4L2 Consumer Notes:

 * The onboard HDMI capture card is locked to a specific frame resolution and
   and frame rate (1080p @ 60Hz), and so `1080` is the only supported format
   when using this consumer.

 * The V4L2 API does not support RDMA, and so the `c.rdma` option is ignored.

### GStreamer (Onboard HDMI Capture Card)

This consumer (`gst`) also captures frames from the onboard HDMI capture card,
but uses the `v4l2src` GStreamer plugin that wraps the V4L2 API to support
capturing frames for using within a GStreamer pipeline.

GStreamer Consumer Notes:

 * The onboard HDMI capture card is locked to a specific frame resolution and
   and frame rate (1080p @ 60Hz), and so `1080` is the only supported format
   when using this consumer.

 * The GStreamer V4L2 plugin does not support RDMA, and so the `c.rdma` option
   is ignored.

### AJA Video Systems (SDI and HDMI)

This consumer (`aja`) captures video frames from an AJA Video Systems device
that supports video capture. This can be either an SDI or an HDMI video capture
card.

AJA Consumer Notes:

 * The tool must be built with AJA Video Systems support in order for this
   consumer to be available (see Building section for details).

 * The following parameters can be used to configure the AJA device and
   channel that are used to capture the frames:

   `-c.device` -- Integer specifying the device index (i.e. `0` or `1`)

   `-c.channel` -- Integer specifying the channel number, starting at 1
   (i.e. `1` specifies `NTV2_CHANNEL_1`).

 * The `c.rdma` flag can be used to enable (`1`) or disable (`0`) the use of
   RDMA with the consumer. If RDMA is to be used, the AJA drivers loaded on
   the system must also support RDMA.

## Example Configurations

The following sections present various configurations that have been
successfully tested using this tool.

### AJA SDI or HDMI to AJA SDI or HDMI

In this configuration, an SDI or HDMI cable is connected between either two
channels on the same device or between two separate devices. For example, if a
cable is connected between channels 1 and 2 of a single AJA device, the
following command can be used:

```sh
$ loopback-latency -p aja -p.device 0 -p.channel 1 -c aja -c.device 0 -c.channel 2
```

Note that the above device and channel parameter values are the defaults for
the AJA producer and consumer components, so the following can be used as
shorthand for the above:

```sh
$ loopback-latency -p aja -c aja
```

If instead channel 1 of two separate devices (0 and 1) are connected, the
following can be used:

```sh
$ loopback-latency -p aja -p.device 0 -p.channel 1 -c aja -c.device 1 -c.channel 1
```

### AJA SDI to Onboard HDMI Using an SDI-to-HDMI Converter

If an AJA SDI playback device and an SDI-to-HDMI converter are available (for
example, an AJA Hi5-12G), then a loopback between SDI and HDMI can be
configured using any of the available HDMI consumers. For example, if an SDI
cable is connected from channel 1 of AJA device 0 to the SDI-to-HDMI converter,
which is in turn connected to the onboard HDMI capture board, then the following
can be used to measure the loopback using a V4L2 consumer:

```sh
$ loopback-latency -p aja -p.device 0 -p.channel 1 -c v4l2
```

*Note:* When using an SDI-to-HDMI converter, the output from the converter
must be set to full range RGB (rather than SMPTE) for the tool to work.

### GPU HDMI to Onboard HDMI

If an HDMI cable is connected between the GPU and the onboard HDMI capture
card, then the `gl` or `gst` producers can be used along with the `v4l2` or
`gst` consumers. For example, a `gl` to `v4l2` measurement can be done using:

```sh
$ loopback-latency -p gl -c v4l2
```

## Graphing Results

The tool includes an `-o {file}` option that can be used to output a CSV file
with all of the measured times for every frame. This file can then be used with
the `graph_results.py` tool in order to generate a graph of the measurements.

For example, if the latencies are measured with:

```sh
$ loopback-latency -p aja -c aja -o latencies.csv
```

The graph can then be generated using the following, which will open a window
on the desktop to display the graph:

```sh
$ graph_results.py --file latencies.csv
```

The above graphs the times directly as measured by the tool. To instead generate
a graph for the **Final Estimated Latencies**, the `--estimate` flag can be
provided to the script. The `--png {path}` option can also be used to output a
PNG file of the graph instead of opening a window on the display. For example,
the following will generate the **Final Estimated Latencies** graph and write
the output to `graph.png`:

```sh
$ graph_results.py --file latencies.csv --estimate --png graph.png
```

See the `-h` documentation for the `graph_results.py` script for more options.
