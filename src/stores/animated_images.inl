// Animated images: the APNG, GIF and WebP decoders of SKIF's animated covers (generic_library2.cpp), and
//   IsAnimatedImageFile. Kept in a file of their own so they can be tested on their own (tools\image_tests).
//
//   Include it in one file only, where stb_image is implemented: the GIF decoder uses stb_image's internal frame decoder
//     (stbi__gif_load_next), which only exists there. Before it, that file includes:
//       stb_image.h          with STB_IMAGE_IMPLEMENTATION, and GIF and PNG not excluded (STBI_ONLY_GIF, STBI_ONLY_PNG)
//       libwebp              src/webp/decode.h and src/webp/demux.h (packages_misc\libwebp)
//       the standard library <algorithm>, <cstring>, <filesystem>, <fstream>, <functional>, <memory>, <string>, <vector>
//   No Windows or DirectX types are used here.
//
// Animated covers (APNG, GIF, WebP), decoded frame by frame. Neither decoder SKIF uses gives the frames of an APNG (stb_image
//   and WIC both read only its default image), and stb_image's own GIF frame loader (stbi_load_gif_from_memory) keeps
//     every frame in memory at once, reads outside its buffer for frames that restore the previous one (v2.29), and
//       disposes of frames differently from browsers (see _DecodeGIF).
//       So all three are decoded here, one frame at a time:
//   APNG: each frame becomes a PNG of its own (the frame's size, the chunks shared by all frames, its data as IDAT),
//         which stb_image decodes; it is then drawn onto the canvas as the frame says (blend op, dispose op)
//   GIF:  stb_image's frame decoder (stbi__gif_load_next: internal, in the file that implements stb_image), which draws
//         onto the canvas itself
//   WebP: libwebp's animation decoder (WebPAnimDecoder), which draws onto the canvas itself

#pragma once

static uint32_t
_ReadBE32 (const uint8_t* p)
{
  return (static_cast <uint32_t> (p [0]) << 24) | (static_cast <uint32_t> (p [1]) << 16) |
         (static_cast <uint32_t> (p [2]) <<  8) |  static_cast <uint32_t> (p [3]);
}

static uint32_t
_ReadBE16 (const uint8_t* p)
{
  return (static_cast <uint32_t> (p [0]) << 8) | static_cast <uint32_t> (p [1]);
}

static void
_WriteBE32 (uint8_t* p, uint32_t v)
{
  p [0] = static_cast <uint8_t> (v >> 24); p [1] = static_cast <uint8_t> (v >> 16);
  p [2] = static_cast <uint8_t> (v >>  8); p [3] = static_cast <uint8_t> (v);
}

static bool
_IsChunk (const uint8_t* type, const char* name)
{
  return memcmp (type, name, 4) == 0;
}

static constexpr uint8_t _PNGSignature [8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

// A PNG chunk (length, type, data, CRC) appended to png. stb_image skips CRCs, but they are written properly anyway.
static void
_AppendPNGChunk (std::vector <uint8_t>& png, const char* type, const uint8_t* data, size_t length)
{
  struct crc_table_s {
    uint32_t entries [256] = { };

    crc_table_s (void)
    {
      for (uint32_t n = 0; n < 256; n++)
      {
        uint32_t c = n;

        for (int k = 0; k < 8; k++)
          c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);

        entries [n] = c;
      }
    }
  };

  static const crc_table_s crc_table; // Thread-safe initialization (worker threads)
  const uint32_t*          table = crc_table.entries;

  size_t pos = png.size ( );
  png.resize (pos + 12 + length);

  uint8_t* p = png.data ( ) + pos;
  _WriteBE32 (p, static_cast <uint32_t> (length));
  memcpy     (p + 4, type, 4);

  if (length > 0)
    memcpy   (p + 8, data, length);

  uint32_t crc = 0xFFFFFFFFu;

  for (size_t i = 4; i < 8 + length; i++)
    crc = table [(crc ^ p [i]) & 0xFF] ^ (crc >> 8);

  _WriteBE32 (p + 8 + length, crc ^ 0xFFFFFFFFu);
}

