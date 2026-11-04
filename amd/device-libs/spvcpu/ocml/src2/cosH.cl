/*===--------------------------------------------------------------------------
 *                   ROCm Device Libraries
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *===------------------------------------------------------------------------*/

#include "mathH.h"

UGEN(cos)

half
MATH_MANGLE(cos)(half x)
{
    return __builtin_cosf16(x);
}
