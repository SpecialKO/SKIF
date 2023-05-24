#include <SKIF.h>
#include <font_awesome.h>
#include <sk_utility/utility.h>
#include <SKIF_utility.h>
#include <SKIF_imgui.h>
#include <fsutil.h>

// Registry Settings
#include <registry.h>
#include <updater.h>

static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance( );

void
SKIF_UI_Tab_DrawAbout (void)
{
  SKIF_ImGui_Spacing      ( );

  SKIF_ImGui_Columns      (2, nullptr, true);

  SK_RunOnce (
    ImGui::SetColumnWidth (0, 600.0f * SKIF_ImGui_GlobalDPIScale)
  );

  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  // ImColor::HSV (0.11F, 1.F, 1.F)   // Orange
  // ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info) // Blue Bullets
  // ImColor(100, 255, 218); // Teal
  // ImGui::GetStyleColorVec4(ImGuiCol_TabHovered);

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "Beginner's Guide to Special K (SK):"
                            );

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      ("Lovingly referred to as the Swiss Army Knife of PC gaming, Special K does a bit of everything. "
                            "It is best known for fixing and enhancing graphics, its many detailed performance analysis and correction mods, "
                            "and a constantly growing palette of tools that solve a wide variety of issues affecting PC games.");

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      ("Among its main features are a latency-free borderless window mode, HDR retrofit for "
                            "SDR games, Nvidia Reflex addition in unsupported games, as well as texture modding "
                            "for players and modders alike. While not all features are supported in all games, most "
                            "DirectX 11 and 12 titles can make use of one if not more of these features."
  );
  ImGui::NewLine          ( );
  ImGui::Text             ("To get started just hop on over to the");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), ICON_FA_GAMEPAD " Library");
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::SameLine         ( );
  ImGui::Text             ("and launch a game!");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImColor::HSV (0.11F, 1.F, 1.F), ICON_FA_SMILE_BEAM);

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );

  float fY1 = ImGui::GetCursorPosY();

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "Getting started with Epic, GOG, Steam, or Xbox games:");

  SKIF_ImGui_Spacing      ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "1 ");
  ImGui::SameLine         ( );
  ImGui::Text             ("Go to the");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), ICON_FA_GAMEPAD " Library");
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::SameLine         ( );
  ImGui::Text             ("tab.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "2 ");
  ImGui::SameLine         ( );
  ImGui::TextWrapped      ("Select and launch the game.");

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );

  float fY2 = ImGui::GetCursorPosY();

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Getting started with other games:"
  );

  SKIF_ImGui_Spacing      ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "1 ");
  ImGui::SameLine         ( );
  ImGui::Text             ("Go to the");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      ICON_FA_GAMEPAD " Library"
  );
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::SameLine         ( );
  ImGui::Text             ("tab.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "2 ");
  ImGui::SameLine         ( );
  ImGui::Text             ("Click on");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      ICON_FA_PLUS_SQUARE " Add Game"
  );
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    AddGamePopup = PopupState::Open;
    SKIF_Tab_ChangeTo = UITab_Library;
  }
  ImGui::SameLine         ( );
  ImGui::Text             ("to add the game to the list.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "3 ");
  ImGui::SameLine         ( );
  ImGui::TextWrapped      ("Launch the game.");

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );

  float fY3 = ImGui::GetCursorPosY();
          
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_ROCKET);
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Quick launch Special K for select games through Steam:"
  );

  SKIF_ImGui_Spacing      ( );

  extern int
      SKIF_Util_RegisterApp (bool force = false);
  if (SKIF_Util_RegisterApp ( ) > 0)
  {
    ImGui::TextWrapped      ("Your system is set up to quickly launch injection through Steam.");

    SKIF_ImGui_Spacing      ( );

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          "1 ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      ("Right click the desired game in Steam, and select \"Properties...\".");
    ImGui::EndGroup         ( );

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          "2 ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      ("Copy and paste the below into the \"Launch Options\" field.");
    ImGui::EndGroup         ( );

    ImGui::TreePush         ("");
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    char szSteamCommand[MAX_PATH] = "SKIF %COMMAND%";
    ImGui::InputTextEx      ("###Launcher", NULL, szSteamCommand, MAX_PATH, ImVec2(0, 0), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor    ( );
    ImGui::TreePop          ( );

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          "3 ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      ("Launch the game as usual through Steam.");
    ImGui::EndGroup         ( );
  }

  else {
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImColor::HSV (0.11F,   1.F, 1.F),
      ICON_FA_EXCLAMATION_TRIANGLE " ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      ("Your system is not set up to use this install of Special K to quickly launch injection through Steam.");

    SKIF_ImGui_Spacing      ( );
    
    SKIF_ImGui_Spacing      (1.0f);
    ImGui::SameLine         ( );
    
    ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    if (ImGui::Button       ("  Set this install as default  "))
      SKIF_Util_RegisterApp (true);
    ImGui::PopStyleColor    ( );
    
    // We need som additional spacing at the bottom here to push down the Components section in the right column
    SKIF_ImGui_Spacing      (2.00f);
  }

  ImGui::NewLine          ( );

  static SKIF_Updater& _updater = SKIF_Updater::GetInstance ( );

  if ((_updater.GetState ( ) & UpdateFlags_Available) != UpdateFlags_Available)
    ImGui::NewLine          ( ); // Omitted to allow the "view release notes..." link to appear without pushing down the Update button

  float fY4 = ImGui::GetCursorPosY();
          
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_AWARD);//ICON_FA_WRENCH);
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    "Tips and Tricks:");

  SKIF_ImGui_Spacing      ( );

  if (SKIF_Util_IsHDRSupported())
  {
    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          (const char *)u8"\u2022 ");
    ImGui::SameLine         ( );
    ImGui::Text             ("Use");
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      ICON_FA_WINDOWS " + Ctrl + Shift + H");
    ImGui::SameLine         ( );
    ImGui::Text             ("to toggle HDR for the display the");
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        ICON_FA_MOUSE_POINTER);
    ImGui::SameLine         ( );
    ImGui::Text             ("is at.");
    ImGui::EndGroup         ( );

    ImGui::Spacing          ( );
    ImGui::Spacing          ( );
  }

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  float fX1 = ImGui::GetCursorPosX();
  ImGui::Text             ("Hold down");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    ICON_FA_KEYBOARD " Ctrl + Shift");
  ImGui::SameLine         ( );
  ImGui::Text             ("when starting a game to access compatibility options");
  ImGui::SetCursorPosX    (fX1);
  ImGui::Text             ("or quickly perform a local install of the appropriate wrapper DLL for the game.");
  ImGui::EndGroup         ( );


  float pushColumnSeparator =
    (900.0f * SKIF_ImGui_GlobalDPIScale) - ImGui::GetCursorPosY                () -
                                          (ImGui::GetTextLineHeightWithSpacing () );

  ImGui::ItemSize (
    ImVec2 (0.0f, pushColumnSeparator)
  );


  ImGui::NextColumn       ( ); // Next Column
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "About the Injection Frontend (SKIF):"    );

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      ("You are looking at the Special K Injection Frontend, commonly referred to as \"SKIF\".\n\n"
                           "The injection frontend is used to manage the global injection service which injects Special K into games as they start, and even games that are already running!\n\n"
                           "The frontend also provides convenient shortcuts to special locations, including config and log files, cloud saves, and external resources like PCGamingWiki and SteamDB.");

  ImGui::SetCursorPosY    (fY1);

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                  "Multiplayer games:");

  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor::HSV (0.11F,   1.F, 1.F),
    ICON_FA_EXCLAMATION_TRIANGLE " ");
  ImGui::SameLine         (0.0f, 6.0f);
  ImGui::Text             ("Do not use Special K in multiplayer games!");
  ImGui::EndGroup         ( );

  SKIF_ImGui_SetHoverTip (
    "In particular games where anti-cheat\nprotection might be present."
  );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
      ICON_FA_EXTERNAL_LINK_ALT " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("More on the wiki"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/en/SpecialK/Global#multiplayer-games");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/en/SpecialK/Global#multiplayer-games");
  ImGui::EndGroup         ( );

  ImGui::SetCursorPosY    (fY2);

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "More on how to inject Special K:"
  );

  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
      ICON_FA_EXTERNAL_LINK_ALT " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("Global (system-wide)"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/SpecialK/Global");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/SpecialK/Global");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
    ICON_FA_EXTERNAL_LINK_ALT " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("Local (game-specific)"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/SpecialK/Local");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/SpecialK/Local");
  ImGui::EndGroup         ( );

  ImGui::SetCursorPosY    (fY3);

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Online resources:"   );
  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor (25, 118, 210),
      ICON_FA_BOOK " "   );
  ImGui::SameLine         ( );

  if (ImGui::Selectable   ("Wiki"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/");
  ImGui::EndGroup         ( );


  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor (114, 137, 218),
      ICON_FA_DISCORD " "   );
  ImGui::SameLine         ( );

  if (ImGui::Selectable   ("Discord"))
    SKIF_Util_OpenURI     (L"https://discord.gg/specialk");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://discord.gg/specialk");
  ImGui::EndGroup         ( );


  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    (_registry.iStyle == 2) ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow) : ImVec4 (ImColor (247, 241, 169)),
      ICON_FA_DISCOURSE " " );
  ImGui::SameLine         ( );

  if (ImGui::Selectable   ("Forum"))
    SKIF_Util_OpenURI     (L"https://discourse.differentk.fyi/");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://discourse.differentk.fyi/");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor (249, 104, 84),
      ICON_FA_PATREON " "   );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("Patreon"))
    SKIF_Util_OpenURI     (L"https://www.patreon.com/Kaldaien");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://www.patreon.com/Kaldaien");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
      ICON_FA_GITHUB " "   );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("GitHub"))
    SKIF_Util_OpenURI     (L"https://github.com/SpecialKO");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://github.com/SpecialKO");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         (0.0f, 10.0f);
  //ImGui::SetCursorPosX    (ImGui::GetCursorPosX ( ) + 1.0f);
  ImGui::TextColored      (
    (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
      ICON_FA_FILE_CONTRACT " ");
  ImGui::SameLine         (0.0f, 10.0f);
  if (ImGui::Selectable   ("Privacy Policy"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/Privacy");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/Privacy");
  ImGui::EndGroup         ( );

  ImGui::SetCursorPosY    (fY4);
    
  ImGui::PushStyleColor   (
    ImGuiCol_SKIF_TextCaption, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                            );
    
  ImGui::PushStyleColor   (
    ImGuiCol_CheckMark, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption)
                            );

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Components:"
  );
    
  ImGui::PushStyleColor   (
    ImGuiCol_SKIF_TextBase, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                            );
    
  ImGui::PushStyleColor   (
    ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                            );

  SKIF_ImGui_Spacing      ( );
  
  SKIF_UI_DrawComponentVersion ( );

  ImGui::PopStyleColor    (4);

  ImGui::Columns          (1);

  ImGui::PopStyleColor    ( );
}