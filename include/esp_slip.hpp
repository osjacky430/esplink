#pragma once

#include <boost/asio.hpp>
#include <boost/asio/high_resolution_timer.hpp>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <vector>

#include "esp_utility.hpp"

namespace esputil {

class ESPSLIP {
 private:
  static constexpr auto SLIP_HEADER_SIZE           = 8;
  static constexpr auto MINIMUM_DATA_SIZE          = 2;
  static constexpr auto MINIMUM_PACKET_SIZE        = SLIP_HEADER_SIZE + MINIMUM_DATA_SIZE + 2;  // 2: start and end END
  static constexpr std::uint8_t REQUEST_DIRECTION  = 0x0;
  static constexpr std::uint8_t RESPONSE_DIRECTION = 0x01;

  static constexpr std::uint8_t SLIP_END     = 0xC0;
  static constexpr std::uint8_t SLIP_ESC     = 0xDB;
  static constexpr std::uint8_t SLIP_ESC_END = 0xDC;
  static constexpr std::uint8_t SLIP_ESC_ESC = 0xDD;
  static constexpr auto is_slip_end = [](auto const t_in) { return static_cast<std::uint8_t>(t_in) == SLIP_END; };

  static constexpr auto get_err_string = [](std::uint32_t t_err) {
    constexpr auto RCV_MSG_INVALID    = 0x5;
    constexpr auto FAILED_TO_ACT      = 0x6;
    constexpr auto INVALID_CRC        = 0x7;
    constexpr auto FLASH_WRITE_ERR    = 0x8;
    constexpr auto FLASH_READ_ERR     = 0x9;
    constexpr auto FLASH_READ_LEN_ERR = 0xa;
    constexpr auto DEFLATE_ERR        = 0xb;
    switch (t_err) {
      case RCV_MSG_INVALID:
        return "Received message is invalid (parameters or length field is invalid)";
      case FAILED_TO_ACT:
        return "Failed to act on received message";
      case INVALID_CRC:
        return "Invalid CRC in message";
      case FLASH_WRITE_ERR:
        return "Mismatch in the 8-bit CRC between the value ROM loader reads back and the data read from "
               "flash";
      case FLASH_READ_ERR:
        return "SPI read failed";
      case FLASH_READ_LEN_ERR:
        return "SPI read request length is too long";
      case DEFLATE_ERR:
        return "Deflate error (compressed uploads only)";
      default:
        return "Unknown error";
    }
  };

  using iterator       = boost::asio::buffers_iterator<boost::asio::const_buffers_1>;
  bool unpaired_start_ = false;

 public:
  struct Response {
    std::uint8_t directions_ = RESPONSE_DIRECTION;  // 0x01
    std::uint8_t command_;                          // request command
    std::uint16_t size_;                            // data field size, at least 2 or 4 byte
    std::uint32_t value_;                           // read_reg command result
    std::vector<std::uint8_t> data_;                //
  };

  using Result = Response;

  [[nodiscard]] Result decode_packet(auto t_buffer, std::size_t t_byte_read) const {
    auto const vec = [=]() {
      auto packet_start = boost::next(std::find_if(t_buffer, boost::next(t_buffer, t_byte_read), is_slip_end));
      std::vector<std::uint32_t> ret;  // prevent std::uint8_t overflow during arithmetic
      ret.reserve(t_byte_read);
      for (std::uint8_t prev = 0, curr = static_cast<std::uint8_t>(*packet_start);
           packet_start != boost::next(t_buffer, t_byte_read);
           prev = curr, curr = static_cast<std::uint8_t>(*++packet_start)) {
        if (curr == SLIP_ESC) {
          continue;
        } else if (curr == SLIP_ESC_END and prev == SLIP_ESC) {
          ret.push_back(SLIP_END);
        } else if (curr == SLIP_ESC_ESC and prev == SLIP_ESC) {
          ret.push_back(SLIP_ESC);
        } else {
          ret.push_back(curr);
        }
      }

      return ret;
    }();

    auto const low_byte  = vec[2];
    auto const high_byte = vec[3];
    auto const data_size = high_byte << 8 | low_byte;

    spdlog::debug("Raw bytes (len = {}):\n", vec.size(), fmt::join(vec, " "));
    print_byte_stream(vec.begin(), vec.end());

    assert(vec.front() == RESPONSE_DIRECTION);
    assert(t_byte_read == SLIP_HEADER_SIZE + data_size + 1U);
    auto const status_byte_idx = vec.size() - 4;
    if (vec.at(status_byte_idx) != 0) {
      auto const& code = vec.at(status_byte_idx + 1);
      auto const& desc = get_err_string(code);
      throw std::runtime_error(fmt::format("Operation failed with error code \"{:02X}\": {}", code, desc));
    }

    Response resp;

    resp.command_ = static_cast<std::uint8_t>(vec[1]);
    resp.size_    = static_cast<std::uint16_t>(data_size);
    resp.value_   = (vec[7] << 24) | (vec[6] << 16) | (vec[5] << 8) | vec[4];
    resp.data_ = std::vector<std::uint8_t>(vec.begin() + SLIP_HEADER_SIZE, vec.begin() + SLIP_HEADER_SIZE + data_size);

    return resp;
  }

