#
# Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

cmake_minimum_required(VERSION 3.10)

project(loopback-latency LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)

# Hide CUDA_ARCHITECTURES warnings with newer CMake versions.
if (${CMAKE_VERSION} GREATER "3.18")
    cmake_policy(SET CMP0104 OLD)
endif()

# Hide GLVND warnings with newer CMake versions.
if (${CMAKE_VERSION} GREATER "3.11")
    cmake_policy(SET CMP0072 NEW)
endif()

# Find general dependencies.
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(GStreamer_FIND_VERSION 1.14)

find_package(PkgConfig REQUIRED)
find_package(GStreamer REQUIRED)
find_package(OpenGL REQUIRED)

pkg_search_module(GLFW REQUIRED glfw3)
pkg_search_module(GTK3 REQUIRED gtk+-3.0)

# Find DeepStream SDK
if(NOT DEEPSTREAM_SDK)
    message(WARNING "DEEPSTREAM_SDK not provided. Building without support for DeepStream (GStreamer Producer RDMA).")
else()
    find_path(DEEPSTREAM_INCLUDE_DIRS nvbufsurface.h PATHS ${DEEPSTREAM_SDK}/sources/includes)
    if(DEEPSTREAM_INCLUDE_DIRS STREQUAL "DEEPSTREAM_INCLUDE_DIRS-NOTFOUND")
        message(FATAL_ERROR "Unable to find DeepStream includes.")
    endif()

    # DeepStream libraries may exist alongside the core Tegra libraries for Tegra platforms.
    set(TEGRA_ROOT /usr/lib/aarch64-linux-gnu/tegra)

    find_library(NVBUFSURFACE_LIB NAMES nvbufsurface PATHS ${DEEPSTREAM_SDK}/lib ${TEGRA_ROOT})
    if(NVBUFSURFACE_LIB STREQUAL "NVBUFSURFACE_LIB-NOTFOUND")
        message(FATAL_ERROR "Unable to find NvBufSurface library (libnvbufsurface).")
    endif()

    find_library(NVDSBUFFERPOOL_LIB NAMES nvdsbufferpool PATHS ${DEEPSTREAM_SDK}/lib ${TEGRA_ROOT})
    if(NVDSBUFFERPOOL_LIB STREQUAL "NVDSBUFFERPOOL_LIB-NOTFOUND")
        message(FATAL_ERROR "Unable to find NvDsBufferPool library (libnvdsbufferpool).")
    endif()
endif()

# Find AJA NTV2 SDK
if(NOT NTV2_SDK)
    message(WARNING "NTV2_SDK not provided. Building without support for AJA devices.")
else()
    if(NOT EXISTS ${NTV2_SDK}/ajalibraries/ajantv2/includes/ntv2enums.h)
        message(FATAL_ERROR "NTV2_SDK does not point to a valid AJA NTV2 SDK.")
    endif()

    find_library(AJANTV2_LIB NAMES ajantv2 PATHS ${NTV2_SDK}/lib)
    if(AJANTV2_LIB STREQUAL "AJANTV2_LIB-NOTFOUND")
        message(FATAL_ERROR "Unable to find AJA NTV2 library (libajantv2).")
    endif()
endif()

# Begin application definition.
set(SOURCES
    src/main.cpp
    src/CudaUtils.cu
    src/DurationList.cpp
    src/GLProducer.cpp
    src/GStreamerConsumer.cpp
    src/GStreamerProducer.cpp
    src/Producer.cpp
    src/V4L2Consumer.cpp
)
if(NTV2_SDK)
    list(APPEND SOURCES
        src/AJABase.cpp
        src/AJAConsumer.cpp
        src/AJAProducer.cpp
    )
endif()

add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    ${GSTREAMER_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
    ${GTK3_INCLUDE_DIRS}
)
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    ${GSTREAMER_VIDEO_LIBRARIES}
    ${GLFW_LIBRARIES}
    ${GTK3_LIBRARIES}
    ${OPENGL_LIBRARIES}
    cuda dl pthread rt
)

if(DEEPSTREAM_SDK)
    target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
        ${DEEPSTREAM_INCLUDE_DIRS}
    )
    target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
        ${NVBUFSURFACE_LIB}
        ${NVDSBUFFERPOOL_LIB})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
        -DENABLE_DEEPSTREAM)
endif()

if(NTV2_SDK)
    target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
        ${NTV2_SDK}/ajalibraries/
        ${NTV2_SDK}/ajalibraries/ajantv2/includes
        ${NTV2_SDK}/ajalibraries/ajantv2/src/lin
    )
    target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
        ${AJANTV2_LIB})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
        -DENABLE_AJA
        -DAJALinux
        -DAJA_LINUX)
endif()

# This CUDA flag ensures that every thread uses its own default stream
# instead of sharing a single stream for the entire process. This allows
# memcpy operations to overlap so that they are not blocked by CUDA
# operations in other threads.
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --default-stream per-thread")
