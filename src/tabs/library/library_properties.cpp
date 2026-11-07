//
// Copyright 2020 - 2026  Andon "Kaldaien" Coleman and Aemony
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

// Properties dialog of a game (Manage > Properties): see tabs/properties.h

#include "library_ui.h"
#include <tabs/properties.h>
#include <utility/hotkeys.h>
#include <utility/env_vars.h>
#include <utility/file_detection.h>

#include <algorithm>
#include <cctype>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

#pragma region Helpers

// The label column is at least 150 px wide (scaled), and as wide as the widest label drawn (translations may be longer).
//   A label wider than the column widens it from the next frame on, which is requested right away. The width is kept
//     without the DPI scale, as each Properties window may be on a monitor of its own.
static float    s_PropertiesLabelWidth      = 0.0f; // Unscaled
static uint32_t s_PropertiesLabelGeneration = 0;    // The language the width was measured in

float
SKIF_Properties_GetLabelWidth (void)
{
  const uint32_t generation = SKIF_L10n::GetInstance ( ).GetGeneration ( );

  if (generation != s_PropertiesLabelGeneration)
  {
    s_PropertiesLabelWidth      = 0.0f;
    s_PropertiesLabelGeneration = generation;
  }

  return std::max (150.0f, s_PropertiesLabelWidth) * SKIF_ImGui_GlobalDPIScale;
}

void
SKIF_Properties_Label (const char* label, const char* tooltip)
{
  const float x = ImGui::GetCursorPosX ( );

  // Widen the column if this label needs it (applied from the next frame on, so ask for one)
  const float needed = ImGui::CalcTextSize (label).x + ImGui::GetStyle ( ).ItemSpacing.x * 2.0f;

  if (needed > SKIF_Properties_GetLabelWidth ( ) + 0.5f)
  {
    s_PropertiesLabelWidth = needed / SKIF_ImGui_GlobalDPIScale;

    extern int addAdditionalFrames;
    addAdditionalFrames += 1;
  }

  ImGui::AlignTextToFramePadding ( );
  ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption), "%s", label);

  if (tooltip != nullptr)
    SKIF_ImGui_SetHoverTip (tooltip);

  ImGui::SameLine (x + SKIF_Properties_GetLabelWidth ( ));
}

bool
SKIF_Properties_BrowseForFile (std::wstring& out, const wchar_t* folder)
{
  LPWSTR  pwszFilePath = NULL;
  HRESULT hr           =
    SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, SKIF_ImGui_GetFocusedWindow ( ), {{L"Executables", L"*.exe;*.bat;*.lnk"}}, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST, FOLDERID_ComputerFolder, folder);

  bool ret = (SUCCEEDED (hr) && pwszFilePath != NULL);

  if (ret)
    out = pwszFilePath;

  if (pwszFilePath != NULL)
    CoTaskMemFree (pwszFilePath);

  return ret;
}

bool
SKIF_Properties_BrowseForFolder (std::wstring& out, const wchar_t* folder)
{
  LPWSTR  pwszFolder = NULL;
  HRESULT hr         =
    SKIF_Util_FileExplorer_BrowseForFolder (&pwszFolder, SKIF_ImGui_GetFocusedWindow ( ), FOLDERID_ComputerFolder, folder);

  bool ret = (SUCCEEDED (hr) && pwszFolder != NULL);

  if (ret)
    out = pwszFolder;

  if (pwszFolder != NULL)
    CoTaskMemFree (pwszFolder);

  return ret;
}

// InputText on a std::string (imgui_stdlib is not part of the project)
static bool
_InputString (const char* id, std::string& value, const char* hint = nullptr, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None)
{
  static char buffer [1024 + 2] = { };

  strncpy_s (buffer, value.c_str ( ), _TRUNCATE);

  ImGui::SetNextItemWidth (-FLT_MIN);

  bool changed = (hint != nullptr)
    ? ImGui::InputTextWithHint (id, hint, buffer, 1024, flags)
    : ImGui::InputText         (id,       buffer, 1024, flags);

  SKIF_ImGui_BlockDragMoveWhenItemActive ( );

  if (changed)
    value = buffer;

  return changed;
}

// A read-only field showing a value that can still be selected and copied
static void
_ReadOnlyString (const char* id, const std::string& value, const char* hint = "")
{
  static char buffer [MAX_PATH * 2 + 2] = { };

  strncpy_s (buffer, value.c_str ( ), _TRUNCATE);

  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
  ImGui::SetNextItemWidth (-FLT_MIN);
  ImGui::InputTextWithHint (id, hint, buffer, sizeof (buffer) - 2, ImGuiInputTextFlags_ReadOnly);
  SKIF_ImGui_BlockDragMoveWhenItemActive ( );
  ImGui::PopStyleColor    ( );
}

static std::string
_ToForwardSlashes (std::string path)
{
  std::replace (path.begin ( ), path.end ( ), '\\', '/');
  return path;
}

static std::wstring
_ToBackslashes (std::wstring path)
{
  std::replace (path.begin ( ), path.end ( ), L'/', L'\\');
  return path;
}

static bool
_SamePath (const std::wstring& a, const std::wstring& b)
{
  std::wstring x = _ToBackslashes (std::filesystem::path (a).lexically_normal ( ).wstring ( )),
               y = _ToBackslashes (std::filesystem::path (b).lexically_normal ( ).wstring ( ));

  while (! x.empty ( ) && x.back ( ) == L'\\') x.pop_back ( );
  while (! y.empty ( ) && y.back ( ) == L'\\') y.pop_back ( );

  return (_wcsicmp (x.c_str ( ), y.c_str ( )) == 0);
}

#pragma endregion


#pragma region General

// Name, Favorited, Hidden, Category
class PropertiesSection_General : public IPropertySection
{
public:
  const char*    GetTitle (void) const override { return ""; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::General; }
  int            GetOrder (void) const override { return 10; }

  void Load (app_record_s& app) override
  {
    strncpy_s (name, app.skif.name.c_str ( ), _TRUNCATE);
    nameOrig      = app.skif.name;
    hint          = (! app.names.original.empty ( )) ? app.names.original : app.names.clean;

    favorite      = favoriteOrig = app.IsFavorite ( );
    clientFavorite = (app.skif.pinned == -1 && app.GetClientFavorite ( ) == 1);
    clientName     = app.store_utf8;
    hidden        = hiddenOrig   = app.IsHidden   ( );
    category      = categoryOrig = app.skif.category;
    newCategory [0] = '\0';
  }

  void Draw (app_record_s& app) override
  {
    static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

    UNREFERENCED_PARAMETER (app);

    // Name
    SKIF_Properties_Label   (_("Name"), _("Leave empty to use the name of the game."));
    ImGui::SetNextItemWidth (-FLT_MIN);
    ImGui::InputTextWithHint ("###PropsName", hint.c_str ( ), name, MAX_PATH);
    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    // Library
    SKIF_Properties_Label   (_("Library"));
    SKIF_ImGui_Checkbox         (L_("Favorited###PropsFavorite"), &favorite);

    // A favorite only because the game is one in the store client (Steam), until it is changed here
    if (clientFavorite)
      SKIF_ImGui_SetHoverTip (SKIF_Lib_GetClientFavoriteTip (clientName));
    ImGui::SameLine         ( );
    ImGui::Spacing          ( );
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Hidden###PropsHidden")));
    SKIF_ImGui_Checkbox         (L_("Hidden###PropsHidden"),      &hidden);

    // Category (also in the game's Manage > Category menu)
    const bool categoryDisabled = favorite;

    SKIF_Properties_Label   (_("Category"), _("Favorited games are always shown under Favorites."));

    SKIF_ImGui_PushDisableState (categoryDisabled);

    const float fComboWidth = (ImGui::GetContentRegionAvail ( ).x - ImGui::GetStyle ( ).ItemSpacing.x) * 0.5f;

    ImGui::SetNextItemWidth (fComboWidth);
    // TRANSLATORS: The default category of the library
    if (ImGui::BeginCombo   ("###PropsCategory", (category.empty ( )) ? C_("category", "Games") : category.c_str ( )))
    {
      if (ImGui::Selectable (CL_("category", "Games###PropsCategoryNone"), category.empty ( )))
        category.clear ( );

      int index = 0;

      for (auto& entry : _registry.vecCategories)
      {
        // The default categories
        if (entry.name == "Favorites" || entry.name == "Games")
          continue;

        if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("%s###PropsCategory-%d", entry.name.c_str ( ), index++), category == entry.name))
          category = entry.name;
      }

      // A new category typed below, which is only created on Save
      if (! category.empty ( ) && ! SKIF_Lib_CategoryExists (category))
        // TRANSLATORS: {0} is the name of a category that does not exist yet
        ImGui::Selectable ((F_("{0} (new)", category) + "###PropsCategoryNew").c_str ( ), true);

      ImGui::EndCombo ( );
    }

    ImGui::SameLine         ( );
    ImGui::SetNextItemWidth (-FLT_MIN);

    if (ImGui::InputTextWithHint ("###PropsNewCategory", _("New category..."), newCategory, maxCategoryNameLen, ImGuiInputTextFlags_EnterReturnsTrue))
    {
      std::string szName = newCategory;
      newCategory [0]    = '\0';

      // Same as the Category menu: the default categories map to the favorite state
      if (szName == "Favorites")
        favorite = true;

      else if (szName == "Games")
        category.clear ( );

      else if (! szName.empty ( ))
        category = szName;
    }

    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    SKIF_ImGui_PopDisableState (categoryDisabled);
  }

  bool IsDirty (void) const override
  {
    return (std::string (name) != nameOrig     ||
            favorite           != favoriteOrig ||
            hidden             != hiddenOrig   ||
            category           != categoryOrig);
  }

  uint32_t Apply (app_record_s& app) override
  {
    uint32_t ret = PropertiesApply_None;

    // A name that is the same as the game's own is not a custom name
    std::string newName = name;

    if (newName == app.names.original || newName == app.names.clean)
      newName.clear ( );

    if (newName != app.skif.name)
    {
      app.skif.name = newName;
      ret |= PropertiesApply_Repopulate | PropertiesApply_WriteMetaDB;
    }

    if (hidden != hiddenOrig)
    {
      app.skif.hidden = (hidden) ? 1 : 0;
      ret |= PropertiesApply_Repopulate | PropertiesApply_WriteMetaDB;
    }

    if (favorite != favoriteOrig)
    {
      app.skif.pinned = (favorite) ? 1 : 0;
      ret |= PropertiesApply_Resort | PropertiesApply_WriteMetaDB;
    }

    if (category != categoryOrig)
    {
      if (! category.empty ( ))
        SKIF_Lib_AddCategory (category); // Does nothing if it exists already

      app.skif.category = category;
      ret |= PropertiesApply_Resort | PropertiesApply_WriteMetaDB;
    }

    return ret;
  }

private:
  char        name        [MAX_PATH + 2]      = { };
  char        newCategory [maxCategoryNameLen] = { };
  std::string nameOrig;
  std::string hint;
  bool        favorite     = false,
              favoriteOrig = false;
  bool        clientFavorite = false; // Favorited in the store client, and not set in Special K
  std::string clientName;             // The store client
  bool        hidden       = false,
              hiddenOrig   = false;
  std::string category,
              categoryOrig;
};

#pragma endregion


#pragma region Launch behavior

class PropertiesSection_LaunchOptions;

