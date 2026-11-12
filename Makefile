CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 \
         -std=gnu11 \
         -ffreestanding \
         -fno-pie \
         -fno-pic \
         -fno-stack-protector \
         -fno-builtin \
         -fno-asynchronous-unwind-tables \
         -fno-unwind-tables \
         -mno-sse \
         -mno-mmx \
         -mno-80387 \
         -Wall -Wextra \
         -O2 \
         -Ikernel/cpu \
         -Ikernel/drivers \
         -Ikernel/shell

# Folders where make looks for source files
vpath %.c   kernel kernel/cpu kernel/drivers kernel/shell
vpath %.asm kernel/cpu

OBJS = build/boot.o build/kernel.o \
       build/vga.o build/keyboard.o build/shell.o \
       build/idt.o build/pic.o build/irq.o build/interrupts.o

.PHONY: all iso run clean

all: iso

build:
	mkdir -p build

build/boot.o: boot/boot.asm | build
	$(AS) -f elf32 $< -o $@

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.asm | build
	$(AS) -f elf32 $< -o $@

build/kernel.elf: $(OBJS) linker.ld
	$(LD) -m elf_i386 \
	    -T linker.ld \
	    -nostdlib \
	    -z max-page-size=0x1000 \
	    -z noexecstack \
	    -o $@ \
	    $(OBJS)

iso: build/kernel.elf
	mkdir -p build/isodir/boot/grub
	cp build/kernel.elf build/isodir/boot/
	cp boot/grub/grub.cfg build/isodir/boot/grub/
	grub2-mkrescue -o build/kpos.iso build/isodir

run: iso
	qemu-system-x86_64 \
	    -cdrom build/kpos.iso \
	    -m 128M

clean:
	rm -rf build/*