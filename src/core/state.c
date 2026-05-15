#include "state.h"
#include "nes_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    NESEMU_STATE_VERSION = 1
};

static const uint8_t nes_state_magic[8] = { 'N', 'E', 'S', 'E', 'M', 'U', 'S', 'T' };

typedef struct StateWriter {
    uint8_t *data;
    size_t size;
    size_t pos;
    int overflow;
} StateWriter;

typedef struct StateReader {
    const uint8_t *data;
    size_t size;
    size_t pos;
} StateReader;

typedef struct StateRuntime {
    NesCpu cpu;
    NesPpu ppu;
    NesApu apu;
    NesMapper mapper;
    NesJoypad joypad;
    NesJoypad joypad2;
    uint8_t ram[2048];
    uint16_t reset_vector;
    NesMirroring mirroring;
} StateRuntime;

static void state_write_bytes(StateWriter *writer, const void *data, size_t size)
{
    if (writer == NULL || writer->overflow) {
        return;
    }
    if (size > (size_t)-1 - writer->pos) {
        writer->overflow = 1;
        return;
    }
    if (writer->data != NULL) {
        if (size > writer->size - writer->pos) {
            writer->overflow = 1;
            return;
        }
        memcpy(writer->data + writer->pos, data, size);
    }
    writer->pos += size;
}

static void state_write_u8(StateWriter *writer, uint8_t value)
{
    state_write_bytes(writer, &value, 1);
}

static void state_write_u16(StateWriter *writer, uint16_t value)
{
    uint8_t data[2];

    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
    state_write_bytes(writer, data, sizeof(data));
}

static void state_write_u32(StateWriter *writer, uint32_t value)
{
    uint8_t data[4];

    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
    data[2] = (uint8_t)((value >> 16) & 0xFFu);
    data[3] = (uint8_t)((value >> 24) & 0xFFu);
    state_write_bytes(writer, data, sizeof(data));
}

static void state_write_u64(StateWriter *writer, uint64_t value)
{
    int i;

    for (i = 0; i < 8; ++i) {
        state_write_u8(writer, (uint8_t)((value >> (i * 8)) & 0xFFu));
    }
}

static void state_write_i32(StateWriter *writer, int value)
{
    state_write_u32(writer, (uint32_t)(int32_t)value);
}

static void state_write_i16(StateWriter *writer, int16_t value)
{
    state_write_u16(writer, (uint16_t)value);
}

static void state_write_double(StateWriter *writer, double value)
{
    uint64_t bits = 0;

    memcpy(&bits, &value, sizeof(bits));
    state_write_u64(writer, bits);
}

static int state_read_bytes(StateReader *reader, void *data, size_t size)
{
    if (reader == NULL || data == NULL || size > reader->size - reader->pos) {
        return 0;
    }
    memcpy(data, reader->data + reader->pos, size);
    reader->pos += size;
    return 1;
}

static int state_read_u8(StateReader *reader, uint8_t *value)
{
    return state_read_bytes(reader, value, 1);
}

static int state_read_u16(StateReader *reader, uint16_t *value)
{
    uint8_t data[2];

    if (!state_read_bytes(reader, data, sizeof(data))) {
        return 0;
    }
    *value = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    return 1;
}

static int state_read_u32(StateReader *reader, uint32_t *value)
{
    uint8_t data[4];

    if (!state_read_bytes(reader, data, sizeof(data))) {
        return 0;
    }
    *value = (uint32_t)data[0] |
             ((uint32_t)data[1] << 8) |
             ((uint32_t)data[2] << 16) |
             ((uint32_t)data[3] << 24);
    return 1;
}

static int state_read_u64(StateReader *reader, uint64_t *value)
{
    uint64_t result = 0;
    int i;

    for (i = 0; i < 8; ++i) {
        uint8_t byte;

        if (!state_read_u8(reader, &byte)) {
            return 0;
        }
        result |= (uint64_t)byte << (i * 8);
    }
    *value = result;
    return 1;
}

static int state_read_i32(StateReader *reader, int *value)
{
    uint32_t temp;

    if (!state_read_u32(reader, &temp)) {
        return 0;
    }
    *value = (int)(int32_t)temp;
    return 1;
}

static int state_read_i16(StateReader *reader, int16_t *value)
{
    uint16_t temp;

    if (!state_read_u16(reader, &temp)) {
        return 0;
    }
    *value = (int16_t)temp;
    return 1;
}

static int state_read_double(StateReader *reader, double *value)
{
    uint64_t bits;

    if (!state_read_u64(reader, &bits)) {
        return 0;
    }
    memcpy(value, &bits, sizeof(bits));
    return 1;
}

