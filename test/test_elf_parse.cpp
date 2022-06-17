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
  SECTION("check identity") {
    CHECK(parsed.identity_.get_class_str() == "ELF32"s);
    CHECK(parsed.identity_.get_endianess() == "little endian"s);
    CHECK(parsed.identity_.get_os_abi_str() == "UNIX System V"s);

    CHECK(std::to_integer<int>(parsed.identity_.abi_ver_) == 0);
    CHECK(std::to_integer<int>(parsed.identity_.version_) == 1);
    REQUIRE(parsed.content_.index() == 0);
  }

  // Only these are used when parsing section and program header
  auto const x86_info = std::get<0>(parsed.content_);
  SECTION("check file header") {
    CHECK(x86_info.file_header_.entry_ == 0x40380080U);
    CHECK(x86_info.file_header_.phnum_ == 3);
    CHECK(x86_info.file_header_.phoff_ == 52);
    CHECK(x86_info.file_header_.shnum_ == 23);
    CHECK(x86_info.file_header_.shoff_ == 82980);
    CHECK(x86_info.file_header_.shstrndx_ == 22);
  }

  auto const check_section = [](auto const& t_sections, std::string_view t_name, auto t_addr, auto t_offset,
                                auto t_size) {
    auto const section = ranges::find_if(t_sections, ContainSectionName{t_name});
    CHECK(section != t_sections.end());
    CHECK(section->second.addr_ == t_addr);
    CHECK(section->second.offset_ == t_offset);
    CHECK(section->second.size_ == t_size);
  };

  // only check loadable section as those are what goes to the output binary
  SECTION("check section") {
    CHECK(x86_info.get_loadable_count() == 5);

    auto const& loadable_section = x86_info.get_loadable_sections();
    check_section(loadable_section, ".vector_table", 0x40380000U, 0x2000U, 0x80U);
    check_section(loadable_section, ".text", 0x40380080U, 0x2080U, 0x1ECU);
    check_section(loadable_section, ".rodata", 0x3FF00000U, 0x1000U, 0xB8U);
    check_section(loadable_section, ".init_array", 0x40380270U, 0x2270U, 0x4U);
    check_section(loadable_section, ".fini_array", 0x40380274U, 0x2274U, 0x10U);
  }

  SECTION("check merge section") {
    auto const merged = x86_info.merge_adjacent_loadable();  //

    check_section(merged, ".vector_table", 0x40380000U, 0x2000U, 0x80U + 0x1ECU);
    check_section(merged, ".rodata", 0x3FF00000U, 0x1000U, 0xB8U);
    check_section(merged, ".init_array", 0x40380270U, 0x2270U, 0x4U + 0x10U);
  }
}
