#include "ImagePixelLimit.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>

#include <inference-addon-cpp/Errors.hpp>

#include "addon/LlmErrors.hpp"
#include "inference-addon-cpp/Logger.hpp"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <stb_image.h>

namespace image_pixel_limit {
namespace {
constexpr uint64_t PIXELS_PER_MEGAPIXEL = 1'000'000;
constexpr std::string_view CONFIG_KEY = "image-max-megapixels";

qvac_errors::StatusError
invalidFile(const std::string& path, const std::string& reason) {
  return {
      qvac_lib_inference_addon_llama::errors::ADDON_ID,
      qvac_errors::general_error::toString(
          qvac_errors::general_error::InvalidArgument),
      "[ImagePixelLimit] " + reason + ": " + path};
}

std::filesystem::path utf8FilePath(const std::string& path) {
  try {
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
  } catch (const std::system_error&) {
    throw invalidFile(path, "Invalid UTF-8 file path");
  }
}

size_t regularFileSize(const std::string& path) {
  const auto filePath = utf8FilePath(path);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(filePath, ec) || ec) {
    throw invalidFile(path, "Media path is not a regular file");
  }
  const auto size = std::filesystem::file_size(filePath, ec);
  if (ec || size > std::numeric_limits<size_t>::max() ||
      size >
          static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    throw invalidFile(path, "Cannot read media file size");
  }
  return static_cast<size_t>(size);
}

bool isMtmdAudio(const uint8_t* data, size_t size) {
  if (size < 12) {
    return false;
  }
  const bool wav = std::memcmp(data, "RIFF", 4) == 0 &&
                   std::memcmp(data + 8, "WAVE", 4) == 0;
  const bool mp3 = std::memcmp(data, "ID3", 3) == 0 ||
                   (data[0] == 0xff && (data[1] & 0xe0) == 0xe0);
  const bool flac = std::memcmp(data, "fLaC", 4) == 0;
  return wav || mp3 || flac;
}

[[noreturn]] void rejectUnknownDimensions(const std::string& path = {}) {
  std::string message =
      "[ImagePixelLimit] Cannot determine image dimensions before decoding. "
      "Input was rejected to enforce image-max-megapixels.";
  if (!path.empty()) {
    message += " File: " + path;
  }
  QLOG(qvac_lib_inference_addon_cpp::logger::Priority::WARNING, message);
  throw qvac_errors::StatusError(
      qvac_lib_inference_addon_llama::errors::ADDON_ID,
      qvac_errors::general_error::toString(
          qvac_errors::general_error::InvalidArgument),
      message);
}

struct BufferCursor {
  const uint8_t* data;
  size_t size;
  size_t offset = 0;
};

int readBuffer(void* user, char* data, int count) {
  auto& cursor = *static_cast<BufferCursor*>(user);
  if (count <= 0) {
    return 0;
  }
  const auto available = cursor.size - cursor.offset;
  const auto length = std::min(available, static_cast<size_t>(count));
  std::memcpy(data, cursor.data + cursor.offset, length);
  cursor.offset += length;
  return static_cast<int>(length);
}

void skipBuffer(void* user, int count) {
  auto& cursor = *static_cast<BufferCursor*>(user);
  if (count >= 0) {
    cursor.offset +=
        std::min(cursor.size - cursor.offset, static_cast<size_t>(count));
  } else {
    const auto backward = static_cast<uint64_t>(-static_cast<int64_t>(count));
    cursor.offset -= std::min(cursor.offset, static_cast<size_t>(backward));
  }
}

int endOfBuffer(void* user) {
  const auto& cursor = *static_cast<BufferCursor*>(user);
  return cursor.offset == cursor.size;
}

void checkDimensions(int width, int height, uint64_t maxPixels) {
  const auto pixels =
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
  if (pixels <= maxPixels) {
    return;
  }

  const std::string message =
      "[ImagePixelLimit] Image " + std::to_string(width) + " x " +
      std::to_string(height) + " exceeds the " +
      std::to_string(maxPixels / PIXELS_PER_MEGAPIXEL) +
      " MP limit. Increase image-max-megapixels to allow larger images.";
  QLOG(qvac_lib_inference_addon_cpp::logger::Priority::WARNING, message);
  throw qvac_errors::StatusError(
      qvac_lib_inference_addon_llama::errors::ADDON_ID,
      qvac_errors::general_error::toString(
          qvac_errors::general_error::InvalidArgument),
      message);
}
} // namespace

