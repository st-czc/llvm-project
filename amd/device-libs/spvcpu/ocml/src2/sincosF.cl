/*===--------------------------------------------------------------------------
 *                   ROCm Device Libraries
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *===------------------------------------------------------------------------*/

#include "mathF.h"

float
MATH_MANGLE(sincos)(float x, __private float *cp)
{
    float sn, cs;
    __builtin_sincosf(x, &sn, &cs);
    *cp = cs;
    return sn;
}
