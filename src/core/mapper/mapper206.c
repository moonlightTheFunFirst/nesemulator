#include "mapper206.h"

#include <stddef.h>

enum {
    MAPPER206_PRG_BANK_SIZE = 0x2000,
    MAPPER206_CHR_BANK_SIZE = 0x0400
};

static size_t mapper206_prg_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->prg_rom_size / MAPPER206_PRG_BANK_SIZE;
}

static size_t mapper206_chr_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->chr_mem_size / MAPPER206_CHR_BANK_SIZE;
}

static uint8_t mapper206_read_prg_bank(const NesMapper *mapper, size_t bank, uint16_t address)
{
    size_t bank_count = mapper206_prg_bank_count(mapper);
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || bank_count == 0) {
        return 0xFFu;
    }
    bank %= bank_count;
    offset = bank * MAPPER206_PRG_BANK_SIZE + (size_t)(address & 0x1FFFu);
    if (offset < mapper->prg_rom_size) {
        return mapper->prg_rom[offset];
    }
    return 0xFFu;
}

static size_t mapper206_chr_bank_for_address(const NesMapper *mapper, uint16_t address)
{
    uint8_t bank = 0;

    address &= 0x1FFFu;
    if (address < 0x0800u) {
        bank = (uint8_t)(mapper->mapper206_regs[0] + ((address >> 10) & 1u));
    } else if (address < 0x1000u) {
        bank = (uint8_t)(mapper->mapper206_regs[1] + (((address - 0x0800u) >> 10) & 1u));
    } else if (address < 0x1400u) {
        bank = mapper->mapper206_regs[2];
    } else if (address < 0x1800u) {
        bank = mapper->mapper206_regs[3];
    } else if (address < 0x1C00u) {
        bank = mapper->mapper206_regs[4];
    } else {
        bank = mapper->mapper206_regs[5];
    }
    return (size_t)bank;
}

static size_t mapper206_chr_offset(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper206_chr_bank_count(mapper);
    size_t bank;

    if (bank_count == 0) {
        return 0;
    }
    bank = mapper206_chr_bank_for_address(mapper, address) % bank_count;
    return bank * MAPPER206_CHR_BANK_SIZE + (size_t)(address & 0x03FFu);
}

static uint8_t mapper206_normalize_bank_value(uint8_t reg, uint8_t value)
{
    if (reg < 2u) {
        return (uint8_t)(value & 0x3Eu);
    }
    if (reg < 6u) {
        return (uint8_t)(value & 0x3Fu);
    }
    return (uint8_t)(value & 0x0Fu);
}

void nes_mapper206_init(NesMapper *mapper)
{
    if (mapper == NULL) {
        return;
    }
    mapper->mapper206_bank_select = 0;
    mapper->mapper206_regs[0] = 0;
    mapper->mapper206_regs[1] = 2;
    mapper->mapper206_regs[2] = 4;
    mapper->mapper206_regs[3] = 5;
    mapper->mapper206_regs[4] = 6;
    mapper->mapper206_regs[5] = 7;
    mapper->mapper206_regs[6] = 0;
    mapper->mapper206_regs[7] = 1;
}

uint8_t nes_mapper206_prg_read(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper206_prg_bank_count(mapper);
    size_t last_bank;
    size_t second_last_bank;
    size_t bank;

    if (mapper == NULL || mapper->prg_rom == NULL || bank_count == 0 || address < 0x8000u) {
        return 0xFFu;
    }
    last_bank = bank_count - 1u;
    second_last_bank = bank_count >= 2u ? bank_count - 2u : 0;
    switch (address & 0xE000u) {
    case 0x8000u:
        bank = mapper->mapper206_regs[6];
        break;
    case 0xA000u:
        bank = mapper->mapper206_regs[7];
        break;
    case 0xC000u:
        bank = second_last_bank;
        break;
    case 0xE000u:
        bank = last_bank;
        break;
    default:
        return 0xFFu;
    }
    return mapper206_read_prg_bank(mapper, bank, address);
}

void nes_mapper206_prg_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    uint8_t reg;

    if (mapper == NULL || address < 0x8000u) {
        return;
    }
    if ((address & 0xE001u) == 0x8000u) {
        mapper->mapper206_bank_select = (uint8_t)(value & 0x07u);
    } else if ((address & 0xE001u) == 0x8001u) {
        reg = (uint8_t)(mapper->mapper206_bank_select & 0x07u);
        mapper->mapper206_regs[reg] = mapper206_normalize_bank_value(reg, value);
    }
}

uint8_t nes_mapper206_chr_read(const NesMapper *mapper, uint16_t address)
{
    size_t offset;

    if (mapper == NULL || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return 0xFFu;
    }
    offset = mapper206_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        return mapper->chr_mem[offset];
    }
    return 0xFFu;
}

void nes_mapper206_chr_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    size_t offset;

    if (mapper == NULL || !mapper->chr_is_ram || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return;
    }
    offset = mapper206_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        mapper->chr_mem[offset] = value;
    }
}

static void mapper206_init_ops(NesMapper *mapper, NesMirroring mirroring)
{
    (void)mirroring;
    nes_mapper206_init(mapper);
}

static int mapper206_cpu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL) {
        return 0;
    }
    if (address >= 0x8000u) {
        *value = nes_mapper206_prg_read(&nes->mapper, address);
        return 1;
    }
    return 0;
}

static int mapper206_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL) {
        return 0;
    }
    if (address >= 0x8000u) {
        nes_mapper206_prg_write(&nes->mapper, address, value);
        return 1;
    }
    return 0;
}

static int mapper206_ppu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL || address >= 0x2000u) {
        return 0;
    }
    *value = nes_mapper206_chr_read(&nes->mapper, address);
    return 1;
}

static int mapper206_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL || address >= 0x2000u) {
        return 0;
    }
    nes_mapper206_chr_write(&nes->mapper, address, value);
    return 1;
}

const NesMapperOps nes_mapper206_ops = {
    206,
    mapper206_init_ops,
    mapper206_cpu_read,
    mapper206_cpu_write,
    mapper206_ppu_read,
    mapper206_ppu_write
};
