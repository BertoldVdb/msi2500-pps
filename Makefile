# Build the MSi2500 PPS firmware.

CC      = sdcc
CFLAGS  = -mmcs51 --model-small --std-sdcc99 --opt-code-size --Werror
LDFLAGS = -mmcs51 --model-small \
          --code-loc 0x0000 --code-size 0x1800 \
          --xram-loc 0x1C00 --xram-size 0x0100 \
          --iram-size 256

TARGET  = msi2500pps
# main.c first: the linker takes the start up and the vectors from the first module
SRC     = main.c core.c desc.c ep0.c usb.c stream.c pps.c list.c \
          bitbang.c i2c.c uart.c mem.c
HDR     = $(wildcard *.h)
REL     = $(SRC:.c=.rel)

# Two artifacts: the raw image, which msi2500load pushes into RAM over USB, and
# the same image as a C array for an application that carries its firmware with
# it rather than shipping a file alongside.
all: $(TARGET).bin $(TARGET)_fw.c

%.rel: %.c $(HDR)
	$(CC) $(CFLAGS) -c $<

$(TARGET).ihx: $(REL)
	$(CC) $(LDFLAGS) -o $(TARGET).ihx $(REL)

$(TARGET).bin: $(TARGET).ihx tools/stampid.py
	makebin -p $< $@
	@test $$(stat -c %s $@) -le 6144 || { echo "$@ ends past 0x1800"; rm -f $@; exit 1; }
	python3 tools/stampid.py $@
	@echo "--- size ---"
	@ls -l $@
	@grep -E "CSEG|XSEG|XISEG|DSEG|ISEG|BSEG|GSINIT|HOME" $(TARGET).mem || true

$(TARGET)_fw.c: $(TARGET).bin tools/mkfwc.py
	python3 tools/mkfwc.py $< $@

clean:
	rm -f *.ihx *.bin *_fw.c *.rel *.lst *.rst *.sym *.asm *.lk *.map *.mem *.adb *.cdb *.omf

.PHONY: all clean
