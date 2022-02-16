#pragma once

#include "esp_common/constants.hpp"
#include "esp_common/utility.hpp"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <fmt/ranges.h>
#include <fstream>
#include <ios>
#include <iterator>
#include <range/v3/algorithm/count_if.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/sliding.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace esplink {

enum class Format { x86 = 1, x86_64 = 2 };

template <Format Fmt>
using Address = std::conditional_t<Fmt == Format::x86, std::uint32_t, std::uint64_t>;

static constexpr auto IDENTITY_SIZE           = 0x10;
static constexpr auto X86_FILE_HEADER_SIZE    = 0x34;
static constexpr auto X86_SECTION_HEADER_SIZE = 0x28;
static constexpr auto X86_PROGRAM_HEADER_SIZE = 0x20;
static constexpr auto X64_FILE_HEADER_SIZE    = 0x40;
static constexpr auto X64_SECTION_HEADER_SIZE = 0x40;
static constexpr auto X64_PROGRAM_HEADER_SIZE = 0x38;

struct Identity {
  std::uint32_t magic_number_;
  std::byte class_;
  std::byte endianness_;
  std::byte version_;
  std::byte os_abi_;
  std::byte abi_ver_;
  std::array<std::byte, 7> pad_;

  [[nodiscard]] constexpr auto get_endianess() const noexcept {
    auto const endianness = std::to_integer<std::uint8_t>(this->endianness_);
    if (endianness == 1) {
      return "little endian";
    }

    if (endianness == 2) {
      return "big endian";
    }

    return "Unknown";
  }

  [[nodiscard]] constexpr auto get_class_str() const noexcept {
    auto const fmt = static_cast<Format>(this->class_);
    if (fmt == Format::x86) {
      return "ELF32";
    }

    if (fmt == Format::x86_64) {
      return "ELF64";
    }

    return "UNKNOWN";
  }

  [[nodiscard]] constexpr auto get_os_abi_str() const noexcept {
    auto const os_abi = std::to_integer<std::uint8_t>(this->os_abi_);

    constexpr std::array os_abi_table = {"UNIX System V",   "HP-UX",    "NetBSD",     "Linux",          "GNU Hurd",
                                         "Solaris",         "AIX",      "IRIX",       "FreeBSD",        "Tru64 UNIX",
                                         "Novell Modesto ", "OpenBSD",  "OpenVMS",    "NonStop Kernel", "AROS",
                                         "Fenix OS",        "Capsicum", "Stratus VOS"};
    return os_abi_table.at(os_abi);
  }
};

template <Format Fmt>
struct FileHeaderWithoutIdentity {
  std::uint16_t type_;
  std::uint16_t machine_;
  std::uint32_t version_;
  Address<Fmt> entry_;
  Address<Fmt> phoff_;
  Address<Fmt> shoff_;
  std::uint32_t flags_;
  std::uint16_t ehsize_;
  std::uint16_t phentsize_;
  std::uint16_t phnum_;
  std::uint16_t shentsize_;
  std::uint16_t shnum_;
  std::uint16_t shstrndx_;
};

static_assert(sizeof(Identity) == IDENTITY_SIZE);
static_assert(sizeof(FileHeaderWithoutIdentity<Format::x86>) == X86_FILE_HEADER_SIZE - IDENTITY_SIZE);
static_assert(sizeof(FileHeaderWithoutIdentity<Format::x86_64>) == X64_FILE_HEADER_SIZE - IDENTITY_SIZE);

template <Format Fmt>
struct SectionHeader {
  using AddressType = Address<Fmt>;

  std::uint32_t name_;
  std::uint32_t type_;
  AddressType flags_;
  AddressType addr_;
  AddressType offset_;
  AddressType size_;
  std::uint32_t link_;
  std::uint32_t info_;
  AddressType addralign_;
  AddressType entsize_;

  static constexpr auto RISCV_ATTRIBUTE_TYPE_NUM = 0x70000003;

