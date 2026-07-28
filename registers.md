# Rib:Bit Firmware Register Map

This firmware exposes a byte-addressed virtual register map over I2C.

- Write transactions use the first byte as the register address.
- A write that only sends the register address updates the internal register pointer without changing data.
- Multi-byte values are big-endian.
- Some addresses are shared by separate read and write behaviors.
- Reading or writing any register increments the sequence counter.

For the MBUS entries below, `MBUS` refers to the MikroBUS socket defined in the MikroBUS Standard Specification v2.00. The relevant references are the `mikroBUS socket standard` overview on page 5 and the `Pinout specification` on page 6, which define the SPI, UART, I2C, `RST`, `INT`, `AN`, `PWM`, and power pins exposed by a MikroBUS socket.

### `0x01` `VERSION` / `DEBUG`

Read: returns the firmware version as a single byte.

Write: sets the runtime debug flags register.

Defined flags:

- Bit `0` (`0x01`): I2C traffic debug indicator.

When bit `0` is set, firmware drives debug LED A high during I2C write handling and low after I2C read responses, giving a visible activity marker for host traffic. If debug LEDs are not compiled in, this flag has no visible hardware effect.

All other bits are currently reserved and ignored by the implemented logic.

Use this address as read-only when probing firmware version and write-only when controlling debug behavior.

### `0x02` `GPS_STATUS` / `GPS_CONTROL`

Read: returns one status byte.

- Bit `0`: GPS power state. `1` means enabled.

Write: accepts one control byte.

- Bit `0`: GPS power enable. `1` turns GPS power on, `0` turns it off.
- Other bits are ignored.

### `0x03` `MBUS_STATUS` / `MBUS_CONTROL`

Read: returns one status byte.

- Bit `0`: mBus power state. `1` means enabled.
- Bit `1`: mBus currently owns the shared SPI select state.

Write: accepts one control byte.

- Bit `0`: mBus power enable.
- Bit `1`: select or release the MikroBUS SPI bus.
- Other bits are ignored.

Per the MikroBUS socket overview and pinout specification, the SPI group on the socket is `CS`, `SCK`, `MISO`, and `MOSI`. Setting bit `1` gives the MikroBUS socket ownership of that shared SPI path and deselects the other SPI devices. Clearing it releases the SPI selection.

Bit `0` is board-specific power gating for the Citizen Bit MikroBUS socket. The MikroBUS standard defines `+3.3V`, `+5V`, and ground pins on the socket, but it does not define how a host board must switch those rails.

### `0x04` `CAMERA_STATUS` / `CAMERA_CONTROL`

Read: returns one status byte.

- Bit `0`: camera power state. `1` means enabled.
- Bit `1`: camera currently owns the shared SPI select state.

Write: accepts one control byte.

- Bit `0`: camera power enable.
- Bit `1`: select or release the camera on the shared SPI bus.
- Other bits are ignored.

Setting bit `1` selects the camera and deselects the other SPI devices. Clearing it releases the SPI selection.

### `0x05` `LORA_STATUS` / `LORA_CONTROL`

Read: returns one status byte.

- Bit `0`: always `1`. LoRa has no firmware-controlled power switch.
- Bit `1`: LoRa currently owns the shared SPI select state.

Write: accepts one control byte.

- Bit `1`: select or release LoRa on the shared SPI bus.
- Bit `0` has no effect.
- Other bits are ignored.

### `0x06` `SD_STATUS` / `SD_CONTROL`

Read: returns one status byte.

- Bit `0`: always `1`. The SD path has no firmware-controlled power switch.
- Bit `1`: SD currently owns the shared SPI select state.

Write: accepts one control byte.

- Bit `1`: select or release the SD device on the shared SPI bus.
- Bit `0` has no effect.
- Other bits are ignored.

### `0x07` `BUTTONS_STATE` / `BUTTONS_IRQ`

Read: returns one byte containing the current button states.

- Bits `0` through `5` map to `SW_0` through `SW_5`.
- A bit is `1` when the button is pressed and `0` when released.
- Reading this register clears the button IRQ bit.

Write: accepts one byte as a button IRQ mask.

- Bits `0` through `5` define which buttons are intended to participate in IRQ masking.

The current firmware stores this mask but does not use it to gate IRQ generation, so button-change interrupts still assert whenever the button ISR runs.

### `0x08` `MBUS_INT_DIGITAL`

Read: switches the `MBUS_INT` pin to input mode and returns one byte.

- Value `0x00`: pin reads low.
- Value `0x01`: pin reads high.

Write: switches the `MBUS_INT` pin to output mode and accepts one byte.

- Bit `0`: output level. `1` drives high, `0` drives low.
- Other bits are ignored.

