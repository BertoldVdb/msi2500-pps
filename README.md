# msi2500-pps

Replacement firmware for the 8051 core inside the Mirics MSi2500 USB SDR
bridge. On top of the normal I/Q streaming it adds:

- **PPS timestamping**: edges on GPIO_0 (or the USB SOF interrupt) are tied to
  the streaming interrupt count and the capture sample counter, so you can
  locate a PPS edge in the sample stream.
- **Bit-banged UART TX and I2C master** on GPIO_1 (SDA) and GPIO_2 (SCL/TX).
- **USB compliance fixes**.

The firmware runs from RAM: a host loads it over USB (vendor request `0x44`)
and reboots the core into it (request `0x40`).
[libmirisdr-6](https://github.com/BertoldVdb/libmirisdr-6) is the host library
that loads this firmware and uses its features.

## Building

Needs [SDCC](https://sdcc.sourceforge.net/) and Python 3.

```sh
make
```

This produces:

- `msi2500pps.bin` - the raw image, stamped with a build id
- `msi2500pps_fw.c` - the same image as a C array, for hosts that embed the
  firmware instead of shipping a separate file

## Vendor requests

| Request | Purpose |
|---------|---------|
| `0x52`  | Enable/disable PPS capture, select source and divider, request an anchor |
| `0x54`  | UART transmit (`wValue` = bit delay) |
| `0x55`  | I2C write (`wValue` = address and flags, `wIndex` = delay) |
| `0x56`  | I2C read |
| `0x57`  | I2C status of the last transfer (0 = ACKed) |
| `0x58`  | Recover a stuck I2C bus |
| `0x59`  | Call a function at `wValue`, entry state in `callCtx` (0x1FF8); 0x1A00-0x1BFF is free for the code, 0x1800-0x19FF is reserved |
| `0x5A`  | Load a register list into a bank (`wValue` bit 0), run it or queue it (bit 1); `wIndex` = tuner port gap; no data = stop |

List entries are 4 bytes, `[register, value low, mid, high]` for a register below `0x20`, or a command with a 16 bit argument: `0x80` wait microseconds, `0x81` wait stream interrupts (counted from the previous wait), `0x82` repeat from the start, `0x83` switch to the queued bank, `0x84` wait for the SPI master's done flag.

The stock requests `0x40`-`0x45` are reimplemented based on experimentation and how the drivers use it.
See `main.c` for the exact payload layouts and `msi2500.h` for the memory map and register definitions.

## License

MIT, see [LICENSE](LICENSE).