uint64_t takeMaxPixels(std::unordered_map<std::string, std::string>& config) {
  const auto hyphen = config.find(std::string(CONFIG_KEY));
  const auto underscore = config.find("image_max_megapixels");
  if (hyphen != config.end() && underscore != config.end()) {
    throw qvac_errors::StatusError(
        qvac_lib_inference_addon_llama::errors::ADDON_ID,
        qvac_errors::general_error::toString(
            qvac_errors::general_error::InvalidArgument),
        "Set only one of image-max-megapixels and image_max_megapixels");
  }
  const auto it = hyphen != config.end() ? hyphen : underscore;
  if (it == config.end()) {
    return DEFAULT_MAX_PIXELS;
  }

  uint64_t megapixels = 0;
  const auto* begin = it->second.data();
  const auto* end = begin + it->second.size();
  const auto result = std::from_chars(begin, end, megapixels);
  if (result.ec != std::errc{} || result.ptr != end || megapixels == 0 ||
      megapixels >
          std::numeric_limits<uint64_t>::max() / PIXELS_PER_MEGAPIXEL) {
    throw qvac_errors::StatusError(
        qvac_lib_inference_addon_llama::errors::ADDON_ID,
        qvac_errors::general_error::toString(
            qvac_errors::general_error::InvalidArgument),
        "image-max-megapixels must be a positive integer");
  }
  config.erase(it);
  return megapixels * PIXELS_PER_MEGAPIXEL;
}

void checkBuffer(const uint8_t* data, size_t size, uint64_t maxPixels) {
  if (data == nullptr || size == 0) {
    throw qvac_errors::StatusError(
        qvac_lib_inference_addon_llama::errors::ADDON_ID,
        qvac_errors::general_error::toString(
            qvac_errors::general_error::InvalidArgument),
        "[ImagePixelLimit] Media buffer is empty");
  }
  // Keep this magic check aligned with mtmd-helper's audio dispatch.
  if (isMtmdAudio(data, size)) {
    return;
  }
  BufferCursor cursor{data, size};
  const stbi_io_callbacks callbacks{readBuffer, skipBuffer, endOfBuffer};
  int width = 0;
  int height = 0;
  int channels = 0;
  if (stbi_info_from_callbacks(
          &callbacks, &cursor, &width, &height, &channels) != 0) {
    checkDimensions(width, height, maxPixels);
  } else {
    rejectUnknownDimensions();
  }
}

void checkFile(const std::string& path, uint64_t maxPixels) {
  regularFileSize(path);
  std::ifstream stream(utf8FilePath(path), std::ios::binary);
  if (!stream) {
    throw invalidFile(path, "Failed to open media file");
  }
  std::array<uint8_t, 12> magic{};
  stream.read(reinterpret_cast<char*>(magic.data()), magic.size());
  if (isMtmdAudio(magic.data(), static_cast<size_t>(stream.gcount()))) {
    return;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  if (stbi_info(path.c_str(), &width, &height, &channels) != 0) {
    checkDimensions(width, height, maxPixels);
  } else {
    rejectUnknownDimensions(path);
  }
}

std::vector<uint8_t> readRegularFile(const std::string& path) {
  const auto size = regularFileSize(path);
  std::ifstream stream(utf8FilePath(path), std::ios::binary);
  if (!stream) {
    throw invalidFile(path, "Failed to open media file");
  }
  std::vector<uint8_t> media(size);
  stream.read(
      reinterpret_cast<char*>(media.data()),
      static_cast<std::streamsize>(media.size()));
  if (stream.gcount() != static_cast<std::streamsize>(size)) {
    throw invalidFile(path, "Failed to read complete media file");
  }
  return media;
}

} // namespace image_pixel_limit
