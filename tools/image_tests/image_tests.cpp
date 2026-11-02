// Tests of the animated cover decoders (src/stores/animated_images.inl): APNG, GIF and WebP frames, delays, disposal and
//   blending, IsAnimatedImageFile, still WebPs, and malformed files. The expected frames (expected.txt, frames\) come from
//     reference decoders (ffmpeg, Pillow), see make_expected.py. Built and run by run_tests.ps1; returns 0 if all pass.
//
//   image_tests [folder with images\, frames\ and expected.txt]

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_PSD
#define STBI_ONLY_GIF
#include <stb_image.h>

#include <libwebp/src/webp/decode.h>
#include <libwebp/src/webp/demux.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/stores/animated_images.inl"

namespace fs = std::filesystem;

static int failures = 0,
           checks   = 0;

static void
Check (bool ok, const std::string& name, const std::string& what)
{
  checks++;

  if (! ok)
  {
    failures++;
    printf ("FAIL  %s: %s\n", name.c_str ( ), what.c_str ( ));
  }
}

// A frame's RGBA, with the color of fully transparent pixels taken as black (as in make_expected.py)
static std::vector <uint8_t>
Normalize (const uint8_t* rgba, size_t pixels)
{
  std::vector <uint8_t> out (rgba, rgba + pixels * 4);

  for (size_t i = 0; i < pixels; i++)
    if (out [i * 4 + 3] == 0)
      memset (&out [i * 4], 0, 3);

  return out;
}

// The largest difference of a channel between two frames. Blending semi-transparent pixels may differ by one between
//   decoders (rounding: ffmpeg truncates), so up to 1 is accepted.
static int
MaxDifference (const std::vector <uint8_t>& a, const std::vector <uint8_t>& b)
{
  if (a.size ( ) != b.size ( ))
    return 256;

  int most = 0;

  for (size_t i = 0; i < a.size ( ); i++)
    most = std::max (most, std::abs (static_cast <int> (a [i]) - static_cast <int> (b [i])));

  return most;
}

static std::vector <uint8_t>
ReadFile (const fs::path& path)
{
  std::ifstream         file (path, std::ios::binary);
  std::vector <uint8_t> data ((std::istreambuf_iterator <char> (file)), std::istreambuf_iterator <char> ( ));
  return data;
}

struct frame_s   { uint32_t delay = 0; std::vector <uint8_t> rgba; };
struct decoded_s { int result = -1; uint32_t width = 0, height = 0; std::vector <frame_s> frames; };

// Decodes the frames of an animated image as SKIF does (_OpenAnimatedImage, then its decoder). stopAfter: onFrame returns
//   false after that many frames.
static decoded_s
Decode (const std::vector <uint8_t>& file, size_t stopAfter = SIZE_MAX)
{
  decoded_s out;
  apng_s    apng;
  uint32_t  count = 0;

  const bool isWebP = _IsWebP (file.data ( ), file.size ( )),
             isGIF  = (file.size ( ) >= 4 && memcmp (file.data ( ), "GIF8", 4) == 0);

  const bool opened =
    (isWebP) ? _ScanWebP  (file.data ( ), file.size ( ), out.width, out.height, count) :
    (isGIF)  ? _ScanGIF   (file.data ( ), file.size ( ), out.width, out.height, count) :
               _ParseAPNG (file.data ( ), file.size ( ), apng);

  if (! opened)
    return out;

  if (! isWebP && ! isGIF)
    out.width = apng.width, out.height = apng.height;

  auto onFrame = [&] (const uint8_t* canvas, uint32_t delay)
  {
    out.frames.push_back ({ delay, Normalize (canvas, static_cast <size_t> (out.width) * out.height) });
    return out.frames.size ( ) < stopAfter;
  };

  out.result = (isWebP) ? _DecodeWebP (file.data ( ), file.size ( ), onFrame) :
               (isGIF)  ? _DecodeGIF  (file.data ( ), file.size ( ), onFrame)
                        : _DecodeAPNG (apng, onFrame);
  return out;
}

struct expected_s { std::string name; uint32_t width = 0, height = 0; std::vector <frame_s> frames; };

static std::vector <expected_s>
ReadExpected (const fs::path& folder)
{
  std::vector <expected_s> out;
  std::ifstream            file (folder / "expected.txt");
  std::string              line;

  while (std::getline (file, line))
  {
    if (! line.empty ( ) && line.back ( ) == '\r')
      line.pop_back ( );

    if (line.empty ( ))
      continue;

    std::istringstream s (line);

    if (line [0] != ' ')
    {
      expected_s e; size_t count = 0;
      s >> e.name >> count >> e.width >> e.height;
      out.push_back (e);
    }

    else if (! out.empty ( ))
    {
      frame_s f;
      s >> f.delay;
      out.back ( ).frames.push_back (f);
    }
  }

  // The frames, from frames\<file>.z
  for (auto& e : out)
  {
    const auto z    = ReadFile (folder / "frames" / (e.name + ".z"));
    int        size = 0;
    char*      rgba = stbi_zlib_decode_malloc (reinterpret_cast <const char*> (z.data ( )), static_cast <int> (z.size ( )), &size);
    const auto each = static_cast <size_t> (e.width) * e.height * 4;

    if (rgba != nullptr && each != 0 && static_cast <size_t> (size) == each * e.frames.size ( ))
      for (size_t i = 0; i < e.frames.size ( ); i++)
        e.frames [i].rgba.assign (rgba + i * each, rgba + (i + 1) * each);

    STBI_FREE (rgba);
  }

  return out;
}

