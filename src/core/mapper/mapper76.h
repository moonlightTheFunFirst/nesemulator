#ifndef NESEMU_MAPPER76_H
#define NESEMU_MAPPER76_H

#include "nesemu.h"

#include <stdint.h>

void nes_mapper76_init(NesMapper *mapper);
uint8_t nes_mapper76_prg_read(const NesMapper *mapper, uint16_t address);
void nes_mapper76_prg_write(NesMapper *mapper, uint16_t address, uint8_t value);
uint8_t nes_mapper76_chr_read(const NesMapper *mapper, uint16_t address);
void nes_mapper76_chr_write(NesMapper *mapper, uint16_t address, uint8_t value);

extern const NesMapperOps nes_mapper76_ops;

#endif
