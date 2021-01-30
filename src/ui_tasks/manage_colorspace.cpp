#if 0
using NvAPI_Disp_GetHdrCapabilities_pfn = NvAPI_Status (__cdecl *)(NvU32, NV_HDR_CAPABILITIES*);
using NvAPI_Disp_HdrColorControl_pfn    = NvAPI_Status (__cdecl *)(NvU32, NV_HDR_COLOR_DATA*);
using NvAPI_Disp_ColorControl_pfn       = NvAPI_Status (__cdecl *)(NvU32, NV_COLOR_DATA*);

NvAPI_Disp_GetHdrCapabilities_pfn NvAPI_Disp_GetHdrCapabilities_Original = nullptr;
NvAPI_Disp_HdrColorControl_pfn    NvAPI_Disp_HdrColorControl_Original    = nullptr;
NvAPI_Disp_ColorControl_pfn       NvAPI_Disp_ColorControl_Original       = nullptr;

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"  Nv API  "

struct nvapi_desc_s {
  bool                     active               = false;

  struct {
    NvU32                  supports_YUV422_12bit : 1;
    NvU32                  supports_10b_12b_444  : 2;
  } color_support_hdr = { };

  struct {
    NvU32                  display_id      = std::numeric_limits <NvU32>::max ();
    NV_HDR_CAPABILITIES_V2 hdr_caps        = { };
    NV_HDR_COLOR_DATA_V2   hdr_data        = { };

    // TODO
    //std::vector  <
    //  std::tuple < NV_BPC,
    //               NV_COLOR_FORMAT,
    //               NV_DYNAMIC_RANGE >
    //                                >   color_modes =
    //                     { std::make_tuple ( NV_BPC_DEFAULT,
    //                                         NV_COLOR_FORMAT_DEFAULT,
    //                                         NV_DYNAMIC_RANGE_AUTO ); }
  } raw = { };

  NV_HDR_MODE         mode                 = NV_HDR_MODE_OFF;
  NV_COLOR_FORMAT     color_format         = NV_COLOR_FORMAT_DEFAULT;
  NV_DYNAMIC_RANGE    dynamic_range        = NV_DYNAMIC_RANGE_AUTO;
  NV_BPC              bpc                  = NV_BPC_DEFAULT;

  bool                isHDR10 (void) const noexcept
                                           { return ( mode == NV_HDR_MODE_UHDA ||
                                                      mode == NV_HDR_MODE_UHDA_PASSTHROUGH ); }

  bool setColorEncoding_HDR ( NV_COLOR_FORMAT fmt,
                              NV_BPC          bpc,
                              NvU32           display_id = std::numeric_limits <NvU32>::max () );

  bool setColorEncoding_SDR ( NV_COLOR_FORMAT fmt,
                              NV_BPC          bpc,
                              NvU32           display_id = std::numeric_limits <NvU32>::max () );

  const char* getBpcStr (void) const
  {
    static std::unordered_map <NV_BPC, const char*>
      bpc_map =
        { { NV_BPC_DEFAULT, "Default?" },
          { NV_BPC_6,       "6 bpc"    },
          { NV_BPC_8,       "8 bpc"    },
          { NV_BPC_10,      "10 bpc"   },
          { NV_BPC_12,      "12 bpc"   },
          { NV_BPC_16,      "16 bpc"   } };

    return bpc_map [bpc];
  }

  const char* getFormatStr (void) const
  {
    static std::unordered_map <NV_COLOR_FORMAT, const char*>
      color_fmt_map =
          { { NV_COLOR_FORMAT_RGB,    "RGB 4:4:4"  },
            { NV_COLOR_FORMAT_YUV422, "YUV 4:2:2"  },
            { NV_COLOR_FORMAT_YUV444, "YUV 4:4:4", },
            { NV_COLOR_FORMAT_YUV420, "YUV 4:2:0", },
            { NV_COLOR_FORMAT_DEFAULT,"Default?",  },
            { NV_COLOR_FORMAT_AUTO,   "Auto",      } };

    return color_fmt_map [color_format];
  }

  const char* getRangeStr (void) const
  {
    static std::unordered_map <_NV_DYNAMIC_RANGE, const char*>
      dynamic_range_map =
        { { NV_DYNAMIC_RANGE_VESA, "Limited"    },
          { NV_DYNAMIC_RANGE_CEA,  "Full Range" },
          { NV_DYNAMIC_RANGE_AUTO, "Auto"     } };

    return dynamic_range_map [dynamic_range];
  }
} nvapi_hdr = { };

enum SK_HDR_TRANSFER_FUNC
{
  sRGB,        // ~= Power-Law: 2.2

  scRGB,       // Linear, but source material is generally sRGB
               //  >> And you have to undo that transformation!

  G19,         //   Power-Law: 1.9
  G20,         //   Power-Law: 2.0
  G22,         //   Power-Law: 2.2
  G24,         //   Power-Law: 2.4

  Linear=scRGB,// Alias for scRGB  (Power-Law: 1.0 = Linear!)

  SMPTE_2084,  // Perceptual Quantization
  HYBRID_LOG,  // TODO
  NONE
};

SK_HDR_TRANSFER_FUNC
getEOTF (void);

  wchar_t                 display_name [128]   = { };

