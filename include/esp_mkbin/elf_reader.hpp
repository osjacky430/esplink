#pragma once

#include <cstddef>
#include <fmt/ranges.h>
#include <fstream>
#include <iterator>
#include <range/v3/algorithm/find_if.hpp>
#include <spdlog/spdlog.h>
#include <string_view>
#include <variant>
#include <vector>

namespace esplink {

enum class Format { x86 = 1, x86_64 = 2 };

template <Format Fmt>
using Address = std::conditional_t<Fmt == Format::x86, std::uint32_t, std::uint64_t>;

struct Identity {
  std::uint32_t magic_number_;
  std::byte class_;
  std::byte endianness_;
  std::byte version_;
  std::byte os_abi_;
  std::byte abi_ver_;
  std::array<std::byte, 7> pad_;

  constexpr auto get_endianess() const noexcept {
    if (auto const endianness = std::to_integer<std::uint8_t>(this->endianness_); endianness == 1) {
      return "little endian";
    } else if (endianness == 2) {
      return "big endian";
    }

    return "Unknown";
  }

  constexpr auto get_class_str() const noexcept {
    if (auto const fmt = static_cast<Format>(this->class_); fmt == Format::x86) {
      return "ELF32";
    } else if (fmt == Format::x86_64) {
      return "ELF64";
    }

    return "UNKONW";
  }

