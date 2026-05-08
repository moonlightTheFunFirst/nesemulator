#ifndef NESEMU_MAPPER95_H
#define NESEMU_MAPPER95_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper95_init(NesMapper *mapper);
uint8_t nes_mapper95_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper95_prg_write(NesMapper *mapper, uint16_t address, uint8_t value);
uint8_t nes_mapper95_ppu_read(NesEmu *nes, uint16_t address);
void nes_mapper95_ppu_write(NesEmu *nes, uint16_t address, uint8_t value);

extern const NesMapperOps nes_mapper95_ops;

#endif
