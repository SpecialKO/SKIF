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
#include <vector>

#define STEAM_API_NODLL
#include <steam/steam_api.h>

#include <Windows.h>
#include <debugapi.h>

class SKIF_Achievement_Manager
{
public:
  struct achievement_s {
    std::string api_ref_name;
    std::string desc;
    std::string name;
    bool        unlocked;
    time_t      unlock_time;
  };

  STEAM_CALLBACK_MANUAL ( SKIF_Achievement_Manager,
                          OnUserStatsReceived,
                          UserStatsReceived_t,
                          stat_receipt_callback )
  {
    OutputDebugStringW (L"OnUserStatsReceived: ");
    OutputDebugStringW (std::to_wstring (pParam->m_nGameID).c_str ());
    OutputDebugStringW (L"\n");

    if ( pParam->m_eResult != k_EResultOK ||
         pParam->m_nGameID == 0 )
    {
      return;
    }

    uint32 NumAch =
      pUserStats->GetNumAchievements ();

    for ( uint32 i = 0 ; i < NumAch ; ++i )
    {
      achievement_s ach =
        { pUserStats->GetAchievementName (i), u8"", u8"",
            false, 0 };

      __time32_t unlock_time;

      if (
        pUserStats->GetAchievementAndUnlockTime (
          ach.api_ref_name.c_str (),
         &ach.unlocked, (uint32_t *)&unlock_time
        )
      )
      {
        // Extend from 32-bit epoch to portable time_t
        ach.unlock_time =
            unlock_time;

        ach.name =
          pUserStats->GetAchievementDisplayAttribute (
            ach.api_ref_name.c_str (), u8"name"
          );

        ach.desc =
          pUserStats->GetAchievementDisplayAttribute (
            ach.api_ref_name.c_str (), u8"desc"
          );

        achievements.push_back (ach);
      }
    }
  }

  SKIF_Achievement_Manager (void) //:
    //stat_receipt_callback (this, &SKIF_Achievement_Manager::OnUserStatsReceived)
      { };

  void refresh (ISteamUserStats* pUser)
  {
    if (pUser == nullptr)
      return;

    pUserStats = pUser;

    achievements.clear ();

    stat_receipt_callback.Register (this, &SKIF_Achievement_Manager::OnUserStatsReceived);

    pUserStats->RequestCurrentStats                 ();
    pUserStats->RequestGlobalAchievementPercentages ();
  }

//protected:
  std::vector <achievement_s> achievements;

private:
  ISteamUserStats* pUserStats = nullptr;
};