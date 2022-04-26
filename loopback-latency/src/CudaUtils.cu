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

#include <cuda.h>

#include "CudaUtils.h"
#include "Console.h"

void* CudaAlloc(size_t size, bool enableRDMA)
{
    void* ptr;
    cudaMalloc(&ptr, size);
    if (enableRDMA)
    {
        unsigned int syncFlag = 1;
        if (cuPointerSetAttribute(&syncFlag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, (CUdeviceptr)ptr))
        {
            Error("Failed to set CU_POINTER_ATTRIBUTE_SYNC_MEMOPS");
            cudaFree(ptr);
            return nullptr;
        }
    }
    return ptr;
}

void CudaFree(void* ptr)
{
    cudaFree(ptr);
}

void CudaMemcpyDtoH(void* host, void* dev, size_t bytes)
{
    cudaMemcpy(host, dev, bytes, cudaMemcpyDeviceToHost);
}

void CudaMemcpyHtoD(void* dev, void* host, size_t bytes)
{
    cudaMemcpy(dev, host, bytes, cudaMemcpyHostToDevice);
}

__global__
void WriteRGBA(uint32_t *ptr, size_t elementCount, uint32_t value)
{
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    for (int i = index; i < elementCount; i += stride)
    {
        ptr[i] = value;
    }
}

void CudaWriteRGBA(uint32_t* ptr, size_t elementCount, uint8_t r, uint8_t g, uint8_t b)
{
    unsigned int blockSize = 1024;
    unsigned int numBlocks = (elementCount + blockSize - 1) / blockSize;

    uint32_t abgr = (0xFF << 24) | (b << 16) | (g << 8) | (r << 0);
    WriteRGBA<<<numBlocks, blockSize>>>(ptr, elementCount, abgr);

    cudaStreamSynchronize(cudaStreamPerThread);
}

__global__
void SimulateProcessing(uint32_t* ptr, size_t elementCount, size_t loopCount)
{
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    int stride = blockDim.x * gridDim.x;
    for (int i = index; i < elementCount; i += stride)
    {
        int val = 0;
        for (int j = 0; j < loopCount; j++)
        {
            val += sin((i + j) / 1000.0f);
        }
        ptr[i] = val;
    }
}

void CudaSimulateProcessing(uint32_t* ptr, size_t elementCount, size_t loopCount)
{
    if (!loopCount)
        return;

    unsigned int blockSize = 1024;
    unsigned int numBlocks = (elementCount + blockSize - 1) / blockSize;

    SimulateProcessing<<<numBlocks, blockSize>>>(ptr, elementCount, loopCount);

    cudaStreamSynchronize(cudaStreamPerThread);
}