static void state_write_u8_array(StateWriter *writer, const uint8_t *values, size_t count)
{
    state_write_bytes(writer, values, count);
}

static void state_write_u32_array(StateWriter *writer, const uint32_t *values, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        state_write_u32(writer, values[i]);
    }
}

static void state_write_i16_array(StateWriter *writer, const int16_t *values, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        state_write_i16(writer, values[i]);
    }
}

static int state_read_u8_array(StateReader *reader, uint8_t *values, size_t count)
{
    return state_read_bytes(reader, values, count);
}

static int state_read_u32_array(StateReader *reader, uint32_t *values, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (!state_read_u32(reader, &values[i])) {
            return 0;
        }
    }
    return 1;
}

static int state_read_i16_array(StateReader *reader, int16_t *values, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (!state_read_i16(reader, &values[i])) {
            return 0;
        }
    }
    return 1;
}

static void state_write_cpu(StateWriter *writer, const NesCpu *cpu)
{
    state_write_u8(writer, cpu->a);
    state_write_u8(writer, cpu->x);
    state_write_u8(writer, cpu->y);
    state_write_u8(writer, cpu->p);
    state_write_u8(writer, cpu->sp);
    state_write_u16(writer, cpu->pc);
    state_write_u64(writer, cpu->cycles);
    state_write_i32(writer, cpu->nmi_pending);
    state_write_i32(writer, cpu->nmi_delay);
    state_write_i32(writer, cpu->irq_pending);
    state_write_i32(writer, cpu->extra_cycles);
    state_write_i32(writer, cpu->io_write_delay);
    state_write_i32(writer, cpu->stopped);
}

static int state_read_cpu(StateReader *reader, NesCpu *cpu)
{
    return state_read_u8(reader, &cpu->a) &&
           state_read_u8(reader, &cpu->x) &&
           state_read_u8(reader, &cpu->y) &&
           state_read_u8(reader, &cpu->p) &&
           state_read_u8(reader, &cpu->sp) &&
           state_read_u16(reader, &cpu->pc) &&
           state_read_u64(reader, &cpu->cycles) &&
           state_read_i32(reader, &cpu->nmi_pending) &&
           state_read_i32(reader, &cpu->nmi_delay) &&
           state_read_i32(reader, &cpu->irq_pending) &&
           state_read_i32(reader, &cpu->extra_cycles) &&
           state_read_i32(reader, &cpu->io_write_delay) &&
           state_read_i32(reader, &cpu->stopped);
}

static void state_write_ppu(StateWriter *writer, const NesPpu *ppu)
{
    state_write_u8(writer, ppu->ctrl);
    state_write_u8(writer, ppu->mask);
    state_write_u8(writer, ppu->status);
    state_write_u8(writer, ppu->oam_addr);
    state_write_u8(writer, ppu->write_latch);
    state_write_u8(writer, ppu->fine_x);
    state_write_u8(writer, ppu->data_buffer);
    state_write_u8_array(writer, ppu->oam, sizeof(ppu->oam));
    state_write_u8_array(writer, ppu->nametable, sizeof(ppu->nametable));
    state_write_u8_array(writer, ppu->palette, sizeof(ppu->palette));
    state_write_u16(writer, ppu->v);
    state_write_u16(writer, ppu->t);
    state_write_u8(writer, ppu->scroll_x);
    state_write_u8(writer, ppu->scroll_y);
    state_write_i32(writer, ppu->scanline);
    state_write_i32(writer, ppu->cycle);
    state_write_i32(writer, ppu->sprite0_hit_position);
    state_write_i32(writer, ppu->pending_write_position);
    state_write_u16(writer, ppu->pending_write_address);
    state_write_u8(writer, ppu->pending_write_value);
    state_write_u8(writer, ppu->pending_write);
    state_write_i32(writer, ppu->mapper4_irq_cache_scanline);
    state_write_i32(writer, ppu->mapper4_irq_cache_cycle);
    state_write_u8(writer, ppu->mapper4_irq_cache_valid);
    state_write_u64(writer, ppu->frame);
    state_write_u8(writer, ppu->frame_ready);
    state_write_u32_array(writer, ppu->framebuffer, NESEMU_SCREEN_WIDTH * NESEMU_SCREEN_HEIGHT);
}

