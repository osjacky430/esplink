#pragma once

#include "esp_common/constants.hpp"
#include <array>
#include <cstdint>

namespace esplink {

struct ImageHeader {
  std::uint8_t magic_number_                          = ESP32_MAGIC_NUMBER;
  std::uint8_t segment_num_                           = 0;
  std::uint8_t spi_mode_                              = 0;
  std::uint8_t spi_speed_and_flash_chip_size_         = 0;
  std::uint32_t entry_address_                        = 0;
  std::uint8_t wp_pin_                                = 0;
  std::array<std::uint8_t, 3> spi_pin_drive_settings_ = {0, 0, 0};
  std::uint16_t chip_id_                              = 0;
  std::uint8_t min_chip_rev_                          = 0;
  std::array<std::uint8_t, 8> reserved_               = {0, 0, 0, 0, 0, 0, 0, 0};
  std::uint8_t hash_                                  = 0;
};

struct ImageSegmentHeader {
  std::uint32_t load_addr_;
  std::uint32_t section_length_;
};

}  // namespace esplink