In the MikroBUS pinout specification, `INT` is the dedicated hardware interrupt signal on the socket. Use this register when the add-on board exposes a digital interrupt or event line on `INT`.

### `0x09` `MBUS_INT_ANALOG`

Read: switches the `MBUS_INT` pin to input mode and returns a 16-bit big-endian analog sample.

Write: switches the `MBUS_INT` pin to output mode and accepts a 16-bit big-endian PWM value.

Send exactly two data bytes for writes.

This behavior does not match the MikroBUS standard signal role for `INT`, which is defined as a hardware interrupt pin rather than an analog or PWM signal. Treat this register as a board-specific repurposing of the physical `INT` line.

### `0x0A` `MBUS_RESET_DIGITAL`

Read: switches the `MBUS_RESET` pin to input mode and returns one byte.

- Value `0x00`: pin reads low.
- Value `0x01`: pin reads high.

Write: switches the `MBUS_RESET` pin to output mode and accepts one byte.

- Bit `0`: output level. `1` drives high, `0` drives low.
- Other bits are ignored.

In the MikroBUS pinout specification, `RST` is the socket reset signal. Use this register for normal digital assertion or release of the add-on board reset line.

### `0x0B` `MBUS_RESET_ANALOG`

Read: switches the `MBUS_RESET` pin to input mode and returns a 16-bit big-endian analog sample.

Write: switches the `MBUS_RESET` pin to output mode and accepts a 16-bit big-endian PWM value.

Send exactly two data bytes for writes.

This behavior does not match the MikroBUS standard signal role for `RST`, which is defined as a reset line rather than an analog or PWM signal. Treat this register as a board-specific repurposing of the physical `RST` line.

### `0x0C` `MBUS_SERIAL_CONFIG`

Read: currently returns a single byte `0x00`. The active serial configuration is not reported back yet.

Write: configures the mBus UART. Payload is five bytes total after the register address.

- Bytes `0` to `3`: baud rate as a 32-bit big-endian unsigned integer.
- Byte `4`: serial format selector.

Supported serial format selectors are:

- `0x00` to `0x07`: `5N1` through `8N2`
- `0x08` to `0x0F`: `5E1` through `8E2`
- `0x10` to `0x17`: `5O1` through `8O2`

Invalid format values fall back to `8N1`. Baud rates above `2000000` are clamped to `2000000`. Writes shorter than five payload bytes are ignored.

Per the MikroBUS socket standard, the UART group on the socket is `RX` and `TX`. This register configures that UART path as exposed to the MikroBUS add-on board.

### `0x0D` `MBUS_SERIAL_DATA`

Read: returns one byte from the mBus receive FIFO.

- If data is available, the oldest buffered byte is returned.
- If the FIFO becomes empty after the read, the mBus serial IRQ bit is cleared.
- If no data is available, the register returns `0x00`.

Write: sends all remaining bytes in the I2C write payload to the mBus UART transmit path.

This register behaves like a FIFO, not a latched value.

Per the MikroBUS add-on board pinout notes, socket `RX` and `TX` connect to the module-side UART pins. Use this register for streaming bytes over the MikroBUS UART channel, not for packetized framing.

### `0x0E` `MBUS_SERIAL_LENGTH`

Read: returns the current number of bytes waiting in the mBus receive FIFO.

Write: not implemented. Writes are ignored.

This length applies to data buffered from the MikroBUS UART `RX` and `TX` path.

### `0x0F` `GPS_DATA`

Read: returns one byte from the GPS receive FIFO.

- If data is available, the oldest buffered byte is returned.
- If the FIFO becomes empty after the read, the GPS IRQ bit is cleared.
- If no data is available, the register returns `0x00`.

Write: not implemented. Writes are ignored.

This register behaves like a FIFO, not a latched value.

### `0x10` `GPS_LENGTH`

Read: returns the current number of bytes waiting in the GPS receive FIFO.

Write: not implemented. Writes are ignored.

### `0xFE` `SEQUENCE`

Read: returns the current sequence byte.

- The value increments after every register read and after every register write.
- The counter never remains at `0x00`; if it wraps, it is forced to `0x01`.

Write: not implemented. Writes are ignored.

Use this register to detect missed host polls or device resets.

### `0xFF` `IRQ`

Read: returns the current IRQ bitmask without clearing it.

- Bit `0`: button event pending.
- Bit `1`: GPS data pending.
- Bit `2`: mBus event pending.
- Bit `3`: `MBUS_INT` event pending.
- Bit `4`: `MBUS_RESET` event pending.
- Bit `5`: mBus serial data pending.
- Bit `6`: camera event pending.
- Bit `7`: LoRa event pending.

Write: currently has no effect. The firmware accepts the write but does not update the IRQ register.

At present, only the button, GPS, and mBus serial bits are actively asserted or cleared by the implemented logic in `main.cpp`.
