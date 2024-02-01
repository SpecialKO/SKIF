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

  static SKIF_GamingCollection& GetInstance (void)
  {
      static SKIF_GamingCollection instance;
      return instance;
  }
  static void RefreshRunningApps (std::vector <std::pair <std::string, app_record_s> > *apps);
  static void SortApps (std::vector <std::pair <std::string, app_record_s> > *apps);
  SKIF_GamingCollection (SKIF_GamingCollection const&) = delete; // Delete copy constructor
  SKIF_GamingCollection (SKIF_GamingCollection&&)      = delete; // Delete move constructor

private:
  SKIF_GamingCollection (void);
  Trie           m_tLabels;
  Trie           m_tLabelsFiltered;
  nlohmann::json m_jsonMetaDB;

  ID3D11ShaderResourceView* m_pPatTexSRV;
  ID3D11ShaderResourceView* m_pSKLogoTexSRV;
  ID3D11ShaderResourceView* m_pSKLogoTexSRV_small;
};