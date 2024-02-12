//
// Copyright 2020 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <Windows.h>
#include <gsl/gsl>

#ifndef SECURITY_WIN32 
#define SECURITY_WIN32 
#endif

#include <Security.h>
#include <secext.h>
#include <userenv.h>

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Userenv.lib")

std::string
SK_WideCharToUTF8 (const std::wstring& in)
{
  /*
  size_t len =
    WideCharToMultiByte ( CP_UTF8, 0x00, in.c_str (), -1,
                           nullptr, 0, nullptr, FALSE );

  std::string out (
    len * 2 + 2,
      '\0'
  );

  WideCharToMultiByte   ( CP_UTF8, 0x00,          in.c_str  (),
                          gsl::narrow_cast <int> (in.length ()),
                                                 out.data   (),
                          gsl::narrow_cast <DWORD>       (len),
                            nullptr,                   FALSE );

  out.resize(len);
  */
  
  // CC BY-SA 4.0: https://stackoverflow.com/a/59617138
  int count = 
    WideCharToMultiByte (CP_UTF8, 0, in.c_str(), static_cast <int> (in.length()), NULL, 0, NULL, NULL);
  std::string out       (count, 0);
  WideCharToMultiByte   (CP_UTF8, 0, in.c_str(), -1, &out[0], count, NULL, NULL);


  return out;
}

std::wstring
SK_UTF8ToWideChar (const std::string& in)
{
  /*
  size_t len =
    MultiByteToWideChar ( CP_UTF8, 0x00, in.c_str (), -1,
                           nullptr, 0 );

  std::wstring out (
    len * 2 + 2,
      L'\0'
  );

  MultiByteToWideChar   ( CP_UTF8, 0x00,          in.c_str  (),
                          gsl::narrow_cast <int> (in.length ()),
                                                 out.data   (),
                          gsl::narrow_cast <DWORD>       (len) );

  out.resize(len);
  */

  // CC BY-SA 4.0: https://stackoverflow.com/a/59617138
  int count = 
    MultiByteToWideChar (CP_UTF8, 0, in.c_str(), static_cast <int> (in.length()), NULL, 0);
  std::wstring out      (count, 0);
  MultiByteToWideChar   (CP_UTF8, 0, in.c_str(), static_cast <int> (in.length()), &out[0], count);

  return out;
}

void
SK_StripLeadingSlashesA (char *szInOut)
{
  auto IsSlash = [](char a) -> bool {
    return (a == '\\' || a == '/');
  };

  size_t      len = strlen (szInOut);
  size_t  new_len = len;

  char* szStart = szInOut;

  while (          *szStart  != '\0' &&
        *CharNextA (szStart) != '\0' )
  {
    if (IsSlash (*szStart))
    {
      szStart =
        CharNextA (szStart);

      --new_len;
    }

    else
      break;
  }

  if (len != new_len)
  {
    char *szOut = szInOut;
    char *szIn  = szStart;

    for ( size_t i = 0 ; i < new_len ; ++i )
    {
      *szOut =           *szIn;

      if (*szOut == '\0')
        break;

       szIn  = CharNextA (szIn);
       szOut = CharNextA (szOut);
    }
  }
}

void
SK_StripTrailingSlashesA (char* szInOut)
{
  auto IsSlash = [](char a) -> bool {
    return (a == '\\' || a == '/');
  };

  char* szNextUnique = szInOut + 1;
  char* szNext       = szInOut;

  while (*szNext != '\0')
  {
    if (*szNextUnique == '\0')
    {
      *CharNextA (szNext) = '\0';
      break;
    }

    if (IsSlash (*szNext))
    {
      if (IsSlash (*szNextUnique))
      {
        szNextUnique =
          CharNextA (szNextUnique);

        continue;
      }
    }

    ++szNext;
     *szNext = *szNextUnique;
                szNextUnique =
     CharNextA (szNextUnique);
  }
}

void
SK_FixSlashesA (char* szInOut)
{
  if (szInOut == nullptr)
    return;

  char*   pszInOut  = szInOut;
  while (*pszInOut != '\0')
  {
    if (*pszInOut == '/')
        *pszInOut = '\\';

    pszInOut =
      CharNextA (pszInOut);
  }
}

