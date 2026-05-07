#ifndef NESEMU_MAPPER19_H
#define NESEMU_MAPPER19_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper19_init(NesMapper *mapper);
uint8_t nes_mapper19_cpu_read(NesEmu *nes, uint16_t address);
void nes_mapper19_cpu_write(NesEmu *nes, uint16_t address, uint8_t value);
uint8_t nes_mapper19_ppu_read(const NesEmu *nes, uint16_t address);
void nes_mapper19_ppu_write(NesEmu *nes, uint16_t address, uint8_t value);
int nes_mapper19_prg_ram_write_enabled(const NesMapper *mapper, uint16_t address);
void nes_mapper19_clock_irq(NesEmu *nes, int cycles);

#endif
