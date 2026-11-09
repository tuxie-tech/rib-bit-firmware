---
layout: layouts/catalog-page
title: Rib:bit Firmware
hero: firmware-hero.png
info: >-
    The firmware for the Rib:bit's on-board controller, which gives the micro:bit I2C access to the buttons, GPS, MikroBUS, camera, SD, and LoRa lines.
tags:
    - software
    - firmware
    - rib:bit
---

## Overview

The Rib:bit carries its own ATmega328PB controller that sits between the micro:bit and the board's peripherals. The firmware makes that controller look like a small set of byte-wide registers on the micro:bit's I2C bus.

The Rib:bit MakeCode extensions talk to this firmware for you, so most projects never need this page. It is here for anyone building their own host software or changing the firmware itself.

The firmware is currently being rewritten for consistency, so expect details to change.

## What it handles

- Button state and button interrupts
- Power for the GPS receiver, MikroBUS socket, and camera
- Chip-select ownership of the shared SPI bus (MikroBUS, camera, LoRa, SD)
- MikroBUS serial port and the `INT`, `RST`, `AN`, and `PWM` pin functions
- Buffering GPS data for the host to read

## Talking to it

- The controller answers on I2C address `0x10`.
- The first byte of a write is the register address. A write that only sends the address sets the register pointer for the next read.
- Multi-byte values are big-endian.
- Every register read or write increments the sequence counter in register `0xFE`.
- Register `0xFF` holds the pending interrupt flags, and the controller signals the micro:bit on its interrupt line (pin P8 on the micro:bit).

## Register map

| Address | Name | Purpose |
| --- | --- | --- |
| `0x01` | `VERSION` / `DEBUG` | Read the firmware version, or write debug flags. |
| `0x02` | `GPS_STATUS` / `GPS_CONTROL` | GPS power state and control. |
| `0x03` | `MBUS_STATUS` / `MBUS_CONTROL` | MikroBUS power, and ownership of the SPI select. |
| `0x04` | `CAMERA_STATUS` / `CAMERA_CONTROL` | Camera power, and ownership of the SPI select. |
| `0x05` | `LORA_STATUS` / `LORA_CONTROL` | LoRa SPI select. |
| `0x06` | `SD_STATUS` / `SD_CONTROL` | SD card SPI select. |
| `0x07` | `BUTTONS_STATE` / `BUTTONS_IRQ` | Current button states, and the button interrupt mask. |
| `0x08` | `MBUS_INT_DIGITAL` | MikroBUS `INT` pin as a digital line. |
| `0x09` | `MBUS_INT_ANALOG` | MikroBUS `INT` pin as an analog read or PWM output. |
| `0x0A` | `MBUS_RESET_DIGITAL` | MikroBUS `RST` pin as a digital line. |
| `0x0B` | `MBUS_RESET_ANALOG` | MikroBUS `RST` pin as an analog read or PWM output. |
| `0x0C` | `MBUS_SERIAL_CONFIG` | Configure the MikroBUS serial port. |
| `0x0D` | `MBUS_SERIAL_DATA` | Read or write MikroBUS serial data. |
| `0x0E` | `MBUS_SERIAL_LENGTH` | Bytes waiting in the MikroBUS receive buffer. |
| `0x0F` | `GPS_DATA` | Read GPS data one byte at a time. |
| `0x10` | `GPS_LENGTH` | Bytes waiting in the GPS receive buffer. |
| `0xFE` | `SEQUENCE` | Transaction counter. |
| `0xFF` | `IRQ` | Pending interrupt flags: bit 0 buttons, bit 1 GPS, bit 2 MikroBUS, bit 3 MikroBUS `INT`, bit 4 MikroBUS `RST`, bit 5 MikroBUS serial data, bit 6 camera, bit 7 LoRa. |

Bit-level details for every register are in [registers.md](https://github.com/tuxie-tech/rib-bit-firmware/blob/main/registers.md) in the firmware repository.

## Building the firmware

The project uses [PlatformIO](https://platformio.org/) and targets the ATmega328PB with the Arduino framework, running at 8 MHz.

1. Install PlatformIO (the VS Code extension or the command line tool).
2. Clone the repository: `git clone https://github.com/tuxie-tech/rib-bit-firmware`
3. Build with `pio run`.
4. Flash with `pio run -t upload`. The project is set up to upload through an FT234 serial adapter (`avrft234`).

The board version and pin assignments are set in `include/config.h`.

## More resources

- Firmware repository: [tuxie-tech/rib-bit-firmware](https://github.com/tuxie-tech/rib-bit-firmware)
- MakeCode extensions that use this firmware: [Rib:bit Basics](../pxt-rib-bit/), [Rib:bit GPS Support](../pxt-rib-bit-gps/), [Rib:bit MikroBUS Support](../pxt-rib-bit-mikrobus/), [Rib:bit LoRa Radio Support](../pxt-rib-bit-lora/)
