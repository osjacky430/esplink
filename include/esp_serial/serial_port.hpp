#pragma once

#include <boost/asio.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/high_resolution_timer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/windows/object_handle.hpp>
#include <boost/next_prior.hpp>
#include <chrono>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <source_location>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include "esp_common/utility.hpp"

namespace esplink {

template <typename P>
struct MatchCondition;

template <typename PacketProtocol>
class Serial : PacketProtocol {
  enum class Set : std::uint64_t { High = TIOCMBIC, Low = TIOCMBIS };
  void set_dtr(auto const& t_native_handle, Set const t_set) const noexcept {
    std::uint64_t out_val     = TIOCM_DTR;
    [[maybe_unused]] auto ret = ioctl(t_native_handle, to_underlying(t_set), &out_val);
    assert(ret == 0);
  }

  void set_rts(auto const& t_native_handle, Set const t_set) const noexcept {
    std::uint64_t out_val     = TIOCM_RTS;
    [[maybe_unused]] auto ret = ioctl(t_native_handle, to_underlying(t_set), &out_val);
    assert(ret == 0);
  }

  void hard_reset() noexcept {
    using namespace std::chrono_literals;
    boost::asio::high_resolution_timer sleep_timer(this->port_.get_executor());
    auto const& native_handle = this->port_.lowest_layer().native_handle();

    this->set_dtr(native_handle, Set::High);
    this->set_rts(native_handle, Set::Low);
    sleep_timer.expires_from_now(100ms);
    sleep_timer.wait();
    this->set_rts(native_handle, Set::High);
  }

  void reset() noexcept {
    using namespace std::chrono_literals;
    boost::asio::high_resolution_timer sleep_timer(this->port_.get_executor());
    auto const& native_handle = this->port_.lowest_layer().native_handle();

    // DTR  RTS  -->  EN  IO9  -->   Action
    //  1    1        1    1        No action
    //  0    0        1    1        Clear download mode flag
    //  1    0        0    1        Reset ESP32-C3
    //  0    1        1    0        Set download mode flag
    sleep_timer.expires_from_now(100ms);
    sleep_timer.wait();
    this->set_dtr(native_handle, Set::High);
    this->set_rts(native_handle, Set::Low);
    sleep_timer.expires_from_now(100ms);
    sleep_timer.wait();
    this->set_dtr(native_handle, Set::Low);
    this->set_rts(native_handle, Set::High);
    sleep_timer.expires_from_now(50ms);
    sleep_timer.wait();
    this->set_dtr(native_handle, Set::High);
  }

  void flush_io() noexcept { tcflush(this->port_.lowest_layer().native_handle(), TCIOFLUSH); }

  using PacketProtocol::complete_condition;
  using PacketProtocol::decode_packet;
  using PacketProtocol::generate_packet;

  boost::asio::io_context context_{};
  boost::asio::serial_port port_;

#if BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR
  using bytes_readable = boost::asio::posix::stream_descriptor::bytes_readable;
  boost::asio::posix::stream_descriptor port_helper_{context_, fcntl(port_.native_handle(), F_DUPFD_CLOEXEC)};
#elif BOOST_ASIO_HAS_WINDOWS_OBJECT_HANDLE
#error "not done yet"
#endif

  friend MatchCondition<PacketProtocol>;

 public:
  using TransceiveResult = typename PacketProtocol::Result;

  Serial(Serial const& t_ser) noexcept            = default;
  Serial(Serial&& t_ser) noexcept                 = default;
  Serial& operator=(Serial const& t_ser) noexcept = default;
  Serial& operator=(Serial&& t_ser) noexcept      = default;

