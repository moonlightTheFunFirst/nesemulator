#ifndef NESEMU_MAPPER4_H
#define NESEMU_MAPPER4_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper4_init(NesMapper *mapper);
uint8_t nes_mapper4_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper4_prg_write(NesEmu *nes, uint16_t address, uint8_t value);
uint8_t nes_mapper4_chr_read(const NesMapper *mapper, uint16_t address);
void nes_mapper4_chr_write(NesMapper *mapper, uint16_t address, uint8_t value);
void nes_mapper4_clock_a12_rising(NesEmu *nes);

#endif