static int state_read_ppu(StateReader *reader, NesPpu *ppu)
{
    return state_read_u8(reader, &ppu->ctrl) &&
           state_read_u8(reader, &ppu->mask) &&
           state_read_u8(reader, &ppu->status) &&
           state_read_u8(reader, &ppu->oam_addr) &&
           state_read_u8(reader, &ppu->write_latch) &&
           state_read_u8(reader, &ppu->fine_x) &&
           state_read_u8(reader, &ppu->data_buffer) &&
           state_read_u8_array(reader, ppu->oam, sizeof(ppu->oam)) &&
           state_read_u8_array(reader, ppu->nametable, sizeof(ppu->nametable)) &&
           state_read_u8_array(reader, ppu->palette, sizeof(ppu->palette)) &&
           state_read_u16(reader, &ppu->v) &&
           state_read_u16(reader, &ppu->t) &&
           state_read_u8(reader, &ppu->scroll_x) &&
           state_read_u8(reader, &ppu->scroll_y) &&
           state_read_i32(reader, &ppu->scanline) &&
           state_read_i32(reader, &ppu->cycle) &&
           state_read_i32(reader, &ppu->sprite0_hit_position) &&
           state_read_i32(reader, &ppu->pending_write_position) &&
           state_read_u16(reader, &ppu->pending_write_address) &&
           state_read_u8(reader, &ppu->pending_write_value) &&
           state_read_u8(reader, &ppu->pending_write) &&
           state_read_i32(reader, &ppu->mapper4_irq_cache_scanline) &&
           state_read_i32(reader, &ppu->mapper4_irq_cache_cycle) &&
           state_read_u8(reader, &ppu->mapper4_irq_cache_valid) &&
           state_read_u64(reader, &ppu->frame) &&
           state_read_u8(reader, &ppu->frame_ready) &&
           state_read_u32_array(reader, ppu->framebuffer, NESEMU_SCREEN_WIDTH * NESEMU_SCREEN_HEIGHT);
}

static void state_write_apu(StateWriter *writer, const NesApu *apu)
{
    int i;

    state_write_u8_array(writer, apu->regs, sizeof(apu->regs));
    state_write_u8(writer, apu->status);
    for (i = 0; i < 2; ++i) {
        state_write_i32(writer, apu->pulse_timer_counter[i]);
    }
    state_write_u8_array(writer, apu->pulse_sequence_step, sizeof(apu->pulse_sequence_step));
    state_write_u8_array(writer, apu->pulse_sweep_divider, sizeof(apu->pulse_sweep_divider));
    state_write_u8_array(writer, apu->pulse_sweep_reload, sizeof(apu->pulse_sweep_reload));
    state_write_i32(writer, apu->triangle_timer_counter);
    state_write_u8(writer, apu->triangle_sequence_step);
    state_write_i32(writer, apu->noise_timer_counter);
    state_write_u16(writer, apu->noise_lfsr);
    state_write_u8_array(writer, apu->length_counter, sizeof(apu->length_counter));
    state_write_i32(writer, apu->dmc_timer_counter);
    state_write_u16(writer, apu->dmc_sample_address);
    state_write_u16(writer, apu->dmc_sample_length);
    state_write_u16(writer, apu->dmc_current_address);
    state_write_u16(writer, apu->dmc_bytes_remaining);
    state_write_u8(writer, apu->dmc_sample_buffer);
    state_write_u8(writer, apu->dmc_sample_buffer_empty);
    state_write_u8(writer, apu->dmc_shift);
    state_write_u8(writer, apu->dmc_bits_remaining);
    state_write_u8(writer, apu->dmc_output);
    state_write_u8(writer, apu->dmc_silence);
    state_write_u8(writer, apu->dmc_irq);
    state_write_u8_array(writer, apu->envelope_start, sizeof(apu->envelope_start));
    state_write_u8_array(writer, apu->envelope_decay, sizeof(apu->envelope_decay));
    state_write_u8_array(writer, apu->envelope_divider, sizeof(apu->envelope_divider));
    state_write_u8(writer, apu->triangle_linear_counter);
    state_write_u8(writer, apu->triangle_linear_reload);
    state_write_u8(writer, apu->frame_step);
    state_write_i32(writer, apu->frame_counter_cycles);
    state_write_u8(writer, apu->frame_counter_mode);
    state_write_u8(writer, apu->frame_irq_inhibit);
    state_write_u8(writer, apu->frame_irq);
    state_write_u64(writer, apu->sample_accumulator);
    state_write_double(writer, apu->namco163_lowpass_output);
    state_write_double(writer, apu->namco163_lowpass_output2);
    state_write_double(writer, apu->highpass_prev_input);
    state_write_double(writer, apu->highpass_prev_output);
    state_write_u64(writer, (uint64_t)apu->sample_read_pos);
    state_write_u64(writer, (uint64_t)apu->sample_write_pos);
    state_write_u64(writer, (uint64_t)apu->sample_count);
    state_write_i16(writer, apu->last_render_sample);
    state_write_i16_array(writer, apu->sample_buffer, NESEMU_AUDIO_BUFFER_SAMPLES);
}