// How long a frame is shown (ms). Delays of 10 ms or less are shown for 100 ms, as browsers do (many GIFs rely on it).
static uint32_t
_AnimatedFrameDelay (uint32_t ms)
{
  return (ms <= 10) ? 100 : ms;
}

struct apng_s
{
  struct frame_s {
    uint32_t                width   = 0,
                            height  = 0,
                            x       = 0,
                            y       = 0,
                            delay   = 0; // ms
    uint8_t                 dispose = 0; // 0: none, 1: clear to transparent, 2: restore the canvas from before the frame
    uint8_t                 blend   = 0; // 0: replace, 1: alpha blend over the canvas
    std::vector <uint8_t>   data;        // The frame's zlib stream (IDAT / fdAT data, without the sequence numbers)
  };

  uint32_t                  width   = 0,
                            height  = 0;
  uint8_t                   ihdr [13] = { };
  std::vector <uint8_t>     shared;      // The chunks between IHDR and the image data (PLTE, tRNS, gAMA, iCCP, ...), as they are
  std::vector <frame_s>     frames;      // The animation's frames (the default image only if it is one of them)
};

// Parses an APNG. False if it is not a PNG, is not animated, or is malformed.
static bool
_ParseAPNG (const uint8_t* file, size_t size, apng_s& apng)
{
  if (size < 8 || memcmp (file, _PNGSignature, 8) != 0)
    return false;

  bool   hasIHDR   = false,
         hasACTL   = false,
         imageData = false; // Past the first IDAT
  int    current   = -1;    // The frame the data that follows belongs to (-1: none, e.g. a default image that is no frame)
  size_t pos       = 8;

  while (pos + 12 <= size)
  {
    const uint32_t length = _ReadBE32 (file + pos);
    const uint8_t* type   = file + pos + 4;
    const uint8_t* data   = file + pos + 8;

    if (length > size - pos - 12)
      return false; // Truncated

    if (_IsChunk (type, "IHDR"))
    {
      if (length != 13 || hasIHDR)
        return false;

      memcpy (apng.ihdr, data, 13);
      apng.width  = _ReadBE32 (data);
      apng.height = _ReadBE32 (data + 4);
      hasIHDR     = true;
    }

    else if (! hasIHDR)
      return false; // IHDR comes first

    else if (_IsChunk (type, "acTL"))
      hasACTL = true;

    else if (_IsChunk (type, "fcTL"))
    {
      if (length < 26)
        return false;

      apng_s::frame_s frame;
      frame.width   = _ReadBE32 (data +  4);
      frame.height  = _ReadBE32 (data +  8);
      frame.x       = _ReadBE32 (data + 12);
      frame.y       = _ReadBE32 (data + 16);
      frame.dispose = data [24];
      frame.blend   = data [25];

      const uint32_t num = _ReadBE16 (data + 20),
                     den = _ReadBE16 (data + 22);
      frame.delay   = _AnimatedFrameDelay (num * 1000 / ((den == 0) ? 100 : den));

      if (frame.width  == 0 || static_cast <uint64_t> (frame.x) + frame.width  > apng.width  ||
          frame.height == 0 || static_cast <uint64_t> (frame.y) + frame.height > apng.height ||
          frame.dispose > 2 || frame.blend > 1)
        return false;

      apng.frames.push_back (std::move (frame));
      current = static_cast <int> (apng.frames.size ( )) - 1;
    }

    else if (_IsChunk (type, "IDAT"))
    {
      imageData = true;

      if (current >= 0) // The default image is the first frame (its fcTL came first)
        apng.frames [current].data.insert (apng.frames [current].data.end ( ), data, data + length);
    }

    else if (_IsChunk (type, "fdAT"))
    {
      if (! imageData || current < 0 || length < 4)
        return false;

      apng.frames [current].data.insert (apng.frames [current].data.end ( ), data + 4, data + length);
    }

    else if (_IsChunk (type, "IEND"))
      break;

    // Shared by all the frames (palette, transparency, color space, ...): copied into each frame's PNG
    else if (! imageData)
      apng.shared.insert (apng.shared.end ( ), file + pos, file + pos + 12 + length);

    pos += 12 + static_cast <size_t> (length);
  }

  // Frames without data would be empty: not usable
  for (auto& frame : apng.frames)
    if (frame.data.empty ( ))
      return false;

  return (hasIHDR && hasACTL && apng.frames.size ( ) >= 2);
}

