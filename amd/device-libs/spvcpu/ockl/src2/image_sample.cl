/*===--------------------------------------------------------------------------
 *                   ROCm Device Libraries
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *===------------------------------------------------------------------------*/

#include "ockl.h"

/* Buffer-backed 1D image sample for spvcpu. The image descriptor uses the
 * AMDGPU V# layout: DW0/DW1[15:0] is the base address, DW1[31:16] the record
 * stride, DW2 the record count and DW3 the channel format. The sampler
 * descriptor uses the gfx9 S# subset: DW0[2:0] clamp mode, DW0[15]
 * force-unnormalized, DW1[1:0] and DW1[3:2] mag/min filter. */

#define CLAMP_REPEAT 0u
#define CLAMP_MIRROR 1u
#define CLAMP_TO_EDGE 2u
#define CLAMP_BORDER 4u

#define NUM_UNORM 0u
#define NUM_SNORM 1u
#define NUM_UINT 4u
#define NUM_SINT 5u
#define NUM_FLOAT 7u

static float
sampleUnorm8(uint v)
{
    return (float)v / 255.0f;
}

static float
sampleSnorm8(uint v)
{
    int sv = (int)(char)v;
    float xf = __builtin_fminf(__builtin_fabsf((float)sv), 127.0f);
    return __builtin_copysignf(xf / 127.0f, (float)sv);
}

static float
sampleUnorm16(uint v)
{
    return (float)v / 65535.0f;
}

static float
sampleSnorm16(uint v)
{
    int sv = (int)(short)v;
    float xf = __builtin_fminf(__builtin_fabsf((float)sv), 32767.0f);
    return __builtin_copysignf(xf / 32767.0f, (float)sv);
}

static float
sampleConvert8(uint v, uint numFormat)
{
    switch (numFormat) {
    case NUM_UNORM:
        return sampleUnorm8(v);
    case NUM_SNORM:
        return sampleSnorm8(v);
    case NUM_UINT:
        return as_float(v);
    case NUM_SINT:
        return as_float((uint)(int)(char)v);
    default:
        return 0.0f;
    }
}

static float
sampleConvert16(uint v, uint numFormat)
{
    switch (numFormat) {
    case NUM_UNORM:
        return sampleUnorm16(v);
    case NUM_SNORM:
        return sampleSnorm16(v);
    case NUM_UINT:
        return as_float(v);
    case NUM_SINT:
        return as_float((uint)(int)(short)v);
    default:
        return 0.0f;
    }
}

static float
sampleConvert32(uint v, uint numFormat)
{
    switch (numFormat) {
    case NUM_UINT:
    case NUM_SINT:
    case NUM_FLOAT:
        return as_float(v);
    default:
        return 0.0f;
    }
}

static float4
sampleConvert(const __global uchar *p, uint dataFormat, uint numFormat)
{
    const __global ushort *p16 = (const __global ushort *)p;
    const __global uint *p32 = (const __global uint *)p;

    switch (dataFormat) {
    case 1: /* 8 */
        return (float4)(sampleConvert8(p[0], numFormat), 0.0f, 0.0f, 0.0f);
    case 2: /* 16 */
        return (float4)(sampleConvert16(p16[0], numFormat), 0.0f, 0.0f, 0.0f);
    case 3: /* 8_8 */
        return (float4)(sampleConvert8(p[0], numFormat), sampleConvert8(p[1], numFormat),
                        0.0f, 0.0f);
    case 4: /* 32 */
        return (float4)(sampleConvert32(p32[0], numFormat), 0.0f, 0.0f, 0.0f);
    case 5: /* 16_16 */
        return (float4)(sampleConvert16(p16[0], numFormat), sampleConvert16(p16[1], numFormat),
                        0.0f, 0.0f);
    case 10: /* 8_8_8_8 */
        return (float4)(sampleConvert8(p[0], numFormat), sampleConvert8(p[1], numFormat),
                        sampleConvert8(p[2], numFormat), sampleConvert8(p[3], numFormat));
    case 11: /* 32_32 */
        return (float4)(sampleConvert32(p32[0], numFormat), sampleConvert32(p32[1], numFormat),
                        0.0f, 0.0f);
    case 12: /* 16_16_16_16 */
        return (float4)(sampleConvert16(p16[0], numFormat), sampleConvert16(p16[1], numFormat),
                        sampleConvert16(p16[2], numFormat), sampleConvert16(p16[3], numFormat));
    case 14: /* 32_32_32_32 */
        return (float4)(sampleConvert32(p32[0], numFormat), sampleConvert32(p32[1], numFormat),
                        sampleConvert32(p32[2], numFormat), sampleConvert32(p32[3], numFormat));
    default:
        return (float4)(0.0f);
    }
}

