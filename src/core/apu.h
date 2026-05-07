#ifndef NESEMU_APU_H
#define NESEMU_APU_H

#include "nesemu.h"

#include <stdint.h>

void nes_apu_init(NesApu *apu);
void nes_apu_reset(NesApu *apu);
uint8_t nes_apu_read(NesEmu *nes, uint16_t address);
void nes_apu_write(NesEmu *nes, uint16_t address, uint8_t value);
void nes_apu_clock_frame_counter(NesEmu *nes, int cycles);
void nes_apu_clock_audio(NesEmu *nes, int cycles);

#endif
