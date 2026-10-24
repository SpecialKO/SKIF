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

// Layout of the current cover, the previous (fading out) one, and the Special K logo
#pragma region ImagePosition

struct image_s
{
  ImVec2       size;                      // Holds the calculated size
  ImVec2       position;                  // Holds the calculated cursor position

  // Holds cached values used to determine if stuff needs to be recalculated
  struct
  {
    ImVec2       resolution         = ImVec2 (0, 0); // Holds the original resolution of the image (unaffected by DPI)
    ImVec2       contentRegionAvail = ImVec2 (-1.0f, -1.0f);
    int          coverScaling       = -1;
  } cache;
};

// Returns the cursor position of where to place the image
static ImVec2
CalculateImageSizing (image_s& image, ImVec2 contentRegionAvail, ImVec2 imageResolution, bool forceDefaultSscaling = false)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  // If we have no image, return 0,0
  if (imageResolution.x == 0.0f || imageResolution.y == 0.0f)
    return ImVec2 (0, 0);

  // Do not recalculate when dealing with an unchanged situation
  if (     contentRegionAvail == image.cache.contentRegionAvail &&
              imageResolution == image.cache.resolution         &&
      _registry.iCoverScaling == image.cache.coverScaling)
    return image.position;

  // Reset values
  image.cache.resolution         = imageResolution;
  image.cache.contentRegionAvail = contentRegionAvail;
  image.cache.coverScaling       = _registry.iCoverScaling;
  image.size                     = contentRegionAvail;
  image.position                 = ImVec2 (0, 0);


  float regionAspectRatio = contentRegionAvail.x / contentRegionAvail.y;
  float imageAspectRatio  =    imageResolution.x /    imageResolution.y;

  // Default (enforced unified sort-of 600x900 sizing)
  if (_registry.iCoverScaling == 0 || forceDefaultSscaling)
  {
    ImVec2 defaultSize        = (_registry._UseLowResCovers) ? ImVec2 (220.0f, 330.0f) : ImVec2 (600.0f, 900.0f);
  //float  defaultAspectRatio = defaultSize.x / defaultSize.y;
    image.size                = defaultSize * SKIF_ImGui_GlobalDPIScale;

    // Reapply the original aspect ratio on the cover (keep the height, but extend the width)
    image.size.x              = image.size.y * imageAspectRatio;

    // Fit on the height and crop width, but only below the default size
    if (contentRegionAvail.y <= image.size.y)
    {
      image.size.y = contentRegionAvail.y;
      image.size.x = image.size.y * imageAspectRatio;
    }
  }

  // Fill
  else if (_registry.iCoverScaling == 1)
  {
    if (imageAspectRatio > regionAspectRatio)
      image.size.x = image.size.y * imageAspectRatio;
    else
      image.size.y = image.size.x / imageAspectRatio;
  }

  // Fit
  else if (_registry.iCoverScaling == 2)
  {
    if (imageAspectRatio < regionAspectRatio)
      image.size.x = image.size.y * imageAspectRatio;
    else
      image.size.y = image.size.x / imageAspectRatio;
  }

  // None
  else if (_registry.iCoverScaling == 3)
  {
    image.size = imageResolution;
  }

  // Stretch (disabled)
  else if (_registry.iCoverScaling == 4)
  {
    // Do nothing -- this cases the image to be stretched
  }

  image.size     = ImFloor (image.size);
  image.position = ImFloor (ImVec2 (
              (contentRegionAvail.x - image.size.x) / 2,
              (contentRegionAvail.y - image.size.y) / 2));

  /*
  PLOG_VERBOSE_IF(! ImGui::IsAnyMouseDown()) // Suppress logging while the mouse is down (e.g. window is being resized)
               << "\n"
               << "Scaling mode   : " << _registry.iCoverScaling << "\n"
               << "Content region : " << contentRegionAvail.x << "x" << contentRegionAvail.y << "\n"
               << "Image\n"
               << " > resolution  : " << imageResolution.x << "x" << imageResolution.y << "\n"
               << " > size        : " << image.size.x      << "x" << image.size.y      << "\n"
               << " > position    : " << image.position.x  << "," << image.position.y  << "\n"
               << "Cache\n"
               << " > region      : " << image.cache.contentRegionAvail.x << "x" << image.cache.contentRegionAvail.y << "\n"
               << " > scaling     : " << image.cache.coverScaling << "\n";
  */

  return image.position;
}

#pragma endregion

static image_s cover, cover_old, coverSK;


