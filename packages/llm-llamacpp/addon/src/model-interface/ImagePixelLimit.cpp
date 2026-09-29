#include "ImagePixelLimit.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <string_view>

#include <inference-addon-cpp/Errors.hpp>

#include "addon/LlmErrors.hpp"
#include "inference-addon-cpp/Logger.hpp"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace image_pixel_limit {
namespace {
constexpr uint64_t PIXELS_PER_MEGAPIXEL = 1'000'000;
constexpr std::string_view CONFIG_KEY = "image-max-megapixels";

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
      "[MtmdLlm] Image " + std::to_string(width) + " x " +
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
  }
}

void checkFile(const std::string& path, uint64_t maxPixels) {
  int width = 0;
  int height = 0;
  int channels = 0;
  if (stbi_info(path.c_str(), &width, &height, &channels) != 0) {
    checkDimensions(width, height, maxPixels);
  }
}

} // namespace image_pixel_limit
