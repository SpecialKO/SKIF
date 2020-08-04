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

#pragma once

#include <string>

#define STEAM_API_NODLL
#include "steam/steam_api.h"

#include <Windows.h>
#include <debugapi.h>

class SKIF_UGC_Manager
{
public:
  CCallResult <SKIF_UGC_Manager, SteamUGCQueryCompleted_t> callResultQuery;

  void OnSteamUGCQueryCompleted (SteamUGCQueryCompleted_t *pParam, bool)
  {
    OutputDebugStringW (L"OnQueryCompleted <");
    OutputDebugStringW (std::to_wstring (pParam->m_handle).c_str ());
    OutputDebugStringW (L"> [");
    OutputDebugStringW (std::to_wstring (pParam->m_eResult).c_str ());
    OutputDebugStringW (L"] -- ");
    OutputDebugStringW (std::to_wstring (pParam->m_unTotalMatchingResults).c_str ());
    OutputDebugStringW (L"\n");

    if (pParam->m_eResult == k_EResultOK)
    {
      ugc_totals =
        pParam->m_unTotalMatchingResults;
    }

    pUGC->ReleaseQueryUGCRequest (pParam->m_handle);
 };

  SKIF_UGC_Manager (void) { };

  int  get_ugc_totals (void) { return ugc_totals; }

  void refresh_totals (AppId_t appid, ISteamUGC* pUGC_, ISteamRemoteStorage* pRemoteStorage = nullptr)
  {
    if (! pUGC_)
      return;

    pUGC       = pUGC_;
    ugc_totals = 0;

    //ISteamApps::GetInstalledDepots ()
    //ISteamApps::GetLaunchCommandLine ()
    //ISteamUser::TrackAppUsageEvent (ISteamApps::GetLaunchCommandLine ()

    if (pRemoteStorage != nullptr)
    {
      static int last_count = 0;

      if ( pRemoteStorage->IsCloudEnabledForAccount () )
      {
        pRemoteStorage->SetCloudEnabledForApp  (
           pRemoteStorage->IsCloudEnabledForApp ()
                                               );
      }
    }

    UGCQueryHandle_t total_query =
    //pUGC->CreateQueryUserUGCRequest (user_
    //  /*SteamUser ()->GetSteamID ().GetAccountID ()*/, k_EUserUGCList_Published, (EUGCMatchingUGCType)~0, k_EUserUGCListSortOrder_CreationOrderDesc, 0, appid, 1);
    pUGC->CreateQueryAllUGCRequest (k_EUGCQuery_RankedByPublicationDate, (EUGCMatchingUGCType)k_EUGCMatchingUGCType_Screenshots, 0, appid, 1);
    pUGC->SetReturnTotalOnly (total_query, true);

    callResultQuery.Set (
      pUGC->SendQueryUGCRequest (total_query),
        this,
          &SKIF_UGC_Manager::OnSteamUGCQueryCompleted
    );
  }

  uint32 user_   = 0;

protected:
  int ugc_totals = 0;

private:
  ISteamUGC* pUGC = nullptr;
};