  [[nodiscard]] constexpr auto get_type_str() const noexcept {
    if (constexpr std::array type_str_table =
          {"NULL",       "PROGBITS",   "SYMTAB",        "STRTAB", "RELA",         "HASH", "DYNAMIC",
           "NOTE",       "NOBITS",     "REL",           "SHLIB",  "DYNSYM",       "",     "",
           "INIT_ARRAY", "FINI_ARRAY", "PREINIT_ARRAY", "GROUP",  "SYMTAB_SHNDX", "NUM"};
        this->type_ <= type_str_table.size()) {
      return type_str_table[this->type_];
    }

    return this->type_ == RISCV_ATTRIBUTE_TYPE_NUM ? "RISCV_ATTRIBUTE" : "UNKNOWN";
  }

  [[nodiscard]] constexpr bool have_content() const noexcept {
    constexpr auto NOBITS_TYPE = 0x8U;
    return this->size_ != 0 and this->type_ != NOBITS_TYPE;
  }

  [[nodiscard]] constexpr bool is_loadable() const noexcept {
    constexpr auto LOADABLE_FLAG = 0b10U;
    auto const alloc_flag        = (this->flags_ & LOADABLE_FLAG) != 0;
    return alloc_flag;
  }

  [[nodiscard]] auto get_flag_str() const noexcept {
    constexpr std::array flag_str_table = {'W', 'A', 'X', 'x', 'M', 'S', 'I', 'L', 'O', 'G', 'T'};
    std::string flag_str;

    auto const flag_width = std::bit_width(this->flags_);
    for (std::size_t i = 0; i < flag_width; ++i) {
      if ((this->flags_ & (1U << i)) != 0) {
        flag_str.push_back(flag_str_table.at(i));
      }
    }

    constexpr auto OS_SPECIFIC_FLAGS = 0x0FF00000;
    if ((this->flags_ & OS_SPECIFIC_FLAGS) != 0) {
      flag_str.push_back('o');
    }

    constexpr auto PROCESS_SPECIFIC_FLAGS = 0xF0000000;
    if ((this->flags_ & PROCESS_SPECIFIC_FLAGS) != 0) {
      flag_str.push_back('p');
    }

    constexpr auto EXCLUDE = 0x80000000;
    if ((this->flags_ & EXCLUDE) != 0) {
      flag_str.push_back('E');
    }

    return flag_str;
  }
};

static_assert(sizeof(SectionHeader<Format::x86>) == X86_SECTION_HEADER_SIZE);
static_assert(sizeof(SectionHeader<Format::x86_64>) == X64_SECTION_HEADER_SIZE);

template <Format Fmt>
struct ProgramHeader {
  Address<Fmt> lumped_type_;
  Address<Fmt> offset_;
  Address<Fmt> vaddr_;
  Address<Fmt> paddr_;
  Address<Fmt> filesz_;
  Address<Fmt> memsz_;
  std::uint64_t lumped_align_;

  constexpr bool operator==(ProgramHeader<Fmt> const& /* unused */) const noexcept = default;

  [[nodiscard]] constexpr auto get_flags() const {
    if constexpr (Fmt == Format::x86_64) {
      return static_cast<std::uint32_t>(this->lumped_type_ & 0xFFFFFFFFU);
    } else if constexpr (Fmt == Format::x86) {
      return static_cast<std::uint32_t>(this->lumped_align_ & 0xFFFFFFFFU);
    }

    throw std::runtime_error("Bad format type");
  }

  [[nodiscard]] auto get_flags_str() const {
    constexpr std::array flag_str_table = {'E', 'W', 'R'};

    auto const flag = this->get_flags();
    std::string ret_val;
    for (std::size_t i = 0; i < 3; ++i) {
      if ((flag & (1U << i)) != 0) {
        ret_val.push_back(flag_str_table.at(i));
      }
    }

    if ((flag & 0xF0000000U) != 0) {
      ret_val.push_back('x');
    }

    return ret_val;
  }

