#include "mapper19.h"
#include "../nes_internal.h"

#include <stddef.h>
#include <string.h>

enum {
    MAPPER19_PRG_BANK_SIZE = 0x2000,
    MAPPER19_CHR_BANK_SIZE = 0x0400,
    MAPPER19_IRQ_LIMIT = 0x7FFF,
    MAPPER19_AUDIO_TICK_CYCLES = 15
};

static size_t mapper19_prg_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->prg_rom_size / MAPPER19_PRG_BANK_SIZE;
}

static size_t mapper19_chr_bank_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 0;
    }
    return mapper->chr_mem_size / MAPPER19_CHR_BANK_SIZE;
}

static void mapper19_increment_internal_ram_addr(NesMapper *mapper)
{
    if (mapper->mapper19_ram_auto_increment) {
        mapper->mapper19_ram_addr = (uint8_t)((mapper->mapper19_ram_addr + 1u) & 0x7Fu);
    }
}

static int mapper19_audio_channel_count(const NesMapper *mapper)
{
    if (mapper == NULL) {
        return 1;
    }
    return ((mapper->mapper19_internal_ram[0x7F] >> 4) & 7) + 1;
}

static uint8_t mapper19_audio_ram_sample(const NesMapper *mapper, uint8_t sample_index)
{
    uint8_t value = mapper->mapper19_internal_ram[sample_index >> 1];

    if ((sample_index & 1u) != 0) {
        return (uint8_t)((value >> 4) & 0x0Fu);
    }
    return (uint8_t)(value & 0x0Fu);
}

static void mapper19_write_phase(NesMapper *mapper, int base, uint32_t phase)
{
    mapper->mapper19_internal_ram[base + 1] = (uint8_t)(phase & 0xFFu);
    mapper->mapper19_internal_ram[base + 3] = (uint8_t)((phase >> 8) & 0xFFu);
    mapper->mapper19_internal_ram[base + 5] = (uint8_t)((phase >> 16) & 0xFFu);
}

static void mapper19_clock_audio_channel(NesMapper *mapper, int slot)
{
    int base = 0x78 - slot * 8;
    uint8_t control;
    uint8_t volume;
    uint32_t length;
    uint32_t phase;
    uint32_t frequency;
    uint32_t modulo;
    uint8_t sample_index;
    uint8_t sample;

    if (base < 0x40) {
        return;
    }
    control = mapper->mapper19_internal_ram[base + 4];
    volume = (uint8_t)(mapper->mapper19_internal_ram[base + 7] & 0x0Fu);
    length = 256u - (uint32_t)(control & 0xFCu);
    phase = ((uint32_t)mapper->mapper19_internal_ram[base + 5] << 16) |
            ((uint32_t)mapper->mapper19_internal_ram[base + 3] << 8) |
            mapper->mapper19_internal_ram[base + 1];
    frequency = ((uint32_t)(control & 0x03u) << 16) |
                ((uint32_t)mapper->mapper19_internal_ram[base + 2] << 8) |
                mapper->mapper19_internal_ram[base];

    modulo = (uint32_t)length << 16;
    if (modulo == 0) {
        mapper->mapper19_audio_output[slot] = 0;
        return;
    }
    phase = (phase + frequency) % modulo;
    mapper19_write_phase(mapper, base, phase);

    if (volume == 0) {
        mapper->mapper19_audio_output[slot] = 0;
        return;
    }
    sample_index = (uint8_t)(((phase >> 16) + mapper->mapper19_internal_ram[base + 6]) & 0xFFu);
    sample = mapper19_audio_ram_sample(mapper, sample_index);
    mapper->mapper19_audio_output[slot] = (int16_t)(((int)sample - 8) * (int)volume);
}

static uint8_t mapper19_read_prg_bank(const NesMapper *mapper, size_t bank, uint16_t address)
{
    size_t bank_count = mapper19_prg_bank_count(mapper);
    size_t offset;

    if (mapper == NULL || mapper->prg_rom == NULL || bank_count == 0) {
        return 0xFFu;
    }
    bank %= bank_count;
    offset = bank * MAPPER19_PRG_BANK_SIZE + (size_t)(address & 0x1FFFu);
    if (offset < mapper->prg_rom_size) {
        return mapper->prg_rom[offset];
    }
    return 0xFFu;
}

