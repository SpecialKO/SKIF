// Last-resort crash reporting (see utility/crash_handler.h)
//
//   SetUnhandledExceptionFilter ( ): every unhandled exception of any thread ends up here, C++ exceptions included
//     (0xE06D7363; the CRT's own filter, which this one calls after, turns those into std::terminate ( )). The cause is
//       logged, with the what ( ) of a std::exception, and a minidump is written. The filter that was installed before
//         (the CRT's) then decides what happens, so the process ends as it would have otherwise.
//
//   std::set_terminate ( ): std::terminate ( ) called on the main thread for another reason (e.g. an exception leaving
//     a noexcept function). MSVC keeps a terminate handler per thread, so this only covers the main thread.
//
//   SIGABRT: std::terminate ( ) on any other thread ends in abort ( ), and SIGABRT is handled process wide. That is how
//     a C++ exception leaving a thread of _beginthreadex ends on 64-bit: the CRT's thread start calls std::terminate ( )
//       itself, so neither of the above sees it. Its what ( ) comes from the most recent C++ exception thrown on the
//         thread, which a vectored exception handler remembers (nothing has been unwound yet, so it is still there).

#include <utility/crash_handler.h>

#include <Windows.h>
#include <DbgHelp.h>
#include <exception>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <plog/Log.h>

namespace
{
  std::wstring                 s_dumpPath;
  LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter    = nullptr;
  std::terminate_handler       s_previousTerminate = nullptr;
  volatile LONG                s_handled           = 0; // Only the first crash is reported

  constexpr DWORD EXCEPTION_MSVC_CPP = 0xE06D7363; // 'msc' | 0xE0000000: a C++ exception (throw)

  // The most recent C++ exception thrown on this thread (see _OnAbort ( ))
  thread_local EXCEPTION_RECORD tl_lastThrow  = { };
  thread_local bool             tl_hasThrown  = false;

  // The layout of the MSVC C++ exception information (what the compiler emits for a throw; see ehdata.h). On 64-bit,
  //   the "pointers" are offsets from the image base of the module that threw (ExceptionInformation [3])
  struct cpp_type_descriptor_s  { const void* vftable; void* spare; char name [1]; };
  struct cpp_pmd_s              { int mdisp; int pdisp; int vdisp; };
  struct cpp_catchable_type_s   { unsigned int properties; int type; cpp_pmd_s thisDisplacement; int sizeOrOffset; int copyFunction; };
  struct cpp_catchable_array_s  { int count; int types [1]; };
  struct cpp_throw_info_s       { unsigned int attributes; int unwind; int forwardCompat; int catchableTypeArray; };

  // The what ( ) of the std::exception that was thrown, if it is one. No C++ objects here, for __try
  bool
  _GetCppExceptionWhat (const EXCEPTION_RECORD* record, char* what, size_t size)
  {
    __try
    {
      if (record->ExceptionCode != EXCEPTION_MSVC_CPP || record->NumberParameters < 3)
        return false;

      const ULONG_PTR magic = record->ExceptionInformation [0];

      if (magic != 0x19930520 && magic != 0x19930521 && magic != 0x19930522)
        return false;

      const BYTE* object = reinterpret_cast <const BYTE*> (record->ExceptionInformation [1]);
      const auto* info   = reinterpret_cast <const cpp_throw_info_s*> (record->ExceptionInformation [2]);

      if (object == nullptr || info == nullptr)
        return false;

#ifdef _WIN64
      const BYTE* base   = (record->NumberParameters >= 4) ? reinterpret_cast <const BYTE*> (record->ExceptionInformation [3]) : nullptr;

      if (base == nullptr)
        return false;

      auto _Ptr = [base](int rva) -> const BYTE* { return base + rva; };
#else
      auto _Ptr = [](int ptr) -> const BYTE* { return reinterpret_cast <const BYTE*> (static_cast <uintptr_t> (ptr)); };
#endif

      const auto* types = reinterpret_cast <const cpp_catchable_array_s*> (_Ptr (info->catchableTypeArray));

      for (int i = 0; i < types->count; ++i)
      {
        const auto* type       = reinterpret_cast <const cpp_catchable_type_s*>  (_Ptr (types->types [i]));
        const auto* descriptor = reinterpret_cast <const cpp_type_descriptor_s*> (_Ptr (type->type));

        if (strcmp (descriptor->name, ".?AVexception@std@@") != 0)
          continue;

        // Virtual bases are not a thing for std::exception in practice; only the plain displacement is applied
        const auto* exception = reinterpret_cast <const std::exception*> (object + type->thisDisplacement.mdisp);

        strncpy_s (what, size, exception->what ( ), _TRUNCATE);

        return true;
      }
    }

    __except (EXCEPTION_EXECUTE_HANDLER) { }

    return false;
  }

