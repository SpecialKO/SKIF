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

//#include "ugc.h"
//
//void
//SKIF_UGC_Manager::refresh_totals (AppId_t appid, ISteamUGC* pUGC)
//{
//  ugc_totals = 0;
//
//  //m_UGCQueryHandle = SteamUGC.CreateQueryAllUGCRequest (EUGCQuery., EUGCMatchingUGCType., AppId_t.Invalid, SteamUtils.GetAppID (), 1);
//
//  UGCQueryHandle_t total_query =
//    pUGC->CreateQueryAllUGCRequest (k_EUGCQuery_RankedByPublicationDate, (EUGCMatchingUGCType)k_EUGCMatchingUGCType_Screenshots, appid, appid);
//    pUGC->SetReturnTotalOnly (total_query, true);
//
//  callResultQuery.Set (
//    pUGC->SendQueryUGCRequest (total_query),
//      this,
//        &SKIF_UGC_Manager::OnQueryCompleted
//  );
//}