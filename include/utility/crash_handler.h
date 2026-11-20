#pragma once

#include <string>

// Last-resort crash reporting: an unhandled exception (an access violation, or a C++ exception no one caught, on any
//   thread) is logged to SKIF.log with its cause, and a minidump of the process is written to dumpPath, before the
//     process ends as it would have without this. Installed once, after logging is set up.
void SKIF_CrashHandler_Install (const std::wstring& dumpPath);
