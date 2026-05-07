#include "nesemu.h"

#include <stdlib.h>
#include <string.h>

enum {
    INES_HEADER_SIZE = 16,
    INES_TRAINER_SIZE = 512
};

static void nes_mapper_clear(NesMapper *mapper)
{
    if (mapper->prg_rom != NULL) {
        free(mapper->prg_rom);
    }
    if (mapper->chr_mem != NULL) {
        free(mapper->chr_mem);
    }
    memset(mapper, 0, sizeof(*mapper));
}

void nes_init(NesEmu *nes)
{
    if (nes == NULL) {
        return;
    }
    memset(nes, 0, sizeof(*nes));
}

void nes_shutdown(NesEmu *nes)
{
    if (nes == NULL) {
        return;
    }
    nes_mapper_clear(&nes->mapper);
    memset(nes, 0, sizeof(*nes));
}

void nes_reset(NesEmu *nes)
{
    if (nes == NULL) {
        return;
    }
    nes->buttons = 0;
    if (nes->rom_loaded) {
        nes->reset_vector = (uint16_t)nes_cpu_read(nes, 0xFFFCu);
        nes->reset_vector |= (uint16_t)nes_cpu_read(nes, 0xFFFDu) << 8;
    } else {
        nes->reset_vector = 0;
    }
}

NesResult nes_load_rom_image(NesEmu *nes, const uint8_t *data, size_t size)
{
    NesRomInfo info;
    NesMapper mapper;
    size_t offset;
    size_t prg_size;
    size_t chr_size;
    uint8_t flags6;
    uint8_t flags7;
    uint8_t mapper_id;

    if (nes == NULL || data == NULL) {
        return NES_RESULT_INVALID_ARGUMENT;
    }
    if (size < INES_HEADER_SIZE) {
        return NES_RESULT_INVALID_ROM;
    }
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1Au) {
        return NES_RESULT_INVALID_ROM;
    }

    flags6 = data[6];
    flags7 = data[7];
    mapper_id = (uint8_t)((flags6 >> 4) | (flags7 & 0xF0u));
    if (mapper_id != 0) {
        return NES_RESULT_UNSUPPORTED_MAPPER;
    }

    prg_size = (size_t)data[4] * NESEMU_PRG_BANK_SIZE;
    chr_size = (size_t)data[5] * NESEMU_CHR_BANK_SIZE;
    if (prg_size == 0) {
        return NES_RESULT_INVALID_ROM;
    }

    offset = INES_HEADER_SIZE;
    if ((flags6 & 0x04u) != 0) {
        offset += INES_TRAINER_SIZE;
    }
    if (offset > size || prg_size > size - offset) {
        return NES_RESULT_INVALID_ROM;
    }
    if (chr_size > size - offset - prg_size) {
        return NES_RESULT_INVALID_ROM;
    }

    memset(&info, 0, sizeof(info));
    memset(&mapper, 0, sizeof(mapper));

    info.mapper_id = mapper_id;
    info.prg_banks = data[4];
    info.chr_banks = data[5];
    info.has_trainer = (uint8_t)((flags6 & 0x04u) != 0);
    info.has_battery_ram = (uint8_t)((flags6 & 0x02u) != 0);
    if ((flags6 & 0x08u) != 0) {
        info.mirroring = NES_MIRROR_FOUR_SCREEN;
    } else {
        info.mirroring = (flags6 & 0x01u) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL;
    }

    mapper.prg_rom = (uint8_t *)malloc(prg_size);
    if (mapper.prg_rom == NULL) {
        return NES_RESULT_OUT_OF_MEMORY;
    }
    memcpy(mapper.prg_rom, data + offset, prg_size);
    mapper.prg_rom_size = prg_size;
    offset += prg_size;

    if (chr_size == 0) {
        chr_size = NESEMU_CHR_BANK_SIZE;
        mapper.chr_is_ram = 1;
    }
    mapper.chr_mem = (uint8_t *)malloc(chr_size);
    if (mapper.chr_mem == NULL) {
        free(mapper.prg_rom);
        return NES_RESULT_OUT_OF_MEMORY;
    }
    if (info.chr_banks == 0) {
        memset(mapper.chr_mem, 0, chr_size);
    } else {
        memcpy(mapper.chr_mem, data + offset, chr_size);
    }
    mapper.chr_mem_size = chr_size;

    nes_mapper_clear(&nes->mapper);
    nes->rom = info;
    nes->mapper = mapper;
    nes->rom_loaded = 1;
    nes_reset(nes);
    return NES_RESULT_OK;
}

const char *nes_result_string(NesResult result)
{
    switch (result) {
    case NES_RESULT_OK:
        return "ok";
    case NES_RESULT_INVALID_ARGUMENT:
        return "invalid argument";
    case NES_RESULT_OUT_OF_MEMORY:
        return "out of memory";
    case NES_RESULT_INVALID_ROM:
        return "invalid ROM";
    case NES_RESULT_UNSUPPORTED_MAPPER:
        return "unsupported mapper";
    case NES_RESULT_IO_ERROR:
        return "I/O error";
    default:
        return "unknown error";
    }
}

void nes_set_button(NesEmu *nes, NesButton button, int pressed)
{
    uint8_t mask;

    if (nes == NULL || button < 0 || button >= NES_BUTTON_COUNT) {
        return;
    }
    mask = (uint8_t)(1u << (unsigned int)button);
    if (pressed) {
        nes->buttons |= mask;
    } else {
        nes->buttons &= (uint8_t)~mask;
    }
}

int nes_get_button(const NesEmu *nes, NesButton button)
{
    if (nes == NULL || button < 0 || button >= NES_BUTTON_COUNT) {
        return 0;
    }
    return (nes->buttons & (uint8_t)(1u << (unsigned int)button)) != 0;
}

uint8_t nes_cpu_read(const NesEmu *nes, uint16_t address)
{
    const NesMapper *mapper;
    size_t offset;

    if (nes == NULL || !nes->rom_loaded) {
        return 0xFFu;
    }
    mapper = &nes->mapper;

    if (address >= 0x6000u && address <= 0x7FFFu) {
        return mapper->prg_ram[address - 0x6000u];
    }
    if (address >= 0x8000u && mapper->prg_rom_size > 0) {
        offset = (size_t)(address - 0x8000u);
        if (mapper->prg_rom_size == NESEMU_PRG_BANK_SIZE) {
            offset %= NESEMU_PRG_BANK_SIZE;
        }
        if (offset < mapper->prg_rom_size) {
            return mapper->prg_rom[offset];
        }
    }
    return 0xFFu;
}

void nes_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL || !nes->rom_loaded) {
        return;
    }
    if (address >= 0x6000u && address <= 0x7FFFu) {
        nes->mapper.prg_ram[address - 0x6000u] = value;
    }
}

uint8_t nes_ppu_read(const NesEmu *nes, uint16_t address)
{
    size_t offset;

    if (nes == NULL || !nes->rom_loaded || nes->mapper.chr_mem_size == 0) {
        return 0xFFu;
    }
    offset = (size_t)(address & 0x1FFFu);
    if (offset < nes->mapper.chr_mem_size) {
        return nes->mapper.chr_mem[offset];
    }
    return 0xFFu;
}

void nes_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    size_t offset;

    if (nes == NULL || !nes->rom_loaded || !nes->mapper.chr_is_ram) {
        return;
    }
    offset = (size_t)(address & 0x1FFFu);
    if (offset < nes->mapper.chr_mem_size) {
        nes->mapper.chr_mem[offset] = value;
    }
}
