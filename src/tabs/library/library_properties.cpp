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

#include <algorithm>

#pragma region Helpers

float
SKIF_Properties_GetLabelWidth (void)
{
  return 150.0f * SKIF_ImGui_GlobalDPIScale;
}

void
SKIF_Properties_Label (const char* label, const char* tooltip)
{
  const float x = ImGui::GetCursorPosX ( );

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
    SKIF_Util_FileExplorer_BrowseForFile (&pwszFilePath, SKIF_ImGui_hWnd, { { L"Executables", L"*.exe;*.bat;*.lnk" } }, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST, FOLDERID_ComputerFolder, folder);

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
    SKIF_Util_FileExplorer_BrowseForFolder (&pwszFolder, SKIF_ImGui_hWnd, FOLDERID_ComputerFolder, folder);

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

// An icon in a fixed-width slot before a label, so rows with different icons line up
static void
_LeadingIcon (const ImVec4& color, const char* icon, const char* tooltip)
{
  const float x = ImGui::GetCursorPosX ( );

  ImGui::AlignTextToFramePadding ( );
  ImGui::TextColored (color, "%s", icon);

  if (tooltip != nullptr)
    SKIF_ImGui_SetHoverTip (tooltip);

  ImGui::SameLine (x + ImGui::GetFrameHeight ( ) + ImGui::GetStyle ( ).ItemSpacing.x);
}

static const char*
_InstantPlayName (int value)
{
  return (value == 1) ? "Always" : (value == 2) ? "Never" : "Default";
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
    hidden        = hiddenOrig   = app.IsHidden   ( );
    category      = categoryOrig = app.skif.category;
    newCategory [0] = '\0';
  }

  void Draw (app_record_s& app) override
  {
    static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

    UNREFERENCED_PARAMETER (app);

    // Name
    SKIF_Properties_Label   ("Name", "Leave empty to use the name of the game.");
    ImGui::SetNextItemWidth (-FLT_MIN);
    ImGui::InputTextWithHint ("###PropsName", hint.c_str ( ), name, MAX_PATH);
    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    // Library
    SKIF_Properties_Label   ("Library");
    ImGui::Checkbox         ("Favorited###PropsFavorite", &favorite);
    ImGui::SameLine         ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::Checkbox         ("Hidden###PropsHidden",      &hidden);

    // Category (also in the game's Manage > Category menu)
    const bool categoryDisabled = favorite;

    SKIF_Properties_Label   ("Category", "Favorited games are always shown under Favorites.");

    SKIF_ImGui_PushDisableState (categoryDisabled);

    const float fComboWidth = (ImGui::GetContentRegionAvail ( ).x - ImGui::GetStyle ( ).ItemSpacing.x) * 0.5f;

    ImGui::SetNextItemWidth (fComboWidth);
    if (ImGui::BeginCombo   ("###PropsCategory", (category.empty ( )) ? "Games" : category.c_str ( )))
    {
      if (ImGui::Selectable ("Games###PropsCategoryNone", category.empty ( )))
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
        ImGui::Selectable (SKIF_Util_FormatStringRaw ("%s (new)###PropsCategoryNew", category.c_str ( )), true);

      ImGui::EndCombo ( );
    }

    ImGui::SameLine         ( );
    ImGui::SetNextItemWidth (-FLT_MIN);

    if (ImGui::InputTextWithHint ("###PropsNewCategory", "New category...", newCategory, maxCategoryNameLen, ImGuiInputTextFlags_EnterReturnsTrue))
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
      // Pinned on top (51) counts against the limit of those
      if (! favorite && app.skif.pinned > 50)
        numPinnedOnTop--;

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
  bool        hidden       = false,
              hiddenOrig   = false;
  std::string category,
              categoryOrig;
};

#pragma endregion


#pragma region Launch behavior

// How the game is launched, above its launch options:
//   Instant Play: the game's setting (db.json), used by the launch options the platform client can start itself
//   Auto-stop:    the injection service's behavior, stored for the install folder in the registry (SKIF_LaunchBehaviors)
class PropertiesSection_LaunchBehavior : public IPropertySection
{
public:
  const char*    GetTitle (void) const override { return ""; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::Launch; }
  int            GetOrder (void) const override { return 5; }

  // The working copy, for PropertiesSection_LaunchOptions
  const int*     GetInstantPlay (void) const { return &instantPlay; }

  void Load (app_record_s& app) override
  {
    instantPlayCompatible = app.isInstantPlayCompatible ( );
    instantPlayGlobal     = app.GetInstantPlayDefault   ( );
    instantPlay           = instantPlayOrig = app.skif.instant_play;

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

    // Instant Play (the game's setting)
    if (instantPlayCompatible)
    {
      _LeadingIcon           (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION,
                              "Warning: This skips the regular platform launch process for the game,\n"
                              "including steps like the cloud saves synchronization that usually occurs.");
      SKIF_Properties_Label  ("Instant Play:", "Used by the launch options of the game that the platform client can start itself.\n"
                                               "Other launch options always use Instant Play.");

      ImGui::RadioButton     (SKIF_Util_FormatStringRaw ("Default (%s)###PropsIPDefault", (instantPlayGlobal) ? "Always" : "Never"), &instantPlay, 0);
      SKIF_ImGui_SetHoverTip ("The game will use the default behavior configured in the Settings tab.");
      ImGui::SameLine        ( );
      ImGui::RadioButton     ("Never###PropsIPNever",   &instantPlay, 2);
      SKIF_ImGui_SetHoverTip ("The game will never use instant play except\nwhen launched through the right click menu.");
      ImGui::SameLine        ( );
      ImGui::RadioButton     ("Always###PropsIPAlways", &instantPlay, 1);
      SKIF_ImGui_SetHoverTip ("The game will always use instant play except\nwhen launched through the right click menu.");
    }

    // Auto-stop
    _LeadingIcon           (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB,
                            "This determines how long the service will remain running when launching the game.\n"
                            "Move the mouse over each option to get more information.");
    SKIF_Properties_Label  ("Auto-stop:");

    if (folder.empty ( ))
      ImGui::TextDisabled ("Not available for this game.");
    else
      _DrawAutoStop ( );

    SKIF_ImGui_Spacing ( );
    ImGui::Separator   ( );
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

  void _DrawAutoStop (void)
  {
    static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

    using AutoStop = SKIF_LaunchBehaviors::AutoStop;

    // What Default resolves to: a parent folder's setting, else the Settings tab
    std::string defaultLabel =
      (inherited == AutoStop::OnInjection) ? "Default (Inject, parent folder)" :
      (inherited == AutoStop::OnGameExit)  ? "Default (Exit, parent folder)"   :
      (inherited == AutoStop::Never)       ? "Default (Never, parent folder)"  :
      (_registry.iAutoStopBehavior == 1)   ? "Default (Inject)"                : "Default (Exit)";

    ImGui::RadioButton     ((defaultLabel + "###PropsAutoStopDefault").c_str ( ), &value, 0);
    SKIF_ImGui_SetHoverTip ((inherited != AutoStop::Inherit) ? "The service will use the behavior set for a parent folder of the game."
                                                             : "The service will use the default behavior configured in the Settings tab.");
    ImGui::SameLine        ( );
    ImGui::RadioButton     ("On inject###PropsAutoStopInject", &value, 1);
    SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K\nsuccessfully injects into a game.");
    ImGui::SameLine        ( );
    ImGui::RadioButton     ("On exit###PropsAutoStopExit",     &value, 2);
    SKIF_ImGui_SetHoverTip ("The service will be stopped when Special K\ndetects that the game is being closed.");
    ImGui::SameLine        ( );
    ImGui::BeginGroup      ( );
    ImGui::RadioButton     ("Never###PropsAutoStopNever",      &value, 3);
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
    ImGui::EndGroup        ( );
    SKIF_ImGui_SetHoverTip ("Warning: The service will remain even\nafter the game has been closed.");
  }

  std::wstring                   folder;
  int                            value     = 0,
                                 valueOrig = 0;
  SKIF_LaunchBehaviors::AutoStop inherited = SKIF_LaunchBehaviors::AutoStop::Inherit;
};

#pragma endregion


#pragma region Launch options

// Instant Play (game), the launch options of the game (platform, lc.json and lc_user.json), and the default one
class PropertiesSection_LaunchOptions : public IPropertySection
{
public:
  // gameInstantPlay_: the working copy of the game's Instant Play setting (PropertiesSection_LaunchBehavior), shown on the rows that use it
  explicit PropertiesSection_LaunchOptions (const int* gameInstantPlay_) : gameInstantPlay (gameInstantPlay_) { }

  const char*    GetTitle (void) const override { return ""; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::Launch; }
  int            GetOrder (void) const override { return 10; }

  void Load (app_record_s& app) override
  {
    using LaunchSource = app_record_s::LaunchSource;

    rows.clear ( );

    installDir              = app.install_dir;
    instantPlayCompatible   = app.isInstantPlayCompatible ( );
    instantPlayGlobal       = app.GetInstantPlayDefault   ( );
    clientOptions           = app.GetClientLaunchOptions ( );
    note.clear              ( );
    pickError.clear         ( );

    // Whether the store client could start a launch config from lc_user.json (none can today: they always launch instantly)
    app_record_s::launch_config_s userProbe;
    userProbe.custom_user   = true;
    userClientCanLaunch     = app.ClientCanLaunch (userProbe);

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
        row.instantPlay     = lc.instant_play;
        row.clientCanLaunch = app.ClientCanLaunch (lc);
        row.ref             = app.MakeLaunchRef   (lc);

        // A copy in lc_user.json needs an executable inside the install folder
        if (! lc.isExecutableFileNameValid ( ))
          row.duplicateBlocker = "This launch option has no executable that can be started directly.";

        else if (app_record_s::GetPathRelativeTo (installDir, lc.getExecutableFullPath ( )).empty ( ))
          row.duplicateBlocker = "The executable of this launch option is outside of the game's install folder.";

        if (lc.id == primaryId && source == LaunchSource::Platform)
          primaryRow = static_cast <int> (rows.size ( ));

        rows.push_back (row);
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

    // The default launch config
    defaultOrig    = app.skif.default_launch;
    defaultRow     = _FindRow (defaultOrig);
    defaultMissing = (! defaultOrig.empty ( ) && defaultRow == -1);
    keepMissing    = true;

    selected       = (defaultRow != -1) ? defaultRow : 0;
  }

  void Draw (app_record_s& app) override
  {
    using LaunchSource = app_record_s::LaunchSource;

    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption), "Launch options:");

    if (rows.empty ( ))
    {
      ImGui::TextDisabled ("This game has no launch options.");
      return;
    }

    if (selected < 0 || selected >= static_cast <int> (rows.size ( )))
      selected = 0;

    const int   effectiveDefault = (defaultRow != -1) ? defaultRow : primaryRow;
    // Fill the page: the list, the two button rows below it, and room for two lines of notes
    const float fHeight          = std::max (150.0f * SKIF_ImGui_GlobalDPIScale,
                                             ImGui::GetContentRegionAvail ( ).y - ImGui::GetFrameHeightWithSpacing ( ) * 2.0f
                                                                                - ImGui::GetTextLineHeightWithSpacing ( ) * 2.0f
                                                                                - ImGui::GetStyle ( ).ItemSpacing.y * 2.0f);
    const float fListWidth       = 250.0f * SKIF_ImGui_GlobalDPIScale;

    // The list
    ImGui::BeginGroup ( );

    ImGui::BeginChild ("###PropsLaunchList", ImVec2 (fListWidth, fHeight), ImGuiChildFlags_Borders);

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

      if (ImGui::Selectable (SKIF_Util_FormatStringRaw ("%s%s###PropsLaunchRow-%d", (i == effectiveDefault) ? ICON_FA_STAR "  " : "", (label.empty ( )) ? "<no description>" : label.c_str ( ), i), selected == i))
      {
        if (selected != i)
          pickError.clear ( );

        selected = i;
      }

      if (i == effectiveDefault)
        SKIF_ImGui_SetHoverTip ((defaultRow != -1) ? "The default launch option." : "The default launch option (the primary one, as no other is set).");
    }

    ImGui::EndChild ( );

    // Buttons below the list
    const bool   isUser      = (rows [selected].source == LaunchSource::User);
    const ImVec2 vButtonSize = ImVec2 ((fListWidth - ImGui::GetStyle ( ).ItemSpacing.x * 2.0f) / 3.0f, 0.0f);

    SKIF_ImGui_PushDisableState (! userReadable);
    if (ImGui::Button ("Add###PropsLaunchAdd", vButtonSize))
      _AddUserRow (app);
    SKIF_ImGui_PopDisableState  (! userReadable);
    SKIF_ImGui_SetHoverTip ("Adds a new launch option of your own (lc_user.json).");

    ImGui::SameLine ( );

    const bool canDuplicate = (userReadable && (isUser || rows [selected].duplicateBlocker.empty ( )));

    SKIF_ImGui_PushDisableState (! canDuplicate);
    if (ImGui::Button ("Duplicate###PropsLaunchDuplicate", vButtonSize))
      _Duplicate (selected, 0, false);
    SKIF_ImGui_PopDisableState  (! canDuplicate);
    SKIF_ImGui_SetHoverTip ((canDuplicate || ! userReadable) ? "Creates an editable copy of the selected launch option." : rows [selected].duplicateBlocker.c_str ( ), false, true);

    ImGui::SameLine ( );

    SKIF_ImGui_PushDisableState (! isUser);
    if (ImGui::Button ("Remove###PropsLaunchRemove", vButtonSize))
      _RemoveRow (selected);
    SKIF_ImGui_PopDisableState  (! isUser);
    SKIF_ImGui_SetHoverTip ((isUser) ? "Removes the selected launch option." : "Launch options of the platform and Special K can't be removed.", false, true);

    // The last launch option was removed
    if (rows.empty ( ))
    {
      ImGui::EndGroup ( );
      return;
    }

    const bool   isDefault   = (selected == defaultRow);
    const ImVec2 vDefaultBtn = ImVec2 (fListWidth - vButtonSize.x - ImGui::GetStyle ( ).ItemSpacing.x, 0.0f); // The two arrows take up the width of one button

    if (ImGui::Button ((isDefault) ? "Clear default###PropsLaunchDefault" : ICON_FA_STAR "  Set as default###PropsLaunchDefault", vDefaultBtn))
    {
      defaultRow  = (isDefault) ? -1 : selected;
      keepMissing = false;
    }
    SKIF_ImGui_SetHoverTip ((isDefault) ? "SKIF goes back to using the primary launch option of the game."
                                        : "SKIF uses this launch option when the game is launched through the Play button.");

    ImGui::SameLine ( );

    const bool canUp   = (isUser && selected > 0                                     && rows [selected - 1].source == LaunchSource::User);
    const bool canDown = (isUser && selected < static_cast <int> (rows.size ( )) - 1 && rows [selected + 1].source == LaunchSource::User);

    SKIF_ImGui_PushDisableState (! canUp);
    if (ImGui::Button (ICON_FA_CHEVRON_UP "###PropsLaunchUp", ImVec2 ((vButtonSize.x - ImGui::GetStyle ( ).ItemSpacing.x) / 2.0f, 0.0f)))
      _MoveRow (selected, -1);
    SKIF_ImGui_PopDisableState  (! canUp);

    ImGui::SameLine ( );

    SKIF_ImGui_PushDisableState (! canDown);
    if (ImGui::Button (ICON_FA_CHEVRON_DOWN "###PropsLaunchDown", ImVec2 ((vButtonSize.x - ImGui::GetStyle ( ).ItemSpacing.x) / 2.0f, 0.0f)))
      _MoveRow (selected, +1);
    SKIF_ImGui_PopDisableState  (! canDown);

    ImGui::EndGroup ( );

    ImGui::SameLine ( );

    // The selected launch option
    ImGui::BeginChild ("###PropsLaunchEditor", ImVec2 (0.0f, fHeight + ImGui::GetFrameHeightWithSpacing ( ) * 2.0f), ImGuiChildFlags_None);

    if (rows [selected].source == LaunchSource::User)
      _DrawUserRow     (app, rows [selected]);
    else
      _DrawReadOnlyRow (app, selected);

    ImGui::EndChild ( );

    if (! note.empty ( ))
    {
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info));
      ImGui::TextWrapped    ("%s", note.c_str ( ));
      ImGui::PopStyleColor  ( );
    }

    // The default launch option is gone (removed, or hidden by its Steam branch / DLC)
    if (defaultMissing && keepMissing && defaultRow == -1)
    {
      ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine    ( );
      ImGui::TextWrapped ("The default launch option (%s) is not available, so the primary one is used.", (defaultOrig.desc.empty ( )) ? defaultOrig.exe.c_str ( ) : defaultOrig.desc.c_str ( ));
      ImGui::SameLine    ( );
      if (ImGui::SmallButton ("Clear###PropsLaunchClearMissing"))
        keepMissing = false;
    }
  }

  void Validate (app_record_s& app, std::vector <property_issue_s>& out) override
  {
    UNREFERENCED_PARAMETER (app);

    using Level = property_issue_s::Level;

    if (! userReadable)
      out.push_back ({ Level::Warning, "lc_user.json could not be read, so your own launch options can't be changed here. Fix or remove the file to edit them." });

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
        out.push_back ({ Level::Error, "More than one launch option is called \"" + label + "\"." });

      const std::string& name = (label.empty ( )) ? std::string ("<no description>") : label;

      if (label.empty ( ))
        out.push_back ({ Level::Error, "Every launch option of your own needs a description." });

      if (row.user.exe.empty ( ))
        out.push_back ({ Level::Error, "\"" + name + "\" has no executable." });

      else if (! PathIsRelativeW (SK_UTF8ToWideChar (row.user.exe).c_str ( )) || row.user.exe.rfind ("..", 0) == 0)
        out.push_back ({ Level::Error, "The executable of \"" + name + "\" must be inside the install folder of the game." });

      else if (! PathFileExistsW (_UserFullExe (row.user).c_str ( )))
        out.push_back ({ Level::Warning, "The executable of \"" + name + "\" was not found." });

      if (! row.user.dir.empty ( ) && (! PathIsRelativeW (SK_UTF8ToWideChar (row.user.dir).c_str ( )) || row.user.dir.rfind ("..", 0) == 0))
        out.push_back ({ Level::Error, "The working directory of \"" + name + "\" must be inside the install folder of the game." });
    }
  }

  bool IsDirty (void) const override
  {
    return (_CollectUsers ( )    != userOrig    ||
            _GetDefaultRef ( )   != defaultOrig);
  }

  uint32_t Apply (app_record_s& app) override
  {
    uint32_t ret = PropertiesApply_None;

    std::vector <user_launch_config_s> users = _CollectUsers ( );

    if (users != userOrig)
    {
      // Launch configs are only read when the library is populated
      if (SKIF_UserLaunchConfigs::Set (app, users))
        ret |= PropertiesApply_Repopulate;
      else
        SKIF_ImGui_InfoMessage ("Launch options", "Your launch options could not be saved to lc_user.json.\n\nSee the log for details.");
    }

    app_record_s::launch_ref_s ref = _GetDefaultRef ( );

    if (ref != app.skif.default_launch)
    {
      app.skif.default_launch = ref;
      ret |= PropertiesApply_WriteMetaDB;
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
    int                        instantPlay     = 0;     // The launch config's own value (lc.json "InstantPlay")
    bool                       clientCanLaunch = false;
    std::string                duplicateBlocker;        // Why it can't be copied to lc_user.json (empty if it can)
    app_record_s::launch_ref_s ref;

    // User rows: the lc_user.json entry being edited
    user_launch_config_s       user;
  };

  std::vector <row_s>                rows;           // Platform, SKIF, then User (in lc_user.json order)
  std::vector <user_launch_config_s> userOrig;       // lc_user.json as loaded
  bool                               userReadable          = true;
  bool                               userClientCanLaunch   = false;
  bool                               instantPlayCompatible = false;
  bool                               instantPlayGlobal     = false; // The store's global Instant Play setting (Settings tab)
  int                                selected              =  0;
  int                                primaryRow            = -1;   // The primary platform launch config: the default if no other is set
  int                                defaultRow            = -1;   // The row tagged as the default; -1 = none
  app_record_s::launch_ref_s         defaultOrig;                   // As loaded (db.json "DefaultLaunch")
  bool                               defaultMissing        = false; // defaultOrig is set but matches no row
  bool                               keepMissing           = true;  // Keep defaultOrig while it matches no row (until changed or cleared)
  const int*                         gameInstantPlay       = nullptr; // Owned by PropertiesSection_LaunchBehavior
  std::wstring                       installDir;
  std::string                        clientOptions;                 // Launch options set in the store client (Steam)
  std::string                        note;                          // Shown below the editor (auto-duplicate)
  std::string                        pickError;

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
    row.user.desc = _UniqueDescription ("New launch option");

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

  // Copies a row into a new user row (Duplicate, or Instant Play on a read-only row). Returns the new row, or -1
  int _Duplicate (int index, int instantPlay, bool forInstantPlay)
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
      row.user.instant_play = src.instantPlay;

      // A working directory outside of the install folder can't be stored, so the copy uses the executable's folder
      if (! _MakeUserDir (SK_UTF8ToWideChar (src.workDir), fullExe, row.user.dir))
        row.user.dir.clear ( );
    }

    if (forInstantPlay)
      row.user.instant_play = instantPlay;

    std::string label = _RowLabel (src);
    if (label.empty ( ))
      label = "Launch option";

    row.user.desc = _UniqueDescription (label + ((forInstantPlay) ? " (Instant Play)" : " (copy)"));

    rows.push_back (row);

    const int newIndex = static_cast <int> (rows.size ( )) - 1;

    selected = newIndex;
    pickError.clear ( );

    if (! forInstantPlay)
      note.clear ( );

    return newIndex;
  }

  // Instant Play on a platform / lc.json launch option: those are read-only, so an editable copy is made instead
  void _InstantPlayCopy (int index)
  {
    const row_s       src   = rows [index];
    const std::string label = _RowLabel (src);

    // A copy made before
    for (int i = 0; i < static_cast <int> (rows.size ( )); i++)
    {
      const row_s& row = rows [i];

      if (row.source == app_record_s::LaunchSource::User && row.user.instant_play == 1 &&
          row.user.args == src.args && _SamePath (_UserFullExe (row.user), SK_UTF8ToWideChar (src.fullExe)))
      {
        selected = i;
        note     = "\"" + row.user.desc + "\" is already a copy of \"" + label + "\" with Instant Play.";
        return;
      }
    }

    const int  effectiveDefault = (defaultRow != -1) ? defaultRow : primaryRow;
    const bool wasDefault       = (index == effectiveDefault);
    const int  newIndex         = _Duplicate (index, 1, true);

    if (newIndex == -1)
      return;

    // Otherwise the Play button would keep using the original without Instant Play
    if (wasDefault)
    {
      defaultRow  = newIndex;
      keepMissing = false;
    }

    note = "Created an editable copy of \"" + label + "\" with Instant Play. The original launch option stays unchanged.";

    if (wasDefault)
      note += " The copy is now the default launch option.";

    if (! clientOptions.empty ( ))
      note += " The launch options set in the platform client are not used by the copy.";
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

  // What the game's Instant Play setting resolves to
  const char* _GameInstantPlayName (void) const
  {
    const int value = (gameInstantPlay != nullptr) ? *gameInstantPlay : 0;

    return (value != 0) ? _InstantPlayName (value) : (instantPlayGlobal) ? "Always" : "Never";
  }

  void _DrawInstantPlayRequired (void)
  {
    ImGui::TextDisabled    ("Always (required)");
    SKIF_ImGui_SetHoverTip ("The platform client can't start this launch option itself, so it always uses Instant Play.");
  }

  void _DrawUserRow (app_record_s& app, row_s& row)
  {
    user_launch_config_s& user = row.user;

    SKIF_ImGui_PushDisableState (! userReadable);

    SKIF_Properties_Label ("Description");
    _InputString          ("###PropsLaunchDesc", user.desc, "Shown in the menus");

    // Executable (inside the install folder)
    SKIF_Properties_Label ("Executable", "Relative to the install folder of the game.");

    const float fBrowseWidth = ImGui::CalcTextSize ("Browse...").x + ImGui::GetStyle ( ).FramePadding.x * 2.0f;

    ImGui::BeginGroup     ( );
    ImGui::PushItemWidth  (ImGui::GetContentRegionAvail ( ).x - fBrowseWidth - ImGui::GetStyle ( ).ItemSpacing.x);
    {
      static char buffer [MAX_PATH * 2 + 2] = { };
      strncpy_s (buffer, SK_WideCharToUTF8 (_ToBackslashes (SK_UTF8ToWideChar (user.exe))).c_str ( ), _TRUNCATE);
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      ImGui::InputTextWithHint ("###PropsLaunchExe", "Select an executable", buffer, sizeof (buffer) - 2, ImGuiInputTextFlags_ReadOnly);
      SKIF_ImGui_BlockDragMoveWhenItemActive ( );
      ImGui::PopStyleColor  ( );
    }
    ImGui::PopItemWidth   ( );
    ImGui::SameLine       ( );

    if (ImGui::Button ("Browse...###PropsLaunchBrowseExe"))
    {
      std::wstring picked;

      if (SKIF_Properties_BrowseForFile (picked, installDir.c_str ( )))
      {
        SKIF_Util_ExecutableSelection_s selection =
          SKIF_Util_ResolveExecutableSelection (picked);

        std::wstring rel = (selection.valid) ? app_record_s::GetPathRelativeTo (installDir, selection.path) : L"";

        if (! selection.valid)
          pickError = "Please select an executable (.exe or .bat), or a shortcut to one.";

        else if (rel.empty ( ))
          pickError = "The executable must be inside the install folder of the game.";

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
    SKIF_Properties_Label ("Working directory", "Relative to the install folder of the game.\nLeave it as the executable's folder if unsure.");

    const float fChangeWidth = ImGui::CalcTextSize ("Change...").x + ImGui::GetStyle ( ).FramePadding.x * 2.0f;
    const float fResetWidth  = ImGui::GetFrameHeight ( );

    ImGui::BeginGroup     ( );
    ImGui::PushItemWidth  (ImGui::GetContentRegionAvail ( ).x - fChangeWidth - fResetWidth - ImGui::GetStyle ( ).ItemSpacing.x * 2.0f);
    {
      static char buffer [MAX_PATH * 2 + 2] = { };
      strncpy_s (buffer, (user.dir == ".") ? "(install folder)" : SK_WideCharToUTF8 (_ToBackslashes (SK_UTF8ToWideChar (user.dir))).c_str ( ), _TRUNCATE);
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      ImGui::InputTextWithHint ("###PropsLaunchDir", "(executable folder)", buffer, sizeof (buffer) - 2, ImGuiInputTextFlags_ReadOnly);
      SKIF_ImGui_BlockDragMoveWhenItemActive ( );
      ImGui::PopStyleColor  ( );
    }
    ImGui::PopItemWidth   ( );
    ImGui::SameLine       ( );

    if (ImGui::Button ("Change...###PropsLaunchBrowseDir"))
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
          pickError = "The working directory must be inside the install folder of the game.";
      }
    }

    ImGui::SameLine ( );

    if (ImGui::Button (ICON_FA_XMARK "###PropsLaunchResetDir", ImVec2 (fResetWidth, 0.0f)))
      user.dir.clear ( );
    SKIF_ImGui_SetHoverTip ("Use the folder of the executable.");

    ImGui::EndGroup ( );

    // Arguments
    SKIF_Properties_Label ("Arguments");
    _InputString          ("###PropsLaunchArgs", user.args, "Leave empty if unsure");

    // Instant Play
    if (instantPlayCompatible)
    {
      SKIF_Properties_Label ("Instant Play");

      if (! userClientCanLaunch)
        _DrawInstantPlayRequired ( );

      else
      {
        ImGui::RadioButton (SKIF_Util_FormatStringRaw ("Game setting (%s)###PropsLaunchIPDefault", _GameInstantPlayName ( )), &user.instant_play, 0);
        ImGui::SameLine    ( );
        ImGui::RadioButton ("Never###PropsLaunchIPNever",   &user.instant_play, 2);
        ImGui::SameLine    ( );
        ImGui::RadioButton ("Always###PropsLaunchIPAlways", &user.instant_play, 1);
      }
    }

    SKIF_ImGui_PopDisableState (! userReadable);

    if (! pickError.empty ( ))
    {
      ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine    ( );
      ImGui::TextWrapped ("%s", pickError.c_str ( ));
    }

    // Preview of what is started
    SKIF_ImGui_Spacing ( );

    std::string preview = SK_WideCharToUTF8 (_UserFullExe (user));

    if (! user.args.empty ( ))
      preview += " " + user.args;

    ImGui::TextDisabled ("Starts:");
    ImGui::SameLine     ( );
    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
    ImGui::TextWrapped  ("%s", preview.c_str ( ));
    ImGui::PopStyleColor ( );

    if (! clientOptions.empty ( ))
    {
      ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));
      ImGui::TextWrapped    ("The launch options set in %s (%s) are not used by this launch option.", app.store_utf8.c_str ( ), clientOptions.c_str ( ));
      ImGui::PopStyleColor  ( );
    }
  }

  void _DrawReadOnlyRow (app_record_s& app, int index)
  {
    const row_s& row = rows [index];

    ImGui::TextDisabled ((row.source == app_record_s::LaunchSource::SKIF)
                          ? "Provided by Special K (lc.json). Duplicate it to make changes."
                          : "Provided by the platform. Duplicate it to make changes.");

    SKIF_Properties_Label ("Description");
    _ReadOnlyString       ("###PropsLaunchRODesc", row.label);

    SKIF_Properties_Label ("Executable");
    _ReadOnlyString       ("###PropsLaunchROExe",  row.fullExe);

    SKIF_Properties_Label ("Working directory");
    _ReadOnlyString       ("###PropsLaunchRODir",  row.workDir, "(executable folder)");

    SKIF_Properties_Label ("Arguments");
    _ReadOnlyString       ("###PropsLaunchROArgs", row.args);

    // Instant Play: choosing it creates an editable copy (the original is read-only)
    if (instantPlayCompatible)
    {
      SKIF_Properties_Label ("Instant Play");

      if (! row.clientCanLaunch)
        _DrawInstantPlayRequired ( );

      else
      {
        if (row.instantPlay != 0)
          ImGui::TextDisabled ("%s (set by Special K)", _InstantPlayName (row.instantPlay));
        else
          ImGui::TextDisabled ("Game setting (%s)", _GameInstantPlayName ( ));

        ImGui::SameLine ( );

        const bool disabled = (! userReadable || ! row.duplicateBlocker.empty ( ));

        SKIF_ImGui_PushDisableState (disabled);
        if (ImGui::SmallButton (ICON_FA_ROCKET "  Use Instant Play###PropsLaunchROInstant"))
          _InstantPlayCopy (index);
        SKIF_ImGui_PopDisableState  (disabled);

        SKIF_ImGui_SetHoverTip ((! row.duplicateBlocker.empty ( )) ? row.duplicateBlocker.c_str ( )
                                : "Creates an editable copy of this launch option among your own,\n"
                                  "set to always use Instant Play. The original stays unchanged.", false, true);
      }
    }

    // The client's own launch options (Steam), never edited by SKIF
    if (row.source == app_record_s::LaunchSource::Platform && ! clientOptions.empty ( ))
    {
      SKIF_Properties_Label ("Client options", "Set in the platform client. SKIF does not change these.");
      _ReadOnlyString       ("###PropsLaunchROClient", clientOptions);
    }

    UNREFERENCED_PARAMETER (app);
  }
};