// Decodes the frames of an APNG, drawn onto its canvas (RGBA, width x height), and hands each one to onFrame (canvas,
//   delay in ms), which returns false to stop. Returns the number of frames handed over, or -1 on an error.
static int
_DecodeAPNG (const apng_s& apng, const std::function <bool (const uint8_t*, uint32_t)>& onFrame)
{
  const size_t           stride = static_cast <size_t> (apng.width) * 4;
  std::vector <uint8_t>  canvas   (stride * apng.height, 0), // Starts fully transparent
                         previous;                           // The canvas from before the frame (dispose op 2)
  std::vector <uint8_t>  png;
  int                    count  = 0;

  for (size_t i = 0; i < apng.frames.size ( ); i++)
  {
    const apng_s::frame_s& frame = apng.frames [i];

    // The frame as a PNG of its own
    uint8_t ihdr [13];
    memcpy     (ihdr, apng.ihdr, 13);
    _WriteBE32 (ihdr,     frame.width);
    _WriteBE32 (ihdr + 4, frame.height);

    png.assign      (_PNGSignature, _PNGSignature + 8);
    _AppendPNGChunk (png, "IHDR", ihdr, 13);
    png.insert      (png.end ( ), apng.shared.begin ( ), apng.shared.end ( ));
    _AppendPNGChunk (png, "IDAT", frame.data.data ( ), frame.data.size ( ));
    _AppendPNGChunk (png, "IEND", nullptr, 0);

    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory (png.data ( ), static_cast <int> (png.size ( )), &w, &h, &channels, STBI_rgb_alpha);

    if (pixels == nullptr || static_cast <uint32_t> (w) != frame.width || static_cast <uint32_t> (h) != frame.height)
    {
      stbi_image_free (pixels);
      return -1;
    }

    // Dispose op 2 restores the canvas from before this frame (the first frame clears it instead, as the spec says)
    uint8_t dispose = frame.dispose;

    if (dispose == 2 && i == 0)
      dispose = 1;

    if (dispose == 2)
      previous = canvas;

    // Draw the frame onto the canvas
    for (uint32_t y = 0; y < frame.height; y++)
    {
      const uint8_t* src = pixels + static_cast <size_t> (y) * frame.width * 4;
      uint8_t*       dst = canvas.data ( ) + (static_cast <size_t> (frame.y) + y) * stride + static_cast <size_t> (frame.x) * 4;

      if (frame.blend == 0) // Replace
        memcpy (dst, src, static_cast <size_t> (frame.width) * 4);

      else for (uint32_t x = 0; x < frame.width; x++, src += 4, dst += 4) // Alpha blend over (straight alpha)
      {
        const uint32_t sa = src [3];

        if (sa == 255)
          memcpy (dst, src, 4);

        else if (sa != 0)
        {
          const uint32_t u     = sa * 255,
                         v     = (255 - sa) * dst [3],
                         alpha = u + v;

          for (int c = 0; c < 3; c++)
            dst [c] = static_cast <uint8_t> ((src [c] * u + dst [c] * v + alpha / 2) / alpha);

          dst [3]   = static_cast <uint8_t> ((alpha + 127) / 255);
        }
      }
    }

    stbi_image_free (pixels);

    count++;

    if (! onFrame (canvas.data ( ), frame.delay))
      return count;

    // Dispose of the frame's area before the next frame
    if (dispose == 1)
    {
      for (uint32_t y = 0; y < frame.height; y++)
        memset (canvas.data ( ) + (static_cast <size_t> (frame.y) + y) * stride + static_cast <size_t> (frame.x) * 4, 0, static_cast <size_t> (frame.width) * 4);
    }

    else if (dispose == 2)
    {
      for (uint32_t y = 0; y < frame.height; y++)
      {
        const size_t offset = (static_cast <size_t> (frame.y) + y) * stride + static_cast <size_t> (frame.x) * 4;
        memcpy (canvas.data ( ) + offset, previous.data ( ) + offset, static_cast <size_t> (frame.width) * 4);
      }
    }
  }

  return count;
}