// How the game is launched (General page, below the library settings):
//   Play:         the launch option the Play button uses (the default one, kept by PropertiesSection_LaunchOptions), and
//                   how it starts: the game's Instant Play setting (db.json). Only the platform's primary launch option can
//                     be started by the platform client, so the setting only matters for that one; all others start directly
//   Auto-stop:    the injection service's behavior, stored for the install folder in the registry (SKIF_LaunchBehaviors)
class PropertiesSection_LaunchBehavior : public IPropertySection
{
public:
  const char*    GetTitle (void) const override { return ""; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::General; }
  int            GetOrder (void) const override { return 20; }

  // The launch options (Launch page), which hold the default launch option the Play setting picks
  void           SetLaunchOptions (PropertiesSection_LaunchOptions* options) { launchOptions = options; }

  void Load (app_record_s& app) override
  {
    instantPlayCompatible = app.isInstantPlayCompatible ( );
    instantPlayGlobal     = app.GetInstantPlayDefault   ( );
    instantPlay           = instantPlayOrig = app.skif.instant_play;
    storeName             = app.store_utf8;

    folder = app.install_dir;
    value  = valueOrig = 0;

    if (folder.empty ( ))
      return;

    SKIF_LaunchBehaviors::Resolved exact;
    SKIF_LaunchBehaviors::ReadExact (folder, exact);

    value     = valueOrig = static_cast <int> (exact.autoStop);
    inherited = SKIF_LaunchBehaviors::ResolveParent (folder).autoStop;
  }

  void Draw (app_record_s& app) override
  {
    UNREFERENCED_PARAMETER (app);

    ImGui::Separator   ( );
    SKIF_ImGui_Spacing ( );

    // Play: the launch option and how it starts (defined below PropertiesSection_LaunchOptions)
    if (launchOptions != nullptr)
      _DrawPlay ( );

    // Auto-stop
    SKIF_Properties_Label  (_("Auto-stop"), _("This determines how long the service will remain running when launching the game.\n"
                                         "Move the mouse over each option to get more information."));

    if (folder.empty ( ))
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Not available for this game."));
    else
      _DrawAutoStop ( );
  }

  bool IsDirty (void) const override
  {
    return (value != valueOrig || instantPlay != instantPlayOrig);
  }

  uint32_t Apply (app_record_s& app) override
  {
    uint32_t ret = PropertiesApply_None;

    if (instantPlay != app.skif.instant_play)
    {
      app.skif.instant_play = instantPlay;
      ret |= PropertiesApply_WriteMetaDB;
    }

    // Also what the launcher mode uses (SKIF %COMMAND%)
    if (value != valueOrig && ! folder.empty ( ) && ! SKIF_LaunchBehaviors::SetAutoStop (folder, static_cast <SKIF_LaunchBehaviors::AutoStop> (value)))
      PLOG_ERROR << "Could not save the auto-stop behavior for " << folder;

    return ret;
  }

private:
  bool                           instantPlayCompatible = false;
  bool                           instantPlayGlobal     = false; // The store's global Instant Play setting (Settings tab)
  int                            instantPlay           = 0,
                                 instantPlayOrig       = 0;
  std::string                    storeName;
  PropertiesSection_LaunchOptions*
                                 launchOptions         = nullptr; // Owned by the window, as this section

  void _DrawPlay (void);

  void _DrawAutoStop (void)
  {
    static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

    using AutoStop = SKIF_LaunchBehaviors::AutoStop;

    // What Default resolves to: a parent folder's setting, else the Settings tab
    std::string defaultLabel =
      (inherited == AutoStop::OnInjection) ? _("Default (Inject, parent folder)") :
      (inherited == AutoStop::OnGameExit)  ? _("Default (Exit, parent folder)")   :
      (inherited == AutoStop::Never)       ? _("Default (Never, parent folder)")  :
      (_registry.iAutoStopBehavior == 1)   ? _("Default (Inject)")                : _("Default (Exit)");

    SKIF_ImGui_RadioButton ((defaultLabel + "###PropsAutoStopDefault").c_str ( ), &value, 0);
    SKIF_ImGui_SetHoverTip ((inherited != AutoStop::Inherit) ? _("The service will use the behavior set for a parent folder of the game.")
                                                             : _("The service will use the default behavior configured in the Settings tab."));
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("On inject###PropsAutoStopInject")));
    SKIF_ImGui_RadioButton     (L_("On inject###PropsAutoStopInject"), &value, 1);
    SKIF_ImGui_SetHoverTip (_("The service will be stopped when Special K\nsuccessfully injects into a game."));
    SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("On exit###PropsAutoStopExit")));
    SKIF_ImGui_RadioButton     (L_("On exit###PropsAutoStopExit"),     &value, 2);
    SKIF_ImGui_SetHoverTip (_("The service will be stopped when Special K\ndetects that the game is being closed."));
    ImGui::SameLine        ( );
    ImGui::BeginGroup      ( );
    SKIF_ImGui_RadioButton     (L_("Never###PropsAutoStopNever"),      &value, 3);
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
    ImGui::EndGroup        ( );
    SKIF_ImGui_SetHoverTip (_("Warning: The service will remain even\nafter the game has been closed."));
  }

  std::wstring                   folder;
  int                            value     = 0,
                                 valueOrig = 0;
  SKIF_LaunchBehaviors::AutoStop inherited = SKIF_LaunchBehaviors::AutoStop::Inherit;
};

#pragma endregion


#pragma region Technology

// What the game is made with, from the names of its files (utility/file_detection.h): engine, anti-cheat, SDKs, ...
//   Read only, in a group of its own at the bottom. The folder is scanned in the background the first time it is
//     shown, and the result is cached.
class PropertiesSection_Detection : public IPropertySection
{
public:
  const char*    GetTitle (void) const override { return ""; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::Technology; }
  int            GetOrder (void) const override { return 10; }

  void Load (app_record_s& app) override
  {
    folder = app.install_dir;
  }

  void Draw (app_record_s& app) override
  {
    using namespace SKIF_FileDetection;

    UNREFERENCED_PARAMETER (app);

    const ImVec4 colDisabled = ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled);

    if (folder.empty ( ))
    {
      SKIF_Properties_Label         (_("Detected"));
      SKIF_ImGui_TextColoredWrapped (colDisabled, _("Not available for this game."));
      return;
    }

    const result_ptr result = Get (folder);

    switch (result->state)
    {
      case result_s::State::None:
      case result_s::State::Pending:
        SKIF_Properties_Label         (_("Detected"));
        SKIF_ImGui_TextColoredWrapped (colDisabled, _("Scanning the game's files..."));
        break;

      case result_s::State::Failed:
        SKIF_Properties_Label         (_("Detected"));
        SKIF_ImGui_TextColoredWrapped (colDisabled, _("The game's folder could not be read."));
        break;

      case result_s::State::NoRules:
        SKIF_Properties_Label         (_("Detected"));
        SKIF_ImGui_TextColoredWrapped (colDisabled, _("The detection rules could not be downloaded."));
        break;

      case result_s::State::Done:
      {
        if (result->detections.empty ( ))
        {
          SKIF_Properties_Label         (_("Detected"));
          SKIF_ImGui_TextColoredWrapped (colDisabled, _("Nothing was detected."));
        }

        // Most useful first, one row each
        static constexpr Category order [] = {
          Category::Engine, Category::AntiCheat, Category::Launcher, Category::Container, Category::Emulator, Category::SDK, Category::Other
        };

        for (Category category : order)
        {
          const auto detections = result->Get (category);

          if (detections.empty ( ))
            continue;

          // Each detection with the first file that matched (or how it was found)
          std::string tooltip;

          for (const detection_s* detection : detections)
          {
            if (! tooltip.empty ( ))
              tooltip += "\n";

            tooltip += GetDisplayName (detection->name) + "\n  ";
            tooltip += (detection->deduced) ? _("Deduced from the kinds of files the game has") : detection->example.c_str ( );
          }

          SKIF_Properties_Label  (GetCategoryLabel (category));
          ImGui::TextWrapped     ("%s", result->GetNames (category).c_str ( ));
          SKIF_ImGui_SetHoverTip (tooltip);
        }

        if (result->truncated)
        {
          SKIF_Properties_Label         ("");
          // TRANSLATORS: {0} is a number of files and folders
          SKIF_ImGui_TextColoredWrapped (colDisabled, F_("Only the first {0} files and folders were checked.", kMaxEntries).c_str ( ));
        }

        break;
      }
    }

    // Also for a cached result, as the cache only notices changes to the top level of the game's folder
    if (result->state != result_s::State::Pending)
    {
      SKIF_Properties_Label ("");

      if (ImGui::Button (L_("Scan again###PropsDetectionRescan")))
        Rescan (folder);

      SKIF_ImGui_SetHoverTip (_("Checks the game's files again, for instance after it was updated."), false, true);

      ImGui::SameLine ( );
      SKIF_ImGui_TextColoredWrapped (colDisabled, _("Educated guesses from the names of the game's files (SteamDB's file detection rules)."));
    }
  }

  bool     IsDirty (void) const override { return false; }
  uint32_t Apply   (app_record_s& app) override { UNREFERENCED_PARAMETER (app); return PropertiesApply_None; }

private:
  std::wstring folder;
};

#pragma endregion


#pragma region Launch options

// The launch options of the game (platform, lc.json and lc_user.json), and the default one (picked in General > Play)
class PropertiesSection_LaunchOptions : public IPropertySection
{
public:
  // For the Play setting (PropertiesSection_LaunchBehavior): the launch option the Play button uses (the default one,
  //   else the primary one), picked from a combo
  void DrawPlayCombo (void)
  {
    using LaunchSource = app_record_s::LaunchSource;

    if (rows.empty ( ))
    {
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("This game has no launch options."));
      return;
    }

    // As in the list: Special K disabled / elevated service for the executable
    auto _ServiceIcon = [&](const row_s& row) -> const char*
    {
      const int  svcIdx   = _FindService (_RowFullExe (row));
      const bool blocked  = (svcIdx != -1 && (services [svcIdx].blocked || services [svcIdx].blockedPattern));
      const bool elevated = (svcIdx != -1 && ! blocked && services [svcIdx].elevated);

      return (blocked) ? (ICON_FA_LOCK "  ") : (elevated) ? (ICON_FA_USER_SHIELD "  ") : "";
    };

    const int   effective = _PlayRow ( );
    std::string preview   = (effective != -1) ? _RowLabel (rows [effective]) : "";

    if (preview.empty ( ))
      preview = _("<no description>");

    if (effective != -1)
      preview = _ServiceIcon (rows [effective]) + preview;

    ImGui::SetNextItemWidth (-FLT_MIN);

    if (ImGui::BeginCombo ("###PropsPlayOption", preview.c_str ( )))
    {
      LaunchSource lastSource = rows.front ( ).source;

      for (int i = 0; i < static_cast <int> (rows.size ( )); i++)
      {
        const row_s& row = rows [i];

        // Separators between the platform, SKIF and user launch options (as in the list)
        if (row.source != lastSource)
        {
          lastSource = row.source;
          ImGui::Separator ( );
        }

        std::string label = _RowLabel (row);

        if (label.empty ( ))
          label = _("<no description>");

        // TRANSLATORS: {0} is the name of the platform's own launch option, which Play uses when no other is picked
        if (i == primaryRow)
          label = F_("{0} (primary)", label);

        label = _ServiceIcon (row) + label;

        // The library can't use it (see IsIdentical ( ))
        const bool identical = row.IsIdentical ( );

        SKIF_ImGui_PushDisableState (identical);
        if (ImGui::Selectable ((label + "###PropsPlayOption-" + std::to_string (i)).c_str ( ), i == effective) && ! identical)
        {
          // The primary one is used when none is set
          defaultRow  = (i == primaryRow) ? -1 : i;
          keepMissing = false;
        }
        SKIF_ImGui_PopDisableState  (identical);

        if (identical)
          SKIF_ImGui_SetHoverTip (_("This launch option is identical to another one, so it can't be the default until it is changed."), false, true);
      }

      ImGui::EndCombo ( );
    }

