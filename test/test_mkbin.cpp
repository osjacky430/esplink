#include "catch2/catch_test_macros.hpp"
#include "esp_common/constants.hpp"
#include "esp_common/utility.hpp"
#include "esp_mkbin/app_format.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iterator>
#include <ostream>

TEST_CASE("mkbin generate valid esp32 image file", "[Make ESP32 Image]") {
  std::fstream main("main.bin", std::ios::in | std::ios::binary);  //
  REQUIRE(main.good());

  std::vector<std::uint8_t> file_content;
  file_content.insert(file_content.begin(), std::istreambuf_iterator<char>(main), std::istreambuf_iterator<char>());

  SECTION("file size is aligned to 16 byte") {
    REQUIRE_FALSE(file_content.empty());
    CHECK(file_content.size() % 16 == 0);
  }

  SECTION("result binary have valid header") {
    std::array<std::uint8_t, sizeof(esplink::ImageHeader)> buffer{};
    std::copy_n(file_content.begin(), buffer.size(), buffer.begin());
    auto const header = std::bit_cast<esplink::ImageHeader>(buffer);

    CHECK(header.magic_number_ == esplink::ESP32_MAGIC_NUMBER);
    CHECK(header.segment_num_ == 5);
    CHECK(header.entry_address_ == 0x40380080U);
    CHECK(header.chip_id_ == esplink::to_underlying(esplink::ImageHeaderChipID::ESP32C3));
  }

  // CHECK(file_content.back() == 0x16);  // checksum
}