NvAPI_Status
__cdecl
NvAPI_Disp_GetHdrCapabilities_Override ( NvU32                displayId,
                                         NV_HDR_CAPABILITIES *pHdrCapabilities )
{
  pHdrCapabilities->driverExpandDefaultHdrParameters = 1;
  pHdrCapabilities->static_metadata_descriptor_id    = NV_STATIC_METADATA_TYPE_1;

  NvAPI_Status ret =
    NvAPI_Disp_GetHdrCapabilities_Original ( displayId, pHdrCapabilities );

  if ( pHdrCapabilities->isST2084EotfSupported ||
       pHdrCapabilities->isTraditionalHdrGammaSupported )
  {
    auto& rb =
      SK_GetCurrentRenderBackend ();

    rb.driver_based_hdr = true;
        rb.setHDRCapable (true);
  }

  dll_log->LogEx ( true,
    L"[ HDR Caps ]\n"
    L"  +-----------------+---------------------\n"
    L"  | Red Primary.... |  %f, %f\n"
    L"  | Green Primary.. |  %f, %f\n"
    L"  | Blue Primary... |  %f, %f\n"
    L"  | White Point.... |  %f, %f\n"
    L"  | Min Luminance.. |  %f\n"
    L"  | Max Luminance.. |  %f\n"
    L"  |  \"  FullFrame.. |  %f\n"
    L"  | EDR Support.... |  %s\n"
    L"  | ST2084 Gamma... |  %s\n"
    L"  | HDR Gamma...... |  %s\n"
    L"  | SDR Gamma...... |  %s\n"
    L"  |  ?  4:4:4 10bpc |  %s\n"
    L"  |  ?  4:4:4 12bpc |  %s\n"
    L"  | YUV 4:2:2 12bpc |  %s\n"
    L"  +-----------------+---------------------\n",
      (float)pHdrCapabilities->display_data.displayPrimary_x0,   (float)pHdrCapabilities->display_data.displayPrimary_y0,
      (float)pHdrCapabilities->display_data.displayPrimary_x1,   (float)pHdrCapabilities->display_data.displayPrimary_y1,
      (float)pHdrCapabilities->display_data.displayPrimary_x2,   (float)pHdrCapabilities->display_data.displayPrimary_y2,
      (float)pHdrCapabilities->display_data.displayWhitePoint_x, (float)pHdrCapabilities->display_data.displayWhitePoint_y,
      (float)pHdrCapabilities->display_data.desired_content_min_luminance,
      (float)pHdrCapabilities->display_data.desired_content_max_luminance,
      (float)pHdrCapabilities->display_data.desired_content_max_frame_average_luminance,
             pHdrCapabilities->isEdrSupported                                 ? L"Yes" : L"No",
             pHdrCapabilities->isST2084EotfSupported                          ? L"Yes" : L"No",
             pHdrCapabilities->isTraditionalHdrGammaSupported                 ? L"Yes" : L"No",
             pHdrCapabilities->isTraditionalSdrGammaSupported                 ? L"Yes" : L"No",
            (pHdrCapabilities->dv_static_metadata.supports_10b_12b_444 & 0x1) ? L"Yes" : L"No",
            (pHdrCapabilities->dv_static_metadata.supports_10b_12b_444 & 0x2) ? L"Yes" : L"No",
             pHdrCapabilities->dv_static_metadata.supports_YUV422_12bit       ? L"Yes" : L"No");

  if (ret == NVAPI_OK)
  {
    auto& rb =
      SK_GetCurrentRenderBackend ();

    ///rb.scanout.nvapi_hdr.color_support_hdr.supports_YUV422_12bit =
    ///pHdrCapabilities->dv_static_metadata.supports_YUV422_12bit;
    ///
    ///rb.scanout.nvapi_hdr.color_support_hdr.supports_10b_12b_444 =
    ///pHdrCapabilities->dv_static_metadata.supports_10b_12b_444;

  //pHDRCtl->devcaps.BitsPerColor          = 10;
    //pHDRCtl->devcaps.RedPrimary   [0]      = ((float)pHdrCapabilities->display_data.displayPrimary_x0) / (float)0xC350;
    //pHDRCtl->devcaps.RedPrimary   [1]      = ((float)pHdrCapabilities->display_data.displayPrimary_y0) / (float)0xC350;
    //
    //pHDRCtl->devcaps.GreenPrimary [0]      = ((float)pHdrCapabilities->display_data.displayPrimary_x1) / (float)0xC350;
    //pHDRCtl->devcaps.GreenPrimary [1]      = ((float)pHdrCapabilities->display_data.displayPrimary_y1) / (float)0xC350;
    //
    //pHDRCtl->devcaps.BluePrimary  [0]      = ((float)pHdrCapabilities->display_data.displayPrimary_x2) / (float)0xC350;
    //pHDRCtl->devcaps.BluePrimary  [1]      = ((float)pHdrCapabilities->display_data.displayPrimary_y2) / (float)0xC350;
    //
    //pHDRCtl->devcaps.WhitePoint   [0]      = ((float)pHdrCapabilities->display_data.displayWhitePoint_x) / (float)0xC350;
    //pHDRCtl->devcaps.WhitePoint   [1]      = ((float)pHdrCapabilities->display_data.displayWhitePoint_y) / (float)0xC350;
    //
  ////pHDRCtl->devcaps.ColorSpace            = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    //pHDRCtl->devcaps.MinLuminance          = (float)pHdrCapabilities->display_data.desired_content_min_luminance;
    //pHDRCtl->devcaps.MaxLuminance          = (float)pHdrCapabilities->display_data.desired_content_max_luminance;
    //pHDRCtl->devcaps.MaxFullFrameLuminance = (float)pHdrCapabilities->display_data.desired_content_max_frame_average_luminance;
  }

  return ret;
}

