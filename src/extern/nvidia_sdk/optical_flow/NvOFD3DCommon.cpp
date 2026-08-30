/*
* Copyright (c) 2018-2023 NVIDIA Corporation
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use,
* copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the software, and to permit persons to whom the
* software is furnished to do so, subject to the following
* conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/

// This software contains source code provided by NVIDIA Corporation.
// Modified to support 8-bit output cost buffers on Direct3D.

#include "NvOFD3DCommon.h"

NV_OF_BUFFER_FORMAT DXGIFormatToNvOFBufferFormat(DXGI_FORMAT dxgiFormat)
{
    NV_OF_BUFFER_FORMAT ofBufFormat;
    switch (dxgiFormat)
    {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        ofBufFormat = NV_OF_BUFFER_FORMAT_ABGR8;
        break;
    case DXGI_FORMAT_R16G16_SINT:
        ofBufFormat = NV_OF_BUFFER_FORMAT_SHORT2;
        break;
    case DXGI_FORMAT_R16_UINT:
        ofBufFormat = NV_OF_BUFFER_FORMAT_SHORT;
        break;
    case DXGI_FORMAT_NV12:
        ofBufFormat = NV_OF_BUFFER_FORMAT_NV12;
        break;
    case DXGI_FORMAT_R32_UINT:
        ofBufFormat = NV_OF_BUFFER_FORMAT_UINT;
        break;
    // Map the added 8-bit cost buffer format from its Direct3D representation.
    case DXGI_FORMAT_R8_UINT:
        ofBufFormat = NV_OF_BUFFER_FORMAT_UINT8;
        break;
    case DXGI_FORMAT_R8_UNORM:
        ofBufFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;
        break;
    default:
        ofBufFormat = NV_OF_BUFFER_FORMAT_UNDEFINED;
    }
    return ofBufFormat;
}


DXGI_FORMAT NvOFBufferFormatToDxgiFormat(NV_OF_BUFFER_FORMAT  ofBufFormat)
{
    DXGI_FORMAT dxgiFormat;
    switch (ofBufFormat)
    {
    case NV_OF_BUFFER_FORMAT_ABGR8 :
        dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case NV_OF_BUFFER_FORMAT_SHORT2:
        dxgiFormat =  DXGI_FORMAT_R16G16_SINT;
        break;
    case NV_OF_BUFFER_FORMAT_SHORT:
        dxgiFormat = DXGI_FORMAT_R16_UINT;
        break;
    case NV_OF_BUFFER_FORMAT_NV12 :
        dxgiFormat = DXGI_FORMAT_NV12;
        break;
    case NV_OF_BUFFER_FORMAT_UINT:
        dxgiFormat = DXGI_FORMAT_R32_UINT;
        break;
    // Map the added 8-bit cost buffer format to an unsigned 8-bit Direct3D texture.
    case NV_OF_BUFFER_FORMAT_UINT8:
        dxgiFormat = DXGI_FORMAT_R8_UINT;
        break;
    case NV_OF_BUFFER_FORMAT_GRAYSCALE8:
        dxgiFormat = DXGI_FORMAT_R8_UNORM;
        break;
    default:
        dxgiFormat = DXGI_FORMAT_UNKNOWN;
    }
    return dxgiFormat;
}

uint32_t GetNumberOfPlanes(DXGI_FORMAT dxgiFormat)
{
    switch (dxgiFormat)
    {
    case DXGI_FORMAT_NV12:
        return 2;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_UNORM:
        return 1;
    default:
        NVOF_THROW_ERROR("Invalid buffer format", NV_OF_ERR_UNSUPPORTED_FEATURE);
    }

    return 0;
}