static int state_read_apu(StateReader *reader, NesApu *apu)
{
    uint64_t sample_read_pos;
    uint64_t sample_write_pos;
    uint64_t sample_count;
    int i;

    if (!state_read_u8_array(reader, apu->regs, sizeof(apu->regs)) ||
        !state_read_u8(reader, &apu->status)) {
        return 0;
    }
    for (i = 0; i < 2; ++i) {
        if (!state_read_i32(reader, &apu->pulse_timer_counter[i])) {
            return 0;
        }
    }
    if (!state_read_u8_array(reader, apu->pulse_sequence_step, sizeof(apu->pulse_sequence_step)) ||
        !state_read_u8_array(reader, apu->pulse_sweep_divider, sizeof(apu->pulse_sweep_divider)) ||
        !state_read_u8_array(reader, apu->pulse_sweep_reload, sizeof(apu->pulse_sweep_reload)) ||
        !state_read_i32(reader, &apu->triangle_timer_counter) ||
        !state_read_u8(reader, &apu->triangle_sequence_step) ||
        !state_read_i32(reader, &apu->noise_timer_counter) ||
        !state_read_u16(reader, &apu->noise_lfsr) ||
        !state_read_u8_array(reader, apu->length_counter, sizeof(apu->length_counter)) ||
        !state_read_i32(reader, &apu->dmc_timer_counter) ||
        !state_read_u16(reader, &apu->dmc_sample_address) ||
        !state_read_u16(reader, &apu->dmc_sample_length) ||
        !state_read_u16(reader, &apu->dmc_current_address) ||
        !state_read_u16(reader, &apu->dmc_bytes_remaining) ||
        !state_read_u8(reader, &apu->dmc_sample_buffer) ||
        !state_read_u8(reader, &apu->dmc_sample_buffer_empty) ||
        !state_read_u8(reader, &apu->dmc_shift) ||
        !state_read_u8(reader, &apu->dmc_bits_remaining) ||
        !state_read_u8(reader, &apu->dmc_output) ||
        !state_read_u8(reader, &apu->dmc_silence) ||
        !state_read_u8(reader, &apu->dmc_irq) ||
        !state_read_u8_array(reader, apu->envelope_start, sizeof(apu->envelope_start)) ||
        !state_read_u8_array(reader, apu->envelope_decay, sizeof(apu->envelope_decay)) ||
        !state_read_u8_array(reader, apu->envelope_divider, sizeof(apu->envelope_divider)) ||
        !state_read_u8(reader, &apu->triangle_linear_counter) ||
        !state_read_u8(reader, &apu->triangle_linear_reload) ||
        !state_read_u8(reader, &apu->frame_step) ||
        !state_read_i32(reader, &apu->frame_counter_cycles) ||
        !state_read_u8(reader, &apu->frame_counter_mode) ||
        !state_read_u8(reader, &apu->frame_irq_inhibit) ||
        !state_read_u8(reader, &apu->frame_irq) ||
        !state_read_u64(reader, &apu->sample_accumulator) ||
        !state_read_double(reader, &apu->namco163_lowpass_output) ||
        !state_read_double(reader, &apu->namco163_lowpass_output2) ||
        !state_read_double(reader, &apu->highpass_prev_input) ||
        !state_read_double(reader, &apu->highpass_prev_output) ||
        !state_read_u64(reader, &sample_read_pos) ||
        !state_read_u64(reader, &sample_write_pos) ||
        !state_read_u64(reader, &sample_count) ||
        !state_read_i16(reader, &apu->last_render_sample) ||
        !state_read_i16_array(reader, apu->sample_buffer, NESEMU_AUDIO_BUFFER_SAMPLES)) {
        return 0;
    }
    if (sample_read_pos >= NESEMU_AUDIO_BUFFER_SAMPLES ||
        sample_write_pos >= NESEMU_AUDIO_BUFFER_SAMPLES ||
        sample_count > NESEMU_AUDIO_BUFFER_SAMPLES) {
        return 0;
    }
    apu->sample_read_pos = (size_t)sample_read_pos;
    apu->sample_write_pos = (size_t)sample_write_pos;
    apu->sample_count = (size_t)sample_count;
    return 1;
}