NvAPI_Status
__cdecl
NvAPI_Disp_ColorControl_Override ( NvU32          displayId,
                                   NV_COLOR_DATA *pColorData )
{
  return
    NvAPI_Disp_ColorControl_Original (displayId, pColorData);
}

NvAPI_Status
SK_NvAPI_CheckColorSupport_SDR (
  NvU32                displayId,
  NV_BPC               bpcTest,
  NV_COLOR_FORMAT      formatTest      = NV_COLOR_FORMAT_AUTO,
  NV_DYNAMIC_RANGE     rangeTest       = NV_DYNAMIC_RANGE_AUTO,
  NV_COLOR_COLORIMETRY colorimetryTest = NV_COLOR_COLORIMETRY_AUTO )
{
  if (displayId == 0)
  {
    displayId =
      SK_NvAPI_GetDefaultDisplayId ();
  }

  NV_COLOR_DATA
    colorCheck                   = { };
    colorCheck.version           = NV_COLOR_DATA_VER;
    colorCheck.size              = sizeof (NV_COLOR_DATA);
    colorCheck.cmd               = NV_COLOR_CMD_IS_SUPPORTED_COLOR;
    colorCheck.data.colorimetry  = (NvU8)colorimetryTest;
    colorCheck.data.dynamicRange = (NvU8)rangeTest;
    colorCheck.data.colorFormat  = (NvU8)formatTest;
    colorCheck.data.bpc          = bpcTest;

  return
    NvAPI_Disp_ColorControl_Original ( displayId, &colorCheck );
}

NvAPI_Status
SK_NvAPI_CheckColorSupport_HDR (
  NvU32                displayId,
  NV_BPC               bpcTest,
  NV_COLOR_FORMAT      formatTest      = NV_COLOR_FORMAT_AUTO,
  NV_DYNAMIC_RANGE     rangeTest       = NV_DYNAMIC_RANGE_AUTO,
  NV_COLOR_COLORIMETRY colorimetryTest = NV_COLOR_COLORIMETRY_AUTO )
{
  if (displayId == 0)
  {
    displayId =
      SK_NvAPI_GetDefaultDisplayId ();
  }

  NV_COLOR_DATA
    colorCheck                   = { };
    colorCheck.version           = NV_COLOR_DATA_VER;
    colorCheck.size              = sizeof (NV_COLOR_DATA);
    colorCheck.cmd               = NV_COLOR_CMD_IS_SUPPORTED_COLOR;
    colorCheck.data.colorimetry  = (NvU8)colorimetryTest;
    colorCheck.data.dynamicRange = (NvU8)rangeTest;
    colorCheck.data.colorFormat  = (NvU8)formatTest;
    colorCheck.data.bpc          =       bpcTest;

  return
    NvAPI_Disp_ColorControl_Original ( displayId, &colorCheck );
}