    // The default launch option is gone (removed, or hidden by its Steam branch / DLC)
    if (defaultMissing && keepMissing && defaultRow == -1)
    {
      ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine    ( );
      // TRANSLATORS: {0} is the name of a launch option
      ImGui::TextWrapped ("%s", F_("The default launch option ({0}) is not available, so the primary one is used.", (defaultOrig.desc.empty ( )) ? defaultOrig.exe : defaultOrig.desc).c_str ( ));
      ImGui::SameLine    ( );
      if (ImGui::SmallButton (L_("Clear###PropsLaunchClearMissing")))
        keepMissing = false;
    }
  }

  // Opened for a page (the launch bar's icons): select the launch option the Play button uses, whatever was selected before
  void ShowDefault (void) override
  {
    if (const int row = _PlayRow ( ); row != -1)
    {
      selected = row;
      pickError.clear ( );
    }
  }

  // Whether the platform client can start the launch option Play uses (only the primary one)
  bool PlayClientCanLaunch (void) const { const int row = _PlayRow ( ); return (row != -1 && rows [row].clientCanLaunch); }

  const char*    GetTitle (void) const override { return ""; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::Launch; }
  int            GetOrder (void) const override { return 10; }

  void Load (app_record_s& app) override
  {
    using LaunchSource = app_record_s::LaunchSource;

    // The launch config selected before (when loaded again), to select it again below
    app_record_s::launch_ref_s selectedRef;

    if (loadedOnce && selected >= 0 && selected < static_cast <int> (rows.size ( )))
      selectedRef = _RowRef (rows [selected]);

    rows.clear ( );

    installDir              = app.install_dir;
    clientOptions           = app.GetClientLaunchOptions ( );
    note.clear              ( );
    pickError.clear         ( );

    primaryRow              = -1;
    const int primaryId     = (! app.launch_configs.empty ( )) ? app.launch_configs.begin ( )->first : -1;

    // The game's own launch configs first, then the ones from lc.json
    for (LaunchSource source : { LaunchSource::Platform, LaunchSource::SKIF })
    {
      for (auto& it : app.launch_configs)
      {
        app_record_s::launch_config_s& lc = it.second;

        if (app_record_s::GetLaunchSource (lc) != source || ! app.IsLaunchConfigAvailable (lc))
          continue;

        row_s row;
        row.source          = source;
        row.configId        = lc.id;
        row.label           = (! lc.description.empty ( )) ? SK_WideCharToUTF8 (lc.description) : lc.getExecutableFileNameUTF8 ( );
        row.fullExe         = SK_WideCharToUTF8 (lc.getExecutableFullPath ( ));
        row.args            = SK_WideCharToUTF8 (lc.getLaunchOptions      ( ));
        row.workDir         = SK_WideCharToUTF8 (lc.getWorkingDirectory   ( ));
        row.env             = lc.env;
        row.clientCanLaunch = app.ClientCanLaunch (lc);
        row.ref             = app.MakeLaunchRef   (lc);

        // A copy in lc_user.json needs an executable inside the install folder
        if (! lc.isExecutableFileNameValid ( ))
          row.duplicateBlocker = _("This launch option has no executable that can be started directly.");

        else if (app_record_s::GetPathRelativeTo (installDir, lc.getExecutableFullPath ( )).empty ( ))
          row.duplicateBlocker = _("The executable of this launch option is outside of the game's install folder.");

        if (lc.id == primaryId && source == LaunchSource::Platform)
          primaryRow = static_cast <int> (rows.size ( ));

        rows.push_back (row);
      }
    }

    // Special K per executable (Disable Special K / Elevated service), shared by all launch configs starting it
    {
      static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

      services.clear ( );
      hasServlet = _inject.bHasServlet;

      for (auto& it : app.launch_configs)
      {
        app_record_s::launch_config_s& lc = it.second;

        if (! app.IsLaunchConfigAvailable (lc) || _FindService (lc.getExecutableFullPath ( )) != -1)
          continue;

        service_s svc;
        svc.configId       = lc.id;
        svc.path           = lc.getExecutableFullPath ( );
        svc.blocked        = svc.blockedOrig  = lc.isBlacklisted ( );
        svc.elevated       = svc.elevatedOrig = lc.isElevated    ( );
        svc.ignorePattern  = svc.ignorePatternOrig = lc.isIgnoringBlacklist ( );
        svc.blockedPattern = _inject._TestUserList (lc.getExecutableFullPathUTF8 ( ).c_str ( ), false);
        svc.antiCheat      = lc.isDisabledForAntiCheat ( );

        services.push_back (svc);
      }
    }

    // lc_user.json, in the order of the file
    userReadable = SKIF_UserLaunchConfigs::Get (app, userOrig);

    for (auto& user : userOrig)
    {
      row_s row;
      row.source = LaunchSource::User;
      row.user   = user;
      rows.push_back (row);
    }

    // Your own launch options identical to an earlier one (same executable, arguments and working directory; flagged by
    //   vdf.cpp, so Steam only): the library hides them from the menus and can't use them as the default launch option.
    //     Matched the same way as vdf.cpp does it, in the same order (the map is ordered by id, as the configs were flagged).
    {
      std::map <std::wstring, std::string> firstLabels; // Key -> name of the first launch config with it

      for (auto& it : app.launch_configs)
      {
        app_record_s::launch_config_s& lc = it.second;

        if (! lc.valid || ! lc.isExecutableFileNameValid ( ))
          continue;

        const std::wstring key = lc.getExecutableFileName ( ) + lc.getLaunchOptions ( ) + L"|" + lc.working_dir + SKIF_EnvVars::GetKey (lc.env);

        if (! lc.duplicate_exe_args)
        {
          firstLabels.emplace (key, (! lc.description.empty ( )) ? SK_WideCharToUTF8 (lc.description) : lc.getExecutableFileNameUTF8 ( ));
          continue;
        }

        auto first = firstLabels.find (key);

        if (first == firstLabels.end ( ) || app_record_s::GetLaunchSource (lc) != LaunchSource::User)
          continue;

        const app_record_s::launch_ref_s ref = app.MakeLaunchRef (lc);

        for (auto& row : rows)
        {
          if (row.source != LaunchSource::User || ! row.identicalTo.empty ( ))
            continue;

          const app_record_s::launch_ref_s rowRef = _RowRef (row);

          if (rowRef.args == ref.args && rowRef.desc == ref.desc && _stricmp (rowRef.exe.c_str ( ), ref.exe.c_str ( )) == 0)
          {
            row.identicalTo   = first->second;
            row.identicalUser = row.user;
            break;
          }
        }
      }
    }

    // The default launch config
    defaultOrig    = app.skif.default_launch;
    defaultRow     = _FindRow (defaultOrig);
    defaultMissing = (! defaultOrig.empty ( ) && defaultRow == -1);
    keepMissing    = true;

    // When the window opens: the default launch config. When loaded again (after Save / Apply, or a library refresh): the
    //   same launch config as before, wherever it is in the list now, so the one being edited stays selected; if it is
    //     gone, the one at the same position
    if (! loadedOnce)
      selected = (defaultRow != -1) ? defaultRow : 0;

    else if (int row = _FindRow (selectedRef); row != -1)
      selected = row;

    else if (selected >= static_cast <int> (rows.size ( )))
      selected = std::max (0, static_cast <int> (rows.size ( )) - 1);

    loadedOnce = true;
  }

  void Draw (app_record_s& app) override
  {
    using LaunchSource = app_record_s::LaunchSource;

    if (rows.empty ( ))
    {
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("This game has no launch options."));
      return;
    }

    if (selected < 0 || selected >= static_cast <int> (rows.size ( )))
      selected = 0;

    const ImGuiStyle& style = ImGui::GetStyle ( );

    // Buttons above the list: editing at the left, sharing through the clipboard at the right (below, if there is no room)
    const bool isUser = (rows [selected].source == LaunchSource::User);

    SKIF_ImGui_PushDisableState (! userReadable);
    if (ImGui::Button (L_("Add###PropsLaunchAdd")))
      _AddUserRow (app);
    SKIF_ImGui_PopDisableState  (! userReadable);
    SKIF_ImGui_SetHoverTip (_("Adds a new launch option of your own (lc_user.json)."), false, true);

    ImGui::SameLine ( );

    const bool canDuplicate = (userReadable && (isUser || rows [selected].duplicateBlocker.empty ( )));

    SKIF_ImGui_PushDisableState (! canDuplicate);
    if (ImGui::Button (L_("Duplicate###PropsLaunchDuplicate")))
      _Duplicate (selected);
    SKIF_ImGui_PopDisableState  (! canDuplicate);
    SKIF_ImGui_SetHoverTip ((canDuplicate || ! userReadable) ? _("Creates an editable copy of the selected launch option.") : rows [selected].duplicateBlocker.c_str ( ), false, true);

    ImGui::SameLine ( );

    SKIF_ImGui_PushDisableState (! isUser);
    if (ImGui::Button (L_("Remove###PropsLaunchRemove")))
      _RemoveRow (selected);
    SKIF_ImGui_PopDisableState  (! isUser);
    SKIF_ImGui_SetHoverTip ((isUser) ? _("Removes the selected launch option.") : _("Launch options of the platform and Special K can't be removed."), false, true);

    // The last launch option was removed
    if (rows.empty ( ))
      return;

    ImGui::SameLine ( );

    // (The default launch option is picked in General > Play)

    const bool selectedUser = (rows [selected].source == LaunchSource::User);
    const bool canUp        = (selectedUser && selected > 0                                     && rows [selected - 1].source == LaunchSource::User);
    const bool canDown      = (selectedUser && selected < static_cast <int> (rows.size ( )) - 1 && rows [selected + 1].source == LaunchSource::User);

    SKIF_ImGui_PushDisableState (! canUp);
    if (ImGui::Button (ICON_FA_CHEVRON_UP "###PropsLaunchUp"))
      _MoveRow (selected, -1);
    SKIF_ImGui_PopDisableState  (! canUp);
    SKIF_ImGui_SetHoverTip (_("Moves the launch option up among your own."), false, true);

    ImGui::SameLine ( );

    SKIF_ImGui_PushDisableState (! canDown);
    if (ImGui::Button (ICON_FA_CHEVRON_DOWN "###PropsLaunchDown"))
      _MoveRow (selected, +1);
    SKIF_ImGui_PopDisableState  (! canDown);
    SKIF_ImGui_SetHoverTip (_("Moves the launch option down among your own."), false, true);

    // Sharing your own launch options through the clipboard
    auto _ButtonWidth = [&](const char* label) -> float
    {
      return ImGui::CalcTextSize (label, nullptr, true).x + style.FramePadding.x * 2.0f;
    };

    const float fShareWidth = _ButtonWidth (L_("Import...###PropsLaunchImport")) + _ButtonWidth (L_("Export...###PropsLaunchExportCurrent")) + _ButtonWidth (L_("Export All...###PropsLaunchExportAll")) + style.ItemSpacing.x * 2.0f;

    ImGui::SameLine ( );

    const float fAvail = ImGui::GetContentRegionAvail ( ).x;

    if (fAvail >= fShareWidth)
      ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + fAvail - fShareWidth);
    else
      ImGui::NewLine ( );

    const bool hasUserRows = std::any_of (rows.begin ( ), rows.end ( ), [](const row_s& row) { return row.source == LaunchSource::User; });

    SKIF_ImGui_PushDisableState (! userReadable);
    if (ImGui::Button (L_("Import...###PropsLaunchImport")))
      _Import (app);
    SKIF_ImGui_PopDisableState  (! userReadable);
    SKIF_ImGui_SetHoverTip (_("Adds launch options from the clipboard to your own,\nas copied by Export / Export All."), false, true);

    ImGui::SameLine ( );

    SKIF_ImGui_PushDisableState (! selectedUser);
    if (ImGui::Button (L_("Export...###PropsLaunchExportCurrent")))
      _Export (app, true);
    SKIF_ImGui_PopDisableState  (! selectedUser);
    SKIF_ImGui_SetHoverTip ((selectedUser) ? _("Copies the selected launch option to the clipboard.")
                                           : _("Only your own launch options can be exported."), false, true);

    ImGui::SameLine ( );

    SKIF_ImGui_PushDisableState (! hasUserRows);
    if (ImGui::Button (L_("Export All...###PropsLaunchExportAll")))
      _Export (app, false);
    SKIF_ImGui_PopDisableState  (! hasUserRows);
    SKIF_ImGui_SetHoverTip (_("Copies all of your own launch options to the clipboard."), false, true);

    // The launch options, below the buttons: always room for six of them, the list scrolls for more
    //   (after the buttons, which may have changed the rows, the selection or the default)
    const int   effectiveDefault = _PlayRow ( );

    const float fListHeight = 6.0f * ImGui::GetTextLineHeightWithSpacing ( )
                            + 2.0f * style.ItemSpacing.y // The separators between the platform, SKIF and user launch options
                            + style.WindowPadding.y * 2.0f;

    ImGui::BeginChild ("###PropsLaunchList", ImVec2 (0.0f, fListHeight), ImGuiChildFlags_Borders);

    LaunchSource lastSource = rows.front ( ).source;

    for (int i = 0; i < static_cast <int> (rows.size ( )); i++)
    {
      const row_s& row = rows [i];

      // Separators between the platform, SKIF and user launch options (as in the game menu)
      if (row.source != lastSource)
      {
        lastSource = row.source;
        ImGui::Separator ( );
      }

      const std::string label = _RowLabel (row);

      // As in the game menu: Special K disabled / elevated service for the executable
      const int  svcIdx   = _FindService (_RowFullExe (row));
      const bool blocked  = (svcIdx != -1 && (services [svcIdx].blocked || services [svcIdx].blockedPattern));
      const bool elevated = (svcIdx != -1 && ! blocked && services [svcIdx].elevated);

      if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("%s%s%s%s###PropsLaunchRow-%d",
                                (i == effectiveDefault) ? ICON_FA_STAR        "  " : "",
                                (blocked)               ? ICON_FA_LOCK        "  " : "",
                                (elevated)              ? ICON_FA_USER_SHIELD "  " : "",
                                (label.empty ( )) ? _("<no description>") : label.c_str ( ), i), selected == i))
      {
        if (selected != i)
          pickError.clear ( );

        selected = i;
      }

      if (i == effectiveDefault)
        SKIF_ImGui_SetHoverTip (_("Used by the Play button (General > Play)."));
    }

    ImGui::EndChild ( );

    // Notes / errors of the buttons
    if (! pickError.empty ( ))
    {
      ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine    ( );
      ImGui::TextWrapped ("%s", pickError.c_str ( ));
    }

    else if (! note.empty ( ))
    {
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info));
      ImGui::TextWrapped    ("%s", note.c_str ( ));
      ImGui::PopStyleColor  ( );
    }

    // The fields of the selected launch option, below the list
    SKIF_ImGui_Spacing ( );
    ImGui::Separator   ( );
    SKIF_ImGui_Spacing ( );

    if (rows [selected].source == LaunchSource::User)
      _DrawUserRow     (app, rows [selected]);
    else
      _DrawReadOnlyRow (app, selected);
  }

  void Validate (app_record_s& app, std::vector <property_issue_s>& out) override
  {
    UNREFERENCED_PARAMETER (app);

    using Level = property_issue_s::Level;

    if (! userReadable)
      out.push_back ({ Level::Warning, _("lc_user.json could not be read, so your own launch options can't be changed here. Fix or remove the file to edit them.") });

    // Descriptions must be unique, but only your own launch options can be changed, so only those are reported
    std::set <std::string> names, reported;

    for (const auto& row : rows)
      if (row.source != app_record_s::LaunchSource::User)
        names.emplace (_RowLabel (row));

    for (const auto& row : rows)
    {
      if (row.source != app_record_s::LaunchSource::User)
        continue;

      const std::string label = _RowLabel (row);

      if (! label.empty ( ) && ! names.emplace (label).second && reported.emplace (label).second)
        out.push_back ({ Level::Error, F_("More than one launch option is called \"{0}\".", label) });

      const std::string& name = (label.empty ( )) ? std::string (_("<no description>")) : label;

      if (label.empty ( ))
        out.push_back ({ Level::Error, _("Every launch option of your own needs a description.") });

      if (row.user.exe.empty ( ))
        out.push_back ({ Level::Error, F_("\"{0}\" has no executable.", name) });

      else if (! PathIsRelativeW (SK_UTF8ToWideChar (row.user.exe).c_str ( )) || row.user.exe.rfind ("..", 0) == 0)
        out.push_back ({ Level::Error, F_("The executable of \"{0}\" must be inside the install folder of the game.", name) });

      else if (! PathFileExistsW (_UserFullExe (row.user).c_str ( )))
        out.push_back ({ Level::Warning, F_("The executable of \"{0}\" was not found.", name) });

      if (! row.user.dir.empty ( ) && (! PathIsRelativeW (SK_UTF8ToWideChar (row.user.dir).c_str ( )) || row.user.dir.rfind ("..", 0) == 0))
        out.push_back ({ Level::Error, F_("The working directory of \"{0}\" must be inside the install folder of the game.", name) });

      // The problems themselves are shown next to the variables
      if (SKIF_EnvVars::HasErrors (row.user.env, SKIF_EnvVars::Target::LaunchConfig))
        out.push_back ({ Level::Error, F_("The environment variables of \"{0}\" need to be fixed.", name) });
    }
  }

  std::string GetApplyError (void) const override { return applyError; }

  bool IsDirty (void) const override
  {
    return (_CollectUsers ( )    != userOrig    ||
            _GetDefaultRef ( )   != defaultOrig ||
            _ServicesDirty ( ));
  }

  uint32_t Apply (app_record_s& app) override
  {
    uint32_t ret = PropertiesApply_None;

    std::vector <user_launch_config_s> users = _CollectUsers ( );

    applyError.clear ( );

    if (users != userOrig)
    {
      // Launch configs are only read when the library is populated
      if (SKIF_UserLaunchConfigs::Set (app, users))
        ret |= PropertiesApply_Repopulate;

      // Nothing else of this section is applied either, so all of it can be saved again
      else
      {
        applyError = _("Your launch options could not be saved to lc_user.json. See the log for details.");
        return PropertiesApply_Failed;
      }
    }

    // Special K per executable, through one of the launch configs starting it (also maintains Special K's own files)
    for (auto& svc : services)
    {
      auto it = app.launch_configs.find (svc.configId);

      // Only if it is still the same launch config (the library may have been refreshed meanwhile)
      if (it == app.launch_configs.end ( ) || it->second.getExecutableFullPath ( ) != svc.path)
        continue;

      app_record_s::launch_config_s& lc = it->second;

      if (svc.blocked != svc.blockedOrig)
        lc.setBlacklisted (svc.blocked);

      if (svc.ignorePattern != svc.ignorePatternOrig)
        lc.setIgnoreBlacklist (svc.ignorePattern);

      // Its own setting (also for a blocked executable, should Ignore Blacklist be set)
      if (svc.elevated != svc.elevatedOrig)
        lc.setElevated (svc.elevated);
    }

    app_record_s::launch_ref_s ref = _GetDefaultRef ( );

    if (ref != app.skif.default_launch)
    {
      app.skif.default_launch = ref;
      ret |= PropertiesApply_WriteMetaDB | PropertiesApply_Reload; // The details follow the default launch config
    }

    return ret;
  }