  explicit Serial(std::string_view const t_port, std::uint32_t const t_baud = 115200) : port_{context_, t_port.data()} {
    spdlog::info("Connection Success: {}, baudrate: {}", t_port, t_baud);
    this->reset();
    this->flush_io();
    spdlog::info("Resetting {}", t_port);

    using boost::asio::serial_port_base;
    this->port_.set_option(serial_port_base::baud_rate(t_baud));
    this->port_.set_option(serial_port_base::character_size());
    this->port_.set_option(serial_port_base::parity{serial_port_base::parity::none});
    this->port_.set_option(serial_port_base::flow_control{serial_port_base::flow_control::none});
    spdlog::info("Setting serial port options: {} bps, 8 bits, parity: none, flow_control: none", t_baud);
  }

  /**
   * @brief This function reads available bytes sent by esp chip, recieved data don't necessary comply to communication
   *        protocol
   *
   * @return auto
   */
  auto read_raw() {
    boost::asio::streambuf input;
    bytes_readable command(1);
    this->port_helper_.io_control(command);
    auto const byte_read = boost::asio::read(this->port_helper_, input, boost::asio::transfer_exactly(command.get()));

    auto begin = boost::asio::buffers_begin(input.data());
    auto end   = boost::next(begin, byte_read);

    this->flush_io();
    return std::string(begin, end);
  }

  void transfer_raw(boost::asio::streambuf& t_buffer) { boost::asio::write(this->port_, t_buffer); }

  auto& get_io_context() noexcept { return this->context_; }

  /**
   * @brief This function transmits and recieves data from esp chip, it assumes the data to send and recieve comply to
   *        certain communication protocol defined by PacketProtocol
   *
   * @param t_data  Data to be sent to, the data will be passed to PacketProtocol::generate_packet to generate protocol
   *                compliant packet
   * @param t_retry Number of time to retry if error happened, including timeout
   * @param t_timeout Maximum wait time for income data
   *
   * @return TransceiveResult, defined by PacketProtocol, is the return value of PacketProtocol::decode_packet
   */
  TransceiveResult transceive(auto const& t_data, int t_retry = 0,
                              std::chrono::milliseconds t_timeout = std::chrono::milliseconds(100)) {
    int const retried = t_retry;
    do {
      this->flush_io();  // flush all data sent previously from ESP32

      auto const packet       = this->generate_packet(t_data);
      auto const byte_written = boost::asio::write(this->port_, boost::asio::buffer(packet));
      spdlog::info("Sending Packet: {} ({:x})", t_data.NAME, t_data.COMMAND_BYTE);
      spdlog::debug("Packet content: ({} byte)\n", byte_written);
      print_byte_stream(packet.begin(), packet.end());

      boost::asio::high_resolution_timer timeout_timer{this->context_, t_timeout};
      timeout_timer.async_wait([this](auto t_err) mutable {
        if (not t_err) {
          spdlog::warn("Serial port read timeout");
        }
        this->port_.cancel();
      });

      std::size_t byte_read = 0;
      auto read_done_cb     = [&](auto t_err, auto t_byte_read) mutable {
        if (not t_err) {
          byte_read = t_byte_read;  // if any error happened, discard all buffer, therefore only assign on success
          timeout_timer.cancel();
        }
      };
      boost::asio::streambuf input_buffer;  // local streambuf so that the content will be cleaned up automatically
      boost::asio::async_read_until(this->port_, input_buffer, MatchCondition{this}, read_done_cb);

      this->context_.run();
      this->context_.reset();

      if (byte_read == 0) {
        continue;
      }

      try {
        return this->decode_packet(boost::asio::buffers_begin(input_buffer.data()), byte_read);
      } catch (std::exception& t_e) {
        throw std::runtime_error(fmt::format("{}: {}", t_data.NAME, t_e.what()));
      }
    } while (--t_retry != 0);

    throw std::runtime_error(fmt::format("{}: Read failed after retrying for {} times",  //
                                         t_data.NAME, retried));
  }

  ~Serial() { this->hard_reset(); }
};

template <typename P>
struct MatchCondition {
  Serial<P>* port_;

  auto operator()(auto t_begin, auto t_end) { return this->port_->complete_condition(t_begin, t_end); }
};

}  // namespace esplink

template <typename P>
struct boost::asio::is_match_condition<esplink::MatchCondition<P>> : boost::true_type {};