NvAPI_Status
__cdecl
NvAPI_Disp_HdrColorControl_Override ( NvU32              displayId,
                                      NV_HDR_COLOR_DATA *pHdrColorData )
{
  auto& rb =
    SK_GetCurrentRenderBackend ();

  SK_LOG_FIRST_CALL

    static NV_HDR_COLOR_DATA_V2  expandedData = { };
         NV_HDR_COLOR_DATA_V1   *inputData    =
        (NV_HDR_COLOR_DATA_V1   *)pHdrColorData;

  if (pHdrColorData->version == NV_HDR_COLOR_DATA_VER1)
  {
    NV_HDR_COLOR_DATA_V1    origData =     *((NV_HDR_COLOR_DATA_V1 *)pHdrColorData);
    memcpy (&expandedData, &origData, sizeof (NV_HDR_COLOR_DATA_V1));

    expandedData.version   = NV_HDR_COLOR_DATA_VER2;
    pHdrColorData          = &expandedData;

    pHdrColorData->cmd                           = inputData->cmd;
    pHdrColorData->hdrMode                       = inputData->hdrMode;
    pHdrColorData->static_metadata_descriptor_id = inputData->static_metadata_descriptor_id;

    memcpy ( &pHdrColorData->mastering_display_data,
                 &inputData->mastering_display_data,
          sizeof (inputData->mastering_display_data) );
  }

  if (pHdrColorData->version == NV_HDR_COLOR_DATA_VER2)
  {
    auto& hdr_sec =
      SK_GetDLLConfig ()->get_section (L"NvAPI.HDR");

    auto& hdrBpc =
      hdr_sec.get_value (L"hdrBpc");

    auto& hdrColorFormat =
      hdr_sec.get_value (L"hdrColorFormat");

    auto& hdrDynamicRange =
      hdr_sec.get_value (L"hdrDynamicRange");

    static std::unordered_map <std::wstring, _NV_BPC>
      bpc_map =
        { { L"NV_BPC_DEFAULT", NV_BPC_DEFAULT },
          { L"NV_BPC_6",       NV_BPC_6       },
          { L"NV_BPC_8",       NV_BPC_8       },
          { L"NV_BPC_10",      NV_BPC_10      },
          { L"NV_BPC_12",      NV_BPC_12      },
          { L"NV_BPC_16",      NV_BPC_16      } };

    static std::unordered_map <std::wstring, NV_COLOR_FORMAT>
      color_fmt_map =
        { { L"NV_COLOR_FORMAT_RGB",     NV_COLOR_FORMAT_RGB     },
          { L"NV_COLOR_FORMAT_YUV422",  NV_COLOR_FORMAT_YUV422  },
          { L"NV_COLOR_FORMAT_YUV444",  NV_COLOR_FORMAT_YUV444  },
          { L"NV_COLOR_FORMAT_YUV420",  NV_COLOR_FORMAT_YUV420  },
          { L"NV_COLOR_FORMAT_DEFAULT", NV_COLOR_FORMAT_DEFAULT },
          { L"NV_COLOR_FORMAT_AUTO",    NV_COLOR_FORMAT_AUTO    } };

    static std::unordered_map <std::wstring, _NV_DYNAMIC_RANGE>
      dynamic_range_map =
        { { L"NV_DYNAMIC_RANGE_VESA", NV_DYNAMIC_RANGE_VESA  },
          { L"NV_DYNAMIC_RANGE_CEA",  NV_DYNAMIC_RANGE_CEA   },
          { L"NV_DYNAMIC_RANGE_AUTO", NV_DYNAMIC_RANGE_AUTO  } };

    if (! hdrBpc.empty ())
      pHdrColorData->hdrBpc = bpc_map [hdrBpc];

    if (! hdrColorFormat.empty ())
      pHdrColorData->hdrColorFormat = color_fmt_map [hdrColorFormat];

    if (! hdrDynamicRange.empty ())
      pHdrColorData->hdrDynamicRange = dynamic_range_map [hdrDynamicRange];

    //if (pHdrColorData->hdrMode == NV_HDR_MODE_UHDA_PASSTHROUGH)
    //    pHdrColorData->hdrMode =  NV_HDR_MODE_UHDA;
  }

  NV_COLOR_DATA
  colorCheck                   = { };
  colorCheck.version           = NV_COLOR_DATA_VER;
  colorCheck.cmd               = NV_COLOR_CMD_IS_SUPPORTED_COLOR;
  colorCheck.data.colorimetry  = NV_COLOR_COLORIMETRY_AUTO;
  colorCheck.data.dynamicRange = NV_DYNAMIC_RANGE_AUTO;
  colorCheck.data.colorFormat  = NV_COLOR_FORMAT_RGB;
  colorCheck.data.bpc          = NV_BPC_10;


  if ( NVAPI_OK ==
        SK_NvAPI_CheckColorSupport_HDR (displayId, NV_BPC_10, NV_COLOR_FORMAT_YUV444) )
    rb.scanout.nvapi_hdr.color_support_hdr.supports_10b_12b_444 |=  0x1;
  else
    rb.scanout.nvapi_hdr.color_support_hdr.supports_10b_12b_444 &= ~0x1;

  if ( NVAPI_OK ==
        SK_NvAPI_CheckColorSupport_HDR (displayId, NV_BPC_12, NV_COLOR_FORMAT_YUV444) )
    rb.scanout.nvapi_hdr.color_support_hdr.supports_10b_12b_444 |=   0x2;
  else
    rb.scanout.nvapi_hdr.color_support_hdr.supports_10b_12b_444 &=  ~0x2;

  if ( NVAPI_OK ==
         SK_NvAPI_CheckColorSupport_HDR (displayId, NV_BPC_12, NV_COLOR_FORMAT_YUV422) )
    rb.scanout.nvapi_hdr.color_support_hdr.supports_YUV422_12bit = 1;
  else
    rb.scanout.nvapi_hdr.color_support_hdr.supports_YUV422_12bit = 0;


  auto _LogGameRequestedValues = [&](void) ->
  void
  {
    static SKTL_BidirectionalHashMap <std::wstring, _NV_BPC>
      bpc_map =
        { { L"Default bpc", NV_BPC_DEFAULT },
          { L"6 bpc",       NV_BPC_6       },
          { L"8 bpc",       NV_BPC_8       },
          { L"10 bpc",      NV_BPC_10      },
          { L"12 bpc",      NV_BPC_12      },
          { L"16 bpc",      NV_BPC_16      } };

    static SKTL_BidirectionalHashMap <std::wstring, NV_COLOR_FORMAT>
      color_fmt_map =
        { { L"RGB 4:4:4",  NV_COLOR_FORMAT_RGB     },
          { L"YUV 4:2:2",  NV_COLOR_FORMAT_YUV422  },
          { L"YUV 4:4:4",  NV_COLOR_FORMAT_YUV444  },
          { L"YUV 4:2:0",  NV_COLOR_FORMAT_YUV420  },
          { L"(Default?)", NV_COLOR_FORMAT_DEFAULT },
          { L"[AUTO]",     NV_COLOR_FORMAT_AUTO    } };

    static SKTL_BidirectionalHashMap <std::wstring, _NV_DYNAMIC_RANGE>
      dynamic_range_map =
        { { L"Full Range", NV_DYNAMIC_RANGE_VESA  },
          { L"Limited",    NV_DYNAMIC_RANGE_CEA   },
          { L"Don't Care", NV_DYNAMIC_RANGE_AUTO  } };

    SK_LOG0 ( ( L"HDR:  Max Master Luma: %7.1f, Min Master Luma: %7.5f",
      static_cast <double> (pHdrColorData->mastering_display_data.max_display_mastering_luminance),
      static_cast <double> (
        static_cast <double>
                          (pHdrColorData->mastering_display_data.min_display_mastering_luminance) * 0.0001
                          )
              ), __SK_SUBSYSTEM__ );

    SK_LOG0 ( ( L"HDR:  Max Avg. Luma: %7.1f, Max Luma: %7.1f",
      static_cast <double> (pHdrColorData->mastering_display_data.max_frame_average_light_level),
      static_cast <double> (pHdrColorData->mastering_display_data.max_content_light_level)
              ), __SK_SUBSYSTEM__ );

    SK_LOG0 ( ( L"HDR:  Color ( Bit-Depth: %s, Sampling: %s ), Dynamic Range: %s",
                bpc_map           [pHdrColorData->hdrBpc].         c_str (),
                color_fmt_map     [pHdrColorData->hdrColorFormat]. c_str (),
                dynamic_range_map [pHdrColorData->hdrDynamicRange].c_str ()
              ), __SK_SUBSYSTEM__ );
  };

  auto _Push_NvAPI_HDR_Metadata_to_DXGI_Backend = [&](void) ->
  void
  {
    rb.working_gamut.maxY =
      pHdrColorData->mastering_display_data.max_display_mastering_luminance * 0.00001f;
    rb.working_gamut.minY =
     pHdrColorData->mastering_display_data.min_display_mastering_luminance  *  0.0001f;

    rb.working_gamut.maxLocalY =
      pHdrColorData->mastering_display_data.max_frame_average_light_level   * 0.00001f;

    rb.working_gamut.xr = (float)pHdrColorData->mastering_display_data.displayPrimary_x0 /
                          (float)50000.0f;
    rb.working_gamut.xg = (float)pHdrColorData->mastering_display_data.displayPrimary_x1 /
                          (float)50000.0f;
    rb.working_gamut.xb = (float)pHdrColorData->mastering_display_data.displayPrimary_x2 /
                          (float)50000.0f;

    rb.working_gamut.yr = (float)pHdrColorData->mastering_display_data.displayPrimary_y0 /
                          (float)50000.0f;
    rb.working_gamut.yg = (float)pHdrColorData->mastering_display_data.displayPrimary_y1 /
                          (float)50000.0f;
    rb.working_gamut.yb = (float)pHdrColorData->mastering_display_data.displayPrimary_y2 /
                          (float)50000.0f;

    rb.working_gamut.Xw = (float)pHdrColorData->mastering_display_data.displayWhitePoint_x /
                          (float)50000.0f;
    rb.working_gamut.Yw = (float)pHdrColorData->mastering_display_data.displayWhitePoint_y /
                          (float)50000.0f;
    rb.working_gamut.Zw = 1.0f;
  };

  if (pHdrColorData->cmd == NV_HDR_CMD_SET)
  {
    _LogGameRequestedValues ();

    NvAPI_Status ret =
      NvAPI_Disp_HdrColorControl_Original ( displayId, pHdrColorData );

    if (NVAPI_OK == ret)
    {
      rb.scanout.nvapi_hdr.raw.display_id = displayId;

      if (pHdrColorData->hdrMode != NV_HDR_MODE_OFF)
      {
        rb.setHDRCapable       (true);
        *SK_NvAPI_LastHdrColorControl =
          std::make_pair (ret, std::make_pair (displayId, *pHdrColorData));

        rb.scanout.nvapi_hdr.mode = pHdrColorData->hdrMode;

        NV_HDR_COLOR_DATA query_data         = { };
                          query_data.version = NV_HDR_COLOR_DATA_VER;
                          query_data.cmd     = NV_HDR_CMD_GET;

        NvAPI_Disp_HdrColorControl_Original (displayId, &query_data);

        rb.scanout.nvapi_hdr.color_format   = query_data.hdrColorFormat;
        rb.scanout.nvapi_hdr.dynamic_range  = query_data.hdrDynamicRange;
        rb.scanout.nvapi_hdr.bpc            = query_data.hdrBpc;

        rb.scanout.nvapi_hdr.active         =
       (rb.scanout.nvapi_hdr.mode != NV_HDR_MODE_OFF);

        memcpy ( &rb.scanout.nvapi_hdr.raw.hdr_data,
                   &query_data, sizeof (NV_HDR_COLOR_DATA) );
      }

      _Push_NvAPI_HDR_Metadata_to_DXGI_Backend ();
    }

    return ret;
  }

  NvAPI_Status ret =
    NvAPI_Disp_HdrColorControl_Original ( displayId, pHdrColorData );

  if (pHdrColorData->cmd == NV_HDR_CMD_GET)
  {
    _LogGameRequestedValues ();

    if (ret == NVAPI_OK)
    {
      rb.scanout.nvapi_hdr.mode           = pHdrColorData->hdrMode;
      rb.scanout.nvapi_hdr.color_format   = pHdrColorData->hdrColorFormat;
      rb.scanout.nvapi_hdr.dynamic_range  = pHdrColorData->hdrDynamicRange;
      rb.scanout.nvapi_hdr.bpc            = pHdrColorData->hdrBpc;
      rb.scanout.nvapi_hdr.active         =(pHdrColorData->hdrMode != NV_HDR_MODE_OFF);

      _Push_NvAPI_HDR_Metadata_to_DXGI_Backend ();
    }
  }

  if (inputData->version == NV_HDR_COLOR_DATA_VER1)
  {
    inputData->hdrMode                       = pHdrColorData->hdrMode;

    memcpy (     &inputData->mastering_display_data,
             &pHdrColorData->mastering_display_data,
          sizeof (inputData->mastering_display_data) );

    inputData->static_metadata_descriptor_id =
      pHdrColorData->static_metadata_descriptor_id;
  }

  return ret;
}

