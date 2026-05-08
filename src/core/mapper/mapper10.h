#ifndef NESEMU_MAPPER10_H
#define NESEMU_MAPPER10_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper10_init(NesMapper *mapper);
uint8_t nes_mapper10_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper10_prg_write(NesEmu *nes, uint16_t address, uint8_t value);
uint8_t nes_mapper10_ppu_read(NesEmu *nes, uint16_t address);
void nes_mapper10_ppu_write(NesMapper *mapper, uint16_t address, uint8_t value);

extern const NesMapperOps nes_mapper10_ops;

#endif
