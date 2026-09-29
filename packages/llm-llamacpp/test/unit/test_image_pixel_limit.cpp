#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>
#include <inference-addon-cpp/Errors.hpp>

#include "model-interface/ImagePixelLimit.hpp"

namespace {
std::vector<uint8_t> pngHeader(uint32_t width, uint32_t height) {
  std::vector<uint8_t> png{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00,
                           0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0,    0,
                           0,    0,    0,    0,    0,    0,    0x08, 0x02, 0x00,
                           0x00, 0x00, 0,    0,    0,    0,    0,    0,    0,
                           0,    0,    0,    0,    0};
  for (int index = 0; index < 4; ++index) {
    png[16 + index] = static_cast<uint8_t>(width >> (24 - index * 8));
    png[20 + index] = static_cast<uint8_t>(height >> (24 - index * 8));
  }
  png[37] = 'I';
  png[38] = 'D';
  png[39] = 'A';
  png[40] = 'T';
  return png;
}

void expectRejected(const std::function<void()>& check) {
  try {
    check();
    FAIL() << "Expected an oversized image to be rejected";
  } catch (const qvac_errors::StatusError& error) {
    EXPECT_NE(error.codeString().find("InvalidArgument"), std::string::npos);
    EXPECT_NE(
        std::string(error.what()).find("image-max-megapixels"),
        std::string::npos);
  }
}
} // namespace

TEST(ImagePixelLimit, DefaultAndConfigOverride) {
  std::unordered_map<std::string, std::string> config;
  EXPECT_EQ(
      image_pixel_limit::takeMaxPixels(config),
      image_pixel_limit::DEFAULT_MAX_PIXELS);

  config["image_max_megapixels"] = "64";
  EXPECT_EQ(image_pixel_limit::takeMaxPixels(config), 64'000'000u);
  EXPECT_TRUE(config.empty());

  config["image-max-megapixels"] = "32";
  EXPECT_EQ(image_pixel_limit::takeMaxPixels(config), 32'000'000u);
  EXPECT_TRUE(config.empty());
}

TEST(ImagePixelLimit, RejectsInvalidConfig) {
  for (const std::string value : {"0", "-1", "1.5", "abc", "18446744073710"}) {
    std::unordered_map<std::string, std::string> config{
        {"image-max-megapixels", value}};
    EXPECT_THROW(
        image_pixel_limit::takeMaxPixels(config), qvac_errors::StatusError);
  }
  std::unordered_map<std::string, std::string> duplicate{
      {"image-max-megapixels", "50"}, {"image_max_megapixels", "50"}};
  EXPECT_THROW(
      image_pixel_limit::takeMaxPixels(duplicate), qvac_errors::StatusError);
}

TEST(ImagePixelLimit, BufferBoundaryAndOverride) {
  auto atLimit = pngHeader(10'000, 5'000);
  auto overLimit = pngHeader(10'000, 5'001);
  EXPECT_NO_THROW(
      image_pixel_limit::checkBuffer(
          atLimit.data(), atLimit.size(), 50'000'000));
  expectRejected([&] {
    image_pixel_limit::checkBuffer(
        overLimit.data(), overLimit.size(), 50'000'000);
  });
  EXPECT_NO_THROW(
      image_pixel_limit::checkBuffer(
          overLimit.data(), overLimit.size(), 64'000'000));
}

TEST(ImagePixelLimit, FileIsRejectedBeforeDecode) {
  const auto path =
      std::filesystem::temp_directory_path() /
      ("qvac25639-image-limit-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".png");
  const auto header = pngHeader(16'384, 16'384);
  {
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.is_open());
    out.write(
        reinterpret_cast<const char*>(header.data()),
        static_cast<std::streamsize>(header.size()));
  }
  expectRejected(
      [&] { image_pixel_limit::checkFile(path.string(), 50'000'000); });
  EXPECT_NO_THROW(image_pixel_limit::checkFile(path.string(), 300'000'000));
  std::filesystem::remove(path);
}