static int mapper19_uses_internal_nt_ram(const NesMapper *mapper, uint8_t bank, uint16_t address)
{
    if (bank < 0xE0u) {
        return 0;
    }
    if (address < 0x1000u) {
        return (mapper->mapper19_e800 & 0x40u) == 0;
    }
    if (address < 0x2000u) {
        return (mapper->mapper19_e800 & 0x80u) == 0;
    }
    return 1;
}

static uint16_t mapper19_internal_nt_index(uint8_t bank, uint16_t address)
{
    return (uint16_t)(((bank & 1u) ? 0x0400u : 0x0000u) + (address & 0x03FFu));
}

static uint8_t mapper19_selected_chr_bank(const NesMapper *mapper, uint16_t address)
{
    address &= 0x3FFFu;
    if (address >= 0x3000u && address < 0x3F00u) {
        address = (uint16_t)(address - 0x1000u);
    }
    if (address < 0x2000u) {
        return mapper->mapper19_chr_regs[address / MAPPER19_CHR_BANK_SIZE];
    }
    return mapper->mapper19_chr_regs[8u + ((address - 0x2000u) / MAPPER19_CHR_BANK_SIZE)];
}

void nes_mapper19_init(NesMapper *mapper, NesMirroring mirroring)
{
    int i;

    if (mapper == NULL) {
        return;
    }
    for (i = 0; i < 8; ++i) {
        mapper->mapper19_chr_regs[i] = (uint8_t)i;
    }
    if (mirroring == NES_MIRROR_HORIZONTAL) {
        mapper->mapper19_chr_regs[8] = 0xFEu;
        mapper->mapper19_chr_regs[9] = 0xFEu;
        mapper->mapper19_chr_regs[10] = 0xFFu;
        mapper->mapper19_chr_regs[11] = 0xFFu;
    } else {
        mapper->mapper19_chr_regs[8] = 0xFEu;
        mapper->mapper19_chr_regs[9] = 0xFFu;
        mapper->mapper19_chr_regs[10] = 0xFEu;
        mapper->mapper19_chr_regs[11] = 0xFFu;
    }
    mapper->mapper19_prg_regs[0] = 0;
    mapper->mapper19_prg_regs[1] = 1;
    mapper->mapper19_prg_regs[2] = 2;
    mapper->mapper19_e800 = 0;
    mapper->mapper19_f800 = 0;
    mapper->mapper19_irq_counter = 0;
    mapper->mapper19_irq_enabled = 0;
    mapper->mapper19_irq_pending = 0;
    mapper->mapper19_ram_addr = 0;
    mapper->mapper19_ram_auto_increment = 0;
    mapper->mapper19_sound_disabled = 0;
    mapper->mapper19_audio_cycle_accumulator = 0;
    mapper->mapper19_audio_channel = 0;
    memset(mapper->mapper19_audio_output, 0, sizeof(mapper->mapper19_audio_output));
    memset(mapper->mapper19_internal_ram, 0, sizeof(mapper->mapper19_internal_ram));
}

uint8_t nes_mapper19_cpu_read(NesEmu *nes, uint16_t address)
{
    NesMapper *mapper;

    if (nes == NULL) {
        return 0xFFu;
    }
    mapper = &nes->mapper;
    if (address >= 0x4800u && address <= 0x4FFFu) {
        uint8_t value = mapper->mapper19_internal_ram[mapper->mapper19_ram_addr & 0x7Fu];
        mapper19_increment_internal_ram_addr(mapper);
        return value;
    }
    if (address >= 0x5000u && address <= 0x57FFu) {
        return (uint8_t)(mapper->mapper19_irq_counter & 0xFFu);
    }
    if (address >= 0x5800u && address <= 0x5FFFu) {
        return (uint8_t)((mapper->mapper19_irq_enabled ? 0x80u : 0) |
                         ((mapper->mapper19_irq_counter >> 8) & 0x7Fu));
    }
    if (address >= 0x8000u) {
        size_t bank_count = mapper19_prg_bank_count(mapper);
        size_t last_bank = bank_count == 0 ? 0 : bank_count - 1u;
        size_t bank;

        switch (address & 0xE000u) {
        case 0x8000u:
            bank = mapper->mapper19_prg_regs[0] & 0x3Fu;
            break;
        case 0xA000u:
            bank = mapper->mapper19_prg_regs[1] & 0x3Fu;
            break;
        case 0xC000u:
            bank = mapper->mapper19_prg_regs[2] & 0x3Fu;
            break;
        case 0xE000u:
            bank = last_bank;
            break;
        default:
            return 0xFFu;
        }
        return mapper19_read_prg_bank(mapper, bank, address);
    }
    return 0xFFu;
}