  [[nodiscard]] constexpr auto get_type() const {
    if constexpr (Fmt == Format::x86_64) {
      return static_cast<std::uint32_t>(this->lumped_type_ >> 32U);
    } else if constexpr (Fmt == Format::x86) {
      return this->lumped_type_;
    }

    throw std::runtime_error("Bad format type");
  }

  [[nodiscard]] constexpr auto get_type_str() const {
    constexpr std::array type_str_map = {"NULL", "LOAD", "DYNAMIC", "INTERP", "NOTE", "SHLIB", "PHDR", "TLS"};

    if (auto const type = this->get_type(); type < type_str_map.size()) {
      return type_str_map[type];
    }

    return "UNKNOWN";
  }

  [[nodiscard]] constexpr auto get_align() const {
    if constexpr (Fmt == Format::x86) {
      return static_cast<std::uint32_t>(this->lumped_align_ >> 32U);
    } else if constexpr (Fmt == Format::x86_64) {
      return this->lumped_align_;
    }

    throw std::runtime_error("Bad format type");
  }
};

static_assert(sizeof(ProgramHeader<Format::x86>) == X86_PROGRAM_HEADER_SIZE);
static_assert(sizeof(ProgramHeader<Format::x86_64>) == X64_PROGRAM_HEADER_SIZE);

struct ELFFile {
  template <Format Fmt>
  class Content {
    static constexpr auto should_load = [](auto const& t_sh) {
      auto const& [name, section] = t_sh;
      return section.is_loadable() and section.have_content();
    };

   public:
    FileHeaderWithoutIdentity<Fmt> file_header_{};
    std::vector<ProgramHeader<Fmt>> program_headers_;
    std::vector<std::pair<std::string, SectionHeader<Fmt>>> section_headers_;

    [[nodiscard]] auto merge_adjacent_loadable() const {
      auto const section_comp = [](auto const& t_lhs, auto const& t_rhs) {
        if (t_lhs.second.addr_ != t_rhs.second.addr_) {
          return t_lhs.second.addr_ > t_rhs.second.addr_;
        }

        return t_lhs.second.size_ > t_rhs.second.size_;
      };

      // sort according to address, then combine adjacent
      auto all_loadable = this->get_loadable_sections();
      ranges::sort(all_loadable, section_comp);

      // to ensure last element is merged correctly and pushed into "merged"
      all_loadable.push_back(all_loadable.front());

      decltype(all_loadable) merged;
      auto const check_and_merge_adjacent = [&](auto const& t_zip) {
        auto& next = t_zip[0];
        auto& curr = t_zip[1];
        if (this->get_section_memory_type(curr.second) == this->get_section_memory_type(next.second) and
            curr.second.addr_ + curr.second.size_ == next.second.addr_) {
          curr.second.size_ += next.second.size_;
        } else {
          merged.push_back(next);
        }
      };
      ranges::for_each(all_loadable | ranges::views::sliding(2), check_and_merge_adjacent);

      return merged;
    }

    [[nodiscard]] constexpr auto get_section_memory_type(SectionHeader<Fmt> const& t_section) const {
      return *ranges::find_if(this->program_headers_, [&](auto const& t_ph) {
        return t_ph.vaddr_ <= t_section.addr_ and t_section.addr_ < t_ph.vaddr_ + t_ph.memsz_;
      });
    }

    [[nodiscard]] auto get_loadable_count() const { return ranges::count_if(this->section_headers_, should_load); }

    [[nodiscard]] auto get_loadable_sections() const {
      using ranges::to;
      using ranges::views::filter;
      return this->section_headers_ | filter(should_load) | to<std::vector>;
    }
  };

  using ELFFormatDependentContent = std::variant<Content<Format::x86>, Content<Format::x86_64>>;

  Identity identity_;
  ELFFormatDependentContent content_;

  explicit ELFFile(std::fstream& t_file) : identity_{get_identity(t_file)}, content_{this->parse(t_file)} {}