static uint
sampleTexelBytes(uint dataFormat)
{
    switch (dataFormat) {
    case 1: /* 8 */
        return 1;
    case 2: /* 16 */
    case 3: /* 8_8 */
        return 2;
    case 4: /* 32 */
    case 5: /* 16_16 */
    case 10: /* 8_8_8_8 */
        return 4;
    case 11: /* 32_32 */
    case 12: /* 16_16_16_16 */
        return 8;
    case 14: /* 32_32_32_32 */
        return 16;
    default:
        return 0;
    }
}

static float
sampleTrunc(float x)
{
    return x >= 0.0f ? __builtin_floorf(x) : -__builtin_floorf(-x);
}

static int
sampleLinearIndex(float x, float *alpha)
{
    float xb = x - 0.5f;
    float xt = sampleTrunc(xb);
    // The fractional part is non-negative, so floor(v + 0.5) rounds half away from zero.
    float frac = __builtin_floorf((xb - xt) * 256.0f + 0.5f) / 256.0f;
    float coord = xt + frac;
    float i0 = __builtin_floorf(coord);
    *alpha = coord - i0;
    return (int)i0;
}

static bool
sampleOutOfBorder(float t, uint width)
{
    return t < 0.0f || t > (float)(width - 1);
}

static float
sampleApplyAddressMode(float t, uint width, uint mode)
{
    float d = (float)width;

    switch (mode) {
    case CLAMP_TO_EDGE:
        t = __builtin_fminf(__builtin_fmaxf(t, 0.0f), d - 1.0f);
        break;
    case CLAMP_REPEAT:
        t /= d;
        t -= __builtin_floorf(t);
        t *= d;
        break;
    case CLAMP_MIRROR: {
        t /= d;
        float frac = t - __builtin_floorf(t);
        int rev = ((int)__builtin_floorf(t)) & 1;
        t = rev ? 1.0f - frac : frac;
        t *= d;
        t -= (t == __builtin_floorf(t)) ? (float)rev : 0.0f;
        break;
    }
    default:
        break;
    }

    return t;
}

static float4
sampleTap(float t, ulong base, uint stride, uint width, uint mode, uint dataFormat,
          uint numFormat)
{
    if (mode == CLAMP_BORDER && sampleOutOfBorder(t, width))
        return (float4)(0.0f);

    t = sampleApplyAddressMode(t, width, mode);
    uint index = (uint)t;
    const __global uchar *p = (const __global uchar *)(base + (ulong)index * stride);
    return sampleConvert(p, dataFormat, numFormat);
}

static float4
sampleTap2D(float x, float y, ulong base, uint rowPitch, uint width, uint height, uint modeU,
            uint modeV, uint dataFormat, uint numFormat)
{
    if ((modeU == CLAMP_BORDER && sampleOutOfBorder(x, width)) ||
        (modeV == CLAMP_BORDER && sampleOutOfBorder(y, height)))
        return (float4)(0.0f);

    x = sampleApplyAddressMode(x, width, modeU);
    y = sampleApplyAddressMode(y, height, modeV);

    uint tx = (uint)x;
    uint ty = (uint)y;
    ulong offset = (ulong)ty * rowPitch + (ulong)tx * sampleTexelBytes(dataFormat);
    const __global uchar *p = (const __global uchar *)(base + offset);
    return sampleConvert(p, dataFormat, numFormat);
}

