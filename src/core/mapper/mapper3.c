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
