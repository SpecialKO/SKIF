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

static const char*
_InstantPlayName (int value)
{
  return (value == 1) ? _("Always") : (value == 2) ? _("Never") : _("Default");
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
    SKIF_Properties_Label   (_("Name"), _("Leave empty to use the name of the game."));
    ImGui::SetNextItemWidth (-FLT_MIN);
    ImGui::InputTextWithHint ("###PropsName", hint.c_str ( ), name, MAX_PATH);
    SKIF_ImGui_BlockDragMoveWhenItemActive ( );

    // Library
    SKIF_Properties_Label   (_("Library"));
    SKIF_ImGui_Checkbox         (L_("Favorited###PropsFavorite"), &favorite);
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

// How the game is launched (General page, below the library settings):
//   Instant Play: the game's setting (db.json), used by the launch options the platform client can start itself
//   Auto-stop:    the injection service's behavior, stored for the install folder in the registry (SKIF_LaunchBehaviors)
class PropertiesSection_LaunchBehavior : public IPropertySection
{
public:
  const char*    GetTitle (void) const override { return ""; }
  PropertiesPage GetPage  (void) const override { return PropertiesPage::General; }
  int            GetOrder (void) const override { return 20; }

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

    ImGui::Separator   ( );
    SKIF_ImGui_Spacing ( );

    // Instant Play (the game's setting)
    if (instantPlayCompatible)
    {
      SKIF_Properties_Label  (_("Instant Play"), _("Used by the launch options of the game that the platform client can start itself.\n"
                                              "Other launch options always use Instant Play."));

      // TRANSLATORS: {0} is what the default is: "Always" or "Never"
      SKIF_ImGui_RadioButton (( F_("Default ({0})", (instantPlayGlobal) ? _("Always") : _("Never")) + "###PropsIPDefault").c_str ( ), &instantPlay, 0);
      SKIF_ImGui_SetHoverTip (_("The game will use the default behavior configured in the Settings tab."));
      SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Never###PropsIPNever")));
      SKIF_ImGui_RadioButton     (L_("Never###PropsIPNever"),   &instantPlay, 2);
      SKIF_ImGui_SetHoverTip (_("The game will never use instant play except\nwhen launched through the right click menu."));
      SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Always###PropsIPAlways")));
      SKIF_ImGui_RadioButton     (L_("Always###PropsIPAlways"), &instantPlay, 1);
      SKIF_ImGui_SetHoverTip (_("The game will always use instant play except\nwhen launched through the right click menu."));
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning), ICON_FA_TRIANGLE_EXCLAMATION);
      SKIF_ImGui_SetHoverTip (_("Warning: This skips the regular platform launch process for the game,\n"
                              "including steps like the cloud saves synchronization that usually occurs."));
    }

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
        svc.blockedPattern = _inject._TestUserList (lc.getExecutableFullPathUTF8 ( ).c_str ( ), false);

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
      _Duplicate (selected, 0, false);
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

    const bool isDefault = (selected == defaultRow);

    if (ImGui::Button ((isDefault) ? L_("Clear default###PropsLaunchDefault") : LI_(ICON_FA_STAR, "Set as default###PropsLaunchDefault")))
    {
      defaultRow  = (isDefault) ? -1 : selected;
      keepMissing = false;
    }
    SKIF_ImGui_SetHoverTip ((isDefault) ? _("SKIF goes back to using the primary launch option of the game.")
                                        : _("SKIF uses this launch option when the game is launched through the Play button."));

    ImGui::SameLine ( );

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
    const int   effectiveDefault = (defaultRow != -1) ? defaultRow : primaryRow;

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
        SKIF_ImGui_SetHoverTip ((defaultRow != -1) ? _("The default launch option.") : _("The default launch option (the primary one, as no other is set)."));
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

      if (! svc.blocked && svc.elevated != svc.elevatedOrig)
        lc.setElevated (svc.elevated);
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

  // Special K settings of an executable (as in the details panel), applied through a launch config that starts it
  struct service_s {
    int          configId       = -1;
    std::wstring path;
    bool         blocked        = false,
                 blockedOrig    = false;
    bool         elevated       = false,
                 elevatedOrig   = false;
    bool         blockedPattern = false; // Blocked through a blacklist pattern in the Settings tab
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
      if (svc.blocked != svc.blockedOrig || (! svc.blocked && svc.elevated != svc.elevatedOrig))
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
      SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Save first to change these for this executable."));
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

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    // A blocked executable does not use the service, so it cannot be elevated either
    const bool elevateDisabled = (svc.blocked || svc.blockedPattern);

    bool elevated = (svc.elevated && ! elevateDisabled);

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
      label = _("Launch option");

    row.user.desc = _UniqueDescription ((forInstantPlay)
                                          // TRANSLATORS: {0} is the name of the launch option that was copied
                                          ? F_("{0} (Instant Play)", label)
                                          // TRANSLATORS: {0} is the name of the launch option that was copied
                                          : F_("{0} (copy)", label));

    rows.push_back (row);

    const int newIndex = static_cast <int> (rows.size ( )) - 1;

    selected = newIndex;
    pickError.clear ( );

    if (! forInstantPlay)
      note.clear ( );

    return newIndex;
  }

  // Instant Play on a platform / lc.json launch option: those are read-only, so an editable copy is made instead
  // The launch options set in the store client (Steam) as plain arguments: launching a platform config adds them, but
  //   lc_user.json configs never get them, so an Instant Play copy carries them in its own arguments instead.
  //   With %command%, only what follows it can be kept (dropped: what precedes it, e.g. another program).
  static std::string _FoldClientOptions (const std::string& options, std::string& dropped)
  {
    auto _Trim = [](std::string str) -> std::string
    {
      const size_t first = str.find_first_not_of (" \t");
      const size_t last  = str.find_last_not_of  (" \t");
      return (first == std::string::npos) ? "" : str.substr (first, last - first + 1);
    };

    dropped.clear ( );

    std::string lower = options;
    std::transform (lower.begin ( ), lower.end ( ), lower.begin ( ), [](unsigned char c) { return static_cast <char> (std::tolower (c)); });

    const std::string command = "%command%";
    const size_t      pos     = lower.find (command);

    if (pos == std::string::npos)
      return _Trim (options);

    dropped = _Trim (options.substr (0, pos));

    // SKIF %COMMAND% (the launcher mode of SKIF itself) is not needed, as SKIF does the launching
    if (lower.rfind ("skif", 0) == 0 && (dropped.length ( ) == 4 || std::isspace (static_cast <unsigned char> (dropped [4]))))
      dropped.clear ( );

    return _Trim (options.substr (pos + command.length ( )));
  }

  void _InstantPlayCopy (int index)
  {
    const row_s       src   = rows [index];
    const std::string label = _RowLabel (src);

    // Only launches of the game's own launch configs get the client's launch options
    std::string dropped,
                folded  = (src.source == app_record_s::LaunchSource::Platform && ! clientOptions.empty ( ))
                        ? _FoldClientOptions (clientOptions, dropped) : "";

    std::string args    = src.args;

    if (! folded.empty ( ))
      args = (args.empty ( )) ? folded : args + " " + folded;

    // A copy made before
    for (int i = 0; i < static_cast <int> (rows.size ( )); i++)
    {
      const row_s& row = rows [i];

      if (row.source == app_record_s::LaunchSource::User && row.user.instant_play == 1 &&
          row.user.args == args && _SamePath (_UserFullExe (row.user), SK_UTF8ToWideChar (src.fullExe)))
      {
        selected = i;
        // TRANSLATORS: {0} is the name of the copy, {1} the name of the launch option it is a copy of
        note     = F_("\"{0}\" is already a copy of \"{1}\" with Instant Play.", row.user.desc, label);
        return;
      }
    }

    const int  effectiveDefault = (defaultRow != -1) ? defaultRow : primaryRow;
    const bool wasDefault       = (index == effectiveDefault);
    const int  newIndex         = _Duplicate (index, 1, true);

    if (newIndex == -1)
      return;

    rows [newIndex].user.args = args;

    // Otherwise the Play button would keep using the original without Instant Play
    if (wasDefault)
    {
      defaultRow  = newIndex;
      keepMissing = false;
    }

    note = F_("Created an editable copy of \"{0}\" with Instant Play. The original launch option stays unchanged.", label);

    if (wasDefault)
      note += std::string (" ") + _("The copy is now the default launch option.");

    if (! folded.empty ( ))
      note += std::string (" ") + _("The launch options set in the platform client were added to its arguments.");

    // TRANSLATORS: %command% is the placeholder of Steam's launch options, keep it as it is; {0} is what came before it
    if (! dropped.empty ( ))
      note += " " + F_("The part before %command% ({0}) could not be copied.", dropped);
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
    note = (imported.size ( ) == 1) ? F_("Imported \"{0}\" from the clipboard. Save to keep it.", rows [first].user.desc)
                                    : FP_("Imported {0} launch option from the clipboard. Save to keep it.", "Imported {0} launch options from the clipboard. Save to keep them.", imported.size ( ));
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

    return (value != 0) ? _InstantPlayName (value) : (instantPlayGlobal) ? _("Always") : _("Never");
  }

  void _DrawInstantPlayRequired (void)
  {
    SKIF_ImGui_TextColoredWrapped (ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled), _("Always (required)"));
    SKIF_ImGui_SetHoverTip (_("The platform client can't start this launch option itself, so it always uses Instant Play."));
  }

  void _DrawUserRow (app_record_s& app, row_s& row)
  {
    user_launch_config_s& user = row.user;

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

    // Instant Play
    if (instantPlayCompatible)
    {
      SKIF_Properties_Label (_("Instant Play"));

      if (! userClientCanLaunch)
        _DrawInstantPlayRequired ( );

      else
      {
        // TRANSLATORS: {0} is what the game's setting is: "Always" or "Never"
        SKIF_ImGui_RadioButton ((F_("Game setting ({0})", _GameInstantPlayName ( )) + "###PropsLaunchIPDefault").c_str ( ), &user.instant_play, 0);
        SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Never###PropsLaunchIPNever")));
        SKIF_ImGui_RadioButton (L_("Never###PropsLaunchIPNever"),   &user.instant_play, 2);
        SKIF_ImGui_SameLineIfFits (SKIF_ImGui_CheckboxWidth (L_("Always###PropsLaunchIPAlways")));
        SKIF_ImGui_RadioButton (L_("Always###PropsLaunchIPAlways"), &user.instant_play, 1);
      }
    }

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

    // Instant Play: choosing it creates an editable copy (the original is read-only)
    if (instantPlayCompatible)
    {
      SKIF_Properties_Label (_("Instant Play"));

      if (! row.clientCanLaunch)
        _DrawInstantPlayRequired ( );

      else
      {
        if (row.instantPlay != 0)
          // TRANSLATORS: {0} is "Always" or "Never"
          ImGui::TextDisabled ("%s", F_("{0} (set by Special K)", _InstantPlayName (row.instantPlay)).c_str ( ));
        else
          ImGui::TextDisabled ("%s", F_("Game setting ({0})", _GameInstantPlayName ( )).c_str ( ));

        ImGui::SameLine ( );

        const bool disabled = (! userReadable || ! row.duplicateBlocker.empty ( ));

        SKIF_ImGui_PushDisableState (disabled);
        if (ImGui::SmallButton (LI_(ICON_FA_ROCKET, "Use Instant Play###PropsLaunchROInstant")))
          _InstantPlayCopy (index);
        SKIF_ImGui_PopDisableState  (disabled);

        SKIF_ImGui_SetHoverTip ((! row.duplicateBlocker.empty ( )) ? row.duplicateBlocker.c_str ( )
                                : _("Creates an editable copy of this launch option among your own,\n"
                                    "set to always use Instant Play. The original stays unchanged."), false, true);
      }
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

  bool IsDirty (void) const
  {
    for (auto& section : sections)
      if (section->IsDirty ( ))
        return true;

    return false;
  }
};

static std::vector <std::unique_ptr <properties_window_s>> s_windows;

static std::unique_ptr <properties_window_s>
_SKIF_Properties_Create (app_record_s* pApp)
{
  static int nextId = 0;

  auto wnd = std::make_unique <properties_window_s> ( );

  auto launchBehavior = std::make_unique <PropertiesSection_LaunchBehavior> ( );
  auto launchOptions  = std::make_unique <PropertiesSection_LaunchOptions>  (launchBehavior->GetInstantPlay ( ));

  wnd->sections.push_back (std::make_unique <PropertiesSection_General> ( ));
  wnd->sections.push_back (std::move (launchBehavior));
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

  ImGui::SetNextWindowPos             (windowRect.GetCenter ( ) + ImVec2 (fOffset, fOffset), ImGuiCond_Appearing, ImVec2 (0.5f, 0.5f));
  ImGui::SetNextWindowSize            (ImVec2 (1030.0f, 920.0f) * fDPI, ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints (ImVec2 (720.0f, 460.0f) * fDPI, ImVec2 (FLT_MAX, FLT_MAX));

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

    const bool focused = ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows);

    // Keep Ctrl+A (Add Game) from triggering while the window has focus
    if (focused)
      allowShortcutCtrlA = false;

    // Closing the window: the title bar's close button, Escape (unless it closes a combo box or menu of ours first)
    //   or Ctrl+W. With unsaved changes the user is asked first (see below)
    bool closeRequest = ! open;
    open              = true;

    if (focused && ImGui::IsKeyPressed (ImGuiKey_Escape, false) && ! ImGui::IsPopupOpen (0u, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
      closeRequest = true;

    if (focused && ImGui::IsKeyChordPressed (SKIF_Hotkey_CloseWindow))
      closeRequest = true;

    // Ctrl+Q exits the app, as from the main window, but asks about unsaved changes first (the main window's Ctrl+Q is
    //   not handled while a Properties window has the focus, see SKIF_Properties_IsFocused). Asked right away: the main
    //     window would otherwise exit the app next frame, before this window gets to the check above Begin ( )
    if (focused && ImGui::IsKeyChordPressed (SKIF_Hotkey_Quit))
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

    // The footer below the pages: up to three lines of warnings / errors, then the buttons
    const ImGuiStyle& style      = ImGui::GetStyle ( );
    const int         issueLines = std::min (static_cast <int> (issues.size ( )), 3);
    const float       fIssuesH   = (issueLines > 0) ? issueLines * ImGui::GetTextLineHeightWithSpacing ( ) + style.ItemSpacing.y : 0.0f;
    const float       fFooterH   = fIssuesH + ImGui::GetFrameHeightWithSpacing ( ) + style.ItemSpacing.y * 2.0f;

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

    for (auto& section : wnd.sections)
    {
      const PropertiesPage page = section->GetPage ( );

      if (! firstSection && page != lastPage)
      {
        const char* pageName =
          (page == PropertiesPage::Launch)   ? _("Launch options") :
          (page == PropertiesPage::SpecialK) ? "Special K"      : pApp->store_utf8.c_str ( );

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
    ImGui::EndChild ( ); // ###PropsContent

    // Warnings and errors of all sections (errors block Save)
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
                                             ImGui::CalcTextSize (L_("Save###PropsSave"),     nullptr, true).x + style.FramePadding.x * 2.0f,
                                             ImGui::CalcTextSize (L_("Cancel###PropsCancel"), nullptr, true).x + style.FramePadding.x * 2.0f }), 0.0f);

    const bool saveDisabled = (! anyDirty || hasErrors);

    // Save / Cancel, centered
    ImGui::SetCursorPosX ((ImGui::GetWindowWidth ( ) - vButtonSize.x * 2.0f - style.ItemSpacing.x) / 2.0f);

    SKIF_ImGui_PushDisableState (saveDisabled);

    // Applies the changes of every section; false (with the reasons shown as toasts in the window) if something was not saved
    auto _Save = [&](void) -> bool
    {
      // Save is not possible while there are errors (the Save button is disabled then, but the prompt can still ask)
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
          // Its changes are kept, so Save can be tried again
          std::string error = section->GetApplyError ( );
          errors.push_back ((error.empty ( )) ? std::string (_("Some of the changes could not be saved.")) : error);
        }

        else
        {
          effects |= ret;

          // Saved: what it shows is what is stored now, so it doesn't apply the same changes again on a later Save
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

    if (ImGui::Button (L_("Save###PropsSave"), vButtonSize))
    {
      if (_Save ( ))
        closed = true;
    }

    SKIF_ImGui_PopDisableState (saveDisabled);

    ImGui::SameLine ( );

    // Cancel discards the changes without asking: it is the explicit way to do so
    if (ImGui::Button (L_("Cancel###PropsCancel"), vButtonSize))
      closed = true;

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

    // Toasts of this window only (bound to its platform window)
    ImGui::RenderNotifications ( );
  }

  ImGui::End ( );

  // Closed: saved, Cancel, or closed (title bar, Escape, Ctrl+W) without unsaved changes / after the prompt
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
        s_windows.push_back (_SKIF_Properties_Create (pSelected));
    }
  }

  // Closed from elsewhere
  else if (ModifyGamePopup == PopupState_Closed)
    s_windows.clear ( );

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

bool
SKIF_Properties_IsFocused (void)
{
  const HWND hWndFocused = SKIF_ImGui_GetFocusedWindow ( );

  if (hWndFocused == NULL)
    return false;

  for (const auto& wnd : s_windows)
    if (wnd->hwnd == hWndFocused)
      return true;

  return false;
}

#pragma endregion