bool
SK_RenderBackend_V2::scan_out_s::
       nvapi_desc_s::setColorEncoding_HDR ( NV_COLOR_FORMAT fmt_,
                                            NV_BPC          bpc_,
                                            NvU32           display_id )
{
  if (display_id == std::numeric_limits <NvU32>::max ())
      display_id = raw.display_id;

  NV_HDR_COLOR_DATA
    dataGetSet         = { };
    dataGetSet.version = NV_HDR_COLOR_DATA_VER;
    dataGetSet.cmd     = NV_HDR_CMD_GET;

  if ( NVAPI_OK ==
         NvAPI_Disp_HdrColorControl_Original ( display_id, &dataGetSet ) )
  {
    dataGetSet.cmd = NV_HDR_CMD_SET;

    if (bpc_ != NV_BPC_DEFAULT)
      dataGetSet.hdrBpc = bpc_;

    if (fmt_ != NV_COLOR_FORMAT_DEFAULT)
      dataGetSet.hdrColorFormat = fmt_;

    dataGetSet.hdrDynamicRange = NV_DYNAMIC_RANGE_AUTO;
    dataGetSet.hdrMode         = mode;

    NV_COLOR_DATA
      colorSet       = { };
    colorSet.version = NV_COLOR_DATA_VER;
    colorSet.cmd     = NV_COLOR_CMD_GET;
    colorSet.size    = sizeof (NV_COLOR_DATA);

    NvAPI_Disp_ColorControl_Original (display_id, &colorSet);

    if ( NVAPI_OK ==
           NvAPI_Disp_HdrColorControl_Original ( display_id, &dataGetSet ) )
    {
      colorSet.cmd                       = NV_COLOR_CMD_SET;
      colorSet.data.colorSelectionPolicy = NV_COLOR_SELECTION_POLICY_USER;
      colorSet.data.colorFormat          = (NvU8)dataGetSet.hdrColorFormat;
      colorSet.data.bpc                  =       dataGetSet.hdrBpc;
      colorSet.data.dynamicRange         = NV_DYNAMIC_RANGE_AUTO;
      colorSet.data.colorimetry          = NV_COLOR_COLORIMETRY_AUTO;

      NvAPI_Disp_ColorControl_Original (display_id, &colorSet);

      SK_GetCurrentRenderBackend ().requestFullscreenMode ();

      raw.hdr_data  = dataGetSet;
      mode          = dataGetSet.hdrMode;
      bpc           = dataGetSet.hdrBpc;
      color_format  = dataGetSet.hdrColorFormat;
      dynamic_range = dataGetSet.hdrDynamicRange;

      NV_COLOR_DATA
        colorCheck                   = { };
        colorCheck.version           = NV_COLOR_DATA_VER;
        colorCheck.cmd               = NV_COLOR_CMD_IS_SUPPORTED_COLOR;
        colorCheck.data.colorimetry  = NV_COLOR_COLORIMETRY_AUTO;
        colorCheck.data.dynamicRange = NV_DYNAMIC_RANGE_AUTO;
        colorCheck.data.colorFormat  = NV_COLOR_FORMAT_RGB;
        colorCheck.data.bpc          = NV_BPC_10;

      if ( NVAPI_OK == NvAPI_Disp_ColorControl_Original (display_id, &colorCheck) )
        color_support_hdr.supports_10b_12b_444 |=  0x1;
      else
        color_support_hdr.supports_10b_12b_444 &= ~0x1;

      colorCheck.data.colorFormat = NV_COLOR_FORMAT_RGB;
      colorCheck.data.bpc         = NV_BPC_12;

      if ( NVAPI_OK == NvAPI_Disp_ColorControl_Original (display_id, &colorCheck) )
        color_support_hdr.supports_10b_12b_444 |=  0x2;
      else
        color_support_hdr.supports_10b_12b_444 &= ~0x2;

      colorCheck.data.colorFormat = NV_COLOR_FORMAT_YUV422;
      colorCheck.data.bpc         = NV_BPC_12;

      if ( NVAPI_OK == NvAPI_Disp_ColorControl_Original (display_id, &colorCheck) )
        color_support_hdr.supports_YUV422_12bit = 1;
      else
        color_support_hdr.supports_YUV422_12bit = 0;

      return true;
    }
  }

  return false;
}

