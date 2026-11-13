# KpOS

KpOS is a small hobby operating system built from scratch for x86 (32-bit). It follows the Multiboot2 standard, boots with GRUB, and is written entirely by hand, from the boot code to the shell.

Current version: **v0.0.6**

## What it can do

- Boots with GRUB and prints to a text-mode screen, with scrolling
- Handles hardware interrupts through the IDT and PIC
- Reads the keyboard, including Shift, Ctrl, arrow keys, and Delete
- Runs an interactive shell with:
  - a command table and argument parsing
  - command history (Up and Down)
  - cursor movement and in-line editing
  - a customizable prompt, colors, and status marks
- Reads the memory map, allocates physical pages, and provides a kernel heap
- Powers off through ACPI (tested in QEMU) and reboots
- Includes a few fun commands: `fortune`, `roll`, `beep`, and `about`
- Stores files and folders on a disk with KPFS v2 (`format`, `ls`, `mkdir`, `cd`, `pwd`)
- Writes, reads, moves, renames, and deletes files and folders (`write`, `cat`, `mv`, `rename`, `rm`)
- Deletes whole folders with `rm -y`, and protects folders you're still inside

See [docs/commands.md](docs/commands.md) for every command and keyboard shortcut, and [docs/color.md](docs/color.md) for the color reference.

## Requirements

To build KpOS you need:

- `gcc` with 32-bit support (`gcc-multilib` on Ubuntu/Debian)
- `nasm`
- `ld` (GNU binutils)
- `grub-mkrescue` (Ubuntu/Debian) or `grub2-mkrescue` (Fedora)
- `xorriso`, which `grub-mkrescue` uses to create the ISO
- `qemu-system-x86_64` to run it

## Build and run

```
make clean && make run
```

This builds the kernel, creates `build/kpos.iso`, and starts QEMU.

To build the ISO without running it:

```
make iso
```

## Using a disk

KpOS reads a second drive attached as `disk.img`. The `run` target creates it the first time, as a 1 MB file.

```
disks              check the disk
format yes         erase and set up KPFS on the disk (erases everything)
ls                 list files and folders
```

Commands for files and folders are listed in [docs/commands.md](docs/commands.md).

## Project layout

```
boot/                 Multiboot2 header and bootloader code
  boot.asm
  grub/grub.cfg
kernel/
  kernel.c            Kernel entry point
  cpu/                Interrupts, IDT, PIC, port I/O
  drivers/            Screen (VGA), keyboard, random numbers, ATA disk driver
  fs/                 KPFS file system (folders, files, IDs)
  lib/                Memory helpers (memcpy, memset)
  shell/              Command prompt and commands
  acpi/               ACPI tables and power-off
  mm/                 Memory map, page allocator, heap
docs/
  commands.md         Command reference
  color-dock.md       Color reference
linker.ld             Memory layout for the kernel
Makefile              Build rules
```

## Adding a command

1. Write a function in `kernel/shell/shell.c` with the signature `static void cmd_name(int argc, char **argv)`.
2. Add a prototype and a line to the `commands[]` table.
3. Add a matching entry to `docs/commands.md`.

## Roadmap

- **v0.0.7:** A text editor for files, so `write` can take more than one line
- **Later:** Growing the file table, USB storage, graphical mode with a mouse, and a text-to-graphics tool

## Status and limits

KpOS is a pre-release. It has no user programs, no protection between programs, and no multi-line file editing yet. Shutdown is tested in QEMU only and hasn't been tested on real hardware.

Disks are only supported as ATA devices with 28-bit addressing (up to 128 GB). The file system requires a one-time format when moving from KPFS v1 to v2.

## License

KpOS is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.