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
| `0x51`  | Read the PPS timestamp counters |
| `0x52`  | Enable/disable PPS capture, select source and divider, request an anchor |
| `0x53`  | Read the captured sample-counter anchor |
| `0x54`  | UART transmit (`wValue` = bit delay) |
| `0x55`  | I2C write (`wValue` = address and flags, `wIndex` = delay) |
| `0x56`  | I2C read |
| `0x57`  | I2C status of the last transfer (0 = ACKed) |
| `0x58`  | Recover a stuck I2C bus |
| `0x59`  | Call a function at `wValue`, entry state in `callCtx` (0x1FF8) |

The stock requests `0x40`-`0x45` are reimplemented based on experimentation and how the drivers use it.
See `main.c` for the exact payload layouts and `msi2500.h` for the memory map and register definitions.

## License

MIT, see [LICENSE](LICENSE).