  auto generate_packet(auto&& t_cmd) {
    auto data_content = t_cmd();

    auto const size_of_data = std::size(data_content);
    auto packet             = std::vector<std::uint8_t>{
      SLIP_END,
      REQUEST_DIRECTION,
      t_cmd.command_byte(),
      static_cast<std::uint8_t>(size_of_data & 0xFF),
      static_cast<std::uint8_t>(size_of_data >> 8),
    };
    packet.reserve(size_of_data + SLIP_HEADER_SIZE + 2);  // 2: initial END and end END

    std::uint32_t const check_sum = [&]() {
      if constexpr (requires { t_cmd.check_sum(); }) {
        return t_cmd.check_sum();
      }

      return std::uint8_t{0};
    }();
    auto check_sum_arr = word_to_byte_array(check_sum);
    packet.insert(packet.end(), std::make_move_iterator(check_sum_arr.begin()),
                  std::make_move_iterator(check_sum_arr.end()));
    for (auto byte : data_content) {
      switch (byte) {
        case SLIP_END:
          packet.push_back(SLIP_ESC);
          packet.push_back(SLIP_ESC_END);
          break;
        case SLIP_ESC:
          packet.push_back(SLIP_ESC);
          packet.push_back(SLIP_ESC_ESC);
          break;
        case SLIP_ESC_END:
          [[fallthrough]];
        case SLIP_ESC_ESC:
          [[fallthrough]];
        default:
          packet.push_back(byte);
          break;
      }
    }

    packet.push_back(SLIP_END);
    return packet;
  }

  /**
   * @brief This function checks whether ESP send complete message to us. The protocol adopted by ESP is SLIP, where
   *        the special byte END (0xC) serves as start and end of the signal. A typical read may look like following:
   *
   *        DB DC C0 XX |  C0  01 08 04 00 07 07 12 20 DB DC 00 00 C0 | XX C0 XX XX
   *        ^^^^^^^^^^^ |  ^^                          ^^ ^^       ^^ | ^^^^^^^^^^^
   *    redundant bytes | END                         ESC ESC_END END | redundant bytes
   *
   * @param t_begin
   * @param t_end
   * @return auto
   */
  auto complete_condition(iterator const t_begin, iterator const t_end) {
    auto const read_size = t_end - t_begin;
    if (read_size != 0) {
      spdlog::debug("Received: \n");
      print_byte_stream(t_begin, t_end);
    }

    if (read_size < MINIMUM_PACKET_SIZE) {
      return std::pair{t_end, false};
    }

    if (auto const slip_start = std::find_if(t_begin, t_end, is_slip_end); slip_start != t_end) {
      bool const is_start = *boost::next(slip_start) == RESPONSE_DIRECTION;
      if (not is_start and this->unpaired_start_) {
        this->unpaired_start_ = false;
        return std::pair{slip_start, true};
      }

      this->unpaired_start_ = is_start;
      if (auto const slip_end = std::find_if(boost::next(slip_start), t_end, is_slip_end);
          slip_end != t_end and (slip_end - slip_start) >= MINIMUM_PACKET_SIZE and is_start) {
        this->unpaired_start_ = false;
        return std::pair{slip_end, true};  // skip last END
      }
    }

    return std::pair{t_end, false};
  }
};

}  // namespace esputil