// The cover panel: the cover (cross-fading from the previous one), the Special K logo, and the loading / missing labels
//   (moved from SKIF_UI_Tab_DrawLibrary, where it was the _DrawGameCover lambda)
void
DrawGameCover (bool sameLine, bool& isCoverHovered, ImVec2& vecPosCover)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static app_record_s*&       pApp           = GetLibraryTab ( ).pApp;
  static bool&                uiCoverVisible = GetLibraryTab ( ).uiCoverVisible;
  static ImVec2&              sizeCover      = GetLibraryTab ( ).sizeCover;
  static ImVec2&              sizeSK         = GetLibraryTab ( ).sizeSK;
  static ImVec2&              sizeSK_small   = GetLibraryTab ( ).sizeSK_small;

  bool isSpecialK = (pApp != nullptr && pApp->IsSpecialK ( )); // pApp only changes once per frame, at the top of SKIF_UI_Tab_DrawLibrary ( )

  static char   cstrLabelLoading[] = "...";
  static char   cstrLabelMissing[] = "                      Missing cover :(\n"
                                      "           Right-click to set one manually\n\n"
                                      "You can drag and drop an image file or URL here";
  static char   cstrLabelGOGUser[] = "Please sign in to GOG Galaxy to\n"
                                      "allow the cover to be populated :)";
  char*         pcstrLabel         =  nullptr;

  if (uiCoverVisible)
  {
    // Special handling for when a cover is being loaded in the background and selection changes from another game back to this one...
    if (_art.IsSavingCover (pApp))
      pcstrLabel = cstrLabelLoading;

    // A new cover is meant to be loaded, so don't do anything for now...
    else if (_art.IsCoverRequested ( ))
    { }

    else if (_art.IsCoverLoading ( ))
      pcstrLabel = cstrLabelLoading;

    else if (_art.IsCoverMissing ( ))
    {
      if (pApp != nullptr && pApp->IsSpecialK ( ))
      {
        // Special K selected -- do nothing
      }

      else {
        if (pApp != nullptr && pApp->CoverNeedsClientSignIn ( )) // GOG: Galaxy signed out
          pcstrLabel = cstrLabelGOGUser;
        else
          pcstrLabel = cstrLabelMissing;
      }
    }

    // No gamma pre-correction on the CPU: the pixel shader gamma-matches vertex alpha in 16 bpc,
    // and the light/dark curve is chosen from the background polarity flag (luminance_scale.w).
    auto CoverTint = [&](float fFade) -> ImVec4
    {
      return (_registry._StyleLightMode) ? ImVec4 (1.0f,  1.0f,  1.0f,  fTint * fFade)
                                         : ImVec4 (fTint, fTint, fTint,         fFade);
    };

    vecPosCover = ImGui::GetCursorPos ( );

    if (ImGui::BeginChild ("###SKIF_COVER", sizeCover, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
      ImVec2 vecPosCoverInner      = ImGui::GetCursorPos();
      ImVec2 vecContentRegionAvail = ImGui::GetContentRegionAvail();

      // Hides previous declaration!

      if (pcstrLabel != nullptr)
      {
        ImVec2 labelSize = ImGui::CalcTextSize (pcstrLabel);
        ImGui::SetCursorPos (ImFloor (ImVec2 (
            (vecContentRegionAvail.x - labelSize.x) / 2,
            (vecContentRegionAvail.y - labelSize.y) / 2)));
        ImGui::TextDisabled (pcstrLabel);
        ImGui::SetCursorPos (vecPosCoverInner);
      }

      // Display previous fading out cover
      if (_art.GetOldCoverTexture ( ) != nullptr && _art.GetOldCoverAlpha ( ) > 0.0f)
      {
        CalculateImageSizing (cover_old, vecContentRegionAvail, _art.GetOldCoverResolution ( ));

        ImGui::SetCursorPos  (cover_old.position); //vecPosImage_old
        SKIF_ImGui_OptImage  (_art.GetOldCoverTexture ( ),
                                                          cover_old.size, //sizeCoverFloored_old,
                                                          ImVec2 (0, 0),  // Top Left coordinates
                                                          ImVec2 (1, 1),  // Bottom Right coordinates
                                                          CoverTint (_art.GetOldCoverAlpha ( )),
                                                          ImVec4 (0.0f, 0.0f, 0.0f, 0.0f) // Never use a border
        );

        ImGui::SetCursorPos (vecPosCoverInner);
      }

      // Display game cover image
      CalculateImageSizing (cover, vecContentRegionAvail, _art.GetCoverResolution ( ));

      ImGui::SetCursorPos  (cover.position); //vecPosImage
      SKIF_ImGui_OptImage  (_art.GetCoverTexture ( ),
                                                        cover.size,     //sizeCoverFloored,
                                                        ImVec2 (0, 0),  // Top Left coordinates
                                                        ImVec2 (1, 1),  // Bottom Right coordinates
                                                        CoverTint (_art.GetCoverAlpha ( )),
                                                        ImVec4 (0.0f, 0.0f, 0.0f, 0.0f) // Never use a border
      );

      ImGui::SetCursorPos (vecPosCoverInner);

      if (isSpecialK ||
       (! isSpecialK && _art.GetSpecialKAlpha ( ) > 0.0f))
      {
        ImVec2 sizeCurrent = (_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass) ? sizeSK_small : sizeSK;

        // Display Special K logo
        CalculateImageSizing (coverSK, vecContentRegionAvail, sizeCurrent, true);
        ImGui::SetCursorPos  (coverSK.position);

        ImGui::Image (((_registry._UseLowResCovers && ! _registry._UseLowResCoversHiDPIBypass) ? _games.GetSKLogoTexture (true) : _games.GetSKLogoTexture (false)),
                                                        coverSK.size,
                                                        ImVec2 (0, 0),                  // Top Left coordinates
                                                        ImVec2 (1, 1),                  // Bottom Right coordinates
                                                        CoverTint (_art.GetSpecialKAlpha ( )), // Tint for Special K's logo
                                                        ImVec4 (0.0f, 0.0f, 0.0f, 0.0f) // Never use a border
        );
      }

      ImGui::EndChild ( );
    }

    isCoverHovered = ImGui::IsItemHovered();

    if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
      CoverMenu = PopupState_Open;

    //float fY =
    //ImGui::GetCursorPosY ( );

    // This sets the same line for the cover as the list + details
    if (sameLine)
      ImGui::SameLine (0.0f, 3.0f * SKIF_ImGui_GlobalDPIScale); // 0.0f, 0.0f
  }
}


