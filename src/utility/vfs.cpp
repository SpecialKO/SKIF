#include <utility/vfs.h>

int
SK_CountWChars (const wchar_t *s, wchar_t c)
{
  return
    (  s == nullptr ||
      *s == L'\0' ) ? 0
                    :
  SK_CountWChars (s + 1, c) +
               ( *s  ==  c );
}
