#include "esp_common/constants.hpp"
#include "esp_common/utility.hpp"
#include "esp_mkbin/app_format.hpp"
#include "esp_mkbin/elf_reader.hpp"
#include <algorithm>
#include <boost/program_options.hpp>
#include <filesystem>
#include <fmt/ranges.h>
#include <fstream>
#include <iostream>
#include <numeric>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/view/reverse.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace bpo = boost::program_options;

namespace {

void print_elf_info(esplink::Identity const& t_ident, auto const& t_info) {
  spdlog::set_pattern("%v");
  spdlog::debug(
    "ELF Header:\n"
    "Class:                             {}\n"
    "Data:                              {}\n"
    "OS/ABI:                            {}\n"
    "Entry point address:               {:#x}\n"
    "Start of program headers:          {} (bytes in file)\n"
    "Section header string table index: {}\n",
    t_ident.get_class_str(), t_ident.get_endianess(), t_ident.get_os_abi_str(), t_info.file_header_.entry_,
    t_info.file_header_.phoff_, t_info.file_header_.shstrndx_);

  auto const& section_header = t_info.section_headers_;
  auto const length_pred = [](auto const& t_lhs, auto const& t_rhs) { return t_lhs.first.size() < t_rhs.first.size(); };
  auto const max_length  = *std::max_element(section_header.begin(), section_header.end(), length_pred);
  spdlog::debug(
    "Section Headers:\n"
    " [Nr] {: <{length}} {: <15} {: <8} {: <8} {: <8} ES Flg Lk Inf Al",
    "Name", "Type", "Addr", "Off", "Size", fmt::arg("length", max_length.first.size()));
  ranges::for_each(section_header, [i = 0U, &max_length](auto const& t_sh) mutable {
    auto const& [name, section] = t_sh;
    spdlog::debug(" [{:>2}] {:<{length}} {:<15} {:08x} {:08x} {:08x} {:02x} {:>3} {:>2} {:>3} {:>2}", ++i, name,
                  section.get_type_str(), section.addr_, section.offset_, section.size_, section.entsize_,
                  section.get_flag_str(), section.link_, section.info_, section.addralign_,
                  fmt::arg("length", max_length.first.size()));
  });

  spdlog::debug(
    "\nProgram Headers:\n"
    " {: <8} {: <8} {: <10} {: <10} {: <8} {: <8} {} Align",
    "Type", "Offset", "VirtAddr", "PhysAddr", "FileSiz", "MemSiz", "Flg");
  ranges::for_each(t_info.program_headers_, [](auto const& t_ph) {
    spdlog::debug(" {: <8} {:#08x} {:#08x} {:#08x} {:#07x}  {:#07x}  {:<3} {:#04x}", t_ph.get_type_str(), t_ph.offset_,
                  t_ph.vaddr_, t_ph.paddr_, t_ph.filesz_, t_ph.memsz_, t_ph.get_flags_str(), t_ph.get_align());
  });

  spdlog::debug("\n");
  spdlog::set_pattern("%+");
}

}  // namespace

