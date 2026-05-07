# NESEMU

NESEMU is an early C-based NES emulator project. The first implementation target is a Windows SDI application with a portable core separated from WinAPI display and input code.

## Current Scope

- Starts a single Windows SDI window.
- Accepts `.nes` files by drag and drop.
- Accepts a ROM path as the first command-line argument.
- Parses iNES ROM images for mapper 0 (NROM).
- Maps mapper 0 CPU PRG ROM/PRG RAM and PPU CHR ROM/CHR RAM.
- Executes documented 6502 CPU opcodes used by mapper 0 games.
- Implements CPU RAM, PPU registers, joypad strobe/read, OAM DMA, and NROM PRG/CHR mapping.
- Renders a 256x240 framebuffer with background and sprite drawing.
- Outputs basic APU pulse, triangle, noise, and DMC audio through the Windows host.
- Mixes Namco 163 mapper 19 wavetable expansion audio.
- Uses a high-resolution frame loop on Windows and pumps audio independently of paint events.
- Tracks controller input:
  - `WASD` or arrow keys: directional pad
  - `Z` or Space: A
  - `X` or Shift: B
  - `C` or Enter: START
  - `V` or Backspace: SELECT
  - `B`: reset

This is not cycle-perfect yet. PPU scrolling, sprite evaluation, APU envelope/sweep/length behavior, and many unofficial CPU opcodes still need accuracy work.

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

Run a ROM directly:

```sh
build/nesemu.exe "rom/Balloon Fight (Japan)/Balloon Fight (Japan).nes"
```

Visual Studio 2022 can open the folder as a CMake project. Configure and build the `nesemu` target from the CMake view.