static void state_write_mapper(StateWriter *writer, const NesMapper *mapper)
{
    state_write_u8(writer, mapper->chr_bank);
    state_write_u8(writer, mapper->mapper4_bank_select);
    state_write_u8_array(writer, mapper->mapper4_regs, sizeof(mapper->mapper4_regs));
    state_write_u8(writer, mapper->mapper4_prg_mode);
    state_write_u8(writer, mapper->mapper4_chr_mode);
    state_write_u8(writer, mapper->mapper4_irq_latch);
    state_write_u8(writer, mapper->mapper4_irq_counter);
    state_write_u8(writer, mapper->mapper4_irq_reload);
    state_write_u8(writer, mapper->mapper4_irq_enabled);
    state_write_u8(writer, mapper->mapper4_irq_pending);
    state_write_u8(writer, mapper->mapper76_bank_select);
    state_write_u8_array(writer, mapper->mapper76_regs, sizeof(mapper->mapper76_regs));
    state_write_u8(writer, mapper->mapper10_prg_bank);
    state_write_u8_array(writer, mapper->mapper10_chr_banks, sizeof(mapper->mapper10_chr_banks));
    state_write_u8(writer, mapper->mapper10_latch0);
    state_write_u8(writer, mapper->mapper10_latch1);
    state_write_u8(writer, mapper->mapper95_bank_select);
    state_write_u8_array(writer, mapper->mapper95_regs, sizeof(mapper->mapper95_regs));
    state_write_u8(writer, mapper->mapper154_bank_select);
    state_write_u8_array(writer, mapper->mapper154_regs, sizeof(mapper->mapper154_regs));
    state_write_u8(writer, mapper->mapper154_mirroring);
    state_write_u8(writer, mapper->mapper206_bank_select);
    state_write_u8_array(writer, mapper->mapper206_regs, sizeof(mapper->mapper206_regs));
    state_write_u8(writer, mapper->mapper88_bank_select);
    state_write_u8_array(writer, mapper->mapper88_regs, sizeof(mapper->mapper88_regs));
    state_write_u8_array(writer, mapper->mapper19_chr_regs, sizeof(mapper->mapper19_chr_regs));
    state_write_u8_array(writer, mapper->mapper19_prg_regs, sizeof(mapper->mapper19_prg_regs));
    state_write_u8(writer, mapper->mapper19_e800);
    state_write_u8(writer, mapper->mapper19_f800);
    state_write_u16(writer, mapper->mapper19_irq_counter);
    state_write_u8(writer, mapper->mapper19_irq_enabled);
    state_write_u8(writer, mapper->mapper19_irq_pending);
    state_write_u8(writer, mapper->mapper19_ram_addr);
    state_write_u8(writer, mapper->mapper19_ram_auto_increment);
    state_write_u8(writer, mapper->mapper19_sound_disabled);
    state_write_i32(writer, mapper->mapper19_audio_cycle_accumulator);
    state_write_u8(writer, mapper->mapper19_audio_channel);
    state_write_i16_array(writer, mapper->mapper19_audio_output, sizeof(mapper->mapper19_audio_output) / sizeof(mapper->mapper19_audio_output[0]));
    state_write_u8_array(writer, mapper->mapper19_internal_ram, sizeof(mapper->mapper19_internal_ram));
    state_write_u8_array(writer, mapper->prg_ram, sizeof(mapper->prg_ram));
}