  constexpr auto get_os_abi_str() const noexcept {
    auto const os_abi = std::to_integer<std::uint8_t>(this->os_abi_);

    constexpr std::array os_abi_table = {"UNIX System V",   "HP-UX",    "NetBSD",     "Linux",          "GNU Hurd",
                                         "Solaris",         "AIX",      "IRIX",       "FreeBSD",        "Tru64 UNIX",
                                         "Novell Modesto ", "OpenBSD",  "OpenVMS",    "NonStop Kernel", "AROS",
                                         "Fenix OS",        "Capsicum", "Stratus VOS"};
    return os_abi_table[os_abi];
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

static_assert(sizeof(Identity) == 0x10);
static_assert(sizeof(FileHeaderWithoutIdentity<Format::x86>) == 0x34 - sizeof(Identity));
static_assert(sizeof(FileHeaderWithoutIdentity<Format::x86_64>) == 0x40 - sizeof(Identity));

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

  constexpr auto get_type_str() const noexcept {
    if (this->type_ <= 0x13U) {
      constexpr std::array type_str_table = {
        "NULL",       "PROGBITS",   "SYMTAB",        "STRTAB", "RELA",         "HASH", "DYNAMIC",
        "NOTE",       "NOBITS",     "REL",           "SHLIB",  "DYNSYM",       "",     "",
        "INIT_ARRAY", "FINI_ARRAY", "PREINIT_ARRAY", "GROUP",  "SYMTAB_SHNDX", "NUM"};
      return type_str_table[this->type_];
    }

    return this->type_ == RISCV_ATTRIBUTE_TYPE_NUM ? "RISCV_ATTRIBUTE" : "UNKNOWN";
  }

  constexpr bool have_content() const noexcept {
    constexpr auto NOBITS_TYPE = 0x8U;
    return this->size_ != 0 and this->type_ != NOBITS_TYPE;
  }

  constexpr bool is_loadable() const noexcept {
    constexpr auto LOADABLE_FLAG = 0b10U;
    auto const alloc_flag        = (this->flags_ & LOADABLE_FLAG) != 0;
    return alloc_flag;
  }

  constexpr auto get_flag_str() const noexcept {
    constexpr std::array flag_str_table = {'W', 'A', 'X', 'x', 'M', 'S', 'I', 'L', 'O', 'G', 'T'};
    std::string flag_str;

    for (std::size_t i = 0; i < 32U; ++i) {
      if ((this->flags_ & (1U << i)) != 0) {
        flag_str.push_back(flag_str_table[i]);
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

static_assert(sizeof(SectionHeader<Format::x86>) == 0x28);
static_assert(sizeof(SectionHeader<Format::x86_64>) == 0x40);

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

  constexpr auto get_flags() const {
    if constexpr (Fmt == Format::x86_64) {
      return static_cast<std::uint32_t>(this->lumped_type_ & 0xFFFFFFFFU);
    } else if constexpr (Fmt == Format::x86) {
      return static_cast<std::uint32_t>(this->lumped_align_ & 0xFFFFFFFFU);
    }

    throw "Bad format type";
  }

  constexpr auto get_flags_str() const {
    constexpr std::array flag_str_table = {'E', 'W', 'R'};

    auto const flag = this->get_flags();
    std::string ret_val;
    for (std::size_t i = 0; i < 3; ++i) {
      if ((flag & (1U << i)) != 0) {
        ret_val.push_back(flag_str_table[i]);
      }
    }

    if ((flag & 0xF0000000U) != 0) {
      ret_val.push_back('x');
    }

    return ret_val;
  }

  constexpr auto get_type() const {
    if constexpr (Fmt == Format::x86_64) {
      return static_cast<std::uint32_t>(this->lumped_type_ >> 32U);
    } else if constexpr (Fmt == Format::x86) {
      return this->lumped_type_;
    }

    throw "Bad format type";
  }

  constexpr auto get_type_str() const {
    constexpr std::array type_str_map = {"NULL", "LOAD", "DYNAMIC", "INTERP", "NOTE", "SHLIB", "PHDR", "TLS"};

    if (auto const type = this->get_type(); type < type_str_map.size()) {
      return type_str_map[type];
    }

    return "UNKNOWN";
  }

  constexpr auto get_align() const {
    if constexpr (Fmt == Format::x86) {
      return static_cast<std::uint32_t>(this->lumped_align_ >> 32U);
    } else if constexpr (Fmt == Format::x86_64) {
      return this->lumped_align_;
    }

    throw "Bad format type";
  }
};

static_assert(sizeof(ProgramHeader<Format::x86>) == 0x20);
static_assert(sizeof(ProgramHeader<Format::x86_64>) == 0x38);

struct ELFFile {
  template <Format Fmt>
  struct Content {
    FileHeaderWithoutIdentity<Fmt> file_header_;
    std::vector<ProgramHeader<Fmt>> program_headers_;
    std::vector<SectionHeader<Fmt>> section_headers_;
    std::vector<std::string> section_header_names_;
  };

  using ELFFormatDependentContent = std::variant<Content<Format::x86>, Content<Format::x86_64>>;

  Identity identity_;
  ELFFormatDependentContent content_;

  explicit ELFFile(std::fstream& t_file) noexcept : identity_{get_identity(t_file)}, content_{this->parse(t_file)} {}

  template <Format Fmt>
  constexpr auto get_section_memory_type(SectionHeader<Fmt> const& t_section) const {
    auto const content = std::get<to_underlying(Fmt) - 1>(this->content_);
    return *ranges::find_if(content.program_headers_, [&](auto const& t_ph) {
      return t_ph.vaddr_ <= t_section.addr_ and t_section.addr_ < t_ph.vaddr_ + t_ph.memsz_;
    });
  }

 private:
  static Identity get_identity(std::fstream& t_file) noexcept {
    std::array<char, sizeof(Identity)> buffer;
    t_file.read(buffer.data(), sizeof(Identity));
    return std::bit_cast<Identity>(buffer);
  }

  template <Format Fmt>
  static auto parse_content(std::fstream& t_file) noexcept {
    Content<Fmt> ret_type;
    std::array<char, sizeof(FileHeaderWithoutIdentity<Fmt>)> buffer{};
    t_file.read(buffer.data(), buffer.size());
    ret_type.file_header_   = std::bit_cast<FileHeaderWithoutIdentity<Fmt>>(buffer);
    auto const& file_header = ret_type.file_header_;

    auto read_program_header = [&]() mutable {
      std::array<char, sizeof(ProgramHeader<Fmt>)> ph_buffer{};
      t_file.read(ph_buffer.data(), ph_buffer.size());
      return std::bit_cast<ProgramHeader<Fmt>>(ph_buffer);
    };
    auto const program_header_offset = file_header.phoff_;
    ret_type.program_headers_.reserve(file_header.phnum_);
    t_file.seekg(static_cast<std::streamoff>(program_header_offset));
    std::generate_n(std::back_inserter(ret_type.program_headers_), file_header.phnum_, read_program_header);

    auto read_section_header = [&]() mutable {
      std::array<char, sizeof(SectionHeader<Fmt>)> sh_buffer{};
      t_file.read(sh_buffer.data(), sh_buffer.size());
      return std::bit_cast<SectionHeader<Fmt>>(sh_buffer);
    };
    auto const section_header_offset = file_header.shoff_;
    ret_type.section_headers_.reserve(file_header.shnum_);
    t_file.seekg(static_cast<std::streamoff>(section_header_offset));
    std::generate_n(std::back_inserter(ret_type.section_headers_), file_header.shnum_, read_section_header);

    auto const sh_name_table_offset = ret_type.section_headers_[file_header.shstrndx_].offset_;
    auto get_section_name           = [&](auto const& t_sh) mutable {
      auto const sh_name_offset = sh_name_table_offset + t_sh.name_;  //
      t_file.seekg(static_cast<std::streamoff>(sh_name_offset));
      std::string ret_val;
      std::getline(t_file, ret_val, '\0');
      return ret_val;
    };
    ret_type.section_header_names_.reserve(file_header.shnum_);
    std::transform(ret_type.section_headers_.begin(), ret_type.section_headers_.end(),
                   std::back_inserter(ret_type.section_header_names_), get_section_name);

    return ret_type;
  }

  ELFFormatDependentContent parse(std::fstream& t_file) const {
    if (auto const fmt = static_cast<Format>(this->identity_.class_); fmt == Format::x86) {
      return parse_content<Format::x86>(t_file);
    } else if (fmt == Format::x86_64) {
      return parse_content<Format::x86_64>(t_file);
    }

    throw std::invalid_argument("Unknown format type, abort");
  }
};

}  // namespace esplink