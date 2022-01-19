
#include <boost/program_options.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "esp_boot_cmd.hpp"
#include "esp_chip.hpp"
#include "esp_serial_port.hpp"
#include "esp_slip.hpp"

using namespace std::chrono_literals;

enum class CommandLineOption { Flash, Info, Monitor, NoOpt };

std::istream& operator>>(std::istream& t_in, CommandLineOption& t_opt) {
  std::string token;
  t_in >> token;
  if (token == "flash") {
    t_opt = CommandLineOption::Flash;
  } else if (token == "info") {
    t_opt = CommandLineOption::Info;
  } else if (token == "monitor" || token == "mon") {
    t_opt = CommandLineOption::Monitor;
  } else {
    t_opt = CommandLineOption::NoOpt;
    t_in.setstate(std::ios_base::failbit);
  }

  return t_in;
}

template <esputil::BinImgChipID ChipID>
void do_flash(std::filesystem::path const& t_file, std::string_view const t_port, std::uint32_t const t_baud,
              std::uint32_t t_flash_offset) {
  if (t_file.extension() == "elf") {
    throw std::invalid_argument("elf file is not supported, currently support only .bin file");
  }

  esputil::Serial<esputil::ESPSLIP> loader{t_port, t_baud};
  loader.transceive(esputil::command::SYNC(), 50);

  auto const chip_id_ret          = loader.transceive(esputil::command::READ_REG<0x4000'1000>(), 50);
  auto const [chip_id, chip_name] = esputil::get_chip_info(chip_id_ret.value_);
  spdlog::info("ESP chip detected, (id, chip name) = ({:#x}, {})", esputil::to_underlying(chip_id), chip_name);

  loader.transceive(esputil::command::SPI_ATTACH());
  loader.transceive(esputil::command::SPI_SET_PARAMS<>());
  loader.transceive(esputil::command::FLASH_READ_SLOW(0, 16), 0, 2000ms);

  std::ifstream file(t_file.string(), std::ios::binary | std::ios::in);
  auto const fstart = file.tellg();
  file.seekg(0, std::ios::end);
  auto const file_size = static_cast<std::uint32_t>(file.tellg() - fstart);
  file.seekg(0);

  constexpr std::uint32_t BLOCK_SIZE = 4096;
  std::uint32_t const packet_size    = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  spdlog::info("Reading file: {}, file size: {}", t_file.string(), file_size);
  spdlog::info("Erasing {} bytes in flash at offset {}", file_size, t_flash_offset);
  loader.transceive(esputil::command::FLASH_BEGIN(file_size, packet_size, BLOCK_SIZE, t_flash_offset), 1, 15000ms);

  std::array<char, BLOCK_SIZE> buff;
  for (std::uint32_t sequence = 0; not file.eof(); ++sequence) {
    file.read(buff.data(), BLOCK_SIZE);
    auto const byte_read = static_cast<std::uint32_t>(file.gcount());
    if (sequence == 0) {
      esputil::set_binary_header<ChipID>(buff, 0, 0, 0);
    }

    loader.transceive(esputil::command::FLASH_DATA<BLOCK_SIZE>(byte_read, sequence, buff), 1, 1500ms);
  }

  loader.transceive(esputil::command::FLASH_END<esputil::command::FlashEndOption::Reboot>());
}

using FlashFn = void (*)(std::filesystem::path const&, std::string_view const, std::uint32_t const, std::uint32_t);

std::unordered_map<std::string_view, FlashFn> const FLASH_FN_MAP = []() {
  std::unordered_map<std::string_view, FlashFn> ret_val;
  ret_val["esp32c3"] = do_flash<esputil::BinImgChipID::ESP32C3>;
  return ret_val;
}();

int main(int argc, const char** argv) {
  using namespace boost::program_options;
  options_description flash_options("Parameter for flash");
  flash_options.add_options()                                                       //
    ("port", value<std::string>(), "Port of connected ESP MCU")                     //
    ("baud", value<int>()->default_value(115200), "Baudrate of the communication")  //
    ("offset", value<std::string>(), "Flash offset")                                //
    ("chip", value<std::string>()->default_value("esp32c3"), "Chip type, currently support only esp32c3");

  CommandLineOption opt = CommandLineOption::NoOpt;
  options_description hidden_command_opt("All possible command");
  hidden_command_opt.add_options()                                //
    ("command", value<CommandLineOption>(&opt), "command to do")  //
    ("file", value<std::string>(), "input binary file");

  options_description visible_options("All options");
  visible_options.add(flash_options)
    .add_options()                               //
    ("help", "Show this help message and exit")  //
    ("verbose", "Show debug message during execution");

  positional_options_description pd;
  pd.add("command", 1).add("file", 1);

  options_description all("Allowed options");
  all.add(hidden_command_opt).add(visible_options);

  variables_map vm;
  store(command_line_parser(argc, argv).options(all).positional(pd).run(), vm);
  notify(vm);

  if (vm.count("help")) {
    std::cout << visible_options << '\n';
    return EXIT_SUCCESS;
  } else if (vm.count("command") == 0 or opt == CommandLineOption::NoOpt) {
    std::cerr << "Must specifiy a command!\n";
    return EXIT_FAILURE;
  }

  if (vm.count("verbose")) {
    spdlog::set_level(spdlog::level::debug);
  }

  switch (opt) {
    case CommandLineOption::Flash: {
      std::stringstream ss;
      ss << std::hex << vm["offset"].as<std::string>();
      std::uint32_t offset;
      ss >> offset;
      auto const baud_rate = static_cast<std::uint32_t>(vm["baud"].as<int>());
      auto const& flash_fn = FLASH_FN_MAP.at(vm["chip"].as<std::string>());
      flash_fn(vm["file"].as<std::string>(), vm["port"].as<std::string>(), baud_rate, offset);
      break;
    }
    case CommandLineOption::Info:
      [[fallthrough]];
    case CommandLineOption::Monitor:
      [[fallthrough]];
    case CommandLineOption::NoOpt:
      break;
    default:
      break;
  }

  return EXIT_SUCCESS;
}