private:
  struct row_s {
    app_record_s::LaunchSource source = app_record_s::LaunchSource::Platform;

    // Platform / SKIF rows (read-only): a copy of what is shown, and the launch config they came from
    int                        configId        = -1;
    std::string                label;
    std::string                fullExe;
    std::string                args;
    std::string                workDir;
    SKIF_EnvVars::List         env;                     // lc.json "Env"
    bool                       clientCanLaunch = false;
    std::string                duplicateBlocker;        // Why it can't be copied to lc_user.json (empty if it can)
    app_record_s::launch_ref_s ref;

    // User rows: the lc_user.json entry being edited
    user_launch_config_s       user;

    // User rows identical to another launch option when loaded (its name), and the entry as it was then: the warning
    //   is shown until the executable, arguments, working directory or environment variables are changed (checked again once saved)
    std::string                identicalTo;
    user_launch_config_s       identicalUser;

    bool IsIdentical (void) const
    {
      return (! identicalTo.empty ( ) && user.exe  == identicalUser.exe
                                      && user.args == identicalUser.args
                                      && user.dir  == identicalUser.dir
                                      && user.env  == identicalUser.env);
    }
  };

  // Special K settings of an executable (as in the details panel), applied through a launch config that starts it
  struct service_s {
    int          configId       = -1;
    std::wstring path;
    bool         blocked        = false,
                 blockedOrig    = false;
    bool         elevated       = false,
                 elevatedOrig   = false;
    bool         blockedPattern = false; // Blocked through a blacklist pattern in the Settings tab
    bool         ignorePattern  = false, // The service is used even if Special K is disabled for the executable (launch behaviors)
                 ignorePatternOrig = false;
    bool         antiCheat      = false; // Disabled by default, as anti-cheat was detected (nothing set for the executable)
  };

  std::vector <service_s>            services;
  std::string                        applyError;                    // Why the last Apply failed
  bool                               hasServlet            = true;

  int _FindService (const std::wstring& path) const
  {
    for (int i = 0; i < static_cast <int> (services.size ( )); i++)
      if (_SamePath (services [i].path, path))
        return i;

    return -1;
  }

  bool _ServicesDirty (void) const
  {
    for (const auto& svc : services)
      if (svc.blocked != svc.blockedOrig || svc.ignorePattern != svc.ignorePatternOrig || svc.elevated != svc.elevatedOrig)
        return true;

    return false;
  }

  std::wstring _RowFullExe (const row_s& row) const
  {
    return (row.source == app_record_s::LaunchSource::User) ? _UserFullExe (row.user) : SK_UTF8ToWideChar (row.fullExe);
  }

  void _DrawService (const std::wstring& path)
  {
    SKIF_Properties_Label (_("Special K"), _("Applies to the executable, so to every launch option that starts it."));

    if (! hasServlet)
    {
      ImGui::TextColored  (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine     ( );
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Special K is unavailable due to missing files."));
      return;
    }

    const int idx = _FindService (path);

    // A new executable: the library has no launch config for it until the launch options are saved
    if (idx == -1)
    {
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Apply first to change these for this executable."));
      return;
    }

    service_s& svc = services [idx];

    bool blocked = (svc.blocked || svc.blockedPattern);

    SKIF_ImGui_PushDisableState (svc.blockedPattern);
    if (SKIF_ImGui_Checkbox (L_("Disable Special K###PropsSvcBlocked"), &blocked) && ! svc.blockedPattern)
      svc.blocked = blocked;
    SKIF_ImGui_PopDisableState  (svc.blockedPattern);

    if (svc.blockedPattern)
      SKIF_ImGui_SetHoverTip (_("Special K is disabled through a blacklist pattern in the Settings tab."), true, true);

    else if (svc.antiCheat && svc.blocked)
      SKIF_ImGui_SetHoverTip (_("Special K is disabled by default as the game uses anti-cheat (see Technology).\n"
                                "Uncheck this to use Special K with the game anyway."), true, true);

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    // Ignore blacklist: the service is used for the executable even if Special K is disabled for it (above, or through a
    //   blacklist pattern of the Settings tab), also when SKIF is used as a launcher (SKIF %COMMAND%). Special K stays
    //     disabled for the executable itself: meant for a launcher, so that the game it starts gets Special K.
    SKIF_ImGui_PushDisableState (! blocked);
    SKIF_ImGui_Checkbox   (L_("Ignore blacklist###PropsSvcIgnoreBlacklist"), &svc.ignorePattern);
    SKIF_ImGui_PopDisableState  (! blocked);

    SKIF_ImGui_SetHoverTip (_("Starts the injection service even if Special K is disabled for this executable,\n"
                              "here or through a blacklist pattern in the Settings tab.\n\n"
                              "Special K remains disabled for the executable itself. This is meant for launchers:\n"
                              "the game started by the launcher gets Special K.\n\n"
                              "This also applies when SKIF is used as a launcher, e.g. SKIF %COMMAND%."), true, true);

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    const bool elevateDisabled = ((svc.blocked || svc.blockedPattern) && ! svc.ignorePattern);
    // Shown as it is set (greyed out while it does not matter): blocking the executable keeps its elevation
    bool elevated = svc.elevated;

    SKIF_ImGui_PushDisableState (elevateDisabled);
    if (SKIF_ImGui_Checkbox (L_("Elevated service###PropsSvcElevated"), &elevated) && ! elevateDisabled)
      svc.elevated = elevated;
    SKIF_ImGui_PopDisableState  (elevateDisabled);

    if (elevateDisabled)
      SKIF_ImGui_SetHoverTip (_("Special K is disabled for this executable, so the service is not used."), true, true);
  }

  std::vector <row_s>                rows;           // Platform, SKIF, then User (in lc_user.json order)
  std::vector <user_launch_config_s> userOrig;       // lc_user.json as loaded
  bool                               userReadable          = true;
  int                                selected              =  0;
  bool                               loadedOnce            = false; // Load ( ) keeps the selection after the first time
  int                                primaryRow            = -1;   // The primary platform launch config: the default if no other is set
  int                                defaultRow            = -1;   // The row tagged as the default; -1 = none
  app_record_s::launch_ref_s         defaultOrig;                   // As loaded (db.json "DefaultLaunch")
  bool                               defaultMissing        = false; // defaultOrig is set but matches no row
  bool                               keepMissing           = true;  // Keep defaultOrig while it matches no row (until changed or cleared)
  std::wstring                       installDir;
  std::string                        clientOptions;                 // Launch options set in the store client (Steam)
  std::string                        note;                          // Shown below the editor (auto-duplicate)
  std::string                        pickError;

  // The row of the launch option the Play button uses: the default one, else the primary one (also while a missing default is kept)
  int _PlayRow (void) const
  {
    return (defaultRow >= 0 && defaultRow < static_cast <int> (rows.size ( ))) ? defaultRow : primaryRow;
  }

  static bool _RefMatches (const app_record_s::launch_ref_s& a, const app_record_s::launch_ref_s& b)
  {
    return (a.source == b.source && a.args == b.args && _stricmp (a.exe.c_str ( ), b.exe.c_str ( )) == 0);
  }

  std::string _RowLabel (const row_s& row) const
  {
    return (row.source == app_record_s::LaunchSource::User) ? row.user.desc : row.label;
  }

  std::wstring _UserFullExe (const user_launch_config_s& user) const
  {
    return installDir + LR"(\)" + _ToBackslashes (SK_UTF8ToWideChar (user.exe));
  }

  // Same as app_record_s::MakeLaunchRef ( ) for the launch config the library loads from this entry
  app_record_s::launch_ref_s _RowRef (const row_s& row) const
  {
    if (row.source != app_record_s::LaunchSource::User)
      return row.ref;

    app_record_s::launch_ref_s ref;

    std::wstring full = _UserFullExe (row.user);
    std::wstring rel  = app_record_s::GetPathRelativeTo (installDir, full);

    ref.source = "User";
    ref.exe    = _ToForwardSlashes (SK_WideCharToUTF8 ((rel.empty ( )) ? full : rel));
    ref.args   = row.user.args;
    ref.desc   = row.user.desc;

    return ref;
  }

  // The row matching a reference: the description decides between several matches
  int _FindRow (const app_record_s::launch_ref_s& ref) const
  {
    if (ref.empty ( ))
      return -1;

    int match = -1;

    for (int i = 0; i < static_cast <int> (rows.size ( )); i++)
    {
      app_record_s::launch_ref_s cur = _RowRef (rows [i]);

      if (! _RefMatches (cur, ref))
        continue;

      if (cur.desc == ref.desc)
        return i;

      if (match == -1)
        match = i;
    }

    return match;
  }

  app_record_s::launch_ref_s _GetDefaultRef (void) const
  {
    if (defaultRow >= 0 && defaultRow < static_cast <int> (rows.size ( )))
      return _RowRef (rows [defaultRow]);

    if (defaultMissing && keepMissing)
      return defaultOrig;

    return app_record_s::launch_ref_s { };
  }

  std::vector <user_launch_config_s> _CollectUsers (void) const
  {
    std::vector <user_launch_config_s> users;

    for (const auto& row : rows)
      if (row.source == app_record_s::LaunchSource::User)
        users.push_back (row.user);

    return users;
  }

  std::string _UniqueDescription (const std::string& base) const
  {
    auto _Exists = [&](const std::string& name) -> bool
    {
      for (const auto& row : rows)
        if (_RowLabel (row) == name)
          return true;

      return false;
    };

    if (! _Exists (base))
      return base;

    for (int i = 2; i < 100; i++)
    {
      std::string name = base + " " + std::to_string (i);

      if (! _Exists (name))
        return name;
    }

    return base;
  }

  // Working directory for lc_user.json: empty for the executable's folder, else relative to the install folder ("." for the install folder itself)
  //   Returns false if the folder is outside of the install folder.
  bool _MakeUserDir (const std::wstring& folder, const std::wstring& fullExe, std::string& out) const
  {
    out.clear ( );

    if (folder.empty ( ) || _SamePath (folder, std::filesystem::path (fullExe).parent_path ( ).wstring ( )))
      return true;

    if (_SamePath (folder, installDir))
    {
      out = ".";
      return true;
    }

    std::wstring rel = app_record_s::GetPathRelativeTo (installDir, folder);

    if (rel.empty ( ))
      return false;

    out = _ToForwardSlashes (SK_WideCharToUTF8 (rel));
    return true;
  }

  void _AddUserRow (app_record_s& app)
  {
    UNREFERENCED_PARAMETER (app);

    row_s row;
    row.source    = app_record_s::LaunchSource::User;
    row.user.desc = _UniqueDescription (_("New launch option"));

    // Start from the primary executable
    if (primaryRow != -1)
    {
      std::wstring rel = app_record_s::GetPathRelativeTo (installDir, SK_UTF8ToWideChar (rows [primaryRow].fullExe));

      if (! rel.empty ( ))
        row.user.exe = _ToForwardSlashes (SK_WideCharToUTF8 (rel));
    }

    rows.push_back (row);
    selected = static_cast <int> (rows.size ( )) - 1;
    note.clear      ( );
    pickError.clear ( );
  }

  // Copies a row into a new user row (Duplicate). Returns the new row, or -1
  int _Duplicate (int index)
  {
    const row_s& src = rows [index];

    row_s row;
    row.source = app_record_s::LaunchSource::User;

    if (src.source == app_record_s::LaunchSource::User)
      row.user = src.user;

    else
    {
      if (! src.duplicateBlocker.empty ( ))
        return -1;

      std::wstring fullExe = SK_UTF8ToWideChar (src.fullExe);
      std::wstring rel     = app_record_s::GetPathRelativeTo (installDir, fullExe);

      if (rel.empty ( ))
        return -1;

      row.user.exe          = _ToForwardSlashes (SK_WideCharToUTF8 (rel));
      row.user.args         = src.args;
      row.user.env          = src.env;

      // A working directory outside of the install folder can't be stored, so the copy uses the executable's folder
      if (! _MakeUserDir (SK_UTF8ToWideChar (src.workDir), fullExe, row.user.dir))
        row.user.dir.clear ( );
    }

    std::string label = _RowLabel (src);
    if (label.empty ( ))
      label = _("Launch option");

    // TRANSLATORS: {0} is the name of the launch option that was copied
    row.user.desc = _UniqueDescription (F_("{0} (copy)", label));

    rows.push_back (row);

    const int newIndex = static_cast <int> (rows.size ( )) - 1;

    selected = newIndex;
    pickError.clear ( );
    note.clear      ( );

    return newIndex;
  }

  // Clipboard sharing of your own launch options (the lc_user.json format)
  void _Export (app_record_s& app, bool currentOnly)
  {
    std::vector <user_launch_config_s> users;

    if (! currentOnly)
      users = _CollectUsers ( );

    else if (rows [selected].source == app_record_s::LaunchSource::User)
      users.push_back (rows [selected].user);

    if (users.empty ( ))
      return;

    std::string text = SKIF_UserLaunchConfigs::Export (app, users);

    if (text.empty ( ))
    {
      pickError = _("The launch options could not be exported.");
      return;
    }

    ImGui::SetClipboardText (text.c_str ( ));

    pickError.clear ( );
    note = (users.size ( ) == 1) ? F_("Copied \"{0}\" to the clipboard.", users.front ( ).desc)
                                 : FP_("Copied {0} launch option to the clipboard.", "Copied {0} launch options to the clipboard.", users.size ( ));
  }

  void _Import (app_record_s& app)
  {
    const char* clipboard = ImGui::GetClipboardText ( );

    std::vector <user_launch_config_s> imported;
    std::string                        error;

    if (clipboard == nullptr || ! SKIF_UserLaunchConfigs::Import (app, clipboard, imported, error))
    {
      pickError = (! error.empty ( )) ? error : std::string (_("The clipboard does not contain any launch options."));
      return;
    }

    int first = -1;

    for (const auto& lc : imported)
    {
      row_s row;
      row.source    = app_record_s::LaunchSource::User;
      row.user      = lc;
      row.user.desc = _UniqueDescription ((lc.desc.empty ( )) ? std::string (_("Imported launch option")) : lc.desc);

      rows.push_back (row);

      if (first == -1)
        first = static_cast <int> (rows.size ( )) - 1;
    }

    selected = first;

    pickError.clear ( );
    note = (imported.size ( ) == 1) ? F_ ("Imported \"{0}\" from the clipboard. Apply to keep it.", rows [first].user.desc)
                                    : FP_("Imported {0} launch option from the clipboard. Apply to keep it.", "Imported {0} launch options from the clipboard. Apply to keep them.", imported.size ( ));
  }

  void _RemoveRow (int index)
  {
    if (rows [index].source != app_record_s::LaunchSource::User)
      return;

    if (defaultRow == index)
    {
      defaultRow  = -1;
      keepMissing = false;
    }

    else if (defaultRow > index)
      defaultRow--;

    rows.erase (rows.begin ( ) + index);

    selected = std::max (0, std::min (index, static_cast <int> (rows.size ( )) - 1));
    note.clear      ( );
    pickError.clear ( );
  }

  void _MoveRow (int index, int direction)
  {
    const int other = index + direction;

    if (other < 0 || other >= static_cast <int> (rows.size ( )) ||
        rows [index].source != app_record_s::LaunchSource::User ||
        rows [other].source != app_record_s::LaunchSource::User)
      return;

    std::swap (rows [index], rows [other]);

    if      (defaultRow == index) defaultRow = other;
    else if (defaultRow == other) defaultRow = index;

    selected = other;
  }

  void _DrawUserRow (app_record_s& app, row_s& row)
  {
    user_launch_config_s& user = row.user;

    if (row.IsIdentical ( ))
    {
      ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine    ( );
      // TRANSLATORS: {0} is the name of the other launch option
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning),
        F_("Identical to \"{0}\" (same executable, arguments, working directory and environment variables). It is hidden from the menus and can't be the default launch option until it is changed.", row.identicalTo).c_str ( ));
    }

    SKIF_ImGui_PushDisableState (! userReadable);

    SKIF_Properties_Label (_("Description"));
    _InputString          ("###PropsLaunchDesc", user.desc, _("Shown in the menus"));

    // Executable (inside the install folder)
    SKIF_Properties_Label (_("Executable"), _("Relative to the install folder of the game."));

    const float fBrowseWidth = ImGui::CalcTextSize (L_("Browse...###PropsLaunchBrowseExe"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f;

    ImGui::BeginGroup     ( );
    ImGui::PushItemWidth  (ImGui::GetContentRegionAvail ( ).x - fBrowseWidth - ImGui::GetStyle ( ).ItemSpacing.x);
    {
      static char buffer [MAX_PATH * 2 + 2] = { };
      strncpy_s (buffer, SK_WideCharToUTF8 (_ToBackslashes (SK_UTF8ToWideChar (user.exe))).c_str ( ), _TRUNCATE);
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      ImGui::InputTextWithHint ("###PropsLaunchExe", _("Select an executable"), buffer, sizeof (buffer) - 2, ImGuiInputTextFlags_ReadOnly);
      SKIF_ImGui_BlockDragMoveWhenItemActive ( );
      ImGui::PopStyleColor  ( );
    }
    ImGui::PopItemWidth   ( );
    ImGui::SameLine       ( );

    if (ImGui::Button (L_("Browse...###PropsLaunchBrowseExe")))
    {
      std::wstring picked;

      if (SKIF_Properties_BrowseForFile (picked, installDir.c_str ( )))
      {
        SKIF_Util_ExecutableSelection_s selection =
          SKIF_Util_ResolveExecutableSelection (picked);

        std::wstring rel = (selection.valid) ? app_record_s::GetPathRelativeTo (installDir, selection.path) : L"";

        if (! selection.valid)
          pickError = _("Please select an executable (.exe or .bat), or a shortcut to one.");

        else if (rel.empty ( ))
          pickError = _("The executable must be inside the install folder of the game.");

        else
        {
          pickError.clear ( );
          user.exe = _ToForwardSlashes (SK_WideCharToUTF8 (rel));

          // A shortcut also brings its arguments and working directory
          if (selection.shortcut)
          {
            if (user.args.empty ( ))
              user.args = SK_WideCharToUTF8 (selection.args);

            std::string dir;
            if (_MakeUserDir (selection.working_dir, selection.path, dir))
              user.dir = dir;
          }
        }
      }
    }

    ImGui::EndGroup ( );

    // Working directory
    SKIF_Properties_Label (_("Working directory"), _("Relative to the install folder of the game.\nLeave it as the executable's folder if unsure."));

    const float fChangeWidth = ImGui::CalcTextSize (L_("Change...###PropsLaunchBrowseDir"), nullptr, true).x + ImGui::GetStyle ( ).FramePadding.x * 2.0f;
    const float fResetWidth  = ImGui::GetFrameHeight ( );

    ImGui::BeginGroup     ( );
    ImGui::PushItemWidth  (ImGui::GetContentRegionAvail ( ).x - fChangeWidth - fResetWidth - ImGui::GetStyle ( ).ItemSpacing.x * 2.0f);
    {
      static char buffer [MAX_PATH * 2 + 2] = { };
      strncpy_s (buffer, (user.dir == ".") ? _("(install folder)") : SK_WideCharToUTF8 (_ToBackslashes (SK_UTF8ToWideChar (user.dir))).c_str ( ), _TRUNCATE);
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      ImGui::InputTextWithHint ("###PropsLaunchDir", _("(executable folder)"), buffer, sizeof (buffer) - 2, ImGuiInputTextFlags_ReadOnly);
      SKIF_ImGui_BlockDragMoveWhenItemActive ( );
      ImGui::PopStyleColor  ( );
    }
    ImGui::PopItemWidth   ( );
    ImGui::SameLine       ( );

    if (ImGui::Button (L_("Change...###PropsLaunchBrowseDir")))
    {
      std::wstring picked;

      if (SKIF_Properties_BrowseForFolder (picked, installDir.c_str ( )))
      {
        std::string dir;

        if (_MakeUserDir (picked, _UserFullExe (user), dir))
        {
          pickError.clear ( );
          user.dir = dir;
        }

        else
          pickError = _("The working directory must be inside the install folder of the game.");
      }
    }

    ImGui::SameLine ( );

    if (ImGui::Button (ICON_FA_XMARK "###PropsLaunchResetDir", ImVec2 (fResetWidth, 0.0f)))
      user.dir.clear ( );
    SKIF_ImGui_SetHoverTip (_("Use the folder of the executable."));

    ImGui::EndGroup ( );

    // Arguments
    SKIF_Properties_Label (_("Arguments"));
    _InputString          ("###PropsLaunchArgs", user.args, _("Leave empty if unsure"));

    // Environment variables (lc_user.json "Env")
    SKIF_Properties_Label (_("Environment"), _("Environment variables set when SKIF starts this launch option.\n"
                                               "A variable with an empty value is removed instead.\n\n"
                                               "They are not set when the game is started any other way.\n"
                                               "For mods and overlays, Special K's EnvironmentVars (INI editor) applies however the game is started."));

    ImGui::BeginGroup        ( );
    SKIF_ImGui_EnvVarsEditor ("###PropsLaunchEnv", user.env, SKIF_EnvVars::Target::LaunchConfig);
    ImGui::EndGroup          ( );

    // (Your own launch options always start directly: the platform client can't start them. How the primary one
    //   starts is set in General > Play.)

    SKIF_ImGui_PopDisableState (! userReadable);

    _DrawService (_UserFullExe (user));


    // Preview of what is started
    SKIF_ImGui_Spacing ( );

    std::string preview = SK_WideCharToUTF8 (_UserFullExe (user));

    if (! user.args.empty ( ))
      preview += " " + user.args;

    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Starts:"));
    ImGui::SameLine     ( );
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
    ImGui::TextWrapped  ("%s", preview.c_str ( ));
    ImGui::PopStyleColor ( );

    if (! clientOptions.empty ( ))
    {
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      // TRANSLATORS: {0} is the platform (e.g. Steam), {1} its launch options for the game
      ImGui::TextWrapped    ("%s", F_("The launch options set in {0} ({1}) are not used by this launch option.", app.store_utf8, clientOptions).c_str ( ));
      ImGui::PopStyleColor  ( );
    }
  }

  void _DrawReadOnlyRow (app_record_s& app, int index)
  {
    const row_s& row = rows [index];

    ImGui::TextColored  (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_CIRCLE_EXCLAMATION);
    ImGui::SameLine     ( );
    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), (row.source == app_record_s::LaunchSource::SKIF)
                          ? _("Provided by Special K (lc.json). Duplicate it to make changes.")
                          : _("Provided by the platform. Duplicate it to make changes."));

    SKIF_Properties_Label (_("Description"));
    _ReadOnlyString       ("###PropsLaunchRODesc", row.label);

    SKIF_Properties_Label (_("Executable"));
    _ReadOnlyString       ("###PropsLaunchROExe",  row.fullExe);

    SKIF_Properties_Label (_("Working directory"));
    _ReadOnlyString       ("###PropsLaunchRODir",  row.workDir, _("(executable folder)"));

    SKIF_Properties_Label (_("Arguments"));
    _ReadOnlyString       ("###PropsLaunchROArgs", row.args);

    // lc.json "Env"
    if (! row.env.empty ( ))
    {
      std::string env;

      for (const auto& var : row.env)
        env += ((env.empty ( )) ? "" : "  ") + var.name + "=" + var.value;

      SKIF_Properties_Label (_("Environment"), _("Environment variables set when SKIF starts this launch option."));
      _ReadOnlyString       ("###PropsLaunchROEnv", env);
    }

    _DrawService (SK_UTF8ToWideChar (row.fullExe));

    // The client's own launch options (Steam), never edited by SKIF
    if (row.source == app_record_s::LaunchSource::Platform && ! clientOptions.empty ( ))
    {
      SKIF_Properties_Label (_("Client options"), _("Set in the platform client. SKIF does not change these."));
      _ReadOnlyString       ("###PropsLaunchROClient", clientOptions);
    }

    UNREFERENCED_PARAMETER (app);
  }
};

