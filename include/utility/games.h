#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <stores/generic_library2.h>
#include <nlohmann/json.hpp>

// define character size
#define CHAR_SIZE 128

// A Class representing a Trie node
class Trie
{
public:
  bool  isLeaf                = false;
  Trie* character [CHAR_SIZE] = {   };

  // Constructor
  Trie (void)
  {
    this->isLeaf = false;

    for (int i = 0; i < CHAR_SIZE; i++)
      this->character [i] = nullptr;
  }

  void insert       (        const std::string&);
  bool deletion     (Trie*&, const std::string&);
  bool search       (        const std::string&);
  bool haveChildren (Trie const*);
};

// Helper functions
void InsertTrieKey (std::pair <std::string, app_record_s>* app, Trie* labels);

// Singleton struct
struct SKIF_GamingCollection {
  using AppList = std::vector<std::pair<std::string, app_record_s>>;

  static SKIF_GamingCollection& Get (void)
  {
      static SKIF_GamingCollection instance;
      return instance;
  }
  static void RefreshRunningApps (std::vector <std::pair <std::string, app_record_s> > *apps, bool forced = false);
  static void SortApps           (std::vector <std::pair <std::string, app_record_s> > *apps);
  SKIF_GamingCollection (SKIF_GamingCollection const&) = delete; // Delete copy constructor
  SKIF_GamingCollection (SKIF_GamingCollection&&)      = delete; // Delete move constructor

  std::recursive_mutex& GetMutex() { return m_apps_mutex; }

  const AppList& GetApps() const { return m_apps; }
        AppList& GetApps()       { return m_apps; }


private:
  SKIF_GamingCollection (void);
  Trie           m_tLabels;
  Trie           m_tLabelsFiltered;
  nlohmann::json m_jsonMetaDB;

  ID3D11ShaderResourceView* m_pPatTexSRV;
  ID3D11ShaderResourceView* m_pSKLogoTexSRV;
  ID3D11ShaderResourceView* m_pSKLogoTexSRV_small;
  AppList                   m_apps;
  std::recursive_mutex      m_apps_mutex; // More specifically, this is intended to guard access to the run state of games.
};