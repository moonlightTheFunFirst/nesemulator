#ifndef NESEMU_MAPPER88_H
#define NESEMU_MAPPER88_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper88_init(NesMapper *mapper);
uint8_t nes_mapper88_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper88_prg_write(NesMapper *mapper, uint16_t address, uint8_t value);
uint8_t nes_mapper88_chr_read(const NesMapper *mapper, uint16_t address);
void nes_mapper88_chr_write(NesMapper *mapper, uint16_t address, uint8_t value);

extern const NesMapperOps nes_mapper88_ops;

#endif
