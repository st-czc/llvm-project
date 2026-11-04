/*===--------------------------------------------------------------------------
 *                   ROCm Device Libraries
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *===------------------------------------------------------------------------*/

#include "mathH.h"

UGEN(sin)

half
MATH_MANGLE(sin)(half x)
{
    return __builtin_sinf16(x);
}
