#include "mapper0.h"

#include <stddef.h>

static void mapper0_init(NesMapper *mapper, NesMirroring mirroring)
{
    (void)mapper;
    (void)mirroring;
}

static uint8_t mapper0_prg_read(const NesMapper *mapper, uint16_t address)
{
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || mapper->prg_rom_size == 0 || address < 0x8000u) {
        return 0xFFu;
    }
    offset = (size_t)(address - 0x8000u);
    if (mapper->prg_rom_size == NESEMU_PRG_BANK_SIZE) {
        offset %= NESEMU_PRG_BANK_SIZE;
    }
    if (offset < mapper->prg_rom_size) {
        return mapper->prg_rom[offset];
    }
    return 0xFFu;
}

static int mapper0_cpu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    NesMapper *mapper;

    if (nes == NULL || value == NULL) {
        return 0;
    }
    mapper = &nes->mapper;
    if (address >= 0x6000u && address <= 0x7FFFu) {
        *value = mapper->prg_ram[address - 0x6000u];
        return 1;
    }
    if (address >= 0x8000u) {
        *value = mapper0_prg_read(mapper, address);
        return 1;
    }
    return 0;
}

static int mapper0_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL) {
        return 0;
    }
    if (address >= 0x6000u && address <= 0x7FFFu) {
        nes->mapper.prg_ram[address - 0x6000u] = value;
        return 1;
    }
    if (address >= 0x8000u) {
        return 1;
    }
    return 0;
}

static int mapper0_ppu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    NesMapper *mapper;

    if (nes == NULL || value == NULL || address >= 0x2000u) {
        return 0;
    }
    mapper = &nes->mapper;
    if (mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        *value = 0xFFu;
    } else {
        *value = mapper->chr_mem[address % mapper->chr_mem_size];
    }
    return 1;
}

static int mapper0_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    NesMapper *mapper;

    if (nes == NULL || address >= 0x2000u) {
        return 0;
    }
    mapper = &nes->mapper;
    if (mapper->chr_is_ram && mapper->chr_mem != NULL && mapper->chr_mem_size != 0) {
        mapper->chr_mem[address % mapper->chr_mem_size] = value;
    }
    return 1;
}

const NesMapperOps nes_mapper0_ops = {
    0,
    mapper0_init,
    mapper0_cpu_read,
    mapper0_cpu_write,
    mapper0_ppu_read,
    mapper0_ppu_write
};