// The size of a GIF's canvas and how many frames it has (up to maxCount), by walking its blocks without decoding them.
//   False if it is not a GIF or is malformed before the first frame.
static bool
_ScanGIF (const uint8_t* file, size_t size, uint32_t& width, uint32_t& height, uint32_t& frames, uint32_t maxCount = UINT32_MAX)
{
  if (size < 13 || memcmp (file, "GIF8", 4) != 0)
    return false;

  width  = static_cast <uint32_t> (file [6]) | (static_cast <uint32_t> (file [7]) << 8);
  height = static_cast <uint32_t> (file [8]) | (static_cast <uint32_t> (file [9]) << 8);
  frames = 0;

  size_t pos = 13;

  if (file [10] & 0x80) // Global color table
    pos += 3 * (static_cast <size_t> (2) << (file [10] & 7));

  // Skips data sub-blocks (each: a size byte, then that many bytes; a size of 0 ends them)
  auto _SkipSubBlocks = [&](void) -> bool
  {
    while (pos < size)
    {
      const uint8_t length = file [pos++];

      if (length == 0)
        return true;

      pos += length;
    }

    return false;
  };

  while (pos < size && frames < maxCount)
  {
    const uint8_t block = file [pos++];

    if (block == 0x2C) // Image descriptor: x, y, width, height, flags, (local color table), LZW code size, image data
    {
      if (pos + 9 > size)
        break;

      const uint8_t flags = file [pos + 8];
      pos += 9;

      if (flags & 0x80)
        pos += 3 * (static_cast <size_t> (2) << (flags & 7));

      pos++; // LZW minimum code size

      if (! _SkipSubBlocks ( ))
        break;

      frames++;
    }

    else if (block == 0x21) // Extension: its label, then sub-blocks
    {
      pos++;

      if (! _SkipSubBlocks ( ))
        break;
    }

    else // 0x3B (trailer), or something unexpected
      break;
  }

  return (width > 0 && height > 0);
}

