NAME = accel
MODULES = LSM6DS33.o i2c.o

CFLAGS = -I$(CS107E)/include -g -Wall -Og -std=c99 -ffreestanding
LDFLAGS = -nostdlib -T memmap -L$(CS107E)/lib
LDLIBS = -lpiextra -lpi -lgcc

all : $(NAME).bin

%.bin: %.elf
	arm-none-eabi-objcopy $< -O binary $@

%.elf: %.o $(MODULES)
	arm-none-eabi-gcc $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	arm-none-eabi-gcc $(CFLAGS) -c $< -o $@

%.o: %.s
	arm-none-eabi-as $(ASFLAGS) $< -o $@

%.list: %.o
	arm-none-eabi-objdump --no-show-raw-insn -d $< > $@

run: $(NAME).bin
	rpi-run.py -p $<

clean:
	rm -f *.o *.bin *.elf *.list *~

.PHONY: all clean run
.PRECIOUS: %.elf %.o