void mk_bin_from_elf(std::string_view t_file, std::string_view t_output_name,
                     esplink::ImageHeaderChipID const t_chip_id) {
  std::fstream file_handle{t_file.data(), std::ios::in | std::ios::binary};
  if (not std::filesystem::is_regular_file(t_file) or std::filesystem::path(t_file).extension() != ".elf") {
    throw std::invalid_argument("Invalid --file option");
  }

  esplink::ELFFile elf{file_handle};
  auto const& ident   = elf.identity_;
  auto const x86_info = std::get<0>(elf.content_);

  if (spdlog::get_level() == spdlog::level::debug) {
    ::print_elf_info(ident, x86_info);
  }

  auto const section_headers = [&]() {
    auto const& path_str = std::filesystem::path(t_file).filename().string();
    auto loadable_count  = x86_info.get_loadable_count();
    if (loadable_count <= esplink::ESP32_IMAGE_MAX_SEGMENT) {
      spdlog::info("Find {} loadable segments in {}, less equal than ESP32_IMAGE_MAX_SEGMENT, skip merge",
                   loadable_count, path_str);
      [[likely]] return x86_info.get_loadable_sections();
    }

    spdlog::info("Find {} loadable segments in {}, greater than ESP32_IMAGE_MAX_SEGMENT, merging adjacent segment",
                 loadable_count, path_str);

    if (auto merged_section = x86_info.merge_adjacent_loadable();
        merged_section.size() <= esplink::ESP32_IMAGE_MAX_SEGMENT) {
      return merged_section;
    }

    throw std::runtime_error("Invalid section count even after merged.");
  }();

  std::fstream output_file_handle(t_output_name.data(), std::ios::out | std::ios::binary);
  auto const img_header = esplink::ImageHeader{
    .magic_number_  = esplink::ESP32_MAGIC_NUMBER,
    .segment_num_   = static_cast<std::uint8_t>(section_headers.size()),
    .entry_address_ = x86_info.file_header_.entry_,
    .chip_id_       = esplink::to_underlying(t_chip_id),
  };

  std::uint8_t check_sum           = esplink::ESP32_CHECKSUM_MAGIC;
  auto const write_loadable_to_bin = [&file_handle, &output_file_handle, &check_sum](auto const& t_sh) {
    // spdlog::debug("writing: {:x}, size: {:x}", t_sh.addr_, t_sh.size_);
    auto const& [name, section] = t_sh;
    auto const padded_length    = esplink::padded_size(section.size_, sizeof(std::uint32_t));
    auto const segment_header   = esplink::ImageSegmentHeader{section.addr_, padded_length};
    auto const byte_stream      = std::bit_cast<std::array<char, sizeof(esplink::ImageSegmentHeader)>>(segment_header);

    output_file_handle.write(byte_stream.data(), sizeof(esplink::ImageSegmentHeader));
    std::vector<std::uint8_t> buffer{};  //  temporary buffer for checksum calculation (lazy implementation)
    buffer.reserve(section.size_);
    std::copy_n(std::istreambuf_iterator<char>(file_handle.seekg(section.offset_)), section.size_,
                std::back_inserter(buffer));
    std::copy_n(buffer.begin(), section.size_, std::ostreambuf_iterator<char>(output_file_handle));
    check_sum = std::accumulate(buffer.begin(), buffer.end(), check_sum, std::bit_xor{});
    std::fill_n(std::ostreambuf_iterator<char>(output_file_handle), padded_length - section.size_, '\0');
  };
  auto const img_header_byte_stream = std::bit_cast<std::array<char, sizeof(img_header)>>(img_header);
  output_file_handle.write(img_header_byte_stream.data(), img_header_byte_stream.size());

  ranges::for_each(section_headers, write_loadable_to_bin);
  auto const curr_file_size        = static_cast<std::uint32_t>(output_file_handle.tellg());
  auto const pad_halfword_filesize = esplink::padded_size(curr_file_size + 1U, 4 * sizeof(std::uint32_t));
  auto const size_to_fill          = pad_halfword_filesize - curr_file_size - 1U;
  spdlog::info("Section write completed, current file size: {}, file size after padding: {}, checksum: {:x}",
               curr_file_size, pad_halfword_filesize, check_sum);
  std::fill_n(std::ostreambuf_iterator<char>(output_file_handle), size_to_fill, 0);
  output_file_handle.write(std::bit_cast<std::array<char, 1>>(check_sum).data(), 1);
}

namespace esplink {

std::istream& operator>>(std::istream& t_in, ImageHeaderChipID& t_opt) {
  std::string token;
  t_in >> token;
  if (token == "ESP32") {
    t_opt = ImageHeaderChipID::ESP32;
  } else if (token == "ESP32S2") {
    t_opt = ImageHeaderChipID::ESP32S2;
  } else if (token == "ESP32C3") {
    t_opt = ImageHeaderChipID::ESP32C3;
  } else if (token == "ESP32S3") {
    t_opt = ImageHeaderChipID::ESP32S3;
  } else if (token == "ESP32C2") {
    t_opt = ImageHeaderChipID::ESP32C2;
  } else {
    t_opt = ImageHeaderChipID::INVALID;
    t_in.setstate(std::ios_base::failbit);
  }

  return t_in;
}

}  // namespace esplink

int main(int argc, char** argv) {
  try {
    bpo::options_description mkbin_option("Parameter for mkbin");
    mkbin_option.add_options()                                                    //
      ("verbose", "Show debug message during execution")                          //
      ("file", bpo::value<std::string>()->required(), "elf file to make binary")  //
      ("output", bpo::value<std::string>()->required(), "output file name")       //
      ("chip", bpo::value<esplink::ImageHeaderChipID>()->required(),
       "chip name, possible value: ESP32, ESP32S2, ESP32C3, ESP32S3, ESP32C2")  //
      ("help", "Show this help message and exit")                               //
      ("flash-param", bpo::value<std::string>(), "flash param");                //

    bpo::variables_map vm;
    bpo::store(bpo::command_line_parser(argc, argv).options(mkbin_option).run(), vm);
    if (vm.count("help") != 0) {
      std::cout << mkbin_option << '\n';
      return EXIT_SUCCESS;
    }

    notify(vm);
    if (vm.count("verbose") != 0) {
      spdlog::set_level(spdlog::level::debug);
    }

    mk_bin_from_elf(vm["file"].as<std::string>(), vm["output"].as<std::string>(),
                    vm["chip"].as<esplink::ImageHeaderChipID>());
  } catch (std::exception& t_e) {
    std::cerr << t_e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}