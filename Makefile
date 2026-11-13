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
         -Ikernel/shell \
		 -Ikernel/acpi \
		 -Ikernel/mm \
		 -Ikernel/fs

# Folders where make looks for source files
vpath %.c   kernel kernel/cpu kernel/drivers kernel/shell kernel/acpi kernel/mm kernel/fs
vpath %.asm kernel/cpu

OBJS = build/boot.o build/kernel.o \
       build/vga.o build/keyboard.o build/shell.o build/acpi.o build/memmap.o build/pmm.o build/heap.o build/random.o build/ata.o \
       build/block.o build/kpfs.o \
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

DISK = disk.img

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