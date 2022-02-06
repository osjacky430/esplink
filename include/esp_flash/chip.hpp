#pragma once

#include "esp_common/constants.hpp"

namespace esplink {

static constexpr auto ESP_MAGIC_NUMBER = 0xE9;

enum class ChipID : std::uint32_t {
  Unknown       = 0,
  ESP8266       = 0xFFF0'C101U,
  ESP32_C3_ECO3 = 0x1B31'506FU,
};

inline auto get_chip_info(std::uint32_t const t_chip_id) noexcept {
  constexpr std::array chip_info_table{
    std::pair{ChipID::ESP8266, "ESP8266"},
    std::pair{ChipID::ESP32_C3_ECO3, "ESP32_C3_ECO3"},
  };

  auto const chip_matched = [=](auto t_entry) { return to_underlying(t_entry.first) == t_chip_id; };
  if (auto const result = std::find_if(chip_info_table.begin(), chip_info_table.end(), chip_matched);
      result != chip_info_table.end()) {
    return *result;
  }

  return std::pair{ChipID::Unknown, "Unknown"};
}

template <ImageHeaderChipID ChipID>
inline void set_binary_header(auto& t_buffer, std::uint8_t t_flash_mode, std::uint8_t t_flash_size,
                              std::uint8_t t_flash_freq) noexcept {
  using value_type = std::remove_reference_t<decltype(t_buffer)>::value_type;
  t_buffer[2]      = static_cast<value_type>(t_flash_mode);
  t_buffer[3]      = static_cast<value_type>((t_flash_size << 4) | (t_flash_freq & 0b1111));
  t_buffer[12]     = to_underlying(ChipID);
}

}  // namespace esplink