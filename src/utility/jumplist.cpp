// The Recent category of SKIF's jump list: see utility/jumplist.h

#include <utility/jumplist.h>

#include <SKIF.h>
#include <utility/utility.h>
#include <utility/sk_utility.h>
#include <utility/fsutil.h>
#include <nlohmann/json.hpp>

#include <atlbase.h>
#include <objectarray.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlwapi.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>

// Last, as it defines short macros such as _ ( )
#include <utility/l10n.h>

namespace SKIF_JumpList
{
  // At most this many items are kept (the jump list shows fewer, see AppendRecentCategory ( ))
  static constexpr size_t MaxStoredItems = 25;

  struct Item
  {
    std::wstring title;
    std::wstring path;
    std::wstring arguments; // Trimmed, identifies the item
    std::wstring directory;
    std::wstring icon;
    uint64_t     used = 0;  // Milliseconds since the Unix epoch
  };

  static std::wstring
  _Trimmed (std::wstring str)
  {
    SKIF_Util_TrimSpacesW (str);
    return str;
  }

  static std::wstring
  _GetPath (void)
  {
    static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

    return SK_FormatStringW (LR"(%ws\Assets\jumplist.json)", _path_cache.specialk_userdata);
  }

  // The title of a link (PKEY_Title), or an empty string if it has none
  static std::wstring
  _GetLinkTitle (IShellLinkW* pLink)
  {
    std::wstring title;

    CComQIPtr <IPropertyStore> pPropStore = pLink;
    PROPVARIANT                pv;
    PropVariantInit                (&pv);

    // The title is set as a string (InitPropVariantFromString), so it is read as one (propsys.lib is not linked)
    if (pPropStore != nullptr && SUCCEEDED (pPropStore->GetValue (PKEY_Title, &pv)))
    {
      if (pv.vt == VT_LPWSTR && pv.pwszVal != nullptr)
        title = pv.pwszVal;

      PropVariantClear (&pv);
    }

    return title;
  }

  // The trimmed arguments of a link, or an empty string if they cannot be read
  static std::wstring
  _GetLinkArguments (IShellLinkW* pLink)
  {
    // Arguments can be long (SKIF_Env=), so the buffer is not on the stack
    std::vector <wchar_t> wszArgs (32768, L'\0');

    if (FAILED (pLink->GetArguments (wszArgs.data ( ), static_cast <int> (wszArgs.size ( )))))
      return { };

    return _Trimmed (wszArgs.data ( ));
  }

  // The items of the Recent list Windows kept for SKIF until now, so they are not lost when SKIF starts to use a list
  //   of its own. The order of the list Windows returns is not the order its jump list showed, so they may end up in
  //     a different order. Requires COM.
  static std::vector <Item>
  _ImportFromWindows (void)
  {
    std::vector <Item> items;

    CComPtr <IApplicationDocumentLists> pDocLists;
    CComPtr <IObjectArray>              pList;

    if (FAILED (pDocLists.CoCreateInstance (CLSID_ApplicationDocumentLists))       ||
        FAILED (pDocLists->SetAppID (SKIF_AppUserModelID))                         ||
        FAILED (pDocLists->GetList  (ADLT_RECENT, 0, IID_PPV_ARGS (&pList))))
    {
      PLOG_WARNING << "Could not read the Recent list Windows kept for the jump list, so it is not imported.";
      return items;
    }

    UINT uiCount = 0;
    pList->GetCount (&uiCount);

    const uint64_t now = Now ( );

    std::vector <wchar_t> wszBuffer (32768, L'\0');
    const int             cchBuffer = static_cast <int> (wszBuffer.size ( ));

    for (UINT i = 0; i < uiCount; i++)
    {
      CComPtr <IShellLinkW> pLink;

      // Only links are items of SKIF (others, e.g. files picked in a file dialog, are skipped)
      if (FAILED (pList->GetAt (i, IID_PPV_ARGS (&pLink))))
        continue;

      Item item;
      item.arguments = _GetLinkArguments (pLink);

      if (item.arguments.empty ( ))
        continue;

      item.title = _GetLinkTitle (pLink);
      item.used  = now - i; // Keeps the order of the list

      int iIcon = 0;

      wszBuffer [0] = L'\0';
      if (SUCCEEDED (pLink->GetPath             (wszBuffer.data ( ), cchBuffer, nullptr, SLGP_RAWPATH)))
        item.path      = wszBuffer.data ( );

      wszBuffer [0] = L'\0';
      if (SUCCEEDED (pLink->GetWorkingDirectory (wszBuffer.data ( ), cchBuffer)))
        item.directory = wszBuffer.data ( );

      wszBuffer [0] = L'\0';
      if (SUCCEEDED (pLink->GetIconLocation     (wszBuffer.data ( ), cchBuffer, &iIcon)))
        item.icon      = wszBuffer.data ( );

      items.push_back (std::move (item));
    }

    PLOG_INFO << "Imported " << items.size ( ) << " items from the Recent list Windows kept for the jump list.";

    return items;
  }

