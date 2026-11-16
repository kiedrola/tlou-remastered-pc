// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: small POSIX helpers the renderer uses for diagnostics and thread identity.
// Force-included into the GPU library on every platform (CMakeLists.txt); kept free of
// <windows.h> so its macros do not reach every translation unit.
#pragma once
#ifdef _WIN32
#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
static inline int gettid(void) { return (int)GetCurrentThreadId(); }
#ifdef __cplusplus
}
#endif
#else
#include <unistd.h>
// gow3 (Linux): the mirror of the Windows wrapper: the Win32 name in terms of gettid()
// (glibc >= 2.30). Call sites keep the Windows spelling on both platforms.
static inline unsigned long GetCurrentThreadId(void) { return (unsigned long)gettid(); }
#endif