#pragma endregion


#pragma region Special K

// Per launch option: Special K disabled (blocked) and elevated service, as in the details panel
class PropertiesSection_Service : public IPropertySection
{
public:
  const char*    GetTitle (void) const override { return "Injection service per launch option:"; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::SpecialK; }
  int            GetOrder (void) const override { return 10; }

  void Load (app_record_s& app) override
  {
    static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

    rows.clear ( );

    for (auto& it : app.launch_configs)
    {
      app_record_s::launch_config_s& lc = it.second;

      if (! app.IsLaunchConfigAvailable (lc))
        continue;

      row_s row;
      row.configId       = lc.id;
      row.path           = lc.getExecutableFullPath ( );
      row.label          = (! lc.description.empty ( )) ? SK_WideCharToUTF8 (lc.description) : lc.getExecutableFileNameUTF8 ( );
      row.blocked        = row.blockedOrig  = lc.isBlacklisted ( );
      row.elevated       = row.elevatedOrig = lc.isElevated    ( );
      row.blockedPattern = _inject._TestUserList (lc.getExecutableFullPathUTF8 ( ).c_str ( ), false);

      switch (app_record_s::GetLaunchSource (lc))
      {
        case app_record_s::LaunchSource::SKIF: row.label += "  (Special K)"; break;
        case app_record_s::LaunchSource::User: row.label += "  (yours)";     break;
        default:                                                             break;
      }

      rows.push_back (row);
    }
  }