bool
SK_RenderBackend_V2::scan_out_s::
       nvapi_desc_s::setColorEncoding_SDR ( NV_COLOR_FORMAT fmt_,
                                            NV_BPC          bpc_,
                                            NvU32           display_id )
{
  if (display_id == std::numeric_limits <NvU32>::max ())
  {
    display_id =
      SK_NvAPI_GetDefaultDisplayId ();
  }

  NV_COLOR_DATA
    colorSet       = { };
  colorSet.version = NV_COLOR_DATA_VER;
  colorSet.cmd     = NV_COLOR_CMD_GET;
  colorSet.size    = sizeof (NV_COLOR_DATA);

  NvAPI_Disp_ColorControl_Original (display_id, &colorSet);

  colorSet.cmd                       = NV_COLOR_CMD_SET;
  colorSet.data.colorSelectionPolicy = NV_COLOR_SELECTION_POLICY_USER;
  colorSet.data.colorFormat          = (NvU8)fmt_;
  colorSet.data.bpc                  =       bpc_;
  colorSet.data.dynamicRange         = NV_DYNAMIC_RANGE_AUTO;
  colorSet.data.colorimetry          = NV_COLOR_COLORIMETRY_AUTO;

  if ( NVAPI_OK ==
         NvAPI_Disp_ColorControl_Original ( display_id, &colorSet ) )
  {
    SK_GetCurrentRenderBackend ().requestFullscreenMode ();

    mode          = NV_HDR_MODE_OFF;
    bpc           = colorSet.data.bpc;
    color_format  = (NV_COLOR_FORMAT)colorSet.data.colorFormat;
    dynamic_range = (NV_DYNAMIC_RANGE)colorSet.data.dynamicRange;

    return true;
  }

  return false;
}