static int state_read_mapper(StateReader *reader, NesMapper *mapper)
{
    return state_read_u8(reader, &mapper->chr_bank) &&
           state_read_u8(reader, &mapper->mapper4_bank_select) &&
           state_read_u8_array(reader, mapper->mapper4_regs, sizeof(mapper->mapper4_regs)) &&
           state_read_u8(reader, &mapper->mapper4_prg_mode) &&
           state_read_u8(reader, &mapper->mapper4_chr_mode) &&
           state_read_u8(reader, &mapper->mapper4_irq_latch) &&
           state_read_u8(reader, &mapper->mapper4_irq_counter) &&
           state_read_u8(reader, &mapper->mapper4_irq_reload) &&
           state_read_u8(reader, &mapper->mapper4_irq_enabled) &&
           state_read_u8(reader, &mapper->mapper4_irq_pending) &&
           state_read_u8(reader, &mapper->mapper76_bank_select) &&
           state_read_u8_array(reader, mapper->mapper76_regs, sizeof(mapper->mapper76_regs)) &&
           state_read_u8(reader, &mapper->mapper10_prg_bank) &&
           state_read_u8_array(reader, mapper->mapper10_chr_banks, sizeof(mapper->mapper10_chr_banks)) &&
           state_read_u8(reader, &mapper->mapper10_latch0) &&
           state_read_u8(reader, &mapper->mapper10_latch1) &&
           state_read_u8(reader, &mapper->mapper95_bank_select) &&
           state_read_u8_array(reader, mapper->mapper95_regs, sizeof(mapper->mapper95_regs)) &&
           state_read_u8(reader, &mapper->mapper154_bank_select) &&
           state_read_u8_array(reader, mapper->mapper154_regs, sizeof(mapper->mapper154_regs)) &&
           state_read_u8(reader, &mapper->mapper154_mirroring) &&
           state_read_u8(reader, &mapper->mapper206_bank_select) &&
           state_read_u8_array(reader, mapper->mapper206_regs, sizeof(mapper->mapper206_regs)) &&
           state_read_u8(reader, &mapper->mapper88_bank_select) &&
           state_read_u8_array(reader, mapper->mapper88_regs, sizeof(mapper->mapper88_regs)) &&
           state_read_u8_array(reader, mapper->mapper19_chr_regs, sizeof(mapper->mapper19_chr_regs)) &&
           state_read_u8_array(reader, mapper->mapper19_prg_regs, sizeof(mapper->mapper19_prg_regs)) &&
           state_read_u8(reader, &mapper->mapper19_e800) &&
           state_read_u8(reader, &mapper->mapper19_f800) &&
           state_read_u16(reader, &mapper->mapper19_irq_counter) &&
           state_read_u8(reader, &mapper->mapper19_irq_enabled) &&
           state_read_u8(reader, &mapper->mapper19_irq_pending) &&
           state_read_u8(reader, &mapper->mapper19_ram_addr) &&
           state_read_u8(reader, &mapper->mapper19_ram_auto_increment) &&
           state_read_u8(reader, &mapper->mapper19_sound_disabled) &&
           state_read_i32(reader, &mapper->mapper19_audio_cycle_accumulator) &&
           state_read_u8(reader, &mapper->mapper19_audio_channel) &&
           state_read_i16_array(reader, mapper->mapper19_audio_output, sizeof(mapper->mapper19_audio_output) / sizeof(mapper->mapper19_audio_output[0])) &&
           state_read_u8_array(reader, mapper->mapper19_internal_ram, sizeof(mapper->mapper19_internal_ram)) &&
           state_read_u8_array(reader, mapper->prg_ram, sizeof(mapper->prg_ram));
}

static void state_restore_mapper(NesMapper *destination, const NesMapper *source)
{
    destination->chr_bank = source->chr_bank;
    destination->mapper4_bank_select = source->mapper4_bank_select;
    memcpy(destination->mapper4_regs, source->mapper4_regs, sizeof(destination->mapper4_regs));
    destination->mapper4_prg_mode = source->mapper4_prg_mode;
    destination->mapper4_chr_mode = source->mapper4_chr_mode;
    destination->mapper4_irq_latch = source->mapper4_irq_latch;
    destination->mapper4_irq_counter = source->mapper4_irq_counter;
    destination->mapper4_irq_reload = source->mapper4_irq_reload;
    destination->mapper4_irq_enabled = source->mapper4_irq_enabled;
    destination->mapper4_irq_pending = source->mapper4_irq_pending;
    destination->mapper76_bank_select = source->mapper76_bank_select;
    memcpy(destination->mapper76_regs, source->mapper76_regs, sizeof(destination->mapper76_regs));
    destination->mapper10_prg_bank = source->mapper10_prg_bank;
    memcpy(destination->mapper10_chr_banks, source->mapper10_chr_banks, sizeof(destination->mapper10_chr_banks));
    destination->mapper10_latch0 = source->mapper10_latch0;
    destination->mapper10_latch1 = source->mapper10_latch1;
    destination->mapper95_bank_select = source->mapper95_bank_select;
    memcpy(destination->mapper95_regs, source->mapper95_regs, sizeof(destination->mapper95_regs));
    destination->mapper154_bank_select = source->mapper154_bank_select;
    memcpy(destination->mapper154_regs, source->mapper154_regs, sizeof(destination->mapper154_regs));
    destination->mapper154_mirroring = source->mapper154_mirroring;
    destination->mapper206_bank_select = source->mapper206_bank_select;
    memcpy(destination->mapper206_regs, source->mapper206_regs, sizeof(destination->mapper206_regs));
    destination->mapper88_bank_select = source->mapper88_bank_select;
    memcpy(destination->mapper88_regs, source->mapper88_regs, sizeof(destination->mapper88_regs));
    memcpy(destination->mapper19_chr_regs, source->mapper19_chr_regs, sizeof(destination->mapper19_chr_regs));
    memcpy(destination->mapper19_prg_regs, source->mapper19_prg_regs, sizeof(destination->mapper19_prg_regs));
    destination->mapper19_e800 = source->mapper19_e800;
    destination->mapper19_f800 = source->mapper19_f800;
    destination->mapper19_irq_counter = source->mapper19_irq_counter;
    destination->mapper19_irq_enabled = source->mapper19_irq_enabled;
    destination->mapper19_irq_pending = source->mapper19_irq_pending;
    destination->mapper19_ram_addr = source->mapper19_ram_addr;
    destination->mapper19_ram_auto_increment = source->mapper19_ram_auto_increment;
    destination->mapper19_sound_disabled = source->mapper19_sound_disabled;
    destination->mapper19_audio_cycle_accumulator = source->mapper19_audio_cycle_accumulator;
    destination->mapper19_audio_channel = source->mapper19_audio_channel;
    memcpy(destination->mapper19_audio_output, source->mapper19_audio_output, sizeof(destination->mapper19_audio_output));
    memcpy(destination->mapper19_internal_ram, source->mapper19_internal_ram, sizeof(destination->mapper19_internal_ram));
    memcpy(destination->prg_ram, source->prg_ram, sizeof(destination->prg_ram));
}

