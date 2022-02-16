#pragma once

#include <boost/asio.hpp>
#include <boost/asio/high_resolution_timer.hpp>

#include <chrono>
#include <source_location>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

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
    auto const& native_handle = this->port_.lowest_layer().native_handle();  //

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

  boost::asio::io_service service_;
  boost::asio::serial_port port_;

  friend MatchCondition<PacketProtocol>;

 public:
  using TransceiveResult = typename PacketProtocol::Result;

  Serial(Serial const& t_ser) noexcept = default;
  Serial(Serial&& t_ser) noexcept      = default;
  Serial& operator=(Serial const& t_ser) noexcept = default;
  Serial& operator=(Serial&& t_ser) noexcept = default;

  explicit Serial(std::string_view const t_port, std::uint32_t const t_baud = 115200) : port_{service_, t_port.data()} {
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

      boost::asio::high_resolution_timer timeout_timer{this->service_, t_timeout};
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

      this->service_.run();
      this->service_.reset();

      if (byte_read == 0) {
        continue;
      }

      try {
        return this->decode_packet(boost::asio::buffers_begin(input_buffer.data()), byte_read);
      } catch (std::exception& t_e) {
        throw std::runtime_error(fmt::format("{}: {}", t_data.NAME, t_e.what()));
      }
    } while (t_retry-- != 0);

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