void nes_mapper19_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    NesMapper *mapper;

    if (nes == NULL) {
        return;
    }
    mapper = &nes->mapper;
    if (address >= 0x4800u && address <= 0x4FFFu) {
        mapper->mapper19_internal_ram[mapper->mapper19_ram_addr & 0x7Fu] = value;
        mapper19_increment_internal_ram_addr(mapper);
        return;
    }
    if (address >= 0x5000u && address <= 0x57FFu) {
        mapper->mapper19_irq_counter =
            (uint16_t)((mapper->mapper19_irq_counter & 0x7F00u) | value);
        mapper->mapper19_irq_pending = 0;
        nes_update_irq(nes);
        return;
    }
    if (address >= 0x5800u && address <= 0x5FFFu) {
        mapper->mapper19_irq_counter =
            (uint16_t)((mapper->mapper19_irq_counter & 0x00FFu) |
                       (((uint16_t)value & 0x7Fu) << 8));
        mapper->mapper19_irq_enabled = (uint8_t)((value & 0x80u) != 0);
        mapper->mapper19_irq_pending = 0;
        nes_update_irq(nes);
        return;
    }
    if (address < 0x8000u) {
        return;
    }

    switch (address & 0xF800u) {
    case 0x8000u:
    case 0x8800u:
    case 0x9000u:
    case 0x9800u:
    case 0xA000u:
    case 0xA800u:
    case 0xB000u:
    case 0xB800u:
    case 0xC000u:
    case 0xC800u:
    case 0xD000u:
    case 0xD800u:
        mapper->mapper19_chr_regs[(address - 0x8000u) / 0x0800u] = value;
        break;
    case 0xE000u:
        mapper->mapper19_prg_regs[0] = (uint8_t)(value & 0x3Fu);
        mapper->mapper19_sound_disabled = (uint8_t)((value & 0x40u) != 0);
        break;
    case 0xE800u:
        mapper->mapper19_prg_regs[1] = (uint8_t)(value & 0x3Fu);
        mapper->mapper19_e800 = value;
        break;
    case 0xF000u:
        mapper->mapper19_prg_regs[2] = (uint8_t)(value & 0x3Fu);
        break;
    case 0xF800u:
        mapper->mapper19_f800 = value;
        mapper->mapper19_ram_addr = (uint8_t)(value & 0x7Fu);
        mapper->mapper19_ram_auto_increment = (uint8_t)((value & 0x80u) != 0);
        break;
    default:
        break;
    }
}

uint8_t nes_mapper19_ppu_read(const NesEmu *nes, uint16_t address)
{
    const NesMapper *mapper;
    uint8_t bank;
    size_t bank_count;
    size_t offset;

    if (nes == NULL) {
        return 0xFFu;
    }
    mapper = &nes->mapper;
    address &= 0x3FFFu;
    if (address >= 0x3000u && address < 0x3F00u) {
        address = (uint16_t)(address - 0x1000u);
    }
    bank = mapper19_selected_chr_bank(mapper, address);
    if (mapper19_uses_internal_nt_ram(mapper, bank, address)) {
        return nes->ppu.nametable[mapper19_internal_nt_index(bank, address)];
    }

    bank_count = mapper19_chr_bank_count(mapper);
    if (mapper->chr_mem == NULL || bank_count == 0) {
        return 0xFFu;
    }
    offset = ((size_t)bank % bank_count) * MAPPER19_CHR_BANK_SIZE + (size_t)(address & 0x03FFu);
    if (offset < mapper->chr_mem_size) {
        return mapper->chr_mem[offset];
    }
    return 0xFFu;
}

