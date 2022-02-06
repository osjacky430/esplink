#pragma once

namespace esplink {

inline constexpr auto ESP32_CHECKSUM_MAGIC    = 0xEF;
inline constexpr auto ESP32_MAGIC_NUMBER      = 0xE9;
inline constexpr auto ESP32_IMAGE_MAX_SEGMENT = 3;

enum class ImageHeaderChipID : std::uint16_t {
  ESP32   = 0x0000,
  ESP32S2 = 0x0002,
  ESP32C3 = 0x0005,
  ESP32S3 = 0x0009,
  ESP32C2 = 0x000C,
  INVALID = 0xFFFF
};

}  // namespace esplink