void
SK_StripLeadingSlashesW (wchar_t *wszInOut)
{
  auto IsSlash = [](wchar_t a) -> bool {
    return (a == L'\\' || a == L'/');
  };

  size_t      len = wcslen (wszInOut);
  size_t  new_len = len;

  wchar_t* wszStart = wszInOut;

  while (          *wszStart  != L'\0' &&
        *CharNextW (wszStart) != L'\0' )
  {
    if (IsSlash (*wszStart))
    {
      wszStart =
        CharNextW (wszStart);

      --new_len;
    }

    else
      break;
  }

  if (len != new_len)
  {
    wchar_t *wszOut = wszInOut;
    wchar_t *wszIn  = wszStart;

    for ( size_t i = 0 ; i < new_len ; ++i )
    {
      *wszOut =           *wszIn;

      if (*wszOut == L'\0')
        break;

       wszIn  = CharNextW (wszIn);
       wszOut = CharNextW (wszOut);
    }
  }

  // Else:
}
//
// In-place version of the old code that had to
//   make a copy of the string and then copy-back
//
void
SK_StripTrailingSlashesW (wchar_t* wszInOut)
{
  //wchar_t* wszValidate = wcsdup (wszInOut);

  auto IsSlash = [](wchar_t a) -> bool {
    return (a == L'\\' || a == L'/');
  };

  wchar_t* wszNextUnique = CharNextW (wszInOut);
  wchar_t* wszNext       = wszInOut;

  while (*wszNext != L'\0')
  {
    if (*wszNextUnique == L'\0')
    {
      *CharNextW (wszNext) = L'\0';
      break;
    }

    if (IsSlash (*wszNext))
    {
      if (IsSlash (*wszNextUnique))
      {
        wszNextUnique =
          CharNextW (wszNextUnique);

        continue;
      }
    }

    wszNext = CharNextW (wszNext);
   *wszNext = *wszNextUnique;
    wszNextUnique =
      CharNextW (wszNextUnique);
  }
}

void
SK_FixSlashesW (wchar_t* wszInOut)
{
  if (wszInOut == nullptr)
    return;

  wchar_t* pwszInOut  = wszInOut;
  while ( *pwszInOut != L'\0' )
  {
    if (*pwszInOut == L'/')
        *pwszInOut = L'\\';

    pwszInOut =
      CharNextW (pwszInOut);
  }
}

std::string
__cdecl
SK_FormatString (char const* const _Format, ...)
{
  size_t len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( nullptr, 0, _Format, _ArgList ) + 1ui64;
  }
  va_end   (_ArgList);

  size_t alloc_size =
    sizeof (char) * (len + 2);

  std::unique_ptr <char []> pData =
    std::make_unique <char []> (alloc_size);

  if (! pData)
    return std::string ();

  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( pData.get (), len + 1, _Format, _ArgList );
  }
  va_end   (_ArgList);

  return
    pData.get ();
}

std::wstring
__cdecl
SK_FormatStringW (wchar_t const* const _Format, ...)
{
  size_t len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf ( nullptr, 0, _Format, _ArgList ) + 1ui64;
  }
  va_end   (_ArgList);

  size_t alloc_size =
    sizeof (wchar_t) * (len + 2);

  std::unique_ptr <wchar_t []> pData =
    std::make_unique <wchar_t []> (alloc_size);

  if (! pData)
    return std::wstring ();

  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf ( (wchar_t *)pData.get (), len + 1, _Format, _ArgList );
  }
  va_end   (_ArgList);

  return
    pData.get ();
}

size_t
SK_RemoveTrailingDecimalZeros (char* szNum, size_t bufLen)
{
  if (szNum == nullptr)
    return 0;

  // Remove trailing 0's after the .
  size_t len = bufLen == 0 ?
                  strlen (szNum) :
        std::min (strlen (szNum), bufLen);

  for (size_t i = (len - 1); i > 1; i--)
  {
    if (szNum [i] == '0' && szNum [i - 1] != '.')
      len--;

    if (szNum [i] != '0' && szNum [i] != '\0')
      break;
  }

  szNum [len] = '\0';

  return len;
}