// General > Play: the launch option the Play button uses, and how it starts
void
PropertiesSection_LaunchBehavior::_DrawPlay (void)
{
  SKIF_Properties_Label (_("Play"), _("The launch option the Play button uses.\n"
                                      "Any launch option can also be started through Play with in the right click menu."));
  launchOptions->DrawPlayCombo ( );

  if (! instantPlayCompatible)
    return;

  // TRANSLATORS: How the game is started; {0} is the platform (e.g. Steam)
  const std::string platformLabel = F_("Through {0}", storeName);
  const std::string directLabel   = _("Directly (Instant Play)");

  SKIF_Properties_Label (_("Start"), _("How the Play button starts the launch option: through the platform client, or directly.\n"
                                       "Only the primary launch option can be started through the platform client."));

  // Only the primary launch option can be started by the platform client
  if (! launchOptions->PlayClientCanLaunch ( ))
  {
    // TRANSLATORS: How the game is started, when the platform client can't start the launch option
    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Directly (Instant Play, required)"));
    SKIF_ImGui_SetHoverTip (_("The platform client can't start this launch option itself, so it always uses Instant Play."));
    return;
  }

  const std::string defaultLabel = F_("Default ({0})", (instantPlayGlobal) ? directLabel : platformLabel) + "###PropsIPDefault";
  const std::string neverLabel   = platformLabel + "###PropsIPNever";
  const std::string alwaysLabel  = directLabel   + "###PropsIPAlways";

  SKIF_ImGui_RadioButton    (defaultLabel.c_str ( ), &instantPlay, 0);
  SKIF_ImGui_SetHoverTip    (_("The game will use the default behavior configured in the Settings tab."));
  SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (neverLabel.c_str ( )));
  SKIF_ImGui_RadioButton    (neverLabel.c_str ( ),   &instantPlay, 2);
  SKIF_ImGui_SetHoverTip    (_("The game is started through the platform client, as usual."));
  SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (alwaysLabel.c_str ( )));
  SKIF_ImGui_RadioButton    (alwaysLabel.c_str ( ),  &instantPlay, 1);
  SKIF_ImGui_SetHoverTip    (_("The game is started directly, without the platform client."));
  ImGui::SameLine           ( );
  ImGui::TextColored        (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
  SKIF_ImGui_SetHoverTip    (_("Warning: This skips the regular platform launch process for the game,\n"
                               "including steps like the cloud saves synchronization that usually occurs."));
}