  template <Format Fmt>
  constexpr auto get_section_memory_type(SectionHeader<Fmt> const& t_section) const {
    auto const content = std::get<to_underlying(Fmt) - 1>(this->content_);
    return *ranges::find_if(content.program_headers_, [&](auto const& t_ph) {
      return t_ph.vaddr_ <= t_section.addr_ and t_section.addr_ < t_ph.vaddr_ + t_ph.memsz_;
    });
  }

 private:
  static Identity get_identity(std::fstream& t_file) noexcept {
    std::array<char, sizeof(Identity)> buffer{};
    t_file.read(buffer.data(), sizeof(Identity));
    return std::bit_cast<Identity>(buffer);
  }

  template <Format Fmt>
  static auto parse_program_header(std::fstream& t_file, Address<Fmt> t_offset, std::size_t t_header_num) noexcept {
    std::vector<ProgramHeader<Fmt>> ret_val;
    auto read_program_header = [&]() mutable {
      std::array<char, sizeof(ProgramHeader<Fmt>)> ph_buffer{};
      t_file.read(ph_buffer.data(), ph_buffer.size());
      return std::bit_cast<ProgramHeader<Fmt>>(ph_buffer);
    };

    ret_val.reserve(t_header_num);
    t_file.seekg(static_cast<std::streamoff>(t_offset));
    std::generate_n(std::back_inserter(ret_val), t_header_num, read_program_header);
    return ret_val;
  }

  template <Format Fmt>
  static auto parse_section_header(std::fstream& t_file, Address<Fmt> t_offset, std::size_t t_header_num,
                                   Address<Fmt> t_section_name_offset) noexcept {
    std::vector<std::pair<std::string, SectionHeader<Fmt>>> ret_val;
    std::vector<SectionHeader<Fmt>> section_headers;
    auto read_section_header = [&]() mutable {
      std::array<char, sizeof(SectionHeader<Fmt>)> sh_buffer{};
      t_file.read(sh_buffer.data(), sh_buffer.size());
      return std::bit_cast<SectionHeader<Fmt>>(sh_buffer);
    };

    ret_val.reserve(t_header_num);
    section_headers.reserve(t_header_num);
    t_file.seekg(static_cast<std::streamoff>(t_offset));
    std::generate_n(std::back_inserter(section_headers), t_header_num, read_section_header);

    auto const sh_name_table_offset = section_headers[t_section_name_offset].offset_;
    auto get_section_name           = [&](auto&& t_sh) mutable {
      auto const sh_name_offset = sh_name_table_offset + t_sh.name_;
      t_file.seekg(static_cast<std::streamoff>(sh_name_offset));
      std::string name;
      std::getline(t_file, name, '\0');
      return std::pair{std::move(name), t_sh};
    };
    std::transform(std::make_move_iterator(section_headers.begin()), std::make_move_iterator(section_headers.end()),
                   std::back_inserter(ret_val), get_section_name);

    return ret_val;
  }

  template <Format Fmt>
  static auto parse_content(std::fstream& t_file) noexcept {
    Content<Fmt> ret_type;
    std::array<char, sizeof(FileHeaderWithoutIdentity<Fmt>)> buffer{};
    t_file.read(buffer.data(), buffer.size());
    ret_type.file_header_   = std::bit_cast<FileHeaderWithoutIdentity<Fmt>>(buffer);
    auto const& file_header = ret_type.file_header_;

    ret_type.program_headers_ = parse_program_header<Fmt>(t_file, file_header.phoff_, file_header.phnum_);
    ret_type.section_headers_ = parse_section_header<Fmt>(t_file, file_header.shoff_, file_header.shnum_,  //
                                                          file_header.shstrndx_);

    return ret_type;
  }

  ELFFormatDependentContent parse(std::fstream& t_file) const {
    auto const fmt = static_cast<Format>(this->identity_.class_);
    if (fmt == Format::x86) {
      return parse_content<Format::x86>(t_file);
    }

    if (fmt == Format::x86_64) {
      return parse_content<Format::x86_64>(t_file);
    }

    throw std::invalid_argument("Unknown format type, abort");
  }
};

}  // namespace esplink