static void state_write_joypad(StateWriter *writer, const NesJoypad *joypad)
{
    state_write_u8(writer, joypad->state);
    state_write_u8(writer, joypad->shift);
    state_write_u8(writer, joypad->strobe);
}

static int state_read_joypad(StateReader *reader, NesJoypad *joypad)
{
    return state_read_u8(reader, &joypad->state) &&
           state_read_u8(reader, &joypad->shift) &&
           state_read_u8(reader, &joypad->strobe);
}

static void state_write_image(StateWriter *writer, const NesEmu *nes)
{
    size_t chr_ram_size = nes->mapper.chr_is_ram ? nes->mapper.chr_mem_size : 0;

    state_write_bytes(writer, nes_state_magic, sizeof(nes_state_magic));
    state_write_u32(writer, NESEMU_STATE_VERSION);
    state_write_u8_array(writer, nes->rom.ines_header, sizeof(nes->rom.ines_header));
    state_write_u32(writer, nes->rom.rom_crc32);
    state_write_u32(writer, nes->rom.mapper_id);
    state_write_u32(writer, (uint32_t)nes->mapper.prg_rom_size);
    state_write_u32(writer, (uint32_t)nes->mapper.chr_mem_size);
    state_write_u32(writer, (uint32_t)nes->mapper.chr_is_ram);
    state_write_u32(writer, (uint32_t)chr_ram_size);
    state_write_u32(writer, (uint32_t)nes->rom.mirroring);
    state_write_u16(writer, nes->reset_vector);
    state_write_cpu(writer, &nes->cpu);
    state_write_ppu(writer, &nes->ppu);
    state_write_apu(writer, &nes->apu);
    state_write_mapper(writer, &nes->mapper);
    state_write_joypad(writer, &nes->joypad);
    state_write_joypad(writer, &nes->joypad2);
    state_write_u8_array(writer, nes->ram, sizeof(nes->ram));
    if (chr_ram_size != 0) {
        state_write_u8_array(writer, nes->mapper.chr_mem, chr_ram_size);
    }
}

NesStateResult nes_state_save_size(const NesEmu *nes, size_t *out_size)
{
    StateWriter writer;

    if (nes == NULL || out_size == NULL) {
        return NES_STATE_INVALID_ARGUMENT;
    }
    if (!nes->rom_loaded) {
        return NES_STATE_NO_ROM;
    }
    memset(&writer, 0, sizeof(writer));
    state_write_image(&writer, nes);
    if (writer.overflow) {
        return NES_STATE_INVALID_DATA;
    }
    *out_size = writer.pos;
    return NES_STATE_OK;
}

NesStateResult nes_state_save(const NesEmu *nes, uint8_t *data, size_t size, size_t *out_written)
{
    StateWriter writer;

    if (nes == NULL || data == NULL) {
        return NES_STATE_INVALID_ARGUMENT;
    }
    if (!nes->rom_loaded) {
        return NES_STATE_NO_ROM;
    }
    memset(&writer, 0, sizeof(writer));
    writer.data = data;
    writer.size = size;
    state_write_image(&writer, nes);
    if (writer.overflow) {
        return NES_STATE_BUFFER_TOO_SMALL;
    }
    if (out_written != NULL) {
        *out_written = writer.pos;
    }
    return NES_STATE_OK;
}

