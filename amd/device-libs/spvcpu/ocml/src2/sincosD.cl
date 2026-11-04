/*===--------------------------------------------------------------------------
 *                   ROCm Device Libraries
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *===------------------------------------------------------------------------*/

#include "mathD.h"

double
MATH_MANGLE(sincos)(double x, __private double *cp)
{
    double sn, cs;
    __builtin_sincos(x, &sn, &cs);
    *cp = cs;
    return sn;
}
