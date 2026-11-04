/*===--------------------------------------------------------------------------
 *                   ROCm Device Libraries
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *===------------------------------------------------------------------------*/

#include "mathH.h"

half2
MATH_MANGLE2(sincos)(half2 x, __private half2 *cp)
{
    half2 s;
    half clo, chi;
    s.lo = MATH_MANGLE(sincos)(x.lo, &clo);
    s.hi = MATH_MANGLE(sincos)(x.hi, &chi);
    *cp = (half2)(clo, chi);
    return s;
}

CONSTATTR half
MATH_MANGLE(sincos)(half x, __private half *cp)
{
    half sn, cs;
    __builtin_sincosf16(x, &sn, &cs);
    *cp = cs;
    return sn;
}
