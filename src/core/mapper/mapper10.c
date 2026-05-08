#include "mapper10.h"

#include <stddef.h>

enum {
    MAPPER10_CHR_BANK_SIZE = 0x1000
};

static size_t mapper10_prg_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->prg_rom_size / NESEMU_PRG_BANK_SIZE;
}

static size_t mapper10_chr_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->chr_mem_size / MAPPER10_CHR_BANK_SIZE;
}

static uint8_t mapper10_chr_bank_for_address(const NesMapper *mapper, uint16_t address)
{
    address &= 0x1FFFu;
    if (address < 0x1000u) {
        return mapper->mapper10_chr_banks[mapper->mapper10_latch0 == 0xFDu ? 0 : 1];
    }
    return mapper->mapper10_chr_banks[mapper->mapper10_latch1 == 0xFDu ? 2 : 3];
}

static size_t mapper10_chr_offset(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper10_chr_bank_count(mapper);
    size_t bank;

    if (bank_count == 0) {
        return 0;
    }
    bank = (size_t)mapper10_chr_bank_for_address(mapper, address) % bank_count;
    return bank * MAPPER10_CHR_BANK_SIZE + (size_t)(address & 0x0FFFu);
}

static void mapper10_update_latch(NesMapper *mapper, uint16_t address)
{
    address &= 0x1FFFu;
    if (address >= 0x0FD8u && address <= 0x0FDFu) {
        mapper->mapper10_latch0 = 0xFDu;
    } else if (address >= 0x0FE8u && address <= 0x0FEFu) {
        mapper->mapper10_latch0 = 0xFEu;
    } else if (address >= 0x1FD8u && address <= 0x1FDFu) {
        mapper->mapper10_latch1 = 0xFDu;
    } else if (address >= 0x1FE8u && address <= 0x1FEFu) {
        mapper->mapper10_latch1 = 0xFEu;
    }
}

void nes_mapper10_init(NesMapper *mapper)
{
    if (mapper == NULL) {
        return;
    }
    mapper->mapper10_prg_bank = 0;
    mapper->mapper10_chr_banks[0] = 0;
    mapper->mapper10_chr_banks[1] = 0;
    mapper->mapper10_chr_banks[2] = 0;
    mapper->mapper10_chr_banks[3] = 0;
    mapper->mapper10_latch0 = 0xFEu;
    mapper->mapper10_latch1 = 0xFEu;
}

uint8_t nes_mapper10_prg_read(const NesMapper *mapper, uint16_t address)
{
    size_t bank_count = mapper10_prg_bank_count(mapper);
    size_t bank;
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || bank_count == 0 || address < 0x8000u) {
        return 0xFFu;
    }
    if (address < 0xC000u) {
        bank = (size_t)mapper->mapper10_prg_bank % bank_count;
    } else {
        bank = bank_count - 1u;
    }
    offset = bank * NESEMU_PRG_BANK_SIZE + (size_t)(address & 0x3FFFu);
    if (offset < mapper->prg_rom_size) {
        return mapper->prg_rom[offset];
    }
    return 0xFFu;
}

void nes_mapper10_prg_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    NesMapper *mapper;

    if (nes == NULL || address < 0x8000u) {
        return;
    }
    mapper = &nes->mapper;
    switch (address & 0xF000u) {
    case 0xA000u:
        mapper->mapper10_prg_bank = (uint8_t)(value & 0x0Fu);
        break;
    case 0xB000u:
        mapper->mapper10_chr_banks[0] = (uint8_t)(value & 0x1Fu);
        break;
    case 0xC000u:
        mapper->mapper10_chr_banks[1] = (uint8_t)(value & 0x1Fu);
        break;
    case 0xD000u:
        mapper->mapper10_chr_banks[2] = (uint8_t)(value & 0x1Fu);
        break;
    case 0xE000u:
        mapper->mapper10_chr_banks[3] = (uint8_t)(value & 0x1Fu);
        break;
    case 0xF000u:
        if (nes->rom.mirroring != NES_MIRROR_FOUR_SCREEN) {
            nes->rom.mirroring = (value & 1u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL;
        }
        break;
    default:
        break;
    }
}

uint8_t nes_mapper10_ppu_read(NesEmu *nes, uint16_t address)
{
    NesMapper *mapper;
    size_t offset;
    uint8_t value;

    if (nes == NULL) {
        return 0xFFu;
    }
    mapper = &nes->mapper;
    if (mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return 0xFFu;
    }
    offset = mapper10_chr_offset(mapper, address);
    value = offset < mapper->chr_mem_size ? mapper->chr_mem[offset] : 0xFFu;
    mapper10_update_latch(mapper, address);
    return value;
}

void nes_mapper10_ppu_write(NesMapper *mapper, uint16_t address, uint8_t value)
{
    size_t offset;

    if (mapper == NULL || !mapper->chr_is_ram || mapper->chr_mem == NULL || mapper->chr_mem_size == 0) {
        return;
    }
    offset = mapper10_chr_offset(mapper, address);
    if (offset < mapper->chr_mem_size) {
        mapper->chr_mem[offset] = value;
    }
}