void nes_mapper19_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    NesMapper *mapper;
    uint8_t bank;
    size_t bank_count;
    size_t offset;

    if (nes == NULL) {
        return;
    }
    mapper = &nes->mapper;
    address &= 0x3FFFu;
    if (address >= 0x3000u && address < 0x3F00u) {
        address = (uint16_t)(address - 0x1000u);
    }
    bank = mapper19_selected_chr_bank(mapper, address);
    if (mapper19_uses_internal_nt_ram(mapper, bank, address)) {
        nes->ppu.nametable[mapper19_internal_nt_index(bank, address)] = value;
        return;
    }
    if (!mapper->chr_is_ram) {
        return;
    }
    bank_count = mapper19_chr_bank_count(mapper);
    if (mapper->chr_mem == NULL || bank_count == 0) {
        return;
    }
    offset = ((size_t)bank % bank_count) * MAPPER19_CHR_BANK_SIZE + (size_t)(address & 0x03FFu);
    if (offset < mapper->chr_mem_size) {
        mapper->chr_mem[offset] = value;
    }
}

int nes_mapper19_prg_ram_write_enabled(const NesMapper *mapper, uint16_t address)
{
    unsigned int window;

    if (mapper == NULL || address < 0x6000u || address > 0x7FFFu) {
        return 0;
    }
    if ((mapper->mapper19_f800 & 0xF0u) != 0x40u) {
        return 0;
    }
    window = (unsigned int)((address - 0x6000u) / 0x0800u);
    return (mapper->mapper19_f800 & (uint8_t)(1u << window)) == 0;
}

void nes_mapper19_clock_irq(NesEmu *nes, int cycles)
{
    NesMapper *mapper;
    int remaining;

    if (nes == NULL || cycles <= 0 || nes->rom.mapper_id != 19u) {
        return;
    }
    mapper = &nes->mapper;
    if (!mapper->mapper19_irq_enabled || mapper->mapper19_irq_pending ||
        mapper->mapper19_irq_counter >= MAPPER19_IRQ_LIMIT) {
        return;
    }
    remaining = MAPPER19_IRQ_LIMIT - (int)mapper->mapper19_irq_counter;
    if (cycles >= remaining) {
        mapper->mapper19_irq_counter = MAPPER19_IRQ_LIMIT;
        mapper->mapper19_irq_pending = 1;
        nes_update_irq(nes);
    } else {
        mapper->mapper19_irq_counter = (uint16_t)(mapper->mapper19_irq_counter + (uint16_t)cycles);
    }
}

void nes_mapper19_clock_audio(NesEmu *nes, int cycles)
{
    NesMapper *mapper;
    int channel_count;

    if (nes == NULL || cycles <= 0 || nes->rom.mapper_id != 19u) {
        return;
    }
    mapper = &nes->mapper;
    if (mapper->mapper19_sound_disabled) {
        mapper->mapper19_audio_cycle_accumulator = 0;
        memset(mapper->mapper19_audio_output, 0, sizeof(mapper->mapper19_audio_output));
        return;
    }
    channel_count = mapper19_audio_channel_count(mapper);
    if (mapper->mapper19_audio_channel >= (uint8_t)channel_count) {
        mapper->mapper19_audio_channel = 0;
    }
    mapper->mapper19_audio_cycle_accumulator += cycles;
    while (mapper->mapper19_audio_cycle_accumulator >= MAPPER19_AUDIO_TICK_CYCLES) {
        mapper19_clock_audio_channel(mapper, mapper->mapper19_audio_channel);
        mapper->mapper19_audio_channel++;
        if (mapper->mapper19_audio_channel >= (uint8_t)channel_count) {
            mapper->mapper19_audio_channel = 0;
        }
        mapper->mapper19_audio_cycle_accumulator -= MAPPER19_AUDIO_TICK_CYCLES;
    }
}

double nes_mapper19_audio_sample(const NesEmu *nes)
{
    const NesMapper *mapper;
    int channel_count;
    int i;
    int total = 0;

    if (nes == NULL || nes->rom.mapper_id != 19u || nes->mapper.mapper19_sound_disabled) {
        return 0.0;
    }
    mapper = &nes->mapper;
    channel_count = mapper19_audio_channel_count(mapper);
    for (i = 0; i < channel_count; ++i) {
        total += mapper->mapper19_audio_output[i];
    }
    return (double)total / (double)channel_count;
}
