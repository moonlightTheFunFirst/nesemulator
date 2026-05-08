#include "mapper154.h"

#include <stddef.h>

enum {
    MAPPER154_PRG_BANK_SIZE = 0x2000,
    MAPPER154_CHR_BANK_SIZE = 0x0400
};

static size_t mapper154_prg_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->prg_rom_size / MAPPER154_PRG_BANK_SIZE;
}

static size_t mapper154_chr_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->chr_mem_size / MAPPER154_CHR_BANK_SIZE;
}

static uint8_t mapper154_read_prg_bank(const NesMapper *mapper, size_t bank, uint16_t address)
{
    size_t bank_count = mapper154_prg_bank_count(mapper);
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || bank_count == 0) {
        return 0xFFu;
    }
    bank %= bank_count;
    offset = bank * MAPPER154_PRG_BANK_SIZE + (size_t)(address & 0x1FFFu);
    if (offset < mapper->prg_rom_size) {
        return mapper->prg_rom[offset];
    }
    return 0xFFu;
}

static size_t mapper154_chr_bank_for_address(const NesMapper *mapper, uint16_t address)
{
    uint8_t bank = 0;

    address &= 0x1FFFu;
    if (address < 0x0800u) {
        bank = (uint8_t)(mapper->mapper154_regs[0] + ((address >> 10) & 1u));
    } else if (address < 0x1000u) {
        bank = (uint8_t)(mapper->mapper154_regs[1] + (((address - 0x0800u) >> 10) & 1u));
    } else if (address < 0x1400u) {
        bank = mapper->mapper154_regs[2];
    } else if (address < 0x1800u) {
        bank = mapper->mapper154_regs[3];
    } else if (address < 0x1C00u) {
        bank = mapper->mapper154_regs[4];
    } else {
        bank = mapper->mapper154_regs[5];
    }
    return (size_t)bank;
}

static size_t mapper154_chr_offset(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper154_chr_bank_count(mapper);
    size_t bank;

    if (bank_count == 0) {
        return 0;
    }
    bank = mapper154_chr_bank_for_address(mapper, address) % bank_count;
    return bank * MAPPER154_CHR_BANK_SIZE + (size_t)(address & 0x03FFu);
}

static uint16_t mapper154_nametable_index(const NesMapper *mapper, uint16_t address)
{
    uint16_t mirrored = address;

    if (mirrored >= 0x3000u && mirrored < 0x3F00u) {
        mirrored = (uint16_t)(mirrored - 0x1000u);
    }
    return (uint16_t)((mapper->mapper154_mirroring ? 0x400u : 0) + (mirrored & 0x03FFu));
}

static uint8_t mapper154_normalize_bank_value(uint8_t reg, uint8_t value)
{
    if (reg < 2u) {
        return (uint8_t)(value & 0x3Eu);
    }
    if (reg < 6u) {
        return (uint8_t)((value & 0x3Fu) | 0x40u);
    }
    return (uint8_t)(value & 0x0Fu);
}

void nes_mapper154_init(NesMapper *mapper)
{
    if (mapper == NULL) {
        return;
    }
    mapper->mapper154_bank_select = 0;
    mapper->mapper154_regs[0] = 0;
    mapper->mapper154_regs[1] = 2;
    mapper->mapper154_regs[2] = 0x44u;
    mapper->mapper154_regs[3] = 0x45u;
    mapper->mapper154_regs[4] = 0x46u;
    mapper->mapper154_regs[5] = 0x47u;
    mapper->mapper154_regs[6] = 0;
    mapper->mapper154_regs[7] = 1;
    mapper->mapper154_mirroring = 0;
}

uint8_t nes_mapper154_prg_read(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper154_prg_bank_count(mapper);
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
        bank = mapper->mapper154_regs[6];
        break;
    case 0xA000u:
        bank = mapper->mapper154_regs[7];
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
    return mapper154_read_prg_bank(mapper, bank, address);
}

void nes_mapper154_prg_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    uint8_t reg;

    if (mapper == NULL || address < 0x8000u) {
        return;
    }
    mapper->mapper154_mirroring = (uint8_t)((value >> 6) & 1u);
    if ((address & 0xE001u) == 0x8000u) {
        mapper->mapper154_bank_select = (uint8_t)(value & 0x07u);
    } else if ((address & 0xE001u) == 0x8001u) {
        reg = (uint8_t)(mapper->mapper154_bank_select & 0x07u);
        mapper->mapper154_regs[reg] = mapper154_normalize_bank_value(reg, value);
    }
}

uint8_t nes_mapper154_ppu_read(NesEmu *nes, uint16_t address)
{
    NesMapper *mapper;
    size_t offset;

    if (nes == NULL) {
        return 0xFFu;
    }
    mapper = &nes->mapper;
    address &= 0x3FFFu;
    if (address >= 0x3000u && address < 0x3F00u) {
        address = (uint16_t)(address - 0x1000u);
    }
    if (address >= 0x2000u && address < 0x3000u) {
        return nes->ppu.nametable[mapper154_nametable_index(mapper, address)];
    }
    if (mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return 0xFFu;
    }
    offset = mapper154_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        return mapper->chr_mem[offset];
    }
    return 0xFFu;
}

void nes_mapper154_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    NesMapper *mapper;
    size_t offset;

    if (nes == NULL) {
        return;
    }
    mapper = &nes->mapper;
    address &= 0x3FFFu;
    if (address >= 0x3000u && address < 0x3F00u) {
        address = (uint16_t)(address - 0x1000u);
    }
    if (address >= 0x2000u && address < 0x3000u) {
        nes->ppu.nametable[mapper154_nametable_index(mapper, address)] = value;
        return;
    }
    if (!mapper->chr_is_ram || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return;
    }
    offset = mapper154_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        mapper->chr_mem[offset] = value;
    }
}

static void mapper154_init_ops(NesMapper *mapper, NesMirroring mirroring)
{
    (void)mirroring;
    nes_mapper154_init(mapper);
}

static int mapper154_cpu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL) {
        return 0;
    }
    if (address >= 0x8000u) {
        *value = nes_mapper154_prg_read(&nes->mapper, address);
        return 1;
    }
    return 0;
}

static int mapper154_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL) {
        return 0;
    }
    if (address >= 0x8000u) {
        nes_mapper154_prg_write(&nes->mapper, address, value);
        return 1;
    }
    return 0;
}

static int mapper154_ppu_read(NesEmu *nes, uint16_t address, uint8_t *value)
{
    if (nes == NULL || value == NULL || address >= 0x3F00u) {
        return 0;
    }
    *value = nes_mapper154_ppu_read(nes, address);
    return 1;
}

static int mapper154_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL || address >= 0x3F00u) {
        return 0;
    }
    nes_mapper154_ppu_write(nes, address, value);
    return 1;
}

const NesMapperOps nes_mapper154_ops = {
    154,
    mapper154_init_ops,
    mapper154_cpu_read,
    mapper154_cpu_write,
    mapper154_ppu_read,
    mapper154_ppu_write
};