  void Draw (app_record_s& app) override
  {
    static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

    UNREFERENCED_PARAMETER (app);

    if (! _inject.bHasServlet)
    {
      ImGui::TextColored  (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      ImGui::SameLine     ( );
      ImGui::TextDisabled ("Special K is unavailable due to missing files.");
    }

    if (rows.empty ( ))
    {
      ImGui::TextDisabled ("This game has no launch options.");
      return;
    }

    // Two checkbox columns after the names
    const float fNameWidth = std::max (150.0f * SKIF_ImGui_GlobalDPIScale,
                                       ImGui::GetContentRegionAvail ( ).x - (ImGui::CalcTextSize ("Disable Special K").x + ImGui::CalcTextSize ("Elevated service").x) - ImGui::GetFrameHeight ( ) * 2.0f - ImGui::GetStyle ( ).ItemSpacing.x * 6.0f);

    for (int i = 0; i < static_cast <int> (rows.size ( )); i++)
    {
      row_s& row = rows [i];

      ImGui::PushID (i);

      const float x = ImGui::GetCursorPosX ( );

      ImGui::AlignTextToFramePadding ( );
      ImGui::TextUnformatted         (row.label.c_str ( ));
      SKIF_ImGui_SetHoverText        (SK_WideCharToUTF8 (row.path).c_str ( ));

      ImGui::SameLine (x + fNameWidth);

      // Blocked through a pattern in the Settings tab
      bool blocked = (row.blocked || row.blockedPattern);

      SKIF_ImGui_PushDisableState (row.blockedPattern);
      if (ImGui::Checkbox ("Disable Special K###PropsSvcBlocked", &blocked) && ! row.blockedPattern)
        row.blocked = blocked;
      SKIF_ImGui_PopDisableState  (row.blockedPattern);

      if (row.blockedPattern)
        SKIF_ImGui_SetHoverTip ("Special K is disabled through a blacklist pattern in the Settings tab.", true, true);

      ImGui::SameLine ( );
      ImGui::Spacing  ( );
      ImGui::SameLine ( );

      // A blocked executable does not use the service, so it cannot be elevated either
      const bool elevateDisabled = (row.blocked || row.blockedPattern);

      bool elevated = (row.elevated && ! elevateDisabled);

      SKIF_ImGui_PushDisableState (elevateDisabled);
      if (ImGui::Checkbox ("Elevated service###PropsSvcElevated", &elevated) && ! elevateDisabled)
        row.elevated = elevated;
      SKIF_ImGui_PopDisableState  (elevateDisabled);

      if (elevateDisabled)
        SKIF_ImGui_SetHoverTip ("Special K is disabled for this executable, so the service is not used.", true, true);

      ImGui::PopID ( );
    }
  }

  bool IsDirty (void) const override
  {
    for (const auto& row : rows)
      if (row.blocked != row.blockedOrig || (! row.blocked && row.elevated != row.elevatedOrig))
        return true;

    return false;
  }

  uint32_t Apply (app_record_s& app) override
  {
    for (auto& row : rows)
    {
      auto it = app.launch_configs.find (row.configId);

      // Only if it is still the same launch config (the library may have been refreshed meanwhile)
      if (it == app.launch_configs.end ( ) || it->second.getExecutableFullPath ( ) != row.path)
        continue;

      app_record_s::launch_config_s& lc = it->second;

      if (row.blocked != row.blockedOrig)
        lc.setBlacklisted (row.blocked);

      if (! row.blocked && row.elevated != row.elevatedOrig)
        lc.setElevated (row.elevated);
    }

    return PropertiesApply_None;
  }

private:
  struct row_s {
    int          configId       = -1;
    std::wstring path;
    std::string  label;
    bool         blocked        = false,
                 blockedOrig    = false;
    bool         elevated       = false,
                 elevatedOrig   = false;
    bool         blockedPattern = false;
  };

  std::vector <row_s> rows;
};

#pragma endregion


#pragma region Dialog

static struct properties_dialog_s {
  bool                                             active = false;
  app_key_s                                        game;
  std::vector <std::unique_ptr <IPropertySection>> sections;
} s_dialog;

static void
_SKIF_Properties_Close (app_record_s* pApp)
{
  s_dialog.sections.clear ( );
  s_dialog.active = false;

  ModifyGamePopup = PopupState_Closed;

  if (pApp != nullptr)
    pApp->launch_failed = false;
}

static void
_SKIF_Properties_Build (app_record_s* pApp)
{
  s_dialog.sections.clear ( );

  auto launchBehavior = std::make_unique <PropertiesSection_LaunchBehavior> ( );
  auto launchOptions  = std::make_unique <PropertiesSection_LaunchOptions>  (launchBehavior->GetInstantPlay ( ));

  s_dialog.sections.push_back (std::make_unique <PropertiesSection_General>       ( ));
  s_dialog.sections.push_back (std::move (launchBehavior));
  s_dialog.sections.push_back (std::move (launchOptions));

  if (! pApp->launch_configs.empty ( ))
    s_dialog.sections.push_back (std::make_unique <PropertiesSection_Service>     ( ));

  // Store-specific sections (Custom: the executable; Steam: the client's launch options)
  pApp->AddPropertySections (s_dialog.sections);

  std::stable_sort (s_dialog.sections.begin ( ), s_dialog.sections.end ( ),
    [](const std::unique_ptr <IPropertySection>& a, const std::unique_ptr <IPropertySection>& b)
    {
      if (a->GetPage ( ) != b->GetPage ( ))
        return (static_cast <int> (a->GetPage ( )) < static_cast <int> (b->GetPage ( )));

      return (a->GetOrder ( ) < b->GetOrder ( ));
    });

  for (auto& section : s_dialog.sections)
    section->Load (*pApp);

  s_dialog.game   = pApp->GetKey ( );
  s_dialog.active = true;
}

void
SKIF_Properties_Draw (void)
{
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static app_record_s*&         pApp        = GetLibraryTab ( ).pApp;
  static bool&                  update      = GetLibraryTab ( ).update;

  if (ModifyGamePopup == PopupState_Closed)
  {
    // Closed from elsewhere
    if (s_dialog.active)
      _SKIF_Properties_Close (nullptr);

    return;
  }

  if (pApp == nullptr)
  {
    _SKIF_Properties_Close (nullptr);
    return;
  }

  // The edits belong to the game the dialog was opened for: a different selection discards them
  if (s_dialog.active && s_dialog.game != pApp->GetKey ( ))
  {
    _SKIF_Properties_Close (pApp);
    return;
  }

  if (! s_dialog.active)
    _SKIF_Properties_Build (pApp);

  // Validated before drawing, as the footer is sized after the number of issues
  std::vector <property_issue_s> issues;

  for (auto& section : s_dialog.sections)
    section->Validate (*pApp, issues);

  // A fixed size (80% of the main window, within limits), so the tab bar and buttons stay in place between tabs
  const ImVec2 vpSize      = ImGui::GetWindowViewport ( )->Size;
  const ImVec2 vPopupSize  = ImVec2 (
    std::min (vpSize.x * 0.95f, std::max (vpSize.x * 0.8f, 640.0f * SKIF_ImGui_GlobalDPIScale)),
    std::min (vpSize.y * 0.95f, std::max (vpSize.y * 0.8f, 480.0f * SKIF_ImGui_GlobalDPIScale))
  );

  const std::string title =
    SKIF_Util_FormatStringRaw ("Properties - %s###ModifyGamePopup", pApp->names.normal.c_str ( ));

  // Not auto-resizing; the page scrolls instead of the window
  if (SKIF_ImGui_BeginPopup (title.c_str ( ), nullptr, vPopupSize, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
  {
    if (ModifyGamePopup == PopupState_Open)
    {
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###ModifyGamePopup");
      if (window != nullptr && ! window->Appearing)
        ModifyGamePopup = PopupState_Opened;
    }

    // Keep Ctrl+A (Add Game) from triggering while the dialog is open
    allowShortcutCtrlA = false;

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));

    // The footer below the pages: up to three lines of warnings / errors, then the buttons
    const ImGuiStyle& style      = ImGui::GetStyle ( );
    const int         issueLines = std::min (static_cast <int> (issues.size ( )), 3);
    const float       fIssuesH   = (issueLines > 0) ? issueLines * ImGui::GetTextLineHeightWithSpacing ( ) + style.ItemSpacing.y : 0.0f;
    const float       fFooterH   = fIssuesH + ImGui::GetFrameHeightWithSpacing ( ) + style.ItemSpacing.y * 2.0f;

    bool anyDirty = false;

    if (ImGui::BeginTabBar ("###PropsTabs"))
    {
      static constexpr PropertiesPage pages [] = { PropertiesPage::General, PropertiesPage::Launch, PropertiesPage::SpecialK, PropertiesPage::Store };

      for (PropertiesPage page : pages)
      {
        bool hasSections = false,
             pageDirty   = false;

        for (auto& section : s_dialog.sections)
        {
          if (section->GetPage ( ) != page)
            continue;

          hasSections = true;
          pageDirty   = pageDirty || section->IsDirty ( );
        }

        anyDirty = anyDirty || pageDirty;

        if (! hasSections)
          continue;

        const char* pageName =
          (page == PropertiesPage::General)  ? "General"   :
          (page == PropertiesPage::Launch)   ? "Launch"    :
          (page == PropertiesPage::SpecialK) ? "Special K" : pApp->store_utf8.c_str ( );

        // Changed tabs are marked; the ID stays the same
        std::string label = SKIF_Util_FormatStringRaw ("%s%s###PropsTab-%d", pageName, (pageDirty) ? " *" : "", static_cast <int> (page));

        if (ImGui::BeginTabItem (label.c_str ( )))
        {
          // Every page gets the same area
          ImGui::BeginChild ("###PropsPage", ImVec2 (0.0f, -fFooterH), ImGuiChildFlags_None);

          SKIF_ImGui_Spacing ( );

          for (auto& section : s_dialog.sections)
          {
            if (section->GetPage ( ) != page)
              continue;

            ImGui::PushID (section.get ( ));

            const char* sectionTitle = section->GetTitle ( );

            if (sectionTitle != nullptr && sectionTitle [0] != '\0')
              ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption), "%s", sectionTitle);

            section->Draw (*pApp);

            ImGui::PopID ( );

            SKIF_ImGui_Spacing ( );
          }

          ImGui::EndChild ( );

          ImGui::EndTabItem ( );
        }
      }

      ImGui::EndTabBar ( );
    }

