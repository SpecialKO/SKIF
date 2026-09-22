#include <utility/ini_reader.h>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stringapiset.h>

enum class Encoding
{
  Utf8,
  Utf16LE,
  Utf16BE,
  Utf32LE,
  Utf32BE
};

static Encoding
DetectEncoding (const std::vector<std::uint8_t>& data, size_t& bomSize)
{
  bomSize = 0;

  // UTF-8 BOM: EF BB BF
  if (data.size() >= 3 &&
      data[0] == 0xEF &&
      data[1] == 0xBB &&
      data[2] == 0xBF)
  {
    bomSize = 3;
    return Encoding::Utf8;
  }

  // UTF-32 LE BOM: FF FE 00 00
  if (data.size() >= 4 &&
      data[0] == 0xFF &&
      data[1] == 0xFE &&
      data[2] == 0x00 &&
      data[3] == 0x00)
  {
    bomSize = 4;
    return Encoding::Utf32LE;
  }

  // UTF-32 BE BOM: 00 00 FE FF
  if (data.size() >= 4 &&
      data[0] == 0x00 &&
      data[1] == 0x00 &&
      data[2] == 0xFE &&
      data[3] == 0xFF)
  {
    bomSize = 4;
    return Encoding::Utf32BE;
  }

  // UTF-16 LE BOM: FF FE
  if (data.size() >= 2 &&
      data[0] == 0xFF &&
      data[1] == 0xFE)
  {
    bomSize = 2;
    return Encoding::Utf16LE;
  }

  // UTF-16 BE BOM: FE FF
  if (data.size() >= 2 &&
      data[0] == 0xFE &&
      data[1] == 0xFF)
  {
    bomSize = 2;
    return Encoding::Utf16BE;
  }

  // No BOM: assume UTF-8.
  return Encoding::Utf8;
}

static std::wstring
DecodeUtf8 (const std::uint8_t* data, const size_t size)
{
  if (size == 0)
    return {};

  if (size > static_cast<size_t>(INT_MAX))
    throw std::runtime_error("UTF-8 data is too large");

  const int inputSize = static_cast<int>(size);

  const int outputSize = MultiByteToWideChar(
    CP_UTF8,
    MB_ERR_INVALID_CHARS,
    reinterpret_cast<const char*>(data),
    inputSize,
    nullptr,
    0);

  if (outputSize <= 0)
    throw std::runtime_error("Invalid UTF-8");

  std::wstring result(outputSize, L'\0');

  const int converted = MultiByteToWideChar(
    CP_UTF8,
    MB_ERR_INVALID_CHARS,
    reinterpret_cast<const char*>(data),
    inputSize,
    result.data(),
    outputSize);

  if (converted != outputSize)
    throw std::runtime_error("UTF-8 conversion failed");

  return result;
}

static std::wstring
DecodeUtf16 (const std::uint8_t* data, const size_t size, const Encoding encoding)
{
  if (size % 2 != 0)
    throw std::runtime_error("Invalid UTF-16 byte count");

  const size_t count = size / 2;

  std::wstring result;
  result.reserve(count);

  auto read16 = [&](size_t i) -> std::uint16_t
    {
      const std::uint8_t a = data[i * 2];
      const std::uint8_t b = data[i * 2 + 1];

      return (encoding == Encoding::Utf16LE)
        ? static_cast<std::uint16_t>( a | (b << 8))
        : static_cast<std::uint16_t>((a << 8) | b );
    };

  for (size_t i = 0; i < count; ++i)
  {
    const std::uint16_t ch = read16(i);

    // High surrogate.
    if (ch >= 0xD800 && ch <= 0xDBFF)
    {
      if (i + 1 >= count)
        throw std::runtime_error(
          "Invalid UTF-16 surrogate pair");

      const std::uint16_t low = read16(i + 1);

      if (low < 0xDC00 || low > 0xDFFF)
        throw std::runtime_error(
          "Invalid UTF-16 surrogate pair");

      result.push_back(static_cast<wchar_t>(ch));
      result.push_back(static_cast<wchar_t>(low));

      ++i;
    }
    // Unpaired low surrogate.
    else if (ch >= 0xDC00 && ch <= 0xDFFF)
    {
      throw std::runtime_error(
        "Invalid UTF-16 surrogate");
    }
    else
    {
      result.push_back(static_cast<wchar_t>(ch));
    }
  }

  return result;
}

