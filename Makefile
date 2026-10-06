# Build the MSi2500 PPS firmware.

CC      = sdcc
CFLAGS  = -I.. -mmcs51 --model-small --std-sdcc99 \
          --code-loc 0x0000 --code-size 0x1800 \
          --xram-loc 0x1C00 --xram-size 0x0100 \
          --iram-size 256 \
          --opt-code-size --Werror

TARGET  = msi2500pps
SRC     = main.c
VPATH   = ..

# Two artifacts: the raw image, which msi2500load pushes into RAM over USB, and
# the same image as a C array for an application that carries its firmware with
# it rather than shipping a file alongside.
all: $(TARGET).bin $(TARGET)_fw.c

$(TARGET).ihx: $(SRC) msi2500.h
	$(CC) $(CFLAGS) -o $(TARGET).ihx $(SRC)

$(TARGET).bin: $(TARGET).ihx tools/stampid.py
	makebin -p $< $@
	python3 tools/stampid.py $@
	@echo "--- size ---"
	@ls -l $@
	@grep -E "CSEG|XSEG|XISEG|DSEG|ISEG|BSEG|GSINIT|HOME" $(TARGET).mem || true

$(TARGET)_fw.c: $(TARGET).bin tools/mkfwc.py
	python3 tools/mkfwc.py $< $@

clean:
	rm -f *.ihx *.bin *_fw.c *.rel *.lst *.rst *.sym *.asm *.lk *.map *.mem

.PHONY: all clean