using NvAPI_QueryInterface_pfn = void* (*)(unsigned int ordinal);
      NvAPI_QueryInterface_pfn
      NvAPI_QueryInterface_Original = nullptr;

#define threadsafe_unordered_set Concurrency::concurrent_unordered_set

void*
NvAPI_QueryInterface_Detour (unsigned int ordinal)
{
  static
    threadsafe_unordered_set <unsigned int>
      logged_ordinals;
  if (logged_ordinals.count  (ordinal) == 0)
  {   logged_ordinals.insert (ordinal);

    void* pAddr =
      NvAPI_QueryInterface_Original (ordinal);

    dll_log->Log ( L"NvAPI Ordinal: %lu [%p]  --  %s", ordinal,
                     pAddr, SK_SummarizeCaller ().c_str ()    );

    return
      pAddr;
  }

  return
    NvAPI_QueryInterface_Original (ordinal);
}


BOOL bLibShutdown = FALSE;
BOOL bLibInit     = FALSE;

BOOL
NVAPI::UnloadLibrary (void)
{
  if ( bLibInit     == TRUE &&
       bLibShutdown == FALSE  )
  {
    // Whine very loudly if this fails, because that's not
    //   supposed to happen!
    NVAPI_VERBOSE ()

    NvAPI_Status ret =
      NvAPI_Unload ();//NVAPI_CALL2 (Unload (), ret);

    if (ret == NVAPI_OK)
    {
      bLibShutdown = TRUE;
      bLibInit     = FALSE;
    }
  }

  return
    bLibShutdown;
}

