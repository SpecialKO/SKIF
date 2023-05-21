// Below sections of codes are from the reset utility of Custom Resolution Utility (CRU)
// https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU

/**************************************************************************

Copyright (C) 2013-2021 ToastyX

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

 **************************************************************************/

#pragma once

/* Includes ***************************************************************/

#include <windows.h>
#include <devguid.h>
#include <direct.h>
#include <setupapi.h>
#include <stdio.h>
#include <tchar.h>
#include <tlhelp32.h>

/* Libraries **************************************************************/

#pragma comment(lib, "setupapi.lib")

/* Functions **************************************************************/

BOOL RestartDriver();