  struct dump_request_s {
    EXCEPTION_POINTERS* exception;
    DWORD               threadId;
    bool                written;
  };

  // On a thread of its own, so it also works when the crashing thread has no stack left (a stack overflow)
  DWORD WINAPI
  _WriteDumpThread (LPVOID param)
  {
    auto* request = static_cast <dump_request_s*> (param);

    using MiniDumpWriteDump_pfn = BOOL (WINAPI*) (HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                                  PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

    HMODULE hDbgHelp = LoadLibraryExW (L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

    if (hDbgHelp == nullptr)
      return 0;

    auto _MiniDumpWriteDump = reinterpret_cast <MiniDumpWriteDump_pfn> (GetProcAddress (hDbgHelp, "MiniDumpWriteDump"));

    HANDLE hFile = (_MiniDumpWriteDump != nullptr) ? CreateFileW (s_dumpPath.c_str ( ), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                                                   : INVALID_HANDLE_VALUE;

    if (hFile != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION info = { request->threadId, request->exception, FALSE };

      request->written =
        _MiniDumpWriteDump (GetCurrentProcess ( ), GetCurrentProcessId ( ), hFile,
                            static_cast <MINIDUMP_TYPE> (MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithHandleData),
                            (request->exception != nullptr) ? &info : nullptr, nullptr, nullptr) != FALSE;

      CloseHandle (hFile);

      if (! request->written)
        DeleteFileW (s_dumpPath.c_str ( ));
    }

    FreeLibrary (hDbgHelp);

    return 0;
  }

  void
  _WriteDump (EXCEPTION_POINTERS* exception)
  {
    if (s_dumpPath.empty ( ))
      return;

    dump_request_s request = { exception, GetCurrentThreadId ( ), false };

    HANDLE hThread = CreateThread (nullptr, 0, _WriteDumpThread, &request, 0, nullptr);

    if (hThread != nullptr)
    {
      WaitForSingleObject (hThread, 60 * 1000);
      CloseHandle         (hThread);
    }

    if (request.written)
      PLOG_FATAL << "A minidump was written to " << s_dumpPath;
    else
      PLOG_FATAL << "Could not write a minidump to " << s_dumpPath;
  }

  LONG WINAPI
  _UnhandledExceptionFilter (EXCEPTION_POINTERS* exception)
  {
    if (InterlockedExchange (&s_handled, 1) == 0 && exception != nullptr && exception->ExceptionRecord != nullptr)
    {
      const EXCEPTION_RECORD* record  = exception->ExceptionRecord;
      const void*             address = record->ExceptionAddress;

      // The module the exception happened in, and where in it
      HMODULE hModule = nullptr;
      wchar_t wszModule [MAX_PATH] = L"?";

      if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast <LPCWSTR> (address), &hModule))
        GetModuleFileNameW (hModule, wszModule, MAX_PATH);

      const uintptr_t offset = (hModule != nullptr) ? reinterpret_cast <uintptr_t> (address) - reinterpret_cast <uintptr_t> (hModule) : 0;

      char what [512] = { };

      if (record->ExceptionCode == EXCEPTION_MSVC_CPP)
      {
        if (_GetCppExceptionWhat (record, what, sizeof (what)))
          PLOG_FATAL << "Unhandled C++ exception on thread " << GetCurrentThreadId ( ) << ": " << what;
        else
          PLOG_FATAL << "Unhandled C++ exception on thread " << GetCurrentThreadId ( ) << " (not a std::exception)";
      }

      else
      {
        char code [64] = { };
        sprintf_s (code, "0x%08X", record->ExceptionCode);

        PLOG_FATAL << "Unhandled exception " << code << " on thread " << GetCurrentThreadId ( ) << " at "
                   << wszModule << "+0x" << std::hex << offset << std::dec;

        // An access violation: whether it was a read or a write, and of what
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        {
          char access [96] = { };
          sprintf_s (access, "%s of address 0x%p", (record->ExceptionInformation [0] == 0) ? "Read" :
                                                   (record->ExceptionInformation [0] == 1) ? "Write" : "Execution",
                                                   reinterpret_cast <void*> (record->ExceptionInformation [1]));
          PLOG_FATAL << access;
        }
      }

      _WriteDump (exception);
    }

    // What would have happened otherwise (the CRT's filter: std::terminate ( ) for a C++ exception)
    return (s_previousFilter != nullptr) ? s_previousFilter (exception) : EXCEPTION_CONTINUE_SEARCH;
  }

  void
  _Terminate (void)
  {
    if (InterlockedExchange (&s_handled, 1) == 0)
    {
      try {
        if (std::exception_ptr current = std::current_exception ( ))
          std::rethrow_exception (current);

        PLOG_FATAL << "std::terminate ( ) was called on thread " << GetCurrentThreadId ( ) << " without an exception";
      }

      catch (const std::exception& e)
      {
        PLOG_FATAL << "std::terminate ( ) was called on thread " << GetCurrentThreadId ( ) << " for an unhandled C++ exception: " << e.what ( );
      }

      catch (...)
      {
        PLOG_FATAL << "std::terminate ( ) was called on thread " << GetCurrentThreadId ( ) << " for an unhandled C++ exception (not a std::exception)";
      }

      _WriteDump (nullptr);
    }

    if (s_previousTerminate != nullptr)
      s_previousTerminate ( );

    std::abort ( );
  }

  // Every exception, first: remembers each C++ throw of the thread (a copy of its record; cheap, and nothing else)
  LONG CALLBACK
  _RememberThrow (EXCEPTION_POINTERS* exception)
  {
    if (exception != nullptr && exception->ExceptionRecord != nullptr && exception->ExceptionRecord->ExceptionCode == EXCEPTION_MSVC_CPP)
    {
      tl_lastThrow = *exception->ExceptionRecord;
      tl_lastThrow.ExceptionRecord = nullptr;
      tl_hasThrown = true;
    }

    return EXCEPTION_CONTINUE_SEARCH;
  }

  void __cdecl
  _OnAbort (int)
  {
    if (InterlockedExchange (&s_handled, 1) == 0)
    {
      char what [512] = { };

      // Most often an unhandled C++ exception, whose what ( ) is that of the last one thrown on the thread. It could
      //   also be an exception that was caught before something else called abort ( ), hence the wording
      if (tl_hasThrown && _GetCppExceptionWhat (&tl_lastThrow, what, sizeof (what)))
        PLOG_FATAL << "abort ( ) was called on thread " << GetCurrentThreadId ( ) << "; the last C++ exception thrown on it: " << what;
      else
        PLOG_FATAL << "abort ( ) was called on thread " << GetCurrentThreadId ( );

      _WriteDump (nullptr);
    }

    // Returning lets abort ( ) end the process, as it would have
  }
}

void
SKIF_CrashHandler_Install (const std::wstring& dumpPath)
{
  s_dumpPath          = dumpPath;
  s_previousFilter    = SetUnhandledExceptionFilter (_UnhandledExceptionFilter);
  s_previousTerminate = std::set_terminate          (_Terminate);

  AddVectoredExceptionHandler (0, _RememberThrow); // 0: after any other vectored handler
  std::signal                 (SIGABRT, _OnAbort);
}