float4
OCKL_MANGLE_T(image_sample,1D)(TSHARP i, SSHARP s, float c)
{
    uint width = i[2];
    if (width == 0)
        return (float4)(0.0f);

    uint dw1 = i[1];
    uint stride = dw1 >> 16;
    ulong base = (ulong)i[0] | ((ulong)(dw1 & 0xffff) << 32);
    uint numFormat = (i[3] >> 8) & 0x7u;
    uint dataFormat = (i[3] >> 12) & 0xfu;

    uint s0 = s[0];
    uint s1 = s[1];
    uint mode = s0 & 0x7u;
    bool unnormalized = (s0 & 0x8000u) != 0;
    bool linear = (s1 & 0x3u) != 0;

    float x = unnormalized ? c : c * (float)width;

    if (!linear) {
        float t = __builtin_floorf(x);
        return sampleTap(t, base, stride, width, mode, dataFormat, numFormat);
    }

    float alpha;
    int i0 = sampleLinearIndex(x, &alpha);

    float4 t0 = sampleTap((float)i0, base, stride, width, mode, dataFormat, numFormat);
    float4 t1 = sampleTap((float)(i0 + 1), base, stride, width, mode, dataFormat, numFormat);
    return (1.0f - alpha) * t0 + alpha * t1;
}

float4
OCKL_MANGLE_T(image_sample,2D)(TSHARP i, SSHARP s, float2 c)
{
    uint dw1 = i[1];
    uint rowPitch = dw1 >> 16;
    uint height = i[2];
    uint numFormat = (i[3] >> 8) & 0x7u;
    uint dataFormat = (i[3] >> 12) & 0xfu;

    uint texelBytes = sampleTexelBytes(dataFormat);
    if (texelBytes == 0 || height == 0)
        return (float4)(0.0f);

    uint width = rowPitch / texelBytes;
    if (width == 0)
        return (float4)(0.0f);

    ulong base = (ulong)i[0] | ((ulong)(dw1 & 0xffff) << 32);

    uint s0 = s[0];
    uint s1 = s[1];
    uint modeU = s0 & 0x7u;
    uint modeV = (s0 >> 3) & 0x7u;
    bool unnormalized = (s0 & 0x8000u) != 0;
    bool linear = (s1 & 0x3u) != 0;

    float x = unnormalized ? c.x : c.x * (float)width;
    float y = unnormalized ? c.y : c.y * (float)height;

    if (!linear) {
        float tx = __builtin_floorf(x);
        float ty = __builtin_floorf(y);
        return sampleTap2D(tx, ty, base, rowPitch, width, height, modeU, modeV, dataFormat,
                           numFormat);
    }

    float alpha;
    float beta;
    int i0 = sampleLinearIndex(x, &alpha);
    int j0 = sampleLinearIndex(y, &beta);

    float4 t00 = sampleTap2D((float)i0, (float)j0, base, rowPitch, width, height, modeU, modeV,
                             dataFormat, numFormat);
    float4 t10 = sampleTap2D((float)(i0 + 1), (float)j0, base, rowPitch, width, height, modeU,
                             modeV, dataFormat, numFormat);
    float4 t01 = sampleTap2D((float)i0, (float)(j0 + 1), base, rowPitch, width, height, modeU,
                             modeV, dataFormat, numFormat);
    float4 t11 = sampleTap2D((float)(i0 + 1), (float)(j0 + 1), base, rowPitch, width, height,
                             modeU, modeV, dataFormat, numFormat);

    return (1.0f - alpha) * (1.0f - beta) * t00 + alpha * (1.0f - beta) * t10 +
           (1.0f - alpha) * beta * t01 + alpha * beta * t11;
}