int
main (int argc, char** argv)
{
  const fs::path folder = (argc > 1) ? fs::path (argv [1]) : fs::path (argv [0]).parent_path ( );
  const fs::path images = folder / "images";

  const auto all = ReadExpected (folder);
  Check (all.size ( ) >= 10, "expected.txt", "missing or incomplete (in " + folder.string ( ) + ")");

  // Animated images: the frames, as the reference decoders see them
  for (const auto& e : all)
  {
    const auto file = ReadFile (images / e.name);
    const auto got  = Decode   (file);

    Check (got.result == static_cast <int> (e.frames.size ( )), e.name, "decoded " + std::to_string (got.result) +
                                                                " frames, expected " + std::to_string (e.frames.size ( )));
    Check (got.width == e.width && got.height == e.height, e.name, "wrong size " + std::to_string (got.width) + "x" + std::to_string (got.height));

    for (size_t i = 0; i < std::min (got.frames.size ( ), e.frames.size ( )); i++)
    {
      Check (got.frames [i].delay == e.frames [i].delay, e.name, "frame " + std::to_string (i) + ": delay " +
                                    std::to_string (got.frames [i].delay) + " ms, expected " + std::to_string (e.frames [i].delay));
      const int difference = MaxDifference (got.frames [i].rgba, e.frames [i].rgba);
      Check (difference <= 1, e.name, "frame " + std::to_string (i) + ": pixels differ (by up to " + std::to_string (difference) + ")");
    }

    Check (IsAnimatedImageFile ((images / e.name).wstring ( )), e.name, "not seen as animated");

    // Stopped by onFrame after two frames: decodes no more
    const auto stopped = Decode (file, 2);
    Check (stopped.frames.size ( ) == 2, e.name, "did not stop after two frames");
  }

  // Not animated: still images, and files that are not images at all
  for (const char* name : { "still.png", "still.gif", "webp_still.webp", "webp_still_lossless.webp",
                            "garbage.png", "garbage.gif", "garbage.webp", "missing.png" })
    Check (! IsAnimatedImageFile ((images / name).wstring ( )), name, "seen as animated");

  // Still WebPs (FastTextureLoading): the size, and the pixels of a lossless one
  for (const char* name : { "webp_still.webp", "webp_still_lossless.webp" })
  {
    const auto            file = ReadFile (images / name);
    uint32_t              width = 0, height = 0;
    std::vector <uint8_t> rgba;

    Check (_DecodeWebPStill (file.data ( ), file.size ( ), width, height, rgba) && width == 60 && height == 90 &&
           rgba.size ( ) == 60 * 90 * 4, name, "not decoded");

    // The first frame of webp_lossy, losslessly: the background (20, 40, 60) in the top left corner
    if (strstr (name, "lossless") && rgba.size ( ) >= 4)
      Check (rgba [0] == 20 && rgba [1] == 40 && rgba [2] == 60 && rgba [3] == 255, name, "wrong pixels");
  }

  // Malformed: cut off halfway (the frames decoded before the cut, if any, must be right), or not images at all (must
  //   fail). Neither may crash or read outside the file (run under ASAN / the debug heap to see that).
  for (const auto& e : all)
  {
    fs::path cut = images / e.name;
    cut.replace_filename (cut.stem ( ).string ( ) + "_truncated" + cut.extension ( ).string ( ));

    if (! fs::exists (cut))
      continue;

    const auto got = Decode (ReadFile (cut));
    Check (got.frames.size ( ) < e.frames.size ( ), cut.filename ( ).string ( ), "decoded all the frames of a cut-off file");

    // The last frame decoded may be cut off itself, so only the ones before it are compared
    for (size_t i = 0; i + 1 < got.frames.size ( ); i++)
      Check (MaxDifference (got.frames [i].rgba, e.frames [i].rgba) <= 1, cut.filename ( ).string ( ), "frame " + std::to_string (i) + ": pixels differ");

    // And cut off everywhere else, a byte at a time near the start
    const auto full = ReadFile (images / e.name);

    for (size_t size = 0; size < full.size ( ); size += (size < 256) ? 1 : 61)
      Decode (std::vector <uint8_t> (full.begin ( ), full.begin ( ) + size));
  }

  for (const char* name : { "garbage.png", "garbage.gif", "garbage.webp" })
    Check (Decode (ReadFile (images / name)).result <= 0, name, "decoded");

  printf ("%s: %d of %d checks passed\n", (failures == 0) ? "PASS" : "FAIL", checks - failures, checks);
  return (failures == 0) ? 0 : 1;
}
