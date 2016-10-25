/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _QDMETADATA_H
#define _QDMETADATA_H

#ifdef __cplusplus
extern "C" {
#endif

enum ColorSpace_t{
    ITU_R_601,
    ITU_R_601_FR,
    ITU_R_709,
};

enum IGC_t {
    IGC_NotSpecified,
    IGC_sRGB,
};

struct HSICData_t {
    int32_t hue;
    float   saturation;
    int32_t intensity;
    float   contrast;
};

struct BufferDim_t {
    int32_t sliceWidth;
    int32_t sliceHeight;
};

struct MetaData_t {
    int32_t operation;
    int32_t interlaced;
    struct BufferDim_t bufferDim;
    float refreshrate;
    enum ColorSpace_t colorSpace;
    enum IGC_t igc;
     /* Gralloc sets PRIV_SECURE_BUFFER flag to inform that the buffers are from
      * ION_SECURE. which should not be mapped. However, for GPU post proc
      * feature, GFX needs to map this buffer, in the client context and in SF
      * context, it should not. Hence to differentiate, add this metadata field
      * for clients to set, and GPU will to read and know when to map the
      * SECURE_BUFFER(ION) */
    int32_t mapSecureBuffer;
    /* The supported formats are defined in gralloc_priv.h to
     * support legacy code*/
    uint32_t s3dFormat;
    /* VENUS output buffer is linear for UBWC Interlaced video */
    uint32_t linearFormat;
    /* Set by graphics to indicate that this buffer will be written to but not
     * swapped out */
    uint32_t isSingleBufferMode;
    /* Set by camera to program the VT Timestamp */
    uint64_t vtTimeStamp;
};

enum DispParamType {
    UNUSED0                = 0x0001,
    UNUSED1                = 0x0002,
    PP_PARAM_INTERLACED    = 0x0004,
    UNUSED2                = 0x0008,
    UNUSED3                = 0x0010,
    UNUSED4                = 0x0020,
    UNUSED5                = 0x0040,
    UPDATE_BUFFER_GEOMETRY = 0x0080,
    UPDATE_REFRESH_RATE    = 0x0100,
    UPDATE_COLOR_SPACE     = 0x0200,
    MAP_SECURE_BUFFER      = 0x0400,
    S3D_FORMAT             = 0x0800,
    LINEAR_FORMAT          = 0x1000,
    SET_IGC                = 0x2000,
    SET_SINGLE_BUFFER_MODE = 0x4000,
    SET_VT_TIMESTAMP       = 0x8000,
};

enum DispFetchParamType {
    GET_PP_PARAM_INTERLACED  = 0x0004,
    GET_BUFFER_GEOMETRY      = 0x0080,
    GET_REFRESH_RATE         = 0x0100,
    GET_COLOR_SPACE          = 0x0200,
    GET_MAP_SECURE_BUFFER    = 0x0400,
    GET_S3D_FORMAT           = 0x0800,
    GET_LINEAR_FORMAT        = 0x1000,
    GET_IGC                  = 0x2000,
    GET_SINGLE_BUFFER_MODE   = 0x4000,
    GET_VT_TIMESTAMP         = 0x8000,
};

struct private_handle_t;
int setMetaData(struct private_handle_t *handle, enum DispParamType paramType,
        void *param);

int getMetaData(struct private_handle_t *handle, enum DispFetchParamType paramType,
        void *param);

int copyMetaData(struct private_handle_t *src, struct private_handle_t *dst);
#ifdef __cplusplus
}
#endif

#endif /* _QDMETADATA_H */

