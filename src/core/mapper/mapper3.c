#include "mapper3.h"

#include <stddef.h>

void nes_mapper3_init(NesMapper *mapper)
{
    if (mapper == NULL) {
        return;
    }
    mapper->chr_bank = 0;
}

uint8_t nes_mapper3_prg_read(const NesMapper *mapper, uint16_t address)
{
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || mapper->prg_rom_size == 0) {
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

void nes_mapper3_prg_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    (void)address;

    if (mapper == NULL) {
        return;
    }
    mapper->chr_bank = (uint8_t)(value & 0x03u);
}

uint8_t nes_mapper3_chr_read(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count;
    size_t bank;
    size_t offset;

    if (mapper == NULL || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return 0xFFu;
    }
    bank_count = mapper->chr_mem_size / NESEMU_CHR_BANK_SIZE;
    if (bank_count == 0) {
        bank_count = 1;
    }
    bank = (size_t)mapper->chr_bank % bank_count;
    offset = bank * NESEMU_CHR_BANK_SIZE + (size_t)(address & 0x1FFFu);
    if (offset < mapper->chr_mem_size) {
        return mapper->chr_mem[offset];
    }
    return 0xFFu;
}

void nes_mapper3_chr_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    size_t bank_count;
    size_t bank;
    size_t offset;

    if (mapper == NULL || !mapper->chr_is_ram || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return;
    }
    bank_count = mapper->chr_mem_size / NESEMU_CHR_BANK_SIZE;
    if (bank_count == 0) {
        bank_count = 1;
    }
    bank = (size_t)mapper->chr_bank % bank_count;
    offset = bank * NESEMU_CHR_BANK_SIZE + (size_t)(address & 0x1FFFu);
    if (offset < mapper->chr_mem_size) {
        mapper->chr_mem[offset] = value;
    }
}

static void mapper3_init_ops(NesMapper *mapper, NesMirroring mirroring)
{
    (void)mirroring;
    nes_mapper3_init(mapper);
}

static int mapper3_cpu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL) {
        return 0;
    }
    if (address >= 0x6000u && address <= 0x7FFFu) {
        *value = nes->mapper.prg_ram[address - 0x6000u];
        return 1;
    }
    if (address >= 0x8000u) {
        *value = nes_mapper3_prg_read(&nes->mapper, address);
        return 1;
    }
    return 0;
}

static int mapper3_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL) {
        return 0;
    }
    if (address >= 0x6000u && address <= 0x7FFFu) {
        nes->mapper.prg_ram[address - 0x6000u] = value;
        return 1;
    }
    if (address >= 0x8000u) {
        nes_mapper3_prg_write(&nes->mapper, address, value);
        return 1;
    }
    return 0;
}

static int mapper3_ppu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL || address >= 0x2000u) {
        return 0;
    }
    *value = nes_mapper3_chr_read(&nes->mapper, address);
    return 1;
}

static int mapper3_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL || address >= 0x2000u) {
        return 0;
    }
    nes_mapper3_chr_write(&nes->mapper, address, value);
    return 1;
}

const NesMapperOps nes_mapper3_ops = {
    3,
    mapper3_init_ops,
    mapper3_cpu_read,
    mapper3_cpu_write,
    mapper3_ppu_read,
    mapper3_ppu_write
};
