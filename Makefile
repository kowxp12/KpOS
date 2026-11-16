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
         -fno-tree-loop-distribute-patterns \
         -mno-sse \
         -mno-mmx \
         -mno-80387 \
         -Wall -Wextra \
         -O2 \
         -Ikernel/cpu \
         -Ikernel/drivers \
         -Ikernel/shell \
         -Ikernel/acpi \
         -Ikernel/mm \
         -Ikernel/fs \
         -Ikernel/lib \
         -Ikernel/tools

# Folders where make looks for source files
vpath %.c   kernel kernel/cpu kernel/drivers kernel/shell kernel/acpi kernel/mm kernel/fs kernel/lib kernel/tools
vpath %.asm kernel/cpu

OBJS = build/boot.o build/kernel.o \
       build/vga.o build/keyboard.o build/shell.o build/editor.o build/acpi.o build/memmap.o build/pmm.o build/heap.o build/random.o build/ata.o \
       build/block.o build/kpfs.o build/mem.o build/util.o \
       build/tools.o build/wc.o build/cp.o \
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
	mkdir -p build/isodir/boot/grub/fonts
	cp build/kernel.elf build/isodir/boot/
	cp boot/grub/grub.cfg build/isodir/boot/grub/
	rm -rf build/isodir/boot/grub/themes
	cp -r boot/grub/themes build/isodir/boot/grub/
	$(MKFONT) -s 16 -o build/isodir/boot/grub/fonts/kpos16.pf2 $(FONT_SRC)
	$(MKFONT) -s 28 -o build/isodir/boot/grub/fonts/kpos28.pf2 $(FONT_SRC)
	grub2-mkrescue -o build/kpos.iso build/isodir

DISK = disk.img
FONT_SRC = /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
MKFONT   = $(shell command -v grub2-mkfont 2>/dev/null || command -v grub-mkfont)

disk: $(DISK)

$(DISK):
	dd if=/dev/zero of=$(DISK) bs=512 count=2048

run: iso $(DISK)
	qemu-system-x86_64 \
	    -cdrom build/kpos.iso \
	    -hda $(DISK) \
	    -m 128M

clean:
	rm -rf build/*