NesStateResult nes_state_load(NesEmu *nes, const uint8_t *data, size_t size)
{
    StateReader reader;
    StateRuntime *runtime;
    uint8_t magic[sizeof(nes_state_magic)];
    uint8_t ines_header[16];
    uint32_t version;
    uint32_t rom_crc32;
    uint32_t mapper_id;
    uint32_t prg_rom_size;
    uint32_t chr_mem_size;
    uint32_t chr_is_ram;
    uint32_t chr_ram_size;
    uint32_t mirroring;
    uint8_t *chr_ram = NULL;
    NesStateResult result = NES_STATE_INVALID_DATA;

    if (nes == NULL || data == NULL) {
        return NES_STATE_INVALID_ARGUMENT;
    }
    memset(&reader, 0, sizeof(reader));
    reader.data = data;
    reader.size = size;
    if (!state_read_bytes(&reader, magic, sizeof(magic)) ||
        memcmp(magic, nes_state_magic, sizeof(nes_state_magic)) != 0 ||
        !state_read_u32(&reader, &version)) {
        return NES_STATE_INVALID_DATA;
    }
    if (version != NESEMU_STATE_VERSION) {
        return NES_STATE_UNSUPPORTED_VERSION;
    }
    if (!state_read_u8_array(&reader, ines_header, sizeof(ines_header)) ||
        !state_read_u32(&reader, &rom_crc32) ||
        !state_read_u32(&reader, &mapper_id) ||
        !state_read_u32(&reader, &prg_rom_size) ||
        !state_read_u32(&reader, &chr_mem_size) ||
        !state_read_u32(&reader, &chr_is_ram) ||
        !state_read_u32(&reader, &chr_ram_size) ||
        !state_read_u32(&reader, &mirroring)) {
        return NES_STATE_INVALID_DATA;
    }
    if (!nes->rom_loaded) {
        return NES_STATE_NO_ROM;
    }
    if (memcmp(ines_header, nes->rom.ines_header, sizeof(ines_header)) != 0 ||
        rom_crc32 != nes->rom.rom_crc32 ||
        mapper_id != nes->rom.mapper_id ||
        prg_rom_size != nes->mapper.prg_rom_size ||
        chr_mem_size != nes->mapper.chr_mem_size ||
        chr_is_ram != nes->mapper.chr_is_ram) {
        return NES_STATE_ROM_MISMATCH;
    }
    if (mirroring > NES_MIRROR_FOUR_SCREEN ||
        chr_ram_size != (nes->mapper.chr_is_ram ? nes->mapper.chr_mem_size : 0)) {
        return NES_STATE_INVALID_DATA;
    }

    runtime = (StateRuntime *)calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return NES_STATE_INVALID_DATA;
    }
    if (chr_ram_size != 0) {
        chr_ram = (uint8_t *)malloc(chr_ram_size);
        if (chr_ram == NULL) {
            free(runtime);
            return NES_STATE_INVALID_DATA;
        }
    }
    runtime->mirroring = (NesMirroring)mirroring;
    if (!state_read_u16(&reader, &runtime->reset_vector) ||
        !state_read_cpu(&reader, &runtime->cpu) ||
        !state_read_ppu(&reader, &runtime->ppu) ||
        !state_read_apu(&reader, &runtime->apu) ||
        !state_read_mapper(&reader, &runtime->mapper) ||
        !state_read_joypad(&reader, &runtime->joypad) ||
        !state_read_joypad(&reader, &runtime->joypad2) ||
        !state_read_u8_array(&reader, runtime->ram, sizeof(runtime->ram)) ||
        reader.size - reader.pos != chr_ram_size ||
        (chr_ram_size != 0 && !state_read_u8_array(&reader, chr_ram, chr_ram_size))) {
        result = NES_STATE_INVALID_DATA;
    } else {
        nes->rom.mirroring = runtime->mirroring;
        nes->reset_vector = runtime->reset_vector;
        nes->cpu = runtime->cpu;
        nes->ppu = runtime->ppu;
        nes->apu = runtime->apu;
        state_restore_mapper(&nes->mapper, &runtime->mapper);
        nes->joypad = runtime->joypad;
        nes->joypad2 = runtime->joypad2;
        memcpy(nes->ram, runtime->ram, sizeof(nes->ram));
        if (chr_ram_size != 0) {
            memcpy(nes->mapper.chr_mem, chr_ram, chr_ram_size);
        }
        nes_update_irq(nes);
        result = NES_STATE_OK;
    }
    free(chr_ram);
    free(runtime);
    return result;
}

const char *nes_state_result_string(NesStateResult result)
{
    switch (result) {
    case NES_STATE_OK:
        return "ok";
    case NES_STATE_INVALID_ARGUMENT:
        return "invalid argument";
    case NES_STATE_NO_ROM:
        return "no ROM loaded";
    case NES_STATE_INVALID_DATA:
        return "invalid state data";
    case NES_STATE_UNSUPPORTED_VERSION:
        return "unsupported state version";
    case NES_STATE_ROM_MISMATCH:
        return "state does not match the loaded ROM";
    case NES_STATE_BUFFER_TOO_SMALL:
        return "buffer too small";
    default:
        return "unknown state error";
    }
}
