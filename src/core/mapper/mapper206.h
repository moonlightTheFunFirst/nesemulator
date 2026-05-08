#ifndef NESEMU_MAPPER206_H
#define NESEMU_MAPPER206_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper206_init(NesMapper *mapper);
uint8_t nes_mapper206_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper206_prg_write(NesMapper *mapper, uint16_t address, uint8_t value);
uint8_t nes_mapper206_chr_read(const NesMapper *mapper, uint16_t address);
void nes_mapper206_chr_write(NesMapper *mapper, uint16_t address, uint8_t value);

extern const NesMapperOps nes_mapper206_ops;

#endif
