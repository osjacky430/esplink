#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <string>
#include <string_view>

#include "esp_common/constants.hpp"
#include "esp_common/utility.hpp"

namespace esplink::command {

struct SYNC {
  static constexpr std::string_view NAME     = "SYNC";
  static constexpr std::uint8_t COMMAND_BYTE = 0x08;
  static constexpr std::size_t PACKET_SIZE   = 36;

  constexpr auto operator()() const noexcept {
    constexpr auto v = []() {
      std::array<std::uint8_t, PACKET_SIZE> buff{};
      std::fill(buff.begin(), buff.end(), 0x55);
      buff[0] = 0x07;
      buff[1] = 0x07;
      buff[2] = 0x12;
      buff[3] = 0x20;
      return buff;
    }();
    return v;
  }
};

template <std::uint32_t Addr, std::uint32_t Val, std::uint32_t Mask, std::uint32_t Delay>
struct WRITE_REG {
  static constexpr std::string_view NAME     = "WRITE_REG";
  static constexpr std::uint8_t COMMAND_BYTE = 0x09;

  constexpr auto operator()() const noexcept {
    constexpr auto ret_val = []() {
      std::array<std::uint8_t, 4 * sizeof(std::uint32_t)> v{};

      constexpr auto addr_arr  = word_to_byte_array(Addr);
      constexpr auto val_arr   = word_to_byte_array(Val);
      constexpr auto mask_arr  = word_to_byte_array(Mask);
      constexpr auto delay_arr = word_to_byte_array(Delay);

      auto* iter = std::copy_n(addr_arr.begin(), addr_arr.size(), v.begin());
      iter       = std::copy_n(val_arr.begin(), val_arr.size(), iter);
      iter       = std::copy_n(mask_arr.begin(), mask_arr.size(), iter);
      std::copy_n(delay_arr.begin(), delay_arr.size(), iter);

      return v;
    }();

    return ret_val;
  }
};

template <std::uint32_t Addr>
class READ_REG {
  static constexpr std::array<std::uint8_t, sizeof(Addr)> BYTE_ARRAY =
    std::bit_cast<std::array<std::uint8_t, sizeof(Addr)>>(Addr);

 public:
  static constexpr std::string_view NAME     = "READ_REG";
  static constexpr std::uint8_t COMMAND_BYTE = 0x0A;

  constexpr auto operator()() const noexcept { return BYTE_ARRAY; }
};

struct SPI_ATTACH {
  static constexpr std::string_view NAME     = "SPI_ATTACH";
  static constexpr std::uint8_t COMMAND_BYTE = 0x0D;

  constexpr auto operator()() const noexcept { return std::array<std::uint8_t, 6>{0, 0, 0, 0, 0, 0}; }
};

template <std::uint32_t FlashSize = 4 * 1024 * 1024>
struct SPI_SET_PARAMS {
  static constexpr std::string_view NAME     = "SPI_SET_PARAMS";
  static constexpr std::uint8_t COMMAND_BYTE = 0x0B;
  static constexpr std::size_t PACKET_SIZE   = 6 * sizeof(std::uint32_t);

  constexpr auto operator()() const noexcept {
    constexpr auto ret_val = []() {
      std::array<std::uint8_t, PACKET_SIZE> v{};
      constexpr auto flash_size_arr  = word_to_byte_array(FlashSize);
      constexpr auto block_size_arr  = word_to_byte_array(64 * 1024);
      constexpr auto sector_size_arr = word_to_byte_array(4 * 1024);
      constexpr auto page_size_arr   = word_to_byte_array(256);
      constexpr auto status_mask_arr = word_to_byte_array(0xFFFF);

      auto* iter = std::fill_n(v.begin(), 4, 0);
      iter       = std::copy_n(flash_size_arr.begin(), flash_size_arr.size(), iter);
      iter       = std::copy_n(block_size_arr.begin(), block_size_arr.size(), iter);
      iter       = std::copy_n(sector_size_arr.begin(), sector_size_arr.size(), iter);
      iter       = std::copy_n(page_size_arr.begin(), page_size_arr.size(), iter);
      std::copy_n(status_mask_arr.begin(), status_mask_arr.size(), iter);

      return v;
    }();
    return ret_val;
  }
};

struct FLASH_BEGIN {
  std::uint32_t erase_size_{};
  std::uint32_t packet_count_{};
  std::uint32_t data_size_per_packet_{};
  std::uint32_t flash_offset_{};
  std::uint32_t rom_encrypted_write_ = 0;  // ??

