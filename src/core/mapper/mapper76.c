#include "mapper76.h"

#include <stddef.h>

enum {
    MAPPER76_PRG_BANK_SIZE = 0x2000,
    MAPPER76_CHR_BANK_SIZE = 0x0800
};

static size_t mapper76_prg_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->prg_rom_size / MAPPER76_PRG_BANK_SIZE;
}

static size_t mapper76_chr_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->chr_mem_size / MAPPER76_CHR_BANK_SIZE;
}

static uint8_t mapper76_read_prg_bank(const NesMapper *mapper, size_t bank, uint16_t address)
{
    size_t bank_count = mapper76_prg_bank_count(mapper);
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || bank_count == 0) {
        return 0xFFu;
    }
    bank %= bank_count;
    offset = bank * MAPPER76_PRG_BANK_SIZE + (size_t)(address & 0x1FFFu);
    if (offset < mapper->prg_rom_size) {
        return mapper->prg_rom[offset];
    }
    return 0xFFu;
}

static size_t mapper76_chr_bank_for_address(const NesMapper *mapper, uint16_t address)
{
    uint8_t reg;

    address &= 0x1FFFu;
    reg = (uint8_t)(2u + (address / MAPPER76_CHR_BANK_SIZE));
    return (size_t)mapper->mapper76_regs[reg];
}

static size_t mapper76_chr_offset(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper76_chr_bank_count(mapper);
    size_t bank;

    if (bank_count == 0) {
        return 0;
    }
    bank = mapper76_chr_bank_for_address(mapper, address) % bank_count;
    return bank * MAPPER76_CHR_BANK_SIZE + (size_t)(address & 0x07FFu);
}

static uint8_t mapper76_normalize_bank_value(uint8_t reg, uint8_t value)
{
    if (reg >= 2u && reg < 6u) {
        return (uint8_t)(value & 0x3Fu);
    }
    if (reg >= 6u) {
        return (uint8_t)(value & 0x0Fu);
    }
    return value;
}

void nes_mapper76_init(NesMapper *mapper)
{
    if (mapper == NULL) {
        return;
    }
    mapper->mapper76_bank_select = 0;
    mapper->mapper76_regs[0] = 0;
    mapper->mapper76_regs[1] = 0;
    mapper->mapper76_regs[2] = 0;
    mapper->mapper76_regs[3] = 1;
    mapper->mapper76_regs[4] = 2;
    mapper->mapper76_regs[5] = 3;
    mapper->mapper76_regs[6] = 0;
    mapper->mapper76_regs[7] = 1;
}

uint8_t nes_mapper76_prg_read(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper76_prg_bank_count(mapper);
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
        bank = mapper->mapper76_regs[6];
        break;
    case 0xA000u:
        bank = mapper->mapper76_regs[7];
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
    return mapper76_read_prg_bank(mapper, bank, address);
}

void nes_mapper76_prg_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    uint8_t reg;

    if (mapper == NULL || address < 0x8000u) {
        return;
    }
    if ((address & 0xE001u) == 0x8000u) {
        mapper->mapper76_bank_select = (uint8_t)(value & 0x07u);
    } else if ((address & 0xE001u) == 0x8001u) {
        reg = (uint8_t)(mapper->mapper76_bank_select & 0x07u);
        if (reg >= 2u) {
            mapper->mapper76_regs[reg] = mapper76_normalize_bank_value(reg, value);
        }
    }
}

uint8_t nes_mapper76_chr_read(const NesMapper *mapper, uint16_t address)
{
    size_t offset;

    if (mapper == NULL || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return 0xFFu;
    }
    offset = mapper76_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        return mapper->chr_mem[offset];
    }
    return 0xFFu;
}

void nes_mapper76_chr_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    size_t offset;

    if (mapper == NULL || !mapper->chr_is_ram || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return;
    }
    offset = mapper76_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        mapper->chr_mem[offset] = value;
    }
}

static void mapper76_init_ops(NesMapper *mapper, NesMirroring mirroring)
{
    (void)mirroring;
    nes_mapper76_init(mapper);
}

static int mapper76_cpu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL) {
        return 0;
    }
    if (address >= 0x8000u) {
        *value = nes_mapper76_prg_read(&nes->mapper, address);
        return 1;
    }
    return 0;
}

static int mapper76_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL) {
        return 0;
    }
    if (address >= 0x8000u) {
        nes_mapper76_prg_write(&nes->mapper, address, value);
        return 1;
    }
    return 0;
}

static int mapper76_ppu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL || address >= 0x2000u) {
        return 0;
    }
    *value = nes_mapper76_chr_read(&nes->mapper, address);
    return 1;
}

static int mapper76_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL || address >= 0x2000u) {
        return 0;
    }
    nes_mapper76_chr_write(&nes->mapper, address, value);
    return 1;
}

const NesMapperOps nes_mapper76_ops = {
    76,
    mapper76_init_ops,
    mapper76_cpu_read,
    mapper76_cpu_write,
    mapper76_ppu_read,
    mapper76_ppu_write
};
