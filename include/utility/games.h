#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <stores/generic_library2.h>

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

// Singleton struct
struct SKIF_GamesCollection {

  static SKIF_GamesCollection& GetInstance (void)
  {
      static SKIF_GamesCollection instance;
      return instance;
  }

  SKIF_GamesCollection (SKIF_GamesCollection const&) = delete; // Delete copy constructor
  SKIF_GamesCollection (SKIF_GamesCollection&&)      = delete; // Delete move constructor

private:
  SKIF_GamesCollection (void);
  int  m_iIconWorkers = 0;
  Trie m_tLabels;

  ID3D11ShaderResourceView* m_pPatTexSRV;
  ID3D11ShaderResourceView* m_pSKLogoTexSRV;
  ID3D11ShaderResourceView* m_pSKLogoTexSRV_small;
};

// Helper functions
void InsertTrieKey (std::pair <std::string, app_record_s>* app, Trie* labels);