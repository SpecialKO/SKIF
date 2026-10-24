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

#include "library_ui.h"

#pragma region LaunchGame

void
LaunchGame (app_record_s* pApp)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );

  if (launchConfig == nullptr)
    return;

  DWORD current_time = SKIF_Util_timeGetTime ( );

  if ( pApp->RequiresValidExecutable ( ) &&
      ! launchConfig->isExecutableFullPathValid ( ))
  {
    SKIF_ImGui_InfoMessage ("Missing executable", "Could not launch game due to missing executable:\n\n" + launchConfig->getExecutableFullPathUTF8 ( ));
  }

  else {
    bool localInjection        = (pApp->specialk.injection.injection.type == InjectionType::Local);
    bool usingSK               = localInjection;

    // Increment the uses count and used timestamp
    time_t ltime;
    time (&ltime);
    pApp->skif.used           = std::to_string(ltime);
    pApp->skif.used_formatted = SK_WideCharToUTF8 (SKIF_Util_timeGetTimeAsWStr (ltime));
    pApp->skif.uses++;


    // SERVICE PREPARATIONS


    // Check if the injection service should be used
    if (! usingSK)
    {
      bool isLocalBlacklisted  = launchConfig->isBlacklisted ( ),
           isGlobalBlacklisted = _inject._TestUserList (launchConfig->getExecutableFullPathUTF8 ( ).c_str (), false);

      usingSK = ! launchWithoutSK       &&
                ! isLocalBlacklisted    &&
                ! isGlobalBlacklisted;

      if (usingSK)
      {
        // Whitelist the game if it hasn't been already (store-specific; Steam needs none)
        pApp->WhitelistForInjection (launchConfig);

        // Disable the first service notification
        if (_registry.bMinimizeOnGameLaunch)
          _registry._SuppressServiceNotification = true;
      }

      if (_inject.bHasServlet)
      {
        // Kickstart service if it is currently not running
        if (usingSK && ! _inject.bCurrentState)
          _inject._StartStopInject (false, true, launchConfig->isElevated( ), pApp->skif.auto_stop);

        // Stop the service if the user attempts to launch without SK
        else if (! usingSK && _inject.bCurrentState)
          _inject._StartStopInject (true);
      }

      PLOG_ERROR_IF (! _inject.bHasServlet) << "The injection service could not be started due to missing critical files! Please reinstall Special K using the latest installer.";
    }

    // Create the injection acknowledge events in case of a local injection
    else {
      _inject.SetInjectAckEx     (true);
      _inject.SetInjectExitAckEx (true);
    }


    // LAUNCH PREPARATIONS


    // Some stores and situations always or preferably use an instant launch (but the game menu is always explicit)
    if (! launchInstant && pApp->PrefersInstantLaunch (launchGameMenu, launchConfig))
      launchInstant = true;


    // LAUNCH PROCEDURES

    pApp->launch_failed = false;

    app_record_s::launch_context_s
      ctx                = { };
      ctx.config         = launchConfig;
      ctx.instant        = launchInstant;
      ctx.usingSK        = usingSK;
      ctx.localInjection = localInjection;
      ctx.timeNow        = current_time;

    // Store-specific launch (store client, or an instant launch of the executable)
    pApp->Launch (ctx);

    // Fallback for minimizing SKIF when not using SK if configured as such
    if (_registry.bMinimizeOnGameLaunch && ! usingSK && SKIF_ImGui_hWnd != NULL)
      ShowWindowAsync (SKIF_ImGui_hWnd, SW_SHOWMINNOACTIVE);

    // Update the db.json file with the new Uses/Used values
    SKIF_GamingCollection::Get ( ).MetaDB_UpdateApp (pApp, true);

    // Ensure the sort order is updated as well
    SKIF_GamingCollection::SortApps (&_games.GetApps());
    sort_changed = true;
  }
}


#pragma endregion


#pragma region UpdateGameCover

bool
SaveGameCover (app_record_s* pApp, std::wstring_view path)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  if (path.empty())
    return false;

  std::error_code ec;
  const std::filesystem::path fsPath (path.data());
  std::wstring targetPath = L"";
  std::wstring ext        = SKIF_Util_ToLowerW (fsPath.extension().wstring());
  bool         isURL      = PathIsURL (path.data());
  std::wstring extTarget  = ext;
  bool isImage = false;