static std::wstring
DecodeUtf32 (const std::uint8_t* data, const size_t size, const Encoding encoding)
{
  if (size % 4 != 0)
    throw std::runtime_error("Invalid UTF-32 byte count");

  const size_t count = size / 4;

  std::wstring result;
  result.reserve(count);

  auto read32 = [&](size_t i) -> std::uint32_t
  {
    const std::uint8_t* p = data + i * 4;

    if (encoding == Encoding::Utf32LE)
    {
      return
           static_cast<std::uint32_t>(p[0])        |
          (static_cast<std::uint32_t>(p[1]) << 8 ) |
          (static_cast<std::uint32_t>(p[2]) << 16) |
          (static_cast<std::uint32_t>(p[3]) << 24);
    }

    return
          (static_cast<std::uint32_t>(p[0]) << 24) |
          (static_cast<std::uint32_t>(p[1]) << 16) |
          (static_cast<std::uint32_t>(p[2]) << 8 ) |
           static_cast<std::uint32_t>(p[3]);
  };

  for (size_t i = 0; i < count; ++i)
  {
    const std::uint32_t cp = read32(i);

    // Invalid Unicode scalar values.
    if (cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF))
    {
      throw std::runtime_error(
        "Invalid UTF-32 code point");
    }

    // BMP character.
    if (cp <= 0xFFFF)
    {
      result.push_back(static_cast<wchar_t>(cp));
    }
    else
    {
      // Convert Unicode scalar value to UTF-16 surrogate pair.
      const std::uint32_t value = cp - 0x10000;

      const wchar_t high = static_cast<wchar_t>(
        0xD800 + (value >> 10));

      const wchar_t low = static_cast<wchar_t>(
        0xDC00 + (value & 0x3FF));

      result.push_back(high);
      result.push_back(low);
    }
  }

  return result;
}

static std::vector<std::uint8_t>
ReadAllBytes (const std::wstring& path)
{
  FILE* file = _wfopen(path.c_str(), L"rb");

  if (!file)
    throw std::runtime_error("_wfopen failed");

  if (_fseeki64(file, 0, SEEK_END) != 0)
  {
    fclose(file);
    throw std::runtime_error("_fseeki64 failed");
  }

  const __int64 size = _ftelli64(file);

  if (size < 0)
  {
    fclose(file);
    throw std::runtime_error("_ftelli64 failed");
  }

  if (_fseeki64(file, 0, SEEK_SET) != 0)
  {
    fclose(file);
    throw std::runtime_error("_fseeki64 failed");
  }

  std::vector<std::uint8_t> data(
    static_cast<size_t>(size));

  size_t offset = 0;

  while (offset < data.size())
  {
    const size_t remaining = data.size() - offset;

    // fread's size parameter is size_t, but use reasonably
    // sized chunks to avoid implementation-specific issues.
    const size_t chunk =
      (remaining > 1024 * 1024)
      ? 1024 * 1024
      : remaining;

    const size_t n = fread(
      data.data() + offset,
      1,
      chunk,
      file);

    if (n != chunk)
    {
      fclose(file);
      throw std::runtime_error("fread failed");
    }

    offset += n;
  }

  fclose(file);
  return data;
}

std::wstring
DERP_IniReader_ReadTextFile (const std::wstring& path)
{
  const std::vector<std::uint8_t> data = ReadAllBytes(path);

  size_t bomSize = 0;
  const Encoding encoding = DetectEncoding (data, bomSize);

  const std::uint8_t* content =
    data.data() + bomSize;

  const size_t contentSize =
    data.size() - bomSize;

  switch (encoding)
  {
  case Encoding::Utf8:
    return DecodeUtf8  (content, contentSize);

  case Encoding::Utf16LE:
  case Encoding::Utf16BE:
    return DecodeUtf16 (content, contentSize, encoding);

  case Encoding::Utf32LE:
  case Encoding::Utf32BE:
    return DecodeUtf32 (content, contentSize, encoding);
  }

  throw std::runtime_error("Unknown encoding");
}