  static void
  _Save (std::vector <Item>& items)
  {
    static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

    // Most recently used first, and only the ones that are kept
    std::stable_sort (items.begin ( ), items.end ( ), [](const Item& a, const Item& b) { return a.used > b.used; });

    if (items.size ( ) > MaxStoredItems)
      items.resize (MaxStoredItems);

    nlohmann::json jsonItems = nlohmann::json::array ( );

    for (const Item& item : items)
      jsonItems.push_back ({
        { "title",     SK_WideCharToUTF8 (item.title)     },
        { "path",      SK_WideCharToUTF8 (item.path)      },
        { "arguments", SK_WideCharToUTF8 (item.arguments) },
        { "directory", SK_WideCharToUTF8 (item.directory) },
        { "icon",      SK_WideCharToUTF8 (item.icon)      },
        { "used",      item.used                          }
      });

    CreateDirectoryW (SK_FormatStringW (LR"(%ws\Assets)", _path_cache.specialk_userdata).c_str ( ), nullptr);

    // Written to a temporary file first, so the file is never left half-written
    std::wstring path = _GetPath ( ),
                 temp = path + L".tmp";

    {
      std::ofstream file (temp, std::ios::binary | std::ios::trunc);

      if (! file.is_open ( ))
      {
        PLOG_ERROR << "Could not write the jump list file: " << temp;
        return;
      }

      file << nlohmann::json { { "version", 1 }, { "items", jsonItems } }.dump (2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    if (! MoveFileExW (temp.c_str ( ), path.c_str ( ), MOVEFILE_REPLACE_EXISTING))
      PLOG_ERROR << "Could not replace the jump list file: " << path;
  }

  // Most recently used first. The first time (no file yet), the Recent list Windows kept is imported. Requires COM.
  static std::vector <Item>
  _Load (void)
  {
    std::vector <Item> items;

    std::ifstream file (_GetPath ( ), std::ios::binary);

    if (! file.is_open ( ))
    {
      items = _ImportFromWindows ( );
      _Save (items);
      return items;
    }

    std::string    data ((std::istreambuf_iterator <char> (file)), std::istreambuf_iterator <char> ( ));
    nlohmann::json json = nlohmann::json::parse (data, nullptr, false);

    if (json.is_discarded ( ) || ! json.is_object ( ) || ! json.contains ("items") || ! json ["items"].is_array ( ))
    {
      PLOG_WARNING << "Ignoring the invalid jump list file: " << _GetPath ( );
      return items;
    }

    auto _String = [](const nlohmann::json& obj, const char* key) -> std::wstring
    {
      return (obj.contains (key) && obj [key].is_string ( )) ? SK_UTF8ToWideChar (obj [key].get <std::string> ( )) : std::wstring ( );
    };

    for (const nlohmann::json& obj : json ["items"])
    {
      if (! obj.is_object ( ))
        continue;

      Item item;
      item.title     = _String (obj, "title");
      item.path      = _String (obj, "path");
      item.arguments = _Trimmed (_String (obj, "arguments"));
      item.directory = _String (obj, "directory");
      item.icon      = _String (obj, "icon");
      item.used      = (obj.contains ("used") && obj ["used"].is_number_unsigned ( )) ? obj ["used"].get <uint64_t> ( ) : 0;

      if (! item.arguments.empty ( ))
        items.push_back (std::move (item));
    }

    std::stable_sort (items.begin ( ), items.end ( ), [](const Item& a, const Item& b) { return a.used > b.used; });

    return items;
  }

  // The name of the category: the one Windows gives its own Recent category (an indirect string, so the jump list
  //   shows it in the language of Windows), or SKIF's translation of it if that string cannot be found
  static std::wstring
  _GetCategoryName (void)
  {
    static const std::wstring indirect = []
    {
      wchar_t wszSystem [MAX_PATH] = { };

      if (GetSystemDirectoryW (wszSystem, MAX_PATH) == 0)
        return std::wstring ( );

      // StartTileData.dll holds the headers of the categories of jump lists: Pinned (2000), Recent (2001), Frequent
      //   (2002) and Tasks (2003)
      std::wstring str = SK_FormatStringW (LR"(@%ws\StartTileData.dll,-2001)", wszSystem);

      wchar_t wszName [128] = { };

      if (FAILED (SHLoadIndirectString (str.c_str ( ), wszName, ARRAYSIZE (wszName), nullptr)) || wszName [0] == L'\0')
      {
        PLOG_WARNING << "Could not load the name Windows gives the Recent category of jump lists (" << str << ").";
        return std::wstring ( );
      }

      PLOG_VERBOSE << "The Recent category of the jump list is named after Windows' own: " << wszName;

      return str;
    } ( );

    if (! indirect.empty ( ))
      return indirect;

    // TRANSLATORS: Name of the category of the jump list that lists recently launched games (the same as Windows uses)
    return W_("Recent");
  }

  uint64_t
  Now (void)
  {
    return static_cast <uint64_t> (
      std::chrono::duration_cast <std::chrono::milliseconds> (std::chrono::system_clock::now ( ).time_since_epoch ( )).count ( )
    );
  }

  void
  Add (const std::wstring& title, const std::wstring& path, const std::wstring& arguments, const std::wstring& directory, const std::wstring& icon, uint64_t used)
  {
    Item added;
    added.title     = title;
    added.path      = path;
    added.arguments = _Trimmed (arguments);
    added.directory = directory;
    added.icon      = icon;
    added.used      = used;

    if (added.arguments.empty ( ))
      return;

    Lock lock;

    std::vector <Item> items = _Load ( );

    auto it = std::find_if (items.begin ( ), items.end ( ), [&](const Item& item) { return item.arguments == added.arguments; });

    if (it != items.end ( ))
      *it = std::move (added);
    else
      items.push_back (std::move (added));

    _Save (items);

    PLOG_INFO << "Added '" << title << "' to the Recent category of the jump list.";
  }

  bool
  Touch (const std::wstring& arguments, uint64_t used)
  {
    const std::wstring wanted = _Trimmed (arguments);

    if (wanted.empty ( ))
      return false;

    Lock lock;

    std::vector <Item> items = _Load ( );

    auto it = std::find_if (items.begin ( ), items.end ( ), [&](const Item& item) { return item.arguments == wanted; });

    if (it == items.end ( ))
    {
      PLOG_INFO << "The launch was not started from an item of the Recent category of the jump list (" << items.size ( )
                << " items); looked for the arguments: " << wanted;
      return false;
    }

    it->used = used;

    PLOG_INFO << "Moved '" << it->title << "' to the top of the Recent category of the jump list.";

    _Save (items);

    return true;
  }

  void
  AppendRecentCategory (ICustomDestinationList* pDestList, IObjectArray* pRemovedItems, unsigned int maxItems)
  {
    static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

    Lock lock;

    std::vector <Item> items = _Load ( );

    // Items the user removed from the jump list are removed from the list as well, as Windows does not accept them
    //   again (AppendCategory ( ) fails)
    UINT uiRemoved = 0;

    if (pRemovedItems != nullptr)
      pRemovedItems->GetCount (&uiRemoved);

    if (uiRemoved > 0)
    {
      const size_t sizeBefore = items.size ( );

      for (UINT i = 0; i < uiRemoved; i++)
      {
        CComPtr <IShellLinkW> pLink;

        if (FAILED (pRemovedItems->GetAt (i, IID_PPV_ARGS (&pLink))))
          continue;

        const std::wstring args = _GetLinkArguments (pLink);

        if (! args.empty ( ))
          items.erase (std::remove_if (items.begin ( ), items.end ( ), [&](const Item& item) { return item.arguments == args; }), items.end ( ));
      }

      if (items.size ( ) != sizeBefore)
      {
        PLOG_INFO << "Removed " << (sizeBefore - items.size ( )) << " items the user removed from the jump list.";
        _Save (items);
      }
    }

    if (items.empty ( ))
      return;

    CComPtr <IObjectCollection> pObjColl;

    if (FAILED (pObjColl.CoCreateInstance (CLSID_EnumerableObjectCollection)))
    {
      PLOG_ERROR << "Failed to create CLSID_EnumerableObjectCollection object!";
      return;
    }

    UINT uiAdded = 0;

    for (const Item& item : items)
    {
      if (uiAdded >= maxItems)
        break;

      CComPtr <IShellLinkW> pLink;

      if (FAILED (pLink.CoCreateInstance (CLSID_ShellLink)))
        continue;

      // Items are started by SKIF (its launcher mode); older ones may point to where SKIF used to be
      pLink->SetPath        ((! item.path.empty ( )) ? item.path.c_str ( ) : _path_cache.skif_executable);
      pLink->SetArguments   (item.arguments.c_str ( ));
      pLink->SetDescription (item.arguments.c_str ( )); // The tooltip of the item

      if (! item.directory.empty ( ))
        pLink->SetWorkingDirectory (item.directory.c_str ( ));

      if (! item.icon.empty ( ) && PathFileExistsW (item.icon.c_str ( )))
        pLink->SetIconLocation (item.icon.c_str ( ), 0);

      CComQIPtr <IPropertyStore> pPropStore = pLink.p;
      PROPVARIANT                pv;

      if (pPropStore != nullptr && SUCCEEDED (InitPropVariantFromString (item.title.c_str ( ), &pv)))
      {
        pPropStore->SetValue (PKEY_Title, pv);
        PropVariantClear     (&pv);
        pPropStore->Commit   ( );
      }

      if (SUCCEEDED (pObjColl->AddObject (pLink)))
        uiAdded++;
    }

    CComQIPtr <IObjectArray> pArray = pObjColl.p;

    if (pArray == nullptr || uiAdded == 0)
      return;

    HRESULT hr =
      pDestList->AppendCategory (_GetCategoryName ( ).c_str ( ), pArray);

    // E_ACCESSDENIED: Windows is set to not show recently opened items in jump lists
    if (hr == E_ACCESSDENIED)
      PLOG_INFO << "The Recent category of the jump list is not shown, as Windows is set to not show recent items in jump lists.";
    else if (FAILED (hr))
      PLOG_ERROR << "Failed to append the Recent category to the jump list (HRESULT 0x" << std::hex << static_cast <unsigned long> (hr) << std::dec << ")!";
    else
      PLOG_VERBOSE << "Appended the Recent category with " << uiAdded << " of " << items.size ( ) << " items to the jump list.";
  }

  Lock::Lock (void)
  {
    // Shared by all instances of SKIF in the session. An instance of another integrity level (elevated) may not be
    //   able to open it, in which case the list is used without the lock.
    HANDLE hMutex = CreateMutexW (nullptr, FALSE, LR"(Local\SKIF_JumpList)");

    if (hMutex == NULL)
      hMutex = OpenMutexW (SYNCHRONIZE, FALSE, LR"(Local\SKIF_JumpList)");

    if (hMutex == NULL)
    {
      PLOG_WARNING << "Could not open the lock of the jump list, so it is changed without it.";
      return;
    }

    const DWORD dwWait = WaitForSingleObject (hMutex, 5000);

    if (dwWait == WAIT_OBJECT_0 || dwWait == WAIT_ABANDONED)
      m_hMutex = hMutex;

    else
    {
      PLOG_WARNING << "Timed out waiting for the lock of the jump list, so it is changed without it.";
      CloseHandle (hMutex);
    }
  }

  Lock::~Lock (void)
  {
    if (m_hMutex != nullptr)
    {
      ReleaseMutex (static_cast <HANDLE> (m_hMutex));
      CloseHandle  (static_cast <HANDLE> (m_hMutex));
    }
  }
}
