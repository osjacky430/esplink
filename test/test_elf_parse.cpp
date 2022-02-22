#include "catch2/catch_test_macros.hpp"
#include "esp_mkbin/elf_reader.hpp"
#include <filesystem>
#include <fstream>
#include <range/v3/algorithm/find_if.hpp>

struct ContainSectionName {
  std::string_view name_;

  [[nodiscard]] auto operator()(auto const& t_sh) const noexcept {
    auto const& [name, sh] = t_sh;
    return name == this->name_;
  }
};

TEST_CASE("elf parser can parse section correctly", "[Parse Elf]") {
  using std::string_literals::operator""s;

  std::fstream test_file{"main.elf"};
  REQUIRE(test_file.good());

  esplink::ELFFile parsed{test_file};
  CHECK(parsed.identity_.get_class_str() == "ELF32"s);
  CHECK(parsed.identity_.get_endianess() == "little endian"s);
  CHECK(parsed.identity_.get_os_abi_str() == "UNIX System V"s);

  CHECK(std::to_integer<int>(parsed.identity_.abi_ver_) == 0);
  CHECK(std::to_integer<int>(parsed.identity_.version_) == 1);

  // Only these are used when parsing section and program header
  auto const x86_info = std::get<0>(parsed.content_);
  CHECK(x86_info.file_header_.entry_ == 0x40380080U);
  CHECK(x86_info.file_header_.phnum_ == 3);
  CHECK(x86_info.file_header_.phoff_ == 52);
  CHECK(x86_info.file_header_.shnum_ == 23);
  CHECK(x86_info.file_header_.shoff_ == 82980);
  CHECK(x86_info.file_header_.shstrndx_ == 22);

  CHECK(x86_info.get_loadable_count() == 5);

  // only check loadable section as those are what goes to the output binary
  auto const check_section = [loadable_section = x86_info.get_loadable_sections()](std::string_view t_name, auto t_addr,
                                                                                   auto t_offset, auto t_size) {
    auto const section = ranges::find_if(loadable_section, ContainSectionName{t_name});
    CHECK(section != loadable_section.end());
    CHECK(section->second.addr_ == t_addr);
    CHECK(section->second.offset_ == t_offset);
    CHECK(section->second.size_ == t_size);
  };

  check_section(".vector_table", 0x40380000U, 0x2000U, 0x80U);
  check_section(".text", 0x40380080U, 0x2080U, 0x1ECU);
  check_section(".rodata", 0x3FF00000U, 0x1000U, 0xB8U);
  check_section(".init_array", 0x40380270U, 0x2270U, 0x4U);
  check_section(".fini_array", 0x40380274U, 0x2274U, 0x10U);
}

TEST_CASE("elf parser can merge loadable section", "[Parse Elf]") {
  //
}