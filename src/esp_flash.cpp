#include "esp_common/chip.hpp"
#include "esp_serial/boot_cmd.hpp"
#include "esp_serial/serial_port.hpp"
#include "esp_serial/slip.hpp"
#include <boost/program_options.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace std::chrono_literals;

using FlashFn = void (*)(std::filesystem::path const&, std::string_view const, std::uint32_t const,
                         std::uint32_t const);

template <esplink::ImageHeaderChipID ChipID>
void flash(std::filesystem::path const& t_file, std::string_view const t_port, std::uint32_t const t_baud,
           std::uint32_t const t_flash_offset) {
  using namespace std::chrono_literals;

  if (t_file.extension() == "elf") {
    throw std::invalid_argument("elf file is not supported, currently support only .bin file");
  }

  esplink::Serial<esplink::ESPSLIP> loader{t_port, t_baud};
  loader.transceive(esplink::command::SYNC(), 50);

  auto const chip_id_ret          = loader.transceive(esplink::command::READ_REG<0x4000'1000>(), 50);
  auto const [chip_id, chip_name] = esplink::get_chip_info(chip_id_ret.value_);
  spdlog::info("ESP chip detected, (id, chip name) = ({:#x}, {})", esplink::to_underlying(chip_id), chip_name);

  loader.transceive(esplink::command::SPI_ATTACH());
  loader.transceive(esplink::command::SPI_SET_PARAMS<>());
  auto const flash_read = loader.transceive(esplink::command::FLASH_READ_SLOW{0, 16}, 0, 2000ms);

  [[maybe_unused]] auto const magic_number = flash_read.data_[0];
  assert(magic_number == esplink::ESP_MAGIC_NUMBER);
  auto const spi_mode        = flash_read.data_[2];
  auto const spi_speed       = static_cast<std::uint8_t>(flash_read.data_[3] >> 4U);
  auto const flash_chip_size = static_cast<std::uint8_t>(flash_read.data_[3] & 0xFU);
  spdlog::info("Using flash mode: {}, flash speed: {}, flash chip size: {}", spi_mode, spi_speed, flash_chip_size);

  std::ifstream file(t_file.string(), std::ios::binary | std::ios::in);
  auto const fstart = file.tellg();
  file.seekg(0, std::ios::end);
  auto const file_size = static_cast<std::uint32_t>(file.tellg() - fstart);
  file.seekg(0);

  constexpr std::uint32_t BLOCK_SIZE = 4096;
  std::uint32_t const packet_size    = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  spdlog::info("Reading file: {}, file size: {}", t_file.string(), file_size);
  spdlog::info("Erasing {} bytes in flash at offset {}", file_size, t_flash_offset);
  loader.transceive(esplink::command::FLASH_BEGIN{file_size, packet_size, BLOCK_SIZE, t_flash_offset}, 1, 15000ms);

  std::array<char, BLOCK_SIZE> buff{};
  for (std::uint32_t sequence = 0; not file.eof(); ++sequence) {
    file.read(buff.data(), BLOCK_SIZE);
    auto const byte_read = static_cast<std::uint32_t>(file.gcount());
    if (sequence == 0) {
      esplink::set_binary_header<ChipID>(buff, spi_mode, spi_speed, flash_chip_size);
    }

    loader.transceive(esplink::command::FLASH_DATA<BLOCK_SIZE>{byte_read, sequence, buff}, 1, 1500ms);
  }

  loader.transceive(esplink::command::FLASH_END<esplink::command::FlashEndOption::Reboot>());
}

static auto& get_flash_fn() {
  static std::unordered_map<std::string_view, FlashFn> const FLASH_FN_MAP = []() {
    std::unordered_map<std::string_view, FlashFn> ret_val;
    ret_val["ESP32C3"] = flash<esplink::ImageHeaderChipID::ESP32C3>;
    return ret_val;
  }();

  return FLASH_FN_MAP;
}

int main(int argc, const char** argv) {
  using namespace boost::program_options;
  options_description flash_options("Parameter for flash");
  flash_options.add_options()                                                       //
    ("port", value<std::string>(), "Port of connected ESP MCU")                     //
    ("baud", value<int>()->default_value(115200), "Baudrate of the communication")  //
    ("offset", value<std::string>(), "Flash offset")                                //
    ("flash-param", value<std::string>(),
     "Flash parameter, including SPI flash mode, SPI flash speed, and flash chip size")  //
    ("chip", value<std::string>()->default_value("ESP32C3"), "Chip type, currently support only ESP32C3");

  options_description visible_options("All options");
  visible_options.add(flash_options)
    .add_options()                               //
    ("help", "Show this help message and exit")  //
    ("verbose", "Show debug message during execution");

  positional_options_description pd;
  pd.add("file", 1);

  options_description all("Allowed options");
  all.add(visible_options);

  variables_map vm;
  store(command_line_parser(argc, argv).options(all).positional(pd).run(), vm);
  notify(vm);

  if (vm.count("help") != 0) {
    std::cout << visible_options << '\n';
    return EXIT_SUCCESS;
  }

  if (vm.count("file") == 0) {
    std::cerr << "Must specifiy a file!\n";
    return EXIT_FAILURE;
  }

  if (vm.count("verbose") != 0) {
    spdlog::set_level(spdlog::level::debug);
  }

  std::stringstream ss;
  ss << std::hex << vm["offset"].as<std::string>();
  std::uint32_t offset = 0;
  ss >> offset;
  auto const baud_rate  = static_cast<std::uint32_t>(vm["baud"].as<int>());
  auto const& flash_map = get_flash_fn();
  auto const& flash_fn  = flash_map.at(vm["chip"].as<std::string>());
  flash_fn(vm["file"].as<std::string>(), vm["port"].as<std::string>(), baud_rate, offset);

  return EXIT_SUCCESS;
}