// Decodes the frames of a GIF (stb_image draws each onto its canvas, RGBA, width x height) and hands each one to onFrame
//   (canvas, delay in ms), which returns false to stop. Returns the number of frames handed over, or -1 on an error before
//     the first one. A GIF that breaks off after some frames keeps those.
static int
_DecodeGIF (const uint8_t* file, size_t size, const std::function <bool (const uint8_t*, uint32_t)>& onFrame)
{
  stbi__context context;
  stbi__start_mem (&context, file, static_cast <int> (size));

  if (! stbi__gif_test (&context))
    return -1;

  auto g = std::make_unique <stbi__gif> ( ); // About 40 KB, so not on the stack
  memset (g.get ( ), 0, sizeof (stbi__gif));

  int count = 0,
      comp  = 0;

  for (;;)
  {
    // The previous frame is disposed of here, as browsers do, rather than by stb_image, which restores both "restore to
    //   background" and "restore to previous" frames from the wrong canvas: its frame area (the pixels it decoded, in
    //     g->history) is cleared to transparent (2), or restored from the canvas as it was before that frame was drawn
    //       (3; stb_image keeps that canvas in g->background until the next frame). It then sees "do not dispose".
    if (count > 0)
    {
      const int dispose = (g->eflags & 0x1C) >> 2;

      if (dispose == 2 || dispose == 3)
      {
        const size_t pixels = static_cast <size_t> (g->w) * g->h;

        for (size_t i = 0; i < pixels; i++)
        {
          if (g->history [i])
          {
            if (dispose == 2)
              memset (g->out + i * 4, 0, 4);
            else
              memcpy (g->out + i * 4, g->background + i * 4, 4);
          }
        }

        g->eflags &= ~0x1C;
      }
    }

    stbi_uc* canvas =
      stbi__gif_load_next (&context, g.get ( ), &comp, 4, nullptr);

    if (canvas == nullptr || canvas == reinterpret_cast <stbi_uc*> (&context)) // An error, or the end of the GIF
      break;

    count++;

    if (! onFrame (canvas, _AnimatedFrameDelay (static_cast <uint32_t> (std::max (g->delay, 0)))))
      break;
  }

  STBI_FREE (g->out);
  STBI_FREE (g->history);
  STBI_FREE (g->background);

  return (count > 0) ? count : -1;
}

// WebP: "RIFF", the size, "WEBP"
static bool
_IsWebP (const uint8_t* file, size_t size)
{
  return (size >= 12 && memcmp (file, "RIFF", 4) == 0 && memcmp (file + 8, "WEBP", 4) == 0);
}

// The canvas of an animated WebP and how many frames it has. False if it is not animated, or is malformed.
static bool
_ScanWebP (const uint8_t* file, size_t size, uint32_t& width, uint32_t& height, uint32_t& frames)
{
  const WebPData data  = { file, size };
  WebPDemuxer*   demux = WebPDemux (&data);

  if (demux == nullptr)
    return false;

  width  = WebPDemuxGetI (demux, WEBP_FF_CANVAS_WIDTH);
  height = WebPDemuxGetI (demux, WEBP_FF_CANVAS_HEIGHT);
  frames = WebPDemuxGetI (demux, WEBP_FF_FRAME_COUNT);

  const uint32_t flags = WebPDemuxGetI (demux, WEBP_FF_FORMAT_FLAGS);

  WebPDemuxDelete (demux);

  return ((flags & ANIMATION_FLAG) != 0 && width > 0 && height > 0);
}

// Decodes the frames of an animated WebP (libwebp draws each onto its canvas, RGBA, width x height) and hands each one to
//   onFrame (canvas, delay in ms), which returns false to stop. Returns the number of frames handed over, or -1 on an error
//     before the first one.
static int
_DecodeWebP (const uint8_t* file, size_t size, const std::function <bool (const uint8_t*, uint32_t)>& onFrame)
{
  WebPAnimDecoderOptions options;

  if (! WebPAnimDecoderOptionsInit (&options))
    return -1;

  options.color_mode  = MODE_RGBA; // Straight alpha, as the other decoders give
  options.use_threads = 0;

  const WebPData   data    = { file, size };
  WebPAnimDecoder* decoder = WebPAnimDecoderNew (&data, &options);

  if (decoder == nullptr)
    return -1;

  int count    = 0,
      previous = 0; // When the previous frame ended (ms)

  while (WebPAnimDecoderHasMoreFrames (decoder))
  {
    uint8_t* canvas    = nullptr;
    int      timestamp = 0; // When this frame ends (ms)

    if (! WebPAnimDecoderGetNext (decoder, &canvas, &timestamp) || canvas == nullptr)
      break;

    count++;

    const int delay = std::max (timestamp - previous, 0);
    previous        = timestamp;

    if (! onFrame (canvas, _AnimatedFrameDelay (static_cast <uint32_t> (delay))))
      break;
  }

  WebPAnimDecoderDelete (decoder);

  return (count > 0) ? count : -1;
}

