# esputil <!-- omit in toc -->

A self learning project that tries to flash code to esp chips without any dependency of esp-idf, esptool, etc.

# Table of Contents
- [Table of Contents](#table-of-contents)
- [Disclaimer](#disclaimer)
- [Flashing ESP32](#flashing-esp32)
- [Reference](#reference)

# Disclaimer

This is only for self learning purpose, currently only support flashing esp32c3 chips (not even sure if it succeeded or not). The only thing that I'm sure is the chip doesn't report any error.

# Flashing ESP32

There are several ways to flash code to esp32, one of them is via UART. For `ESP32-C3-DevKitM`, there is a USB-UART bridge (`CP2102N-A02-GQFN28`) on it, which means that all one needs to flash code to `ESP32-C3-DevKitM` is a micro usb cable.

# Reference

1. This project is heavily inspired by [this github repo](https://github.com/cpq/mdk)
2. esp32c3 trm