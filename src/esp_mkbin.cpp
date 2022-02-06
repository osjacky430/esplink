#include <algorithm>
#include <boost/program_options.hpp>
#include <filesystem>
#include <fmt/ranges.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <range/v3/algorithm/count_if.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/reverse.hpp>
#include <range/v3/view/sliding.hpp>
#include <spdlog/spdlog.h>

#include "esp_common/constants.hpp"
#include "esp_common/utility.hpp"
#include "esp_mkbin/app_format.hpp"
#include "esp_mkbin/elf_reader.hpp"

namespace bpo = boost::program_options;

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

  auto const& sh_name    = t_info.section_header_names_;
  auto const length_pred = [](auto const& t_lhs, auto const& t_rhs) { return t_lhs.size() < t_rhs.size(); };
  auto const max_length  = *std::max_element(sh_name.begin(), sh_name.end(), length_pred);
  spdlog::debug(
    "Section Headers:\n"
    " [Nr] {: <{length}} {: <15} {: <8} {: <8} {: <8} ES Flg Lk Inf Al",
    "Name", "Type", "Addr", "Off", "Size", fmt::arg("length", max_length.size()));
  ranges::for_each(t_info.section_headers_, [i = 0U, &sh_name, &max_length](auto const& t_sh) mutable {
    spdlog::debug(" [{:>2}] {:<{length}} {:<15} {:08x} {:08x} {:08x} {:02x} {:>3} {:>2} {:>3} {:>2}", i, sh_name[i],
                  t_sh.get_type_str(), t_sh.addr_, t_sh.offset_, t_sh.size_, t_sh.entsize_, t_sh.get_flag_str(),
                  t_sh.link_, t_sh.info_, t_sh.addralign_, fmt::arg("length", max_length.size()));
    ++i;
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

void mk_bin_from_elf(std::string_view t_file, std::string_view t_output_name,
                     esplink::ImageHeaderChipID const t_chip_id) {
  std::fstream file_handle{t_file.data(), std::ios::in | std::ios::binary};
  esplink::ELFFile elf{file_handle};
  auto const& ident   = elf.identity_;
  auto const x86_info = std::get<0>(elf.content_);

  if (spdlog::get_level() == spdlog::level::debug) {
    print_elf_info(ident, x86_info);
  }

  auto const should_load      = [](auto const& t_sh) { return t_sh.is_loadable() and t_sh.have_content(); };
  auto loadable_segment_count = ranges::count_if(x86_info.section_headers_, should_load);
  spdlog::info("Find {} loadable segments in {}, {}", loadable_segment_count,
               std::filesystem::path(t_file).filename().string(),
               loadable_segment_count < esplink::ESP32_IMAGE_MAX_SEGMENT
                 ? "less equal than ESP32_IMAGE_MAX_SEGMENT, skip merge"
                 : "greater than ESP32_IMAGE_MAX_SEGMENT, merging adjacent segment");
  auto const section_headers = [&]() mutable {
    using ranges::views::filter;
    using ranges::to;

    auto const& sh = x86_info.section_headers_;
    auto filtered  = sh | filter(should_load) | to<std::vector>;
    if (loadable_segment_count <= esplink::ESP32_IMAGE_MAX_SEGMENT) {
      [[likely]] return filtered;
    }

    // sort according to address, then combine adjacent
    auto const section_comp = [](auto const& t_lhs, auto const& t_rhs) {
      if (t_lhs.addr_ != t_rhs.addr_) {
        return t_lhs.addr_ > t_rhs.addr_;
      }

      return t_lhs.size_ > t_rhs.size_;
    };
    ranges::sort(filtered, section_comp);
    filtered.push_back(filtered.front());  // to ensure last element is merged correctly and pushed into "merged"

    decltype(filtered) merged;
    ranges::for_each(filtered | ranges::views::sliding(2), [&](auto const& t_zip) {
      auto& next = t_zip[0];
      auto& curr = t_zip[1];
      if (elf.get_section_memory_type(curr) == elf.get_section_memory_type(next) and
          curr.addr_ + curr.size_ == next.addr_) {
        curr.size_ += next.size_;
      } else {
        merged.push_back(next);
      }
    });

    if (merged.size() <= esplink::ESP32_IMAGE_MAX_SEGMENT) {
      spdlog::info("Merge complete, segment count changed from {} to {}", loadable_segment_count, merged.size());
      // ranges::for_each(merged, [](auto const& t_elem) { spdlog::debug("{:x} {:x}", t_elem.addr_, t_elem.size_); });
    } else {
      throw std::runtime_error("Segment count exceeded ESP32_IMAGE_MAX_SEGMENT even after merging, abort");
    }
    loadable_segment_count = static_cast<std::ptrdiff_t>(merged.size());
    return merged;
  }();

  std::fstream output_file_handle(t_output_name.data(), std::ios::out | std::ios::binary);
  auto const img_header = esplink::ImageHeader{
    .magic_number_  = esplink::ESP32_MAGIC_NUMBER,
    .segment_num_   = static_cast<std::uint8_t>(loadable_segment_count),
    .entry_address_ = x86_info.file_header_.entry_,
    .chip_id_       = esplink::to_underlying(t_chip_id),
  };

  std::uint8_t check_sum     = esplink::ESP32_CHECKSUM_MAGIC;
  auto write_loadable_to_bin = [&file_handle, &output_file_handle, &check_sum](auto const& t_sh) mutable {
    // spdlog::debug("writing: {:x}, size: {:x}", t_sh.addr_, t_sh.size_);
    auto const padded_length  = esplink::padded_size(t_sh.size_, sizeof(std::uint32_t));
    auto const segment_header = esplink::ImageSegmentHeader{.load_addr_ = t_sh.addr_, .section_length_ = padded_length};
    auto const byte_stream    = std::bit_cast<std::array<char, sizeof(esplink::ImageSegmentHeader)>>(segment_header);

    output_file_handle.write(byte_stream.data(), sizeof(esplink::ImageSegmentHeader));
    std::vector<std::uint8_t> buffer;  //  temporary buffer for checksum calculation (lazy implementation)
    buffer.reserve(t_sh.size_);
    std::copy_n(std::istreambuf_iterator<char>(file_handle.seekg(t_sh.offset_)), t_sh.size_,
                std::back_inserter(buffer));
    std::copy_n(buffer.begin(), t_sh.size_, std::ostreambuf_iterator<char>(output_file_handle));
    check_sum = std::accumulate(buffer.begin(), buffer.end(), check_sum, std::bit_xor{});
    std::fill_n(std::ostreambuf_iterator<char>(output_file_handle), padded_length - t_sh.size_, '\0');
  };
  auto const img_header_byte_stream = std::bit_cast<std::array<char, sizeof(esplink::ImageHeader)>>(img_header);
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

std::istream& operator>>(std::istream& t_in, esplink::ImageHeaderChipID& t_opt) {
  std::string token;
  t_in >> token;
  if (token == "ESP32") {
    t_opt = esplink::ImageHeaderChipID::ESP32;
  } else if (token == "ESP32S2") {
    t_opt = esplink::ImageHeaderChipID::ESP32S2;
  } else if (token == "ESP32C3") {
    t_opt = esplink::ImageHeaderChipID::ESP32C3;
  } else if (token == "ESP32S3") {
    t_opt = esplink::ImageHeaderChipID::ESP32S3;
  } else if (token == "ESP32C2") {
    t_opt = esplink::ImageHeaderChipID::ESP32C2;
  } else {
    t_opt = esplink::ImageHeaderChipID::INVALID;
    t_in.setstate(std::ios_base::failbit);
  }

  return t_in;
}

int main(int argc, char** argv) {
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

  try {
    notify(vm);
  } catch (std::exception& t_e) {
    std::cerr << t_e.what() << '\n';
    return EXIT_FAILURE;
  }

  if (vm.count("verbose") != 0) {
    spdlog::set_level(spdlog::level::debug);
  }

  mk_bin_from_elf(vm["file"].as<std::string>(), vm["output"].as<std::string>(),
                  vm["chip"].as<esplink::ImageHeaderChipID>());
  return EXIT_SUCCESS;
}