#if 0

  if (ext == L".jpeg")
    extTarget = L".jpg";

  if (ext == L".webp")
    extTarget = L".png";

  if (ext == L".bmp")
    extTarget = L".jpg";

  if (ext == L".psd")
    extTarget = L".png";

  bool isImage =
    (ext == L".jpg"  ||
     ext == L".jpeg" ||
   //ext == L".jxr"  ||
     ext == L".png"  ||
     ext == L".webp" ||
   //ext == L".tga"  ||
     ext == L".bmp"  ||
     ext == L".psd"  );
   //ext == L".gif"  ||
   //ext == L".tif"  );

  constexpr char* error_title =
    "Unsupported image format";
  constexpr char* error_label =
    "Please use one of these image formats:\n"
    "   *.jpg\n"
  //"   *.jxr\n"
    "   *.png\n"
    "   *.webp\n"
  //"   *.tga\n"
    "   *.bmp\n"
    "   *.psd\n"
  //"   *.gif\n"
  //"   *.tif\n"
    "\n"
    "Note that the app has no support for animated images.";
#endif

  if (! isURL)
  {
    // For local files, check if they do. in fact, exist and are local files
    if (! std::filesystem::is_regular_file (fsPath, ec))
      return false;

    // Identify file type by reading the file signature
    static const auto types =
    {
      FileSignature { L"image/jpeg",                L".jpg",  { 0xFF, 0xD8, 0x00, 0x00 },   // JPEG (SOI; Start of Image)
                                                              { 0xFF, 0xFF, 0x00, 0x00 } }, // JPEG App Markers are masked as they can be all over the place (e.g. 0xFF 0xD8 0xFF 0xED)
      FileSignature { L"image/png",                 L".png",  { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } },
      FileSignature { L"image/webp",                L".webp", { 0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50 },     // 52 49 46 46 ?? ?? ?? ?? 57 45 42 50
                                                              { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF } }, // mask
      FileSignature { L"image/bmp",                 L".bmp",  { 0x42, 0x4D } },
      FileSignature { L"image/vnd.adobe.photoshop", L".psd",  { 0x38, 0x42, 0x50, 0x53 } }
    };

    static size_t
        maxLength  = 0;
    if (maxLength == 0)
    {
      for (auto& type : types)
        if (type.signature.size() > maxLength)
          maxLength = type.signature.size();
    }

    std::ifstream file(fsPath, std::ios::binary);

    if (file)
    {
      std::vector<char> buffer (maxLength);
      file.read (buffer.data(), maxLength);
      file.close();

      for (auto& type : types)
      {
        if (SKIF_Util_HasFileSignature (buffer, type))
        {
          PLOG_INFO << "Detected an " << type.mime_type << " image";

          isImage   = true;
          extTarget = SKIF_Util_ToLowerW (type.file_extension);

          // Shortcut to use a few (incorrect) file extensions for some types
          // TODO: Do away with this cheating, lol
          if (type.file_extension == L".webp")
            extTarget = L".png";

          if (type.file_extension == L".bmp")
            extTarget = L".png";

          if (type.file_extension == L".psd")
            extTarget = L".png";

          break;
        }
      }

      PLOG_ERROR_IF(! isImage) << "Unknown file type!";
    }

    else
      PLOG_ERROR << "Failed to open file!";
  }

  // Unsupported file format
  if (! isImage && ! isURL)
    return false;

  targetPath = pApp->GetAssetDirectory ( );

  if (targetPath == L"")
  {
    PLOG_ERROR << "Could not resolve target path?!";
    return false;
  }

  struct thread_s {
    std::wstring source        = L"";
    std::wstring destination   = L"";
    std::wstring ext_target    = L"";
    std::wstring ext_original  = L"";
    bool         is_url        = false;
    uint32_t     appid         = 0;
    int          store         = 0;
  };

  thread_s* data = new thread_s;

  data->source        = path;
  data->destination   = targetPath;
  data->ext_target    = extTarget;
  data->ext_original  = ext;
  data->is_url        = isURL;
  data->appid         = pApp->id;
  data->store         = (int)pApp->store;

  HANDLE hWorkerThread = (HANDLE)
  _beginthreadex (nullptr, 0x0, [](void* var) -> unsigned
  {
    SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_UpdateCoverWorker");

    // Is this combo really appropriate for this thread?
    SKIF_Util_SetThreadPowerThrottling (GetCurrentThread (), 1); // Enable EcoQoS for this thread
    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

    PLOG_VERBOSE << "SKIF_UpdateCoverWorker thread started!";

    thread_s* _data = static_cast<thread_s*>(var);

    CoInitializeEx (nullptr, 0x0);

    PLOG_INFO  << "Updating game cover asynchronously...";

    std::error_code ec;
    // Create any missing directories
    if (! std::filesystem::exists (            _data->destination, ec))
          std::filesystem::create_directories (_data->destination, ec);

    _data->destination += L"cover";

    // We store the new file in .tmp first
    std::wstring tmpPath = _data->destination + L".tmp";

    bool success = false;
    bool backup  = false;

    if (_data->source == (_data->destination + _data->ext_original))
    {
      PLOG_WARNING << "Source image and destination image is the same!";
    }

    else
    {
      // Backup any existing copies
      backup =  MoveFileEx((_data->destination + L".jpg").c_str(), (_data->destination + L".jpg.old").c_str(), MOVEFILE_REPLACE_EXISTING)
             || MoveFileEx((_data->destination + L".png").c_str(), (_data->destination + L".png.old").c_str(), MOVEFILE_REPLACE_EXISTING);

      // This both downloads a new image from the internet as well as copies a local file to the destination
      // BMP files are downloaded to .tmp, while all others are downloaded to their intended path
      success = (_data->is_url) ? SKIF_Util_GetWebResource (_data->source,         tmpPath)
                                :                 CopyFile (_data->source.c_str(), tmpPath.c_str(), false);

      // If the file was copied successfully, we also need to ensure it's not marked as read-only
      if (success)
        SetFileAttributes (tmpPath.c_str(),
                GetFileAttributes (tmpPath.c_str()) & ~FILE_ATTRIBUTE_READONLY);

      else
      {
        PLOG_ERROR << "Could not save the source image to the destination path!";
        PLOG_ERROR << "Source:      " << _data->source;
        PLOG_ERROR << "Destination: " << tmpPath;
      }
    }

    DirectX::TexMetadata  meta = { };
    DirectX::ScratchImage  img = { };

    // Try and load the image
    if (success)
    {
      success = FastTextureLoading (tmpPath, meta, img);

      if (! success)
      {
        PLOG_ERROR << "The saved image could not be loaded! It is either corrupt or in an unsupported format.";
        PLOG_ERROR << "Source: " << _data->source;
      }
    }

    // Swap it in
    if (success)
    {
      // BMP images needs to be converted to PNG
      // Extremely basic and rudimentary check -- should really preferably read the metadata instead
      if (_data->ext_original == L".bmp")
      {
        PLOG_DEBUG << "Converting BMP image to PNG...";
        success =
          SUCCEEDED (
              DirectX::SaveToWICFile (
                img.GetImages(),
                img.GetImageCount(),
                DirectX::WIC_FLAGS_FORCE_SRGB,
                GetWICCodec (DirectX::WIC_CODEC_PNG),
                (_data->destination + _data->ext_target).c_str()
              )
          );
      }

      // Rename remaining types
      else
      {
        PLOG_DEBUG << "Swapping in the new file...";
        success =
          MoveFileEx((_data->destination + L".tmp").c_str(), (_data->destination + _data->ext_target).c_str(), MOVEFILE_REPLACE_EXISTING);
      }
    }

    // If everything checks out, remove any backups made
    if (success)
    {
      DeleteFile((_data->destination + L".jpg.old").c_str());
      DeleteFile((_data->destination + L".png.old").c_str());
    }

    // If something failed, restore the backups
    else if (backup)
    {
      PLOG_INFO << "Restoring the original cover...";
      MoveFile((_data->destination + L".jpg.old").c_str(), (_data->destination + L".jpg").c_str());
      MoveFile((_data->destination + L".png.old").c_str(), (_data->destination + L".png").c_str());
    }

    // Delete the temporary file after we are done with it
    DeleteFile(tmpPath.c_str());

    PLOG_ERROR_IF(! success) << "Failed to process the new cover image!";

    PostMessage (SKIF_Notify_hWnd, WM_SKIF_REFRESHCOVER, _data->appid, _data->store); // Force a refresh when the cover has been swapped in

    PLOG_INFO  << "Finished updating game cover asynchronously...";

    // Free up the memory we allocated
    delete _data;

    PLOG_VERBOSE << "SKIF_UpdateCoverWorker thread stopped!";

    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

    return 0;
  }, data, 0x0, nullptr);

  bool threadCreated = (hWorkerThread != NULL);

  if (threadCreated) // We don't care about how it goes so the handle is unneeded
    CloseHandle (hWorkerThread);
  else // Someting went wrong during thread creation, so free up the memory we allocated earlier
    delete data;

  return threadCreated;
}

#pragma endregion
