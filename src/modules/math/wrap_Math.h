/**
<<<<<<< HEAD
 * Copyright (c) 2006-2022 LOVE Development Team
=======
 * Copyright (c) 2006-2023 LOVE Development Team
>>>>>>> 22b6388a96735dc70f1b503de7c2b4bfd4d2d5bc
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#ifndef LOVE_MATH_WRAP_MATH_H
#define LOVE_MATH_WRAP_MATH_H

// LOVE
#include "common/config.h"
#include "common/runtime.h"

namespace love
{
namespace math
{

extern "C" LOVE_EXPORT int luaopen_love_math(lua_State *L);

} // random
} // love

#endif // LOVE_MATH_WRAP_MATH_H