// Special K's Patreon panel at the bottom of the cover (moved verbatim from SKIF_UI_Tab_DrawLibrary)
void
DrawSpecialKPatreon (bool& isCoverHovered, ImVec2 vecPosCover)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_GamingCollection& _games      = SKIF_GamingCollection::Get ( );
  static SKIF_LibraryArtwork&   _art        = SKIF_LibraryArtwork  ::Get ( );

  static app_record_s*&       pApp           = GetLibraryTab ( ).pApp;
  static bool&                uiCoverVisible = GetLibraryTab ( ).uiCoverVisible;
  static ImVec2&              sizeCover      = GetLibraryTab ( ).sizeCover;

  bool isSpecialK = (pApp != nullptr && pApp->IsSpecialK ( )); // pApp only changes once per frame, at the top of SKIF_UI_Tab_DrawLibrary ( )

  // Special handling at the bottom of the cover for Special K
  if (_registry.bPatreon                  && // Only if the Patreon stuff is enabled
      uiCoverVisible                      && // Only when the cover is visible, and
    //! _registry._UseLowResCovers        && //      when not using low-res covers, and
    (ImGui::GetContentRegionAvail().x > 150.0f * SKIF_ImGui_GlobalDPIScale) &&
    ((! isSpecialK && _art.GetSpecialKAlpha ( ) > 0.0f) || // either while Special K logo is fading out,
       (pApp        != nullptr            && // or its selected in the list
        pApp->IsSpecialK ( ))))
  {
    ImGui::SetCursorPos  (ImVec2 (vecPosCover.x, ImFloor (sizeCover.y - 197.0f * SKIF_ImGui_GlobalDPIScale))); // 197.0f _for some reasom_?!
                          //fY - (204.0f * SKIF_ImGui_GlobalDPIScale) ));

    if (ImGui::BeginChild ("###SKIF_PATREON", ImVec2 (sizeCover.x, ImFloor (200.0f * SKIF_ImGui_GlobalDPIScale)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
      static bool hoveredPatButton  = false,
                  hoveredPatCredits = false;

      if (_registry.bFadeCovers)
        ImGui::PushStyleVar (ImGuiStyleVar_Alpha, _art.GetSpecialKAlpha ( ));

      // Set all button styling to transparent
      ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4 (0, 0, 0, 0));
      ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4 (0, 0, 0, 0));
      ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4 (0, 0, 0, 0));

      // Remove borders, paddings, and spacing (some of these are required, but maybe not all, but I can't be bothered figuring it out...)
      SKIF_ImGui_PushDisabledSpacing ( );

      // Move the cursor down 50px since the logo displayed as 150px
      ImGui::SetCursorPosY (ImFloor (ImGui::GetCursorPosY() + 50.0f * SKIF_ImGui_GlobalDPIScale));

      bool        clicked =
      ImGui::ImageButton   ("###PatreonLogo",
                           (ImTextureID)_games.GetPatreonTexture ( ),
                            ImFloor (ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale, // Cover is in reality 200x200, but lets show it as a 150x150 image
                                             150.0f * SKIF_ImGui_GlobalDPIScale)),
                            ImVec2 (  0.f,       0.f),
                            ImVec2 (  1.f,       1.f),
                            ImVec4 (  0,     0,     0,    0), // Use a transparent background
         hoveredPatButton ? ImVec4 (  1.0f,  1.0f,  1.0f, 1.00f)
                          : ImVec4 (  0.8f,  0.8f,  0.8f, 0.66f));

      // Restore borders, paddings, and spacing
      SKIF_ImGui_PopDisabledSpacing ( );

      // Restore the custom button styling
      ImGui::PopStyleColor (3);

      hoveredPatButton =
      ImGui::IsItemHovered ( );

      SKIF_ImGui_SetMouseCursorHand ();
      SKIF_ImGui_SetHoverText ("https://www.patreon.com/Kaldaien");

      if (clicked)
        SKIF_Util_OpenURI (
          L"https://www.patreon.com/Kaldaien"
        );

      if (ImGui::GetContentRegionAvail().x > (150.0f + 230.0f) * SKIF_ImGui_GlobalDPIScale)
      {
        ImGui::SetCursorPos  (ImVec2 (ImFloor (ImGui::GetContentRegionAvail().x - (230.0f * SKIF_ImGui_GlobalDPIScale)), 0.0f));

        ImGui::PushStyleColor     (ImGuiCol_ChildBg,        hoveredPatCredits ? ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
                                                                              : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * ImVec4(.8f, .8f, .8f, .66f));
        ImGui::BeginChild         ("###PatronsChild", ImVec2 (230.0f * SKIF_ImGui_GlobalDPIScale,
                                                              200.0f * SKIF_ImGui_GlobalDPIScale),
                                                          ImGuiChildFlags_AlwaysUseWindowPadding, // Never use a border
                                                          ImGuiWindowFlags_NoScrollbar); // ((pApp->tex_cover.isCustom) ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoBackground))

        ImGui::TextColored        (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption) * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f), "Special Kudos to our Patrons:");

        std::string patrons_ = SKIF_Updater::GetInstance ( ).GetPatrons ( );

        ImGui::Spacing            ( );
        ImGui::SameLine           ( );
        ImGui::Spacing            ( );
        ImGui::SameLine           ( );

        ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor     (ImGuiCol_Text,           ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4  (0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::PushStyleColor     (ImGuiCol_FrameBg,        ImColor (0, 0, 0, 0).Value);
        ImGui::PushStyleColor     (ImGuiCol_ScrollbarBg,    ImColor (0, 0, 0, 0).Value);
      //ImGui::PushStyleColor     (ImGuiCol_TextSelectedBg, ImColor (0, 0, 0, 0).Value);
        ImGui::InputTextMultiline ("###Patrons", patrons_.data (), patrons_.length (),
                        ImVec2 (205.0f * SKIF_ImGui_GlobalDPIScale,
                                160.0f * SKIF_ImGui_GlobalDPIScale),
                                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor      (3);
        ImGui::PopStyleVar        ( );

        hoveredPatCredits =
        ImGui::IsItemActive       ( ) ||
        ImGui::IsItemHovered      ( );

        if (ImGui::IsItemActive   ( ))
          allowShortcutCtrlA = false;

        ImGui::EndChild           ( );
        ImGui::PopStyleColor      ( );

        hoveredPatCredits = hoveredPatCredits ||
        ImGui::IsItemHovered      ( );
      }

      if (_registry.bFadeCovers)
        ImGui::PopStyleVar ( );

      ImGui::EndChild           ( );
    }

    isCoverHovered = isCoverHovered ||
    ImGui::IsItemHovered ( );

    if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
      CoverMenu = PopupState_Open;
  }
}
