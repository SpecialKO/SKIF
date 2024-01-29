#include <utility/games.h>
#include <SKIF.h>
#include <utility/utility.h>
#include "stores/Steam/apps_ignore.h"
#include <utility/fsutil.h>
#include <stores/GOG/gog_library.h>
#include <stores/epic/epic_library.h>
#include <stores/Xbox/xbox_library.h>
#include <stores/SKIF/custom_library.h>

// Registry Settings
#include <utility/registry.h>
#include <stores/Steam/steam_library.h>
#include <regex>

// Stuff
#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>
#include <concurrent_queue.h>

CONDITION_VARIABLE LibRefreshPaused = { };

void SKIF_GamesCollection::LoadCustomGames (std::vector <std::unique_ptr<app_generic_s>> *apps)
{
  PLOG_INFO << "Detecting custom SKIF games v2...";

  HKEY    hKey;
  DWORD   dwIndex = 0, dwResult, dwSize;
  DWORD32 dwData  = 0;
  WCHAR   szSubKey[MAX_PATH];
  WCHAR   szData  [     500];

  extern uint32_t SelectNewSKIFGame;

  /* Load custom titles from registry */
  if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Kaldaien\Special K\Games\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
  {
    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, &dwIndex, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndex > 0)
      {
        dwIndex--;

        dwSize   = sizeof (szSubKey) / sizeof (WCHAR);
        dwResult = RegEnumKeyExW (hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          dwSize = sizeof (DWORD32);
          if (RegGetValueW (hKey, szSubKey, L"ID", RRF_RT_REG_DWORD, NULL, &dwData, &dwSize) == ERROR_SUCCESS)
          {
            app_skif_s record;

            record.id = dwData;
            record._status.installed = true;

            dwSize = sizeof(szData) / sizeof (WCHAR);
            if (RegGetValueW (hKey, szSubKey, L"Name", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
              record.names.normal = SK_WideCharToUTF8 (szData);

            dwSize = sizeof (szData) / sizeof (WCHAR);
            if (RegGetValueW (hKey, szSubKey, L"InstallDir", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
              record.install_dir = szData;

            dwSize = sizeof (szData) / sizeof (WCHAR);
            if (RegGetValueW (hKey, szSubKey, L"ExeFileName", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS) // L"Exe"
            {
              app_generic_s::launch_config_s lc(&record);
              lc.id           = 0;
              lc.valid        = true;
              lc.executable   = szData;
              lc.working_dir  = record.install_dir;

              dwSize = sizeof (szData) / sizeof (WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"Exe", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                lc.executable_path = szData;

              dwSize = sizeof (szData) / sizeof (WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"LaunchOptions", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                lc.launch_options = szData;

              //record.launch_configs.emplace (0, lc);

              /*
              dwSize = sizeof (szData) / sizeof (WCHAR);
              if (RegGetValueW (hKey, szSubKey, L"ExeFileName", RRF_RT_REG_SZ, NULL, &szData, &dwSize) == ERROR_SUCCESS)
                record.specialk.profile_dir = szData;
              */
              record.specialk.profile_dir = lc.executable;

              record.specialk.injection.injection.type = app_generic_s::sk_install_state_s::Injection::Type::Global;

              apps->emplace_back (std::make_unique <app_skif_s>(record));
            }
          }
        }
      }
    }

    RegCloseKey (hKey);
  }
}

SKIF_GamesCollection::SKIF_GamesCollection (void)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );


}

bool
SKIF_GamesCollection::RefreshGames (bool refresh)
{
  UNREFERENCED_PARAMETER(refresh);

  return false;
}

std::vector <std::unique_ptr<app_generic_s>>*
SKIF_GamesCollection::GetGames (void)
{
  static snapshot_s& snapshot = snapshots[0];
  static int lastRead = 0;
  int lastWritten = snapshot_idx_written.load ( );

  if (lastRead != lastWritten)
  {
    lastRead = lastWritten;
    snapshot_idx_reading.store (lastRead);
    snapshot = snapshots[lastRead];
  }

  return snapshot.apps;
}

#pragma region Trie Keyboard Hint Search

// Iterative function to insert a key in the Trie
void
Trie::insert (const std::string& key)
{
  // start from root node
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // create a new node if path doesn't exists
    if (curr->character [key [i]] == nullptr)
        curr->character [key [i]]  = new Trie ();

    // go to next node
    curr = curr->character [key [i]];
  }

  // mark current node as leaf
  curr->isLeaf = true;
}

// Iterative function to search a key in Trie. It returns true
// if the key is found in the Trie, else it returns false
bool
Trie::search (const std::string& key)
{
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // go to next node
    curr = curr->character [key [i]];

    // if string is invalid (reached end of path in Trie)
    if (curr == nullptr)
      return false;
  }

  // if current node is a leaf and we have reached the
  // end of the string, return true
  return curr->isLeaf;
}

// returns true if given node has any children
bool
Trie::haveChildren (Trie const* curr)
{
  for (int i = 0; i < CHAR_SIZE; i++)
    if (curr->character [i])
      return true;  // child found

  return false;
}

// Recursive function to delete a key in the Trie
bool
Trie::deletion (Trie*& curr, const std::string& key)
{
  // return if Trie is empty
  if (curr == nullptr)
    return false;

  // if we have not reached the end of the key
  if (key.length ())
  {
    // recur for the node corresponding to next character in the key
    // and if it returns true, delete current node (if it is non-leaf)

    if (        curr                      != nullptr       &&
                curr->character [key [0]] != nullptr       &&
      deletion (curr->character [key [0]], key.substr (1)) &&
                curr->isLeaf == false)
    {
      if (! haveChildren (curr))
      {
        delete curr;
        curr = nullptr;
        return true;
      }

      else {
        return false;
      }
    }
  }

  // if we have reached the end of the key
  if (key.length () == 0 && curr->isLeaf)
  {
    // if current node is a leaf node and don't have any children
    if (! haveChildren (curr))
    {
      // delete current node
      delete curr;
      curr = nullptr;

      // delete non-leaf parent nodes
      return true;
    }

    // if current node is a leaf node and have children
    else
    {
      // mark current node as non-leaf node (DON'T DELETE IT)
      curr->isLeaf = false;

      // don't delete its parent nodes
      return false;
    }
  }

  return false;
}

void
InsertTrieKey (std::pair <std::string, app_record_s>* app, Trie* labels)
{
  static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance ( );

  std::string all_upper = SKIF_Util_ToUpper (app->first),
              all_upper_alnum;
          
  for (const char c : app->first)
  {
    if (! ( isalnum (c) || isspace (c) ))
      continue;

    all_upper_alnum += (char)toupper (c);
  }

  size_t stripped = 0;

  if (_registry.bLibraryIgnoreArticles)
  {
    static const
      std::string toSkip [] =
      {
        std::string ("A "),
        std::string ("AN "),
        std::string ("THE ")
      };

    for ( auto& skip_ : toSkip )
    {
      if (all_upper_alnum.find (skip_) == 0)
      {
        all_upper_alnum =
          all_upper_alnum.substr (
            skip_.length ()
          );

        stripped = skip_.length ();
        break;
      }
    }
  }

  std::string trie_builder;

  for ( const char c : all_upper_alnum)
  {
    trie_builder += c;

    labels->insert (trie_builder);
  }
        
  app->second.names.normal          = app->first;
  app->second.names.all_upper       = all_upper;
  app->second.names.all_upper_alnum = all_upper_alnum;
  app->second.names.pre_stripped    = stripped;
}

#pragma endregion