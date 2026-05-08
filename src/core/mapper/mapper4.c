#include "mapper4.h"
#include "../nes_internal.h"

#include <stddef.h>

enum {
    MAPPER4_PRG_BANK_SIZE = 0x2000,
    MAPPER4_CHR_BANK_SIZE = 0x0400
};

static size_t mapper4_prg_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->prg_rom_size / MAPPER4_PRG_BANK_SIZE;
}

static size_t mapper4_chr_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->chr_mem_size / MAPPER4_CHR_BANK_SIZE;
}

static uint8_t mapper4_read_prg_bank(const NesMapper *mapper, size_t bank, uint16_t address)
{
    size_t bank_count = mapper4_prg_bank_count(mapper);
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || bank_count == 0) {
        return 0xFFu;
    }
    bank %= bank_count;
    offset = bank * MAPPER4_PRG_BANK_SIZE + (size_t)(address & 0x1FFFu);
    if (offset < mapper->prg_rom_size) {
        return mapper->prg_rom[offset];
    }
    return 0xFFu;
}

static size_t mapper4_chr_bank_for_address(const NesMapper *mapper, uint16_t address)
{
    uint8_t bank = 0;

    address &= 0x1FFFu;
    if (!mapper->mapper4_chr_mode) {
        if (address < 0x0800u) {
            bank = (uint8_t)((mapper->mapper4_regs[0] & 0xFEu) + ((address >> 10) & 1u));
        } else if (address < 0x1000u) {
            bank = (uint8_t)((mapper->mapper4_regs[1] & 0xFEu) + (((address - 0x0800u) >> 10) & 1u));
        } else if (address < 0x1400u) {
            bank = mapper->mapper4_regs[2];
        } else if (address < 0x1800u) {
            bank = mapper->mapper4_regs[3];
        } else if (address < 0x1C00u) {
            bank = mapper->mapper4_regs[4];
        } else {
            bank = mapper->mapper4_regs[5];
        }
    } else {
        if (address < 0x0400u) {
            bank = mapper->mapper4_regs[2];
        } else if (address < 0x0800u) {
            bank = mapper->mapper4_regs[3];
        } else if (address < 0x0C00u) {
            bank = mapper->mapper4_regs[4];
        } else if (address < 0x1000u) {
            bank = mapper->mapper4_regs[5];
        } else if (address < 0x1800u) {
            bank = (uint8_t)((mapper->mapper4_regs[0] & 0xFEu) + (((address - 0x1000u) >> 10) & 1u));
        } else {
            bank = (uint8_t)((mapper->mapper4_regs[1] & 0xFEu) + (((address - 0x1800u) >> 10) & 1u));
        }
    }
    return (size_t)bank;
}

static size_t mapper4_chr_offset(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper4_chr_bank_count(mapper);
    size_t bank;

    if (bank_count == 0) {
        return 0;
    }
    bank = mapper4_chr_bank_for_address(mapper, address) % bank_count;
    return bank * MAPPER4_CHR_BANK_SIZE + (size_t)(address & 0x03FFu);
}

void nes_mapper4_init(NesMapper *mapper)
{
    if (mapper == NULL) {
        return;
    }
    mapper->mapper4_bank_select = 0;
    mapper->mapper4_regs[0] = 0;
    mapper->mapper4_regs[1] = 2;
    mapper->mapper4_regs[2] = 4;
    mapper->mapper4_regs[3] = 5;
    mapper->mapper4_regs[4] = 6;
    mapper->mapper4_regs[5] = 7;
    mapper->mapper4_regs[6] = 0;
    mapper->mapper4_regs[7] = 1;
    mapper->mapper4_prg_mode = 0;
    mapper->mapper4_chr_mode = 0;
    mapper->mapper4_irq_latch = 0;
    mapper->mapper4_irq_counter = 0;
    mapper->mapper4_irq_reload = 0;
    mapper->mapper4_irq_enabled = 0;
    mapper->mapper4_irq_pending = 0;
}

uint8_t nes_mapper4_prg_read(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper4_prg_bank_count(mapper);
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
        bank = mapper->mapper4_prg_mode ? second_last_bank : mapper->mapper4_regs[6];
        break;
    case 0xA000u:
        bank = mapper->mapper4_regs[7];
        break;
    case 0xC000u:
        bank = mapper->mapper4_prg_mode ? mapper->mapper4_regs[6] : second_last_bank;
        break;
    case 0xE000u:
        bank = last_bank;
        break;
    default:
        return 0xFFu;
    }
    return mapper4_read_prg_bank(mapper, bank, address);
}

void nes_mapper4_prg_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    NesMapper *mapper;

    if (nes == NULL || address < 0x8000u) {
        return;
    }
    mapper = &nes->mapper;
    switch (address & 0xE001u) {
    case 0x8000u:
        mapper->mapper4_bank_select = (uint8_t)(value & 0x07u);
        mapper->mapper4_prg_mode = (uint8_t)((value >> 6) & 1u);
        mapper->mapper4_chr_mode = (uint8_t)((value >> 7) & 1u);
        break;
    case 0x8001u:
        if (mapper->mapper4_bank_select < 2u) {
            value &= 0xFEu;
        }
        mapper->mapper4_regs[mapper->mapper4_bank_select & 7u] = value;
        break;
    case 0xA000u:
        if (nes->rom.mirroring != NES_MIRROR_FOUR_SCREEN) {
            nes->rom.mirroring = (value & 1u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL;
        }
        break;
    case 0xA001u:
        break;
    case 0xC000u:
        mapper->mapper4_irq_latch = value;
        break;
    case 0xC001u:
        mapper->mapper4_irq_counter = 0;
        mapper->mapper4_irq_reload = 1;
        break;
    case 0xE000u:
        mapper->mapper4_irq_enabled = 0;
        mapper->mapper4_irq_pending = 0;
        nes_update_irq(nes);
        break;
    case 0xE001u:
        mapper->mapper4_irq_enabled = 1;
        break;
    default:
        break;
    }
}

uint8_t nes_mapper4_chr_read(const NesMapper *mapper, uint16_t address)
{
    size_t offset;

    if (mapper == NULL || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return 0xFFu;
    }
    offset = mapper4_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        return mapper->chr_mem[offset];
    }
    return 0xFFu;
}

void nes_mapper4_chr_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    size_t offset;

    if (mapper == NULL || !mapper->chr_is_ram || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return;
    }
    offset = mapper4_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        mapper->chr_mem[offset] = value;
    }
}

void nes_mapper4_clock_a12_rising(NesEmu *nes)
{
    NesMapper *mapper;

    if (nes == NULL || nes->rom.mapper_id != 4u) {
        return;
    }
    mapper = &nes->mapper;
    if (mapper->mapper4_irq_counter == 0 || mapper->mapper4_irq_reload) {
        mapper->mapper4_irq_counter = mapper->mapper4_irq_latch;
        mapper->mapper4_irq_reload = 0;
    } else {
        mapper->mapper4_irq_counter--;
    }
    if (mapper->mapper4_irq_counter == 0 && mapper->mapper4_irq_enabled) {
        mapper->mapper4_irq_pending = 1;
        nes_update_irq(nes);
    }
}
