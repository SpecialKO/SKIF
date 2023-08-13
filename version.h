//
// Copyright 2020-2022 Andon "Kaldaien" Coleman
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

#pragma once



#define SKIF_MAJOR 0
#define SKIF_MINOR 9
#define SKIF_BUILD 91
#define SKIF_REV_N 0
#define SKIF_REV   0


#define _A2(a)     #a
#define  _A(a)  _A2(a)
#define _L2(w)  L ## w
#define  _L(w) _L2(w)


#if (defined (SKIF_REV) && SKIF_REV_N > 0)
#define SKIF_VERSION_STR_A    _A(SKIF_MAJOR) "." _A(SKIF_MINOR) "." _A(SKIF_BUILD) "." _A(SKIF_REV)
#else
#define SKIF_VERSION_STR_A    _A(SKIF_MAJOR) "." _A(SKIF_MINOR) "." _A(SKIF_BUILD)
#endif

#define SKIF_VERSION_STR_W _L(SKIF_VERSION_STR_A)


#define SKIF_FILE_VERSION     SKIF_MAJOR,SKIF_MINOR,SKIF_BUILD,SKIF_REV_N
#define SKIF_PRODUCT_VERSION  SKIF_MAJOR,SKIF_MINOR,SKIF_BUILD,SKIF_REV_N


#define SKIF_WINDOW_TITLE_A          "Special K Injection Frontend"
#define SKIF_WINDOW_TITLE_W       _L("Special K Injection Frontend")
#define SKIF_WINDOW_TITLE             SKIF_WINDOW_TITLE_W
#define SKIF_WINDOW_TITLE_SHORT_A    "Special K"
#define SKIF_WINDOW_TITLE_SHORT_W _L("Special K")
#define SKIF_WINDOW_TITLE_SHORT       SKIF_WINDOW_TITLE_SHORT_W
#define SKIF_WINDOW_HASH          "###Special K Injection Frontend"

