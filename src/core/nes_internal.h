#ifndef NESEMU_INTERNAL_H
#define NESEMU_INTERNAL_H

#include "nesemu.h"

#include <stdint.h>

enum {
    CPU_C = 0x01,
    CPU_Z = 0x02,
    CPU_I = 0x04,
    CPU_D = 0x08,
    CPU_B = 0x10,
    CPU_U = 0x20,
    CPU_V = 0x40,
    CPU_N = 0x80
};

int nes_cpu_step(NesEmu *nes);
void nes_update_irq(NesEmu *nes);

uint8_t nes_cpu_bus_read(NesEmu *nes, uint16_t address);
uint8_t nes_cpu_bus_read_delayed(NesEmu *nes, uint16_t address, int cpu_cycles);
void nes_cpu_bus_write(NesEmu *nes, uint16_t address, uint8_t value);
void nes_cpu_bus_write_delayed(NesEmu *nes, uint16_t address, uint8_t value, int cpu_cycles);

#endif
