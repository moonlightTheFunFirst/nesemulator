#ifndef NESEMU_MAPPER154_H
#define NESEMU_MAPPER154_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper154_init(NesMapper *mapper);
uint8_t nes_mapper154_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper154_prg_write(NesMapper *mapper, uint16_t address, uint8_t value);
uint8_t nes_mapper154_ppu_read(NesEmu *nes, uint16_t address);
void nes_mapper154_ppu_write(NesEmu *nes, uint16_t address, uint8_t value);

extern const NesMapperOps nes_mapper154_ops;

#endif