    // Warnings and errors of all sections (errors block Save)
    bool hasErrors = false;

    for (const auto& issue : issues)
      hasErrors = hasErrors || (issue.level == property_issue_s::Level::Error);

    // Pin the footer to the bottom of the dialog
    ImGui::SetCursorPosY (ImGui::GetWindowHeight ( ) - style.WindowPadding.y - fFooterH + style.ItemSpacing.y);

    if (issueLines > 0)
    {
      ImGui::BeginChild ("###PropsIssues", ImVec2 (0.0f, fIssuesH - style.ItemSpacing.y), ImGuiChildFlags_None);

      for (const auto& issue : issues)
      {
        const bool isError = (issue.level == property_issue_s::Level::Error);

        ImGui::TextColored (ImGui::GetStyleColorVec4 ((isError) ? ImGuiCol_SKIF_Failure : ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
        ImGui::SameLine    ( );
        ImGui::TextWrapped ("%s", issue.message.c_str ( ));
      }

      ImGui::EndChild ( );
    }

    ImVec2 vButtonSize = ImVec2 (80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    const bool  saveDisabled = (! anyDirty || hasErrors);
    const float fCenterX     = ImGui::GetWindowWidth ( ) / 2.0f;

    ImGui::SetCursorPosX (fCenterX - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    SKIF_ImGui_PushDisableState (saveDisabled);

    bool closed = false;

    if (ImGui::Button ("Save###PropsSave", vButtonSize))
    {
      uint32_t effects = PropertiesApply_None;

      for (auto& section : s_dialog.sections)
        if (section->IsDirty ( ))
          effects |= section->Apply (*pApp);

      // Update any locally stored metadata
      if (effects != PropertiesApply_None)
        _games.MetaDB_UpdateApp (pApp, true);

      if (effects & PropertiesApply_Repopulate)
      {
        RepopulateGames = true;
        update          = true;
      }

      else if (effects & PropertiesApply_Resort)
      {
        SKIF_GamingCollection::SortApps (&_games.GetApps ( ));
        sort_changed = true;
      }

      closed = true;
    }

    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    SKIF_ImGui_PopDisableState (saveDisabled);

    ImGui::SameLine      ( );
    ImGui::SetCursorPosX (fCenterX + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button ("Cancel###PropsCancel", vButtonSize))
      closed = true;

    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    ImGui::PopStyleColor ( );

    if (closed)
    {
      _SKIF_Properties_Close (pApp);
      ImGui::CloseCurrentPopup ( );
    }
  }

  else
    _SKIF_Properties_Close (pApp);

  SKIF_ImGui_EndPopup ( );
}

#pragma endregion