#pragma endregion


#pragma region Dialog

// Each Properties window is a window of its own (a separate platform window, like Local Installs and the INI editor),
//   so it is usable whatever the size or mode of the main window, and several can be open at once: one per game.
//   A window edits the game it was opened for, looked up again every frame, so it doesn't depend on the library tab
//   being drawn or on the selection.
struct properties_window_s {
  int                                              id           = 0;     // Part of the ImGui window ID
  bool                                             focusRequest = true;  // Bring the window to the front next frame
  app_key_s                                        game;
  HWND                                             hwnd         = nullptr;
  std::vector <std::unique_ptr <IPropertySection>> sections;

  // Asked when the window is closed (title bar, Escape, Ctrl+W) or the app is exited (Ctrl+Q) with unsaved changes;
  //   what to do once the user has answered the prompt (Save / Don't Save; Cancel keeps the window open)
  enum class PendingAction { None, Close, Exit };
  PendingAction                                    pending      = PendingAction::None;
  bool                                             promptShow   = false;

  // The library swap-in the sections were last loaded after (GetSwapCount ( )): a later one may have replaced the record,
  //   e.g. after new launch options were saved, so they are loaded again from it (see _SKIF_Properties_DrawWindow ( ))
  uint32_t                                         loadedSwap   = 0;

