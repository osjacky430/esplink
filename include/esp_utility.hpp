#pragma once

#include <iterator>
#include <ranges>
#include <type_traits>

#include <spdlog/spdlog.h>

namespace esputil::detail {

template <typename T>
concept is_scoped_enum = std ::is_enum_v<T> and not std::is_convertible_v<int, T>;

}  // namespace esputil::detail

namespace esputil {

inline constexpr auto word_to_byte_array = [](std::uint32_t const t_v) {
  auto const high_halfword = t_v >> 16;
  auto const low_halfword  = t_v & 0xFFFF;
  return std::array{low_halfword & 0xFF, low_halfword >> 8, high_halfword & 0xFF, high_halfword >> 8};
};

inline constexpr auto to_byte = [](auto t_in) { return static_cast<std::uint8_t>(t_in); };

inline constexpr auto to_underlying(detail::is_scoped_enum auto t_enum) noexcept {
  return static_cast<std::underlying_type_t<decltype(t_enum)>>(t_enum);
}

inline void print_byte_stream(auto t_begin, auto t_end) noexcept {
  using std::ranges::subrange;
  using std::views::transform;
  auto const byte_stream_size = t_end - t_begin;
  auto const size_to_print    = static_cast<std::size_t>((byte_stream_size + 15) / 16);  // ceil
  spdlog::set_pattern("%v");

  for (std::size_t i = 0; i < size_to_print; ++i) {
    auto const curr_end = t_end - t_begin < 16 ? t_end : t_begin + 16;
    spdlog::debug("{:04X}  {:02X}", i * 16, fmt::join(subrange(t_begin, curr_end) | transform(to_byte), " "));
    t_begin = curr_end;
  }

  spdlog::debug("");
  spdlog::set_pattern("%+");
}

}  // namespace esputil