  static constexpr std::string_view NAME     = "FLASH_BEGIN";
  static constexpr std::uint8_t COMMAND_BYTE = 0x02;
  static constexpr std::size_t PACKET_SIZE   = 5 * sizeof(std::uint32_t);

  constexpr auto operator()() const noexcept {
    std::array<std::uint8_t, PACKET_SIZE> ret_val{};
    auto erase_size_arr          = word_to_byte_array(this->erase_size_);
    auto packet_count_arr        = word_to_byte_array(this->packet_count_);
    auto data_size_arr           = word_to_byte_array(this->data_size_per_packet_);
    auto flash_offset_arr        = word_to_byte_array(this->flash_offset_);
    auto rom_encrypted_write_arr = word_to_byte_array(this->rom_encrypted_write_);

    auto* iter = std::move(erase_size_arr.begin(), erase_size_arr.end(), ret_val.begin());
    iter       = std::move(packet_count_arr.begin(), packet_count_arr.end(), iter);
    iter       = std::move(data_size_arr.begin(), data_size_arr.end(), iter);
    iter       = std::move(flash_offset_arr.begin(), flash_offset_arr.end(), iter);
    std::move(rom_encrypted_write_arr.begin(), rom_encrypted_write_arr.end(), iter);

    return ret_val;
  }
};

template <std::size_t WriteDataSize>
struct FLASH_DATA {
  static constexpr std::size_t DATA_PACKET = 16;
  std::uint32_t flash_size_;
  std::uint32_t sequence_;
  std::array<char, WriteDataSize> const& buffer_;

  static constexpr std::string_view NAME     = "FLASH_DATA";
  static constexpr std::uint8_t COMMAND_BYTE = 0x03;
  static constexpr int PACKET_SIZE           = -1;

  [[nodiscard]] std::uint8_t check_sum() const noexcept {
    return std::accumulate(this->buffer_.begin(), this->buffer_.begin() + flash_size_, ESP32_CHECKSUM_MAGIC,
                           std::bit_xor{});
  }

  auto operator()() const noexcept {
    std::vector<std::uint8_t> ret_val(DATA_PACKET + this->flash_size_);
    auto flash_size_arr = word_to_byte_array(this->flash_size_);
    auto sequence_arr   = word_to_byte_array(this->sequence_);

    auto iter = std::copy_n(flash_size_arr.begin(), flash_size_arr.size(), ret_val.begin());
    iter      = std::copy_n(sequence_arr.begin(), sequence_arr.size(), iter);
    iter      = std::fill_n(iter, 8, 0);
    std::transform(this->buffer_.begin(), this->buffer_.begin() + this->flash_size_, iter,
                   [](auto const t_chr) { return static_cast<std::uint8_t>(t_chr); });

    return ret_val;
  }
};

enum class FlashEndOption { Reboot, RunUserCode };

template <FlashEndOption Opt>
struct FLASH_END {
  static constexpr std::string_view NAME     = "FLASH_END";
  static constexpr std::uint8_t COMMAND_BYTE = 0x04;
  static constexpr std::size_t PACKET_SIZE   = 4;

  constexpr auto operator()() const noexcept {
    return std::array<std::uint8_t, PACKET_SIZE>{static_cast<std::uint8_t>(Opt == FlashEndOption::RunUserCode), 0, 0,
                                                 0};
  }
};

struct FLASH_READ_SLOW {
  std::uint32_t bootloader_address_;
  std::uint32_t data_length_;

  static constexpr std::string_view NAME     = "FLASH_READ_SLOW";
  static constexpr std::uint8_t COMMAND_BYTE = 0x0E;
  static constexpr std::size_t PACKET_SIZE   = 8;

  constexpr auto operator()() const noexcept {
    auto bootloader_addr_arr = word_to_byte_array(this->bootloader_address_);
    auto data_length_arr     = word_to_byte_array(this->data_length_);

    std::array<std::uint8_t, PACKET_SIZE> ret_val{};
    auto* iter = std::copy_n(bootloader_addr_arr.begin(), bootloader_addr_arr.size(), ret_val.begin());
    std::copy_n(data_length_arr.begin(), data_length_arr.size(), iter);

    return ret_val;
  }
};

}  // namespace esplink::command