  // Scrolls the content to the start of a page (SKIF_Properties_RequestPage ( )). Repeated for a few frames, as the content
  //   of a new window is not laid out (nor its scrolling range known) on its first frame
  PropertiesPage                                   scrollPage   = PropertiesPage::General;
  int                                              scrollFrames = 0;

  bool IsDirty (void) const
  {
    for (auto& section : sections)
      if (section->IsDirty ( ))
        return true;

    return false;
  }
};

static std::vector <std::unique_ptr <properties_window_s>> s_windows;

// The page requested for the window opened next (see SKIF_Properties_RequestPage ( )); -1 for none
static int s_requestedPage = -1;

void
SKIF_Properties_RequestPage (PropertiesPage page)
{
  s_requestedPage = static_cast <int> (page);
}

static std::unique_ptr <properties_window_s>
_SKIF_Properties_Create (app_record_s* pApp)
{
  static int nextId = 0;

  auto wnd = std::make_unique <properties_window_s> ( );

  auto launchBehavior = std::make_unique <PropertiesSection_LaunchBehavior> ( );
  auto launchOptions  = std::make_unique <PropertiesSection_LaunchOptions>  ( );

  // General > Play picks the default launch option, which the launch options section keeps
  launchBehavior->SetLaunchOptions (launchOptions.get ( ));

  wnd->sections.push_back (std::make_unique <PropertiesSection_General> ( ));
  wnd->sections.push_back (std::move (launchBehavior));
  wnd->sections.push_back (std::make_unique <PropertiesSection_Detection> ( ));
  wnd->sections.push_back (std::move (launchOptions));

  // Store-specific sections (Custom: the executable)
  pApp->AddPropertySections (wnd->sections);

  std::stable_sort (wnd->sections.begin ( ), wnd->sections.end ( ),
    [](const std::unique_ptr <IPropertySection>& a, const std::unique_ptr <IPropertySection>& b)
    {
      if (a->GetPage ( ) != b->GetPage ( ))
        return (static_cast <int> (a->GetPage ( )) < static_cast <int> (b->GetPage ( )));

      return (a->GetOrder ( ) < b->GetOrder ( ));
    });

  for (auto& section : wnd->sections)
    section->Load (*pApp);

  wnd->loadedSwap = SKIF_GamingCollection::Get ( ).GetSwapCount ( );

  wnd->id   = nextId++;
  wnd->game = pApp->GetKey ( );

  return wnd;
}