void
SK_NvAPI_PreInitHDR (void)
{
  if (NvAPI_Disp_HdrColorControl_Original == nullptr)
  {
#ifdef _WIN64
    HMODULE hLib =
      SK_Modules->LoadLibraryLL (L"nvapi64.dll");

    if (hLib)
    {
      GetModuleHandleEx (
        GET_MODULE_HANDLE_EX_FLAG_PIN, L"nvapi64.dll", &hLib
      );
#else
    HMODULE hLib =
      SK_Modules->LoadLibraryLL (L"nvapi.dll");

    if (hLib)
    {
      GetModuleHandleEx (
        GET_MODULE_HANDLE_EX_FLAG_PIN, L"nvapi.dll",   &hLib
      );
#endif

      static auto NvAPI_QueryInterface =
        reinterpret_cast <NvAPI_QueryInterface_pfn> (
          SK_GetProcAddress (hLib, "nvapi_QueryInterface")
        );

      if (NvAPI_QueryInterface != nullptr)
      {
        SK_CreateFuncHook ( L"NvAPI_Disp_ColorControl",
                              NvAPI_QueryInterface (2465847309),
                              NvAPI_Disp_ColorControl_Override,
     static_cast_p2p <void> (&NvAPI_Disp_ColorControl_Original) );
        SK_CreateFuncHook ( L"NvAPI_Disp_HdrColorControl",
                              NvAPI_QueryInterface (891134500),
                              NvAPI_Disp_HdrColorControl_Override,
    static_cast_p2p <void> (&NvAPI_Disp_HdrColorControl_Original) );

        SK_CreateFuncHook ( L"NvAPI_Disp_GetHdrCapabilities",
                              NvAPI_QueryInterface (2230495455),
                              NvAPI_Disp_GetHdrCapabilities_Override,
     static_cast_p2p <void> (&NvAPI_Disp_GetHdrCapabilities_Original) );

        MH_QueueEnableHook (NvAPI_QueryInterface (891134500));
        MH_QueueEnableHook (NvAPI_QueryInterface (2230495455));
        MH_QueueEnableHook (NvAPI_QueryInterface (2465847309));
      }
    }
  }
}

BOOL
NVAPI::InitializeLibrary (const wchar_t* wszAppName)
{
  // It's silly to call this more than once, but not necessarily
  //  an error... just ignore repeated calls.
  if (bLibInit == TRUE)
    return TRUE;

  // If init is not false and not true, it's because we failed to
  //   initialize the API once before. Just return the failure status
  //     again.
  if (bLibInit != FALSE)
    return FALSE;

  app_name      = wszAppName;
  friendly_name = wszAppName; // Not so friendly, but whatever...

  NvAPI_Status ret;

  if (! config.apis.NvAPI.enable) {
    nv_hardware = false;
    bLibInit    = TRUE + 1; // Clearly this isn't a boolean; just for looks
    return FALSE;
  }

  // We want this error to be silent, because this tool works on AMD GPUs too!
  NVAPI_SILENT ()
  {
    NVAPI_CALL2 (Initialize (), ret);
  }
  NVAPI_VERBOSE ()

  if (ret != NVAPI_OK) {
    nv_hardware = false;
    bLibInit    = TRUE + 1; // Clearly this isn't a boolean; just for looks
    return FALSE;
  }
  else {
    // True unless we fail the stuff below...
    nv_hardware = true;

    //
    // Time to initialize a few undocumented (if you do not sign an NDA)
    //   parts of NvAPI, hurray!
    //
    static HMODULE hLib = nullptr;

#ifdef _WIN64
    GetModuleHandleEx (GET_MODULE_HANDLE_EX_FLAG_PIN, L"nvapi64.dll", &hLib);
#else
    GetModuleHandleEx (GET_MODULE_HANDLE_EX_FLAG_PIN, L"nvapi.dll",   &hLib);
#endif

  if (hLib != nullptr)
  {
    static auto NvAPI_QueryInterface =
      reinterpret_cast <NvAPI_QueryInterface_pfn> (
        SK_GetProcAddress (hLib, "nvapi_QueryInterface")
      );

    NvAPI_GetPhysicalGPUFromGPUID =
      (NvAPI_GetPhysicalGPUFromGPUID_pfn)NvAPI_QueryInterface   (0x5380AD1Au);
    NvAPI_GetGPUIDFromPhysicalGPU =
      (NvAPI_GetGPUIDFromPhysicalGPU_pfn)NvAPI_QueryInterface   (0x6533EA3Eu);

    if (NvAPI_GetPhysicalGPUFromGPUID == nullptr) {
      dll_log->LogEx (false, L"missing NvAPI_GetPhysicalGPUFromGPUID ");
      nv_hardware = false;
    }

    if (NvAPI_GetGPUIDFromPhysicalGPU == nullptr) {
      dll_log->LogEx (false, L"missing NvAPI_GetGPUIDFromPhysicalGPU ");
      nv_hardware = false;
    }


    if (NvAPI_Disp_HdrColorControl_Original == nullptr)
    {
      SK_CreateFuncHook ( L"NvAPI_Disp_HdrColorControl",
                            NvAPI_QueryInterface (891134500),
                            NvAPI_Disp_HdrColorControl_Override,
   static_cast_p2p <void> (&NvAPI_Disp_HdrColorControl_Original) );

      SK_CreateFuncHook ( L"NvAPI_Disp_GetHdrCapabilities",
                            NvAPI_QueryInterface (2230495455),
                            NvAPI_Disp_GetHdrCapabilities_Override,
   static_cast_p2p <void> (&NvAPI_Disp_GetHdrCapabilities_Original) );

      MH_QueueEnableHook (NvAPI_QueryInterface (891134500));
      MH_QueueEnableHook (NvAPI_QueryInterface (2230495455));

    }
  }
  else
  {
      dll_log->Log (L"unable to complete LoadLibrary (...) ");
      nv_hardware = false;
    }

    if (nv_hardware == false)
    {
      bLibInit = FALSE;
      hLib     = nullptr;
    }
  }

  return
    ( bLibInit = TRUE );
}



bool SK_NvAPI_InitializeHDR (void)
{
  if (nv_hardware && NvAPI_Disp_GetHdrCapabilities_Original != nullptr)
  {
    NV_HDR_CAPABILITIES
      hdr_caps       = { };
    hdr_caps.version = NV_HDR_CAPABILITIES_VER;

    NvAPI_Disp_GetHdrCapabilities_Override (
      SK_NvAPI_GetDefaultDisplayId (), &hdr_caps
    );
  }

  // Not yet meaningful
  return true;
}
#endif