// A WebP as a still image (RGBA, width x height): an animated one's first frame. False if it is not a WebP, or cannot be decoded.
static bool
_DecodeWebPStill (const uint8_t* file, size_t size, uint32_t& width, uint32_t& height, std::vector <uint8_t>& rgba)
{
  WebPBitstreamFeatures features;

  if (! _IsWebP (file, size) || WebPGetFeatures (file, size, &features) != VP8_STATUS_OK)
    return false;

  if (features.has_animation)
  {
    uint32_t frames = 0;

    if (! _ScanWebP (file, size, width, height, frames))
      return false;

    _DecodeWebP (file, size, [&](const uint8_t* canvas, uint32_t) -> bool
    {
      rgba.assign (canvas, canvas + static_cast <size_t> (width) * height * 4);
      return false; // The first frame only
    });

    return (! rgba.empty ( ));
  }

  int      w      = 0,
           h      = 0;
  uint8_t* pixels = WebPDecodeRGBA (file, size, &w, &h);

  if (pixels == nullptr)
    return false;

  width  = static_cast <uint32_t> (w);
  height = static_cast <uint32_t> (h);
  rgba.assign (pixels, pixels + static_cast <size_t> (width) * height * 4);

  WebPFree (pixels);

  return true;
}


// Whether the image file is animated (declared in generic_library2.h)
bool
IsAnimatedImageFile (const std::wstring& path)
{
  std::ifstream file (std::filesystem::path (path), std::ios::binary);

  uint8_t header [8] = { };

  if (! file.read (reinterpret_cast <char*> (header), sizeof (header)))
    return false;

  // PNG: an acTL chunk before the image data (IDAT) makes it an APNG
  if (memcmp (header, _PNGSignature, 8) == 0)
  {
    uint8_t chunk [12] = { }; // Length, type, and the first 4 bytes of acTL's data (its number of frames)

    for (int i = 0; i < 1024 && file.read (reinterpret_cast <char*> (chunk), 8); i++)
    {
      const uint32_t length = _ReadBE32 (chunk);

      if (_IsChunk (chunk + 4, "acTL"))
        return (length >= 8 && file.read (reinterpret_cast <char*> (chunk + 8), 4) && _ReadBE32 (chunk + 8) >= 2);

      if (_IsChunk (chunk + 4, "IDAT") || _IsChunk (chunk + 4, "IEND"))
        return false;

      file.seekg (static_cast <std::streamoff> (length) + 4, std::ios::cur); // Data and CRC
    }

    return false;
  }

  // WebP: an extended file (VP8X) with its animation flag set
  if (memcmp (header, "RIFF", 4) == 0)
  {
    uint8_t extended [13] = { }; // "WEBP", "VP8X", its size, its flags

    if (! file.read (reinterpret_cast <char*> (extended), sizeof (extended)))
      return false;

    return (memcmp (extended, "WEBP", 4) == 0 && memcmp (extended + 4, "VP8X", 4) == 0 && (extended [12] & ANIMATION_FLAG) != 0);
  }

  // GIF: more than one frame (GIF covers are small enough to read whole)
  if (memcmp (header, "GIF8", 4) == 0)
  {
    std::error_code ec;
    const auto size = std::filesystem::file_size (std::filesystem::path (path), ec);

    if (ec || size > 64 * 1024 * 1024)
      return false;

    std::vector <uint8_t> data (static_cast <size_t> (size));
    file.seekg (0);

    if (! file.read (reinterpret_cast <char*> (data.data ( )), data.size ( )))
      return false;

    uint32_t width = 0, height = 0, frames = 0;
    return (_ScanGIF (data.data ( ), data.size ( ), width, height, frames, 2) && frames >= 2);
  }

  return false;
}
