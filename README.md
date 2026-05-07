# NESEMU

NESEMU is an early C-based NES emulator project. The first implementation target is a Windows SDI application with a portable core separated from WinAPI display and input code.

## Current Scope

- Starts a single Windows SDI window.
- Accepts `.nes` files by drag and drop.
- Parses iNES ROM images for mapper 0 (NROM).
- Maps mapper 0 CPU PRG ROM/PRG RAM and PPU CHR ROM/CHR RAM.
- Tracks controller input:
  - `WASD`: directional pad
  - `Z`: A
  - `X`: B
  - `C`: START
  - `V`: SELECT
  - `B`: reset

CPU, PPU, APU, frame timing, and actual game execution are not implemented yet.

## Build

GNU make with MinGW-w64:

```sh
make
```

Run unit tests:

```sh
make test
```

Run the Windows app:

```sh
make run
```

Visual Studio 2022 can open the folder as a CMake project. Configure and build the `nesemu` target from the CMake view.
