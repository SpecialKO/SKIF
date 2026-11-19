#pragma once

#include <cstdint>
#include <string>

struct ICustomDestinationList;
struct IObjectArray;

// The Recent category of SKIF's jump list
//
//   SKIF keeps the items of the category itself (Assets\jumplist.json) and builds the category from them, in the
//     order they were last used, every time the jump list is built (SKIF_Shell_CreateJumpList). This is how Steam
//       does it as well. The Recent list that Windows manages (KDC_RECENT, SHAddToRecentDocs) is not used, as Windows
//         does not move an item of it to the top when it is launched from the jump list, and does not let SKIF do so.
//
//   An item is identified by its arguments, as those are the command line of a launch from the jump list.
//
//   The list is shared by all instances of SKIF (a launcher instance updates it if no other instance is running),
//     so every function reads it from disk and writes it back while it holds a lock shared by them.
namespace SKIF_JumpList
{
  // The time to give an item that is used now (milliseconds since the Unix epoch)
  uint64_t Now (void);

  // Adds the item (or updates the one with the same arguments) and marks it as used at the given time (library launches)
  void     Add  (const std::wstring& title,
                 const std::wstring& path,
                 const std::wstring& arguments,
                 const std::wstring& directory,
                 const std::wstring& icon,
                 uint64_t            used);

  // Marks the item with these arguments as used at the given time (launches from the jump list). Returns false if
  //   there is no such item.
  bool     Touch (const std::wstring& arguments, uint64_t used);

  // Appends the Recent category, with at most the given number of items, to a jump list that is being built (between
  //   ICustomDestinationList::BeginList ( ) and CommitList ( )). Items the user removed from the jump list (returned by
  //     BeginList ( )) are removed from the list for good, as Windows does not accept them again. Requires COM.
  void     AppendRecentCategory (ICustomDestinationList* pDestList, IObjectArray* pRemovedItems, unsigned int maxItems);

  // Held while the jump list is built, so the list and the jump list are changed by one instance at a time
  class Lock
  {
  public:
     Lock (void);
    ~Lock (void);

    Lock            (const Lock&) = delete;
    Lock& operator= (const Lock&) = delete;

  private:
    void* m_hMutex = nullptr;
  };
}
