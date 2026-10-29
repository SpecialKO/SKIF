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

#pragma once

// Properties dialog of a game (Manage > Properties), src/tabs/library/library_properties.cpp
//
//   The dialog is made of sections. When it opens, each section loads a working copy of the settings it edits.
//   Sections only draw and change that copy; nothing is written until Save, when every section with changes applies
//   its copy and reports what the library has to do afterwards (write db.json, resort, repopulate). Cancel simply
//   discards the sections, so no state carries over to the next game.
//
//   The common sections live in library_properties.cpp. Stores add their own through
//   app_record_s::AddPropertySections ( ) (Custom: the executable).

#include <stores/Steam/app_record.h>

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// The pages of the dialog (sidebar); pages without sections are not shown. Today: General and Launch options
enum class PropertiesPage {
  General,
  Launch,
  SpecialK,
  Store
};

// What the library has to do after a section has been applied (combined over all sections, done once)
enum PropertiesApplyEffect : uint32_t {
  PropertiesApply_None        = 0,
  PropertiesApply_WriteMetaDB = 1 << 0, // MetaDB_UpdateApp (db.json)
  PropertiesApply_Resort      = 1 << 1, // SortApps
  PropertiesApply_Repopulate  = 1 << 2, // RepopulateGames (also resorts)
  PropertiesApply_Failed      = 1 << 3, // Nothing was saved (GetApplyError ( ) says why): the window stays open with the changes kept
};

// Shown below the tabs; errors block Save
struct property_issue_s {
  enum class Level {
    Warning,
    Error
  };

  Level       level = Level::Error;
  std::string message;
};

class IPropertySection {
public:
  virtual ~IPropertySection (void) = default;

  virtual const char*    GetTitle (void) const = 0;                 // Caption above the section ("" for none)
  virtual PropertiesPage GetPage  (void) const = 0;                 // The tab the section is on
  virtual int            GetOrder (void) const { return 0; }        // Order within the tab (lower first)

  virtual void           Load     (app_record_s& app) = 0;          // Copies the current values into the working copy
  virtual void           Draw     (app_record_s& app) = 0;          // ImGui; only changes the working copy
  virtual void           Validate (app_record_s& app, std::vector <property_issue_s>& out) { UNREFERENCED_PARAMETER (app); UNREFERENCED_PARAMETER (out); }
  virtual bool           IsDirty  (void) const = 0;                 // The working copy differs from what was loaded
  virtual uint32_t       Apply    (app_record_s& app) = 0;          // Writes the working copy; returns PropertiesApplyEffect flags. Loaded again after a successful Apply
  virtual std::string    GetApplyError (void) const { return ""; }  // Shown as a toast in the window after PropertiesApply_Failed
};

// Layout helpers for sections, so all rows line up: a caption in the label column, then the control column
void  SKIF_Properties_Label             (const char* label, const char* tooltip = nullptr); // Leaves the cursor at the control column, on the same line
float SKIF_Properties_GetLabelWidth     (void);

// File pickers; true if something was picked (out is then set). folder: where the dialog opens (optional)
bool  SKIF_Properties_BrowseForFile     (std::wstring& out, const wchar_t* folder = nullptr); // .exe, .bat and .lnk
bool  SKIF_Properties_BrowseForFolder   (std::wstring& out, const wchar_t* folder = nullptr);

// The windows (library_properties.cpp): one per game, several can be open at once. Setting ModifyGamePopup = PopupState_Open
//   (SKIF.h) opens one for the selected game, or brings its window forward; it is PopupState_Opened while any is open.
//   Called every frame by SKIF.cpp, after the main window.
void  SKIF_Properties_Draw              (void);
