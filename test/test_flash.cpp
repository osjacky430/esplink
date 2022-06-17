#include "catch2/catch_test_macros.hpp"
#include "esp_serial/boot_cmd.hpp"
#include "esp_serial/slip.hpp"
#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/view/sliding.hpp>

constexpr auto contain_esc_and_esc_esc = [](auto const& t_view) {
  return t_view[0] == esplink::ESPSLIP::SLIP_ESC and t_view[1] == esplink::ESPSLIP::SLIP_ESC_ESC;
};

constexpr auto contain_esc_and_esc_end = [](auto const& t_view) {
  return t_view[0] == esplink::ESPSLIP::SLIP_ESC and t_view[1] == esplink::ESPSLIP::SLIP_ESC_END;
};

TEST_CASE("esp flash generates slip protocal comply data", "[SLIP]") {
  esplink::ESPSLIP slip;

  constexpr auto value       = std::bit_cast<std::uint32_t>(std::array{
    std::uint8_t{},
    std::uint8_t{},
    esplink::ESPSLIP::SLIP_ESC,
    esplink::ESPSLIP::SLIP_END,
  });
  constexpr auto read_reg    = esplink::command::READ_REG<value>{};
  auto const read_reg_packet = slip.generate_packet(read_reg);

  auto rng = read_reg_packet | ranges::views::sliding(2);
  CHECK(ranges::find_if(rng, contain_esc_and_esc_esc) != rng.end());

  CHECK(ranges::find_if(rng, contain_esc_and_esc_end) != rng.end());
  CHECK(read_reg_packet.front() == esplink::ESPSLIP::SLIP_END);
  CHECK(read_reg_packet.back() == esplink::ESPSLIP::SLIP_END);

  constexpr auto BUFFER_SIZE = 4096U;
  std::array<char, BUFFER_SIZE> buffer{};

  esplink::command::FLASH_DATA<BUFFER_SIZE> flash_data{esplink::ESPSLIP::SLIP_END, esplink::ESPSLIP::SLIP_ESC, buffer};
  auto const flash_data_packet = slip.generate_packet(flash_data);
  auto flash_rng               = flash_data_packet | ranges::views::sliding(2);
  CHECK(ranges::find_if(flash_rng, contain_esc_and_esc_esc) != flash_rng.end());
  CHECK(ranges::find_if(flash_rng, contain_esc_and_esc_end) != flash_rng.end());
}

TEST_CASE("slip protocal data is decoded correctly", "[SLIP]") {
  esplink::ESPSLIP slip;
  std::vector<std::uint8_t> const buffer{
    0xC0,
    0x1,
    0xE,
    0x8,
    0,
    0x6F,
    0x50,
    0x31,
    0x1B,
    esplink::ESPSLIP::SLIP_ESC,
    esplink::ESPSLIP::SLIP_ESC_END,
    esplink::ESPSLIP::SLIP_ESC,
    esplink::ESPSLIP::SLIP_ESC_ESC,
    0,
    0,
    0,
    0,
  };

  auto const result = slip.decode_packet(buffer.begin(), buffer.size());

  auto rng = result.data_ | ranges::views::sliding(2);
  CHECK(result.command_ == 0xE);
  CHECK(result.size_ == 8);
  CHECK(ranges::find_if(rng, contain_esc_and_esc_end) == rng.end());
  CHECK(ranges::find_if(rng, contain_esc_and_esc_esc) == rng.end());
  CHECK(ranges::find(result.data_, esplink::ESPSLIP::SLIP_END) != result.data_.end());
  CHECK(ranges::find(result.data_, esplink::ESPSLIP::SLIP_ESC) != result.data_.end());

  std::vector<std::uint8_t> const buffer_with_error_status{
    0xC0,
    0x1,
    0xE,
    0x8,
    0,
    0x6F,
    0x50,
    0x31,
    0x1B,
    esplink::ESPSLIP::SLIP_ESC,
    esplink::ESPSLIP::SLIP_ESC_END,
    esplink::ESPSLIP::SLIP_ESC,
    esplink::ESPSLIP::SLIP_ESC_ESC,
    0x1,
    0x5,
    0,
    0,
  };

  CHECK_THROWS_AS(slip.decode_packet(buffer_with_error_status.begin(), buffer_with_error_status.size()),
                  std::runtime_error);
}
