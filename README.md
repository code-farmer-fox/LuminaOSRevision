# LuminaOS

LuminaOS is a small, self-contained hobby operating system for the i386 architecture.
It is built entirely from scratch: a custom bootloader, kernel, filesystem, and a
graphical desktop environment with user-space applications written in a custom
application format (LSP).

## Features

- Custom multiboot bootloader and 32-bit protected-mode kernel
- Physical memory manager and paging
- GDT, IDT, and interrupt handling
- PS/2 keyboard and mouse drivers
- VGA graphics driver with a windowed desktop
- FAT16 filesystem on an IDE disk image
- Process loading and execution of LSP applications
- System call interface for user-space programs
- Built-in applications: Notepad, Files, About, System

## Directory Layout

```
boot/              Bootloader assembly and build scripts
kernel/            Kernel source (C and assembly)
  arch/i386/       x86 entry, GDT, IDT, IRQ, syscalls, user mode
  include/kernel/  Kernel headers
  kernel/          Core kernel modules
build.ps1          Build script (bootloader, kernel, floppy, hdd, LSP apps) for Windows Powershell
run.ps1            QEMU launcher for Unix Bash
build.sh           Build script (bootloader, kernel, floppy, hdd, LSP apps) for Unix Bash
run.sh             QEMU launcher for Unix Bash
```

## Requirements

- Windows with PowerShell or Unix with Bash
- NASM
- i686-elf cross toolchain (`i686-elf-gcc`, `i686-elf-ld`, `i686-elf-objcopy`, `i686-elf-nm`)
- QEMU (`qemu-system-i386`)
- Python (`python312`, if you are using Unix)

## Build

```powershell
.\build.ps1
```
or
```bash
bash build.sh
```

This produces:

- `build/floppy.img` — FAT16 boot floppy containing `KERNEL.BIN`
- `build/hdd.img` — 16 MiB FAT16 data disk with LSP applications
- LSP application binaries compiled from the LuminaOS-SDK examples

## Run

```powershell
.\run.ps1
```
or
```bash
bash run.sh
```
Options:

- `-NoDebug` — do not start the GDB stub (QEMU listens on `tcp::1234` by default)
- `-Serial` — log the serial port to `build/serial.log`
- `-Headless` — run without a display window; debug output goes to `build/debug.log`

Once booted, type `ui` at the shell prompt to enter the desktop.

## Developing Applications

Applications are written in C and compiled to the LuminaOS **LSP** format. The
toolchain, headers, and examples live in the **LuminaOS-SDK** folder. See its
README for details.

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
