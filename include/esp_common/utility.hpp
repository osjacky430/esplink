#pragma once

#include <iterator>
#include <range/v3/view/subrange.hpp>
#include <range/v3/view/transform.hpp>
#include <type_traits>

#include <spdlog/spdlog.h>

namespace esplink::detail {

template <typename T>
concept is_scoped_enum = std ::is_enum_v<T> and not std::is_convertible_v<int, T>;

}  // namespace esplink::detail

namespace esplink {

inline constexpr auto word_to_byte_array = [](std::uint32_t const t_v) {
  auto const high_halfword = t_v >> 16U;
  auto const low_halfword  = t_v & 0xFFFFU;
  return std::array{low_halfword & 0xFFU, low_halfword >> 8U, high_halfword & 0xFFU, high_halfword >> 8U};
};

// inline constexpr auto byte_array_to_word = [](std::array<std::uint8_t, 4> const& t_byte_arr, int t_endianess = 0) {
//
// };

inline constexpr auto to_byte = [](auto t_in) { return static_cast<std::uint8_t>(t_in); };

inline constexpr auto padded_size(std::uint32_t t_size, std::uint32_t t_padding) noexcept {
  return ((t_size + t_padding - 1U) / t_padding) * t_padding;
}

inline constexpr auto to_underlying(detail::is_scoped_enum auto t_enum) noexcept {
  return static_cast<std::underlying_type_t<decltype(t_enum)>>(t_enum);
}

inline void print_byte_stream(auto t_begin, auto t_end) noexcept {
  using ranges::subrange;
  using ranges::views::transform;
  constexpr auto byte_per_line = 16;
  auto const byte_stream_size  = t_end - t_begin;
  auto const line_to_print = static_cast<std::size_t>((byte_stream_size + byte_per_line - 1) / byte_per_line);  // ceil
  spdlog::set_pattern("%v");

  for (std::size_t i = 0; i < line_to_print; ++i) {
    auto const curr_end = t_end - t_begin < byte_per_line ? t_end : t_begin + byte_per_line;
    spdlog::debug("{:04X}  {:02X}", i * byte_per_line,
                  fmt::join(subrange(t_begin, curr_end) | transform(to_byte), " "));
    t_begin = curr_end;
  }

  spdlog::debug("");
  spdlog::set_pattern("%+");
}

}  // namespace esplink