// Draws one window; returns false once it has been closed
static bool
_SKIF_Properties_DrawWindow (properties_window_s& wnd, app_record_s* pApp)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static bool&                  update      = GetLibraryTab ( ).update;

  // Validated before drawing, as the footer is sized after the number of issues
  std::vector <property_issue_s> issues;

  // The library was refreshed since the sections were loaded (e.g. after Apply saved new launch options, which only then
  //   get their per-executable settings): load them again from the current record. Not while there are unsaved changes,
  //     which would be lost; that waits until they are saved or discarded
  if (wnd.loadedSwap != _games.GetSwapCount ( ) && ! wnd.IsDirty ( ))
  {
    for (auto& section : wnd.sections)
      section->Load (*pApp);

    wnd.loadedSwap = _games.GetSwapCount ( );
  }

  for (auto& section : wnd.sections)
    section->Validate (*pApp, issues);

  // A window of its own (never merged into the main window), centered on the main window when it appears
  ImGuiWindowClass wc;
  wc.ClassId                  = ImGui::GetID ("PropertiesClass");
  wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
  ImGui::SetNextWindowClass (&wc);

  const std::string windowId = SKIF_Util_FormatStringRaw ("###SKIF_Properties-%d", wnd.id);
  const float       fDPI     = SKIF_ImGui_GetDPIScaleForWindow (windowId.c_str ( ));

  // Further windows open a bit offset, so they don't cover each other entirely
  extern ImRect windowRect;
  const float   fOffset = static_cast <float> ((wnd.id % 5) * 24) * fDPI;

  // Room for the spacing above and below the Apply / Close buttons (two SKIF_ImGui_Spacing ( ), each a quarter of a line
  //   plus the item spacing; see the footer), at 100%, so the pages keep the height they had before it was added
  const float   fFooterSpacing = 2.0f * (ImGui::GetTextLineHeightWithSpacing ( ) * 0.25f + ImGui::GetStyle ( ).ItemSpacing.y) / SKIF_ImGui_GlobalDPIScale;

  // Centered on the main window, but kept within the work area of its monitor (smaller if needed) on small displays
  const ImVec2  vCenter        = windowRect.GetCenter ( ) + ImVec2 (fOffset, fOffset);

  // 960 px: tall enough for the pages of most games without a scrollbar
  SKIF_ImGui_SetNextWindowCentered    (vCenter, ImVec2 (1030.0f, 960.0f + fFooterSpacing) * fDPI, ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints (SKIF_ImGui_FitSizeToWorkArea (vCenter, ImVec2 (720.0f, 460.0f + fFooterSpacing) * fDPI), ImVec2 (FLT_MAX, FLT_MAX));

  if (wnd.focusRequest)
  {
    ImGui::SetNextWindowFocus ( );

    if (wnd.hwnd != nullptr)
    {
      if (IsIconic (wnd.hwnd))
        ShowWindow (wnd.hwnd, SW_RESTORE);

      SetForegroundWindow (wnd.hwnd);
    }

    wnd.focusRequest = false;
  }

  const bool        anyDirty = wnd.IsDirty ( );

  // The app is being exited from an INI editor (Ctrl+Q, or its own prompt) or another Properties window drawn before this
  //   one: as these windows are drawn after the INI editors, this sees it the same frame, before the main window acts on
  //     it next frame. With unsaved changes, ask first, like the INI editor does
  extern bool bKeepWindowAlive;

  if (! bKeepWindowAlive && anyDirty)
  {
    bKeepWindowAlive = true;
    wnd.pending      = properties_window_s::PendingAction::Exit;
    wnd.promptShow   = true;
    wnd.focusRequest = true; // Bring the window forward (next frame), so the prompt is seen
  }

  // TRANSLATORS: Title of the Properties window; {0} is the name of the game
  const std::string title    = F_("{0} - Properties", pApp->names.normal) + windowId;

  bool open   = true;
  bool closed = false;

  if (ImGui::Begin (title.c_str ( ), &open, ImGuiWindowFlags_NoCollapse      |
                                            ImGuiWindowFlags_NoScrollbar     |
                                            ImGuiWindowFlags_NoDocking       |
                                            ImGuiWindowFlags_NoSavedSettings |
                                          ((anyDirty) ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None))) // Unsaved changes: a dot next to the close button
  {
    if (ImGui::GetWindowViewport ( )->PlatformHandleRaw != nullptr)
      wnd.hwnd = (HWND)ImGui::GetWindowViewport ( )->PlatformHandleRaw;

    // Hotkeys (include/utility/hotkeys.h), with Shortcut ( ): only while this window (or a child of it) has the focus; not
    //   while one of its combo boxes or menus, or its Save Changes prompt, has it. An active text field keeps Escape (it
    //     cancels the edit first). Each Shortcut ( ) is called every frame, as that is what keeps its route registered
    const bool hotkeyClose  = ImGui::Shortcut (SKIF_Hotkey_CloseWindow);
    const bool hotkeyEscape = ImGui::Shortcut (ImGuiKey_Escape);

    // Closing the window: the Close button, the title bar's close button, Escape (unless it closes a combo box or menu
    //   of ours first) or Ctrl+W. With unsaved changes the user is asked first (see below)
    bool closeRequest = ! open;
    open              = true;

    if (hotkeyEscape && ! ImGui::IsPopupOpen (0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
      closeRequest = true;

    if (hotkeyClose)
      closeRequest = true;

    // Ctrl+Q exits the app, as from the main window, but asks about unsaved changes first: this focused route takes priority
    //   over the main window's global one (SKIF.cpp, "Hotkeys: App"). Asked right away: the main window would otherwise
    //     exit the app next frame, before this window gets to the check above Begin ( )
    if (ImGui::Shortcut (SKIF_Hotkey_Quit))
    {
      if (anyDirty)
      {
        wnd.pending    = properties_window_s::PendingAction::Exit;
        wnd.promptShow = true;
      }

      else
        bKeepWindowAlive = false;
    }

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

    // The footer below the pages: up to three lines of warnings / errors, then the buttons with spacing above and below
    //   them (as in the updater's window)
    const ImGuiStyle& style      = ImGui::GetStyle ( );
    const int         issueLines = std::min (static_cast <int> (issues.size ( )), 3);
    const float       fIssuesH   = (issueLines > 0) ? issueLines * ImGui::GetTextLineHeightWithSpacing ( ) + style.ItemSpacing.y : 0.0f;
    const float       fSpacingH  = ImGui::GetTextLineHeightWithSpacing ( ) * 0.25f + style.ItemSpacing.y; // One SKIF_ImGui_Spacing ( ) (a quarter of a line)
    const float       fFooterH   = fIssuesH + fSpacingH * 2.0f + ImGui::GetFrameHeightWithSpacing ( ) + style.ItemSpacing.y * 2.0f;

    // The content is limited to a readable width and centered, so a large (maximized) window doesn't stretch it all the way.
    //   Its width and position don't depend on the page's scrollbar: room for the scrollbar is always kept free on both
    //   sides, so the content never moves when the scrollbar appears or disappears.
    const float fMaxWidth     = 960.0f * SKIF_ImGui_GlobalDPIScale;
    const float fAreaWidth    = ImGui::GetContentRegionAvail ( ).x;
    const float fContentWidth = std::max (1.0f, std::min (fMaxWidth, fAreaWidth - style.ScrollbarSize * 2.0f));
    const float fContentX     = ImGui::GetCursorPosX ( ) + (fAreaWidth - fContentWidth) / 2.0f;

    // Everything on one page, in the order of the pages: each page after the first gets a heading.
    //   The scrolling area spans the window (scrollbar at its edge); the centered column inside it grows with its content.
    ImGui::BeginChild ("###PropsContent", ImVec2 (fAreaWidth, -fFooterH), ImGuiChildFlags_None);

    // Relative to the scrolling area, which starts where the column's offset was measured from
    ImGui::SetCursorPosX (ImGui::GetCursorPosX ( ) + (fAreaWidth - fContentWidth) / 2.0f);
    ImGui::BeginChild    ("###PropsColumn", ImVec2 (fContentWidth, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollWithMouse); // The mouse wheel scrolls the page

    SKIF_ImGui_Spacing  ( );
    SKIF_ImGui_Spacing  ( );

    // A bit more room between the rows, so they don't feel cramped
    ImGui::PushStyleVar (ImGuiStyleVar_ItemSpacing, ImVec2 (style.ItemSpacing.x, style.ItemSpacing.y + 6.0f * SKIF_ImGui_GlobalDPIScale));

    bool           firstSection = true;
    PropertiesPage lastPage     = PropertiesPage::General;
    float          scrollY      = -1.0f; // Screen position of the start of the requested page (SKIF_Properties_RequestPage ( ))

    for (auto& section : wnd.sections)
    {
      const PropertiesPage page = section->GetPage ( );

      if (wnd.scrollFrames > 0 && scrollY < 0.0f && page == wnd.scrollPage && (firstSection || page != lastPage))
        scrollY = ImGui::GetCursorScreenPos ( ).y;

      if (! firstSection && page != lastPage)
      {
        const char* pageName =
          (page == PropertiesPage::Launch)     ? _("Launch options") :
          (page == PropertiesPage::SpecialK)   ? "Special K"         :
          (page == PropertiesPage::Technology) ? _("Technology")     : pApp->store_utf8.c_str ( );

        SKIF_ImGui_Spacing ( );
        ImGui::SeparatorText (pageName);
        SKIF_ImGui_Spacing ( );
      }

      firstSection = false;
      lastPage     = page;

      ImGui::PushID (section.get ( ));

      const char* sectionTitle = section->GetTitle ( );

      if (sectionTitle != nullptr && sectionTitle [0] != '\0')
        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption), "%s", sectionTitle);

      section->Draw (*pApp);

      ImGui::PopID ( );

      SKIF_ImGui_Spacing ( );
    }

    ImGui::PopStyleVar ( ); // ImGuiStyleVar_ItemSpacing

    ImGui::EndChild ( ); // ###PropsColumn

    // Scroll the content area to the requested page (relative to the window, as SetScrollFromPosY ( ) expects)
    if (wnd.scrollFrames > 0)
    {
      if (scrollY >= 0.0f)
        ImGui::SetScrollFromPosY (scrollY - ImGui::GetWindowPos ( ).y, 0.0f);

      wnd.scrollFrames--;
    }

    ImGui::EndChild ( ); // ###PropsContent

    // Warnings and errors of all sections (errors block saving)
    bool hasErrors = false;

    for (const auto& issue : issues)
      hasErrors = hasErrors || (issue.level == property_issue_s::Level::Error);

    // Pin the footer to the bottom of the window
    ImGui::SetCursorPosY (ImGui::GetWindowHeight ( ) - style.WindowPadding.y - fFooterH + style.ItemSpacing.y);

    if (issueLines > 0)
    {
      // Lined up with the content above
      ImGui::SetCursorPosX (fContentX);
      ImGui::BeginChild    ("###PropsIssues", ImVec2 (fContentWidth, fIssuesH - style.ItemSpacing.y), ImGuiChildFlags_None);

      for (const auto& issue : issues)
      {
        const bool isError = (issue.level == property_issue_s::Level::Error);

        ImGui::TextColored (ImGui::GetStyleColorVec4 ((isError) ? ImGuiCol_SKIF_Failure : ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::SameLine    ( );
        ImGui::TextWrapped ("%s", issue.message.c_str ( ));
      }

      ImGui::EndChild ( );
    }

    // Both as wide as the wider of the two (at least as wide as before)
    ImVec2 vButtonSize = ImVec2 (std::max ({ 80.0f * SKIF_ImGui_GlobalDPIScale,
                                             ImGui::CalcTextSize (L_("Apply###PropsApply"), nullptr, true).x + style.FramePadding.x * 2.0f,
                                             ImGui::CalcTextSize (L_("Close###PropsClose"), nullptr, true).x + style.FramePadding.x * 2.0f }), 0.0f);

    const bool applyDisabled = (! anyDirty || hasErrors);

    SKIF_ImGui_Spacing ( );

    // Apply / Close, centered
    ImGui::SetCursorPosX ((ImGui::GetWindowWidth ( ) - vButtonSize.x * 2.0f - style.ItemSpacing.x) / 2.0f);

    // Applies the changes of every section; false (with the reasons shown as toasts in the window) if something was not saved
    auto _Save = [&](void) -> bool
    {
      // Saving is not possible while there are errors (the Apply button is disabled then, but the prompt can still ask)
      if (hasErrors)
      {
        for (const auto& issue : issues)
          if (issue.level == property_issue_s::Level::Error)
            ImGui::InsertNotification ({ ImGuiToastType::Error, 8000, _("Could not save"), "%s", issue.message.c_str ( ) });

        return false;
      }

      uint32_t                 effects = PropertiesApply_None;
      std::vector <std::string> errors;

      for (auto& section : wnd.sections)
      {
        if (! section->IsDirty ( ))
          continue;

        const uint32_t ret = section->Apply (*pApp);

        if (ret & PropertiesApply_Failed)
        {
          // Its changes are kept, so saving can be tried again
          std::string error = section->GetApplyError ( );
          errors.push_back ((error.empty ( )) ? std::string (_("Some of the changes could not be saved.")) : error);
        }

        else
        {
          effects |= ret;

          // Saved: what it shows is what is stored now, so it doesn't apply the same changes again on a later Apply
          section->Load (*pApp);
        }
      }

      // Update any locally stored metadata
      if (effects != PropertiesApply_None)
        _games.MetaDB_UpdateApp (pApp, true);

      if (effects & PropertiesApply_Repopulate)
      {
        RepopulateGames = true;
        update          = true;
      }

      // Only for the selected game: another one is loaded again when it is selected
      else if ((effects & PropertiesApply_Reload) && pApp == GetLibraryTab ( ).pApp)
        update = true;

      else if (effects & PropertiesApply_Resort)
      {
        SKIF_GamingCollection::SortApps (&_games.GetApps ( ));
        sort_changed = true;
      }

      // The window stays open on failure, with the error shown as a toast in it
      for (const auto& error : errors)
        ImGui::InsertNotification ({ ImGuiToastType::Error, 8000, _("Could not save"), "%s", error.c_str ( ) });

      return errors.empty ( );
    };

    // Apply saves the changes and keeps the window open: settings that only appear once something is saved (e.g. those of
    //   a new launch option's executable) show up when the library has been refreshed (the sections are loaded again then)
    SKIF_ImGui_PushDisableState (applyDisabled);

    if (ImGui::Button (L_("Apply###PropsApply"), vButtonSize))
      _Save ( );

    SKIF_ImGui_PopDisableState (applyDisabled);

    ImGui::SameLine ( );

    // Close is the same as closing the window (title bar, Escape, Ctrl+W): with unsaved changes the user is asked first
    if (ImGui::Button (L_("Close###PropsClose"), vButtonSize))
      closeRequest = true;

    SKIF_ImGui_Spacing ( );

    ImGui::PopStyleColor ( );

    if (closeRequest)
    {
      if (anyDirty)
      {
        wnd.pending    = properties_window_s::PendingAction::Close;
        wnd.promptShow = true;
      }

      else
        closed = true;
    }

    // Unsaved changes prompt
    if (wnd.promptShow)
    {
      auto _PerformPending = [&](void)
      {
        if (wnd.pending == properties_window_s::PendingAction::Close)
          closed = true;

        else if (wnd.pending == properties_window_s::PendingAction::Exit)
          bKeepWindowAlive = false; // Continue exiting (the next window with unsaved changes asks as well)
      };

      const std::string promptName = std::string (_("Save Changes?")) + SKIF_Util_FormatStringRaw ("###PropsSavePrompt_%d", wnd.id);
      // TRANSLATORS: {0} is the name of a file (or of a game)
      const std::string message    = F_("Do you want to save changes to {0}?", pApp->names.normal);

      switch (SKIF_ImGui_SaveChangesPrompt (promptName.c_str ( ), &wnd.promptShow, message.c_str ( )))
      {
        case DChoice_Save:
          // On failure the window stays open, with the reasons shown as toasts
          if (_Save ( ))
            _PerformPending ( );
          wnd.pending = properties_window_s::PendingAction::None;
          break;

        case DChoice_DontSave:
          _PerformPending ( );
          wnd.pending = properties_window_s::PendingAction::None;
          break;

        case DChoice_Cancel:
          wnd.pending = properties_window_s::PendingAction::None;
          break;
      }
    }

    // Minimize button (on the title bar, next to the close button)
    if (SKIF_ImGui_TitleBarMinimizeButton ( ))
      ShowWindowAsync ((HWND)ImGui::GetWindowViewport ( )->PlatformHandleRaw, SW_MINIMIZE);

    // Toasts of this window only (bound to its platform window)
    ImGui::RenderNotifications ( );
  }

  ImGui::End ( );

  // Closed: Close, or closed (title bar, Escape, Ctrl+W), without unsaved changes / after the prompt
  if (closed)
  {
    pApp->launch_failed = false;
    return false;
  }

  return true;
}

void
SKIF_Properties_Draw (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  // Manage > Properties: a window for the selected game, or the one already open for it
  if (ModifyGamePopup == PopupState_Open)
  {
    app_record_s* pSelected = GetLibraryTab ( ).pApp;

    if (pSelected != nullptr)
    {
      auto existing = std::find_if (s_windows.begin ( ), s_windows.end ( ),
        [&](const std::unique_ptr <properties_window_s>& wnd) { return wnd->game == pSelected->GetKey ( ); });

      if (existing != s_windows.end ( ))
        (*existing)->focusRequest = true;
      else
        existing = s_windows.insert (s_windows.end ( ), _SKIF_Properties_Create (pSelected));

      // Scrolled to a page if one was requested along with the opening, showing what the game uses now
      if (s_requestedPage != -1)
      {
        (*existing)->scrollPage   = static_cast <PropertiesPage> (s_requestedPage);
        (*existing)->scrollFrames = 3;

        for (auto& section : (*existing)->sections)
          section->ShowDefault ( );
      }
    }
  }

  // Closed from elsewhere
  else if (ModifyGamePopup == PopupState_Closed)
    s_windows.clear ( );

  // A request only applies to the opening it was made with
  s_requestedPage = -1;

  for (auto it = s_windows.begin ( ); it != s_windows.end ( ); )
  {
    // The game the window is for (records are replaced when the library is refreshed); closed if it is gone
    app_record_s* pApp = _games.Find ((*it)->game);

    if (pApp == nullptr || ! _SKIF_Properties_DrawWindow (**it, pApp))
      it = s_windows.erase (it);
    else
      ++it;
  }

  // Opened while any window is
  ModifyGamePopup = (s_windows.empty ( )) ? PopupState_Closed : PopupState_Opened;
}

#pragma endregion
