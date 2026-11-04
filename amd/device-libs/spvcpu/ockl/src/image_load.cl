/*===--------------------------------------------------------------------------
 *                   ROCm Device Libraries
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *===------------------------------------------------------------------------*/

#include "ockl.h"

/* Buffer-backed 1D image load for spvcpu. The descriptor uses the AMDGPU V#
 * layout written by the runtime: DW0 and DW1[15:0] form the base address,
 * DW1[31:16] is the record stride and DW2 the record count. */
float4
OCKL_MANGLE_T(image_load,1Db)(TSHARP i, int c)
{
    uint num_records = i[2];
    if ((uint)c >= num_records)
        return (float4)(0.0f);

    uint dw1 = i[1];
    uint stride = dw1 >> 16;
    ulong base = (ulong)i[0] | ((ulong)(dw1 & 0xffff) << 32);
    const __global uint *ptr = (const __global uint *)(base + (ulong)c * stride);

    float4 result = (float4)(0.0f);
    if (stride >= 4)
        result.x = as_float(ptr[0]);
    if (stride >= 8)
        result.y = as_float(ptr[1]);
    if (stride >= 12)
        result.z = as_float(ptr[2]);
    if (stride >= 16)
        result.w = as_float(ptr[3]);
    return result;
}
