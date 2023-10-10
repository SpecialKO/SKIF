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

  // Triple-buffer updates so we can go lock-free
  struct snapshot_s {
    std::vector <std::unique_ptr<app_generic_s>>* apps;
    Trie labels;
  } snapshots [3];
  
  std::atomic<int>  snapshot_idx_reading = 0,
                    snapshot_idx_written = 1;
  std::atomic<bool> awake                = false; // Used to protect against sporadic wake-ups

  void                         RefreshGames (void);
  std::vector <std::unique_ptr<app_generic_s>>* GetGames (void);

  static SKIF_GamesCollection& GetInstance (void)
  {
      static SKIF_GamesCollection instance;
      return instance;
  }

  SKIF_GamesCollection (SKIF_GamesCollection const&) = delete; // Delete copy constructor
  SKIF_GamesCollection (SKIF_GamesCollection&&)      = delete; // Delete move constructor

private:
  void LoadCustomGames (std::vector <std::unique_ptr<app_generic_s>>* apps); // Load custom SKIF gmaes
  SKIF_GamesCollection (void);
};

// Helper functions
void InsertTrieKey (std::pair <std::string, app_record_s>* app, Trie* labels);