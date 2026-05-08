#ifndef NESEMU_MAPPER3_H
#define NESEMU_MAPPER3_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper3_init(NesMapper *mapper);
uint8_t nes_mapper3_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper3_prg_write(NesMapper *mapper, uint16_t address, uint8_t value);
uint8_t nes_mapper3_chr_read(const NesMapper *mapper, uint16_t address);
void nes_mapper3_chr_write(NesMapper *mapper, uint16_t address, uint8_t value);

extern const NesMapperOps nes_mapper3_ops;

#endif
