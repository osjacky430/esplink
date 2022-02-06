# esplink <!-- omit in toc -->

A self learning project that tries to flash code to esp chips without any dependency of esp-idf, esptool, etc.

# Table of Contents
- [Table of Contents](#table-of-contents)
- [Disclaimer](#disclaimer)
- [Flashing ESP32](#flashing-esp32)
- [Make esp32 binary image from elf file](#make-esp32-binary-image-from-elf-file)
- [Reference](#reference)

# Disclaimer

This is only for self learning purpose, currently only support esp32c3 chips.

# Flashing ESP32

```
./esp-flash --help

All options:
  --help                 Show this help message and exit
  --verbose              Show debug message during execution

Parameter for flash:
  --port arg             Port of connected ESP MCU
  --baud arg (=115200)   Baudrate of the communication
  --offset arg           Flash offset
  --flash-param arg      Flash parameter, including SPI flash mode, SPI flash 
                         speed, and flash chip size
  --chip arg (=esp32c3)  Chip type, currently support only esp32c3
```

Example:

```
./esp-flash flash main.bin --port /dev/ttyUSB0 --offset 0
```

# Make esp32 binary image from elf file

```
./esp-mkbin --help

Parameter for mkbin:
  --verbose             Show debug message during execution
  --file arg            elf file to make binary
  --output arg          output file name
  --chip arg            chip name, possible value: ESP32, ESP32S2, ESP32C3, 
                        ESP32S3, ESP32C2
  --help                Show this help message and exit
  --flash-param arg     flash param
```

Example: 

```
./esp-mkbin --file main.elf --output main.bin --chip ESP32C3
```

# Reference

1. This project is heavily inspired by [this github repo](https://github.com/cpq/mdk)
2. esp32c3 trm