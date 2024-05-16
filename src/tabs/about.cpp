#include <SKIF.h>
#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <utility/fsutil.h>

// Registry Settings
#include <utility/registry.h>
#include <utility/updater.h>
#include <tabs/common_ui.h>

void
SKIF_UI_Tab_DrawAbout (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  float maxWidth    =  0.56f * ImGui::GetContentRegionAvail().x; // Needs to be before the SKIF_ImGui_Columns() call
  bool enableColums = (ImGui::GetContentRegionAvail().x / SKIF_ImGui_GlobalDPIScale >= 750.f);

  auto _PrintSKIFintro = [](void)
  {
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "About the Injection Frontend (SKIF):"    );

    SKIF_ImGui_Spacing      ( );

    ImGui::TextWrapped      ("You are looking at the Special K Injection Frontend app, commonly shortened as 'SKIF'. "
                             "This app injects Special K into any games launched from it, or even into games that are already running!\n\n"
                             "The frontend also provides convenient alternative launch options, shortcuts to useful resources, and a few other helpful things.");
  };

  SKIF_ImGui_Spacing      ( );

  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));

  if (enableColums)
  {
    SKIF_ImGui_Columns    (2, "SKIF_COLUMN_ABOUT", true);
    ImGui::SetColumnWidth (0, maxWidth); // 560.0f * SKIF_ImGui_GlobalDPIScale
  }

  else {
    // This is needed to reproduce the same padding on the left side as when using columns
    ImGui::SetCursorPosX  (ImGui::GetCursorPosX ( ) + ImGui::GetStyle().FramePadding.x);
    ImGui::BeginGroup     ( );
  }

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
                            "SDR games, Nvidia Reflex in unsupported games, as well as texture modding "
                            "for players and modders alike. While not all features are supported in all games, most "
                            "DirectX 11 and 12 titles can make use of one or more of its features."
  );
  ImGui::NewLine          ( );

  if (! enableColums)
  {
    _PrintSKIFintro       ( );
    ImGui::NewLine        ( );
  }

  ImGui::Text             ("Just hop over to the");
  ImGui::SameLine         ( );
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
  if (SKIF_ImGui_Selectable (ICON_FA_GAMEPAD " Library###About-Lib1"))
    SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  ImGui::SameLine         ( );
  ImGui::Text             ("and launch a game!");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImColor::HSV (0.11F, 1.F, 1.F), ICON_FA_FACE_GRIN_BEAM);

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );
  if (enableColums) 
    ImGui::NewLine        ( );

  float fY1 = ImGui::GetCursorPosY();

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "Getting started with a game:");

  SKIF_ImGui_Spacing      ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "1 ");
  ImGui::SameLine         ( );
  ImGui::Text             ("Go to the");
  ImGui::SameLine         ( );
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
  if (SKIF_ImGui_Selectable (ICON_FA_GAMEPAD " Library###About-Lib2"))
    SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  ImGui::SameLine         ( );
  ImGui::Text             ("tab.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "2 ");
  ImGui::SameLine         ( );
  ImGui::TextWrapped      ("Select and launch the game.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "3 ");
  ImGui::SameLine         ( );
  ImGui::Text             ("If the game is missing, use");
  ImGui::SameLine         ( );
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
  if (SKIF_ImGui_Selectable (ICON_FA_SQUARE_PLUS " Add Game"))
  {
    AddGamePopup      = PopupState_Open;
    SKIF_Tab_ChangeTo = UITab_Library;
  }
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  ImGui::SameLine         ( );
  ImGui::Text             ("to add it.");

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );
  if (enableColums) 
    ImGui::NewLine        ( );

  float fY2 = ImGui::GetCursorPosY();
          
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_ROCKET);
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "Launch Special K for select games through Steam:"
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

    ImGui::TreePush         ("SteamSKIFCommand");
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    char szSteamCommand[MAX_PATH] = "SKIF %COMMAND%";
    ImGui::InputTextEx      ("###Launcher", NULL, szSteamCommand, MAX_PATH, ImVec2(0, 0), ImGuiInputTextFlags_ReadOnly);
    if (ImGui::IsItemActive    ( ))
    {
      extern bool allowShortcutCtrlA;
      allowShortcutCtrlA = false;
    }
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
        ICON_FA_TRIANGLE_EXCLAMATION " ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      ("Your system is not set up to use this install of Special K to launch injection through Steam.");

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
  ImGui::NewLine          ( );
  if (enableColums) 
    ImGui::NewLine        ( );

  float fY3 = ImGui::GetCursorPosY();
          
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_AWARD);//ICON_FA_WRENCH);
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    "Tips and Tricks:");

  SKIF_ImGui_Spacing      ( );

  ImGui::BeginChild ("##TipsTricks", ImVec2 (0, (135.0f * SKIF_ImGui_GlobalDPIScale) + ImGui::GetStyle().ScrollbarSize), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

  // Show a (currently not randomized) selection of tips and tricks
  SKIF_UI_TipsAndTricks   ( );

  ImGui::EndChild ( );

//float fY4 = ImGui::GetCursorPosY();

  if (enableColums)
  {
    float pushColumnSeparator =
      (900.0f * SKIF_ImGui_GlobalDPIScale) - ImGui::GetCursorPosY                () -
                                            (ImGui::GetTextLineHeightWithSpacing () );

    ImGui::ItemSize (
      ImVec2 (0.0f, pushColumnSeparator)
    );


    ImGui::NextColumn       ( ); // Next Column

    _PrintSKIFintro         ( );

    ImGui::SetCursorPosY    (fY1);
  }
  else {
    ImGui::NewLine          ( );
    ImGui::NewLine          ( );
  }

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
      ICON_FA_UP_RIGHT_FROM_SQUARE " "      );
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
    ICON_FA_UP_RIGHT_FROM_SQUARE " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("Local (game-specific)"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/SpecialK/Local");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/SpecialK/Local");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
      ICON_FA_UP_RIGHT_FROM_SQUARE " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("Do not use in multiplayer games!"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/en/SpecialK/Global#multiplayer-games");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/en/SpecialK/Global#multiplayer-games");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor::HSV (0.11F,   1.F, 1.F),
      ICON_FA_TRIANGLE_EXCLAMATION " ");
  ImGui::EndGroup         ( );

  SKIF_ImGui_SetHoverTip ("In particular games where anti-cheat\nprotection might be present.");
  
  if (enableColums)
  {
    ImGui::SetCursorPosY    (fY2);
  }
  else {
    ImGui::NewLine          ( );
    ImGui::NewLine          ( );
  }

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
  
  //if (SKIF_ImGui_MenuItemEx2 ("Wiki", ICON_FA_BOOK, ImColor(25, 118, 210)))
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
  ImGui::SameLine         (35.0f * SKIF_ImGui_GlobalDPIScale);

  if (ImGui::Selectable   ("Discord"))
    SKIF_Util_OpenURI     (L"https://discord.gg/specialk");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://discord.gg/specialk");
  ImGui::EndGroup         ( );


  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    (_registry._StyleLightMode) ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow) : ImVec4 (ImColor (247, 241, 169)),
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
    (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
      ICON_FA_GITHUB " "   );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   ("GitHub"))
    SKIF_Util_OpenURI     (L"https://github.com/SpecialKO");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://github.com/SpecialKO");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         (0.0f, 10.0f * SKIF_ImGui_GlobalDPIScale);
  //ImGui::SetCursorPosX    (ImGui::GetCursorPosX ( ) + 1.0f);
  ImGui::TextColored      (
    (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
      ICON_FA_FILE_CONTRACT " ");
  ImGui::SameLine         (0.0f, 10.0f);
  if (ImGui::Selectable   ("Privacy Policy"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/Privacy");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/Privacy");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         (0.0f, 10.0f * SKIF_ImGui_GlobalDPIScale);
  //ImGui::SetCursorPosX    (ImGui::GetCursorPosX ( ) + 1.0f);
  ImGui::TextColored      (
    (_registry._StyleLightMode) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
      ICON_FA_FILE_CONTRACT " ");
  ImGui::SameLine         (0.0f, 10.0f);
  if (ImGui::Selectable   ("Licenses"))
    SKIF_Util_OpenURI     (L"https://github.com/SpecialKO/SKIF/blob/master/LICENSE-3RD-PARTY");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://github.com/SpecialKO/SKIF/blob/master/LICENSE-3RD-PARTY");
  ImGui::EndGroup         ( );
  
  if (enableColums)
  {
    ImGui::SetCursorPosY    (fY3);
  }
  else {
    ImGui::NewLine          ( );
    ImGui::NewLine          ( );
  }
  
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

  if (enableColums)
  {
    ImGui::Columns        (1);
  }
  else {
    ImGui::NewLine        ( );
    ImGui::EndGroup       ( );
  }

  ImGui::PopStyleColor    ( );
}