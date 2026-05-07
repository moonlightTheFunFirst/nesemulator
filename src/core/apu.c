#include "apu.h"
#include "nes_internal.h"
#include "mapper/mapper19.h"

#include <string.h>

enum {
    CPU_CLOCK_NTSC = 1789773
};

static void apu_clock_length_counters(NesApu *apu);
static void apu_clock_quarter_frame(NesApu *apu);
static void apu_clock_sweeps(NesApu *apu);
static void apu_clock_dmc(NesEmu *nes, int cycles);
static int pulse_sweep_mutes(const NesApu *apu, int channel);

void nes_apu_init(NesApu *apu)
{
    if (apu == NULL) {
        return;
    }
    apu->noise_lfsr = 1;
}

void nes_apu_reset(NesApu *apu)
{
    if (apu == NULL) {
        return;
    }
    memset(apu, 0, sizeof(*apu));
    apu->noise_lfsr = 1;
    apu->dmc_sample_address = 0xC000u;
    apu->dmc_sample_length = 1;
    apu->dmc_silence = 1;
}

static const int noise_periods[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

static const int dmc_periods[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 85, 72, 54
};

static const uint8_t apu_length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

static const uint8_t apu_envelope_reg_index[3] = { 0, 4, 12 };

static void dmc_start_sample(NesApu *apu)
{
    apu->dmc_current_address = apu->dmc_sample_address;
    apu->dmc_bytes_remaining = apu->dmc_sample_length;
}

void nes_apu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (address >= 0x4000u && address <= 0x4017u) {
        nes->apu.regs[address - 0x4000u] = value;
    }
    switch (address) {
    case 0x4010u:
        if ((value & 0x80u) == 0) {
            nes->apu.dmc_irq = 0;
            nes_update_irq(nes);
        }
        break;
    case 0x4011u:
        nes->apu.dmc_output = (uint8_t)(value & 0x7Fu);
        break;
    case 0x4012u:
        nes->apu.dmc_sample_address = (uint16_t)(0xC000u | ((uint16_t)value << 6));
        break;
    case 0x4013u:
        nes->apu.dmc_sample_length = (uint16_t)(((uint16_t)value << 4) | 1u);
        break;
    case 0x4001u:
        nes->apu.pulse_sweep_reload[0] = 1;
        break;
    case 0x4005u:
        nes->apu.pulse_sweep_reload[1] = 1;
        break;
    case 0x4003u:
        if ((nes->apu.status & 0x01u) != 0) {
            nes->apu.length_counter[0] = apu_length_table[value >> 3];
        }
        nes->apu.envelope_start[0] = 1;
        nes->apu.pulse_phase[0] = 0.0;
        break;
    case 0x4007u:
        if ((nes->apu.status & 0x02u) != 0) {
            nes->apu.length_counter[1] = apu_length_table[value >> 3];
        }
        nes->apu.envelope_start[1] = 1;
        nes->apu.pulse_phase[1] = 0.0;
        break;
    case 0x400Bu:
        if ((nes->apu.status & 0x04u) != 0) {
            nes->apu.length_counter[2] = apu_length_table[value >> 3];
        }
        nes->apu.triangle_linear_reload = 1;
        break;
    case 0x400Fu:
        if ((nes->apu.status & 0x08u) != 0) {
            nes->apu.length_counter[3] = apu_length_table[value >> 3];
        }
        nes->apu.envelope_start[2] = 1;
        break;
    case 0x4015u:
        nes->apu.status = value;
        nes->apu.dmc_irq = 0;
        nes_update_irq(nes);
        if ((value & 0x01u) == 0) {
            nes->apu.length_counter[0] = 0;
        }
        if ((value & 0x02u) == 0) {
            nes->apu.length_counter[1] = 0;
        }
        if ((value & 0x04u) == 0) {
            nes->apu.length_counter[2] = 0;
        }
        if ((value & 0x08u) == 0) {
            nes->apu.length_counter[3] = 0;
        }
        if ((value & 0x10u) == 0) {
            nes->apu.dmc_bytes_remaining = 0;
            nes->apu.dmc_bits_remaining = 0;
            nes->apu.dmc_silence = 1;
        } else if (nes->apu.dmc_bytes_remaining == 0) {
            dmc_start_sample(&nes->apu);
        }
        break;
    case 0x4017u:
        nes->apu.frame_counter_accumulator = 0.0;
        nes->apu.frame_half_step = 0;
        nes->apu.frame_step = 0;
        nes->apu.frame_irq = 0;
        nes_update_irq(nes);
        if ((value & 0x80u) != 0) {
            apu_clock_quarter_frame(&nes->apu);
            apu_clock_length_counters(&nes->apu);
            apu_clock_sweeps(&nes->apu);
        }
        break;
    default:
        break;
    }
}

uint8_t nes_apu_read(NesEmu *nes, uint16_t address)
{
    uint8_t status = 0;

    if (address == 0x4015u) {
        if (nes->apu.length_counter[0] != 0) {
            status |= 0x01u;
        }
        if (nes->apu.length_counter[1] != 0) {
            status |= 0x02u;
        }
        if (nes->apu.length_counter[2] != 0) {
            status |= 0x04u;
        }
        if (nes->apu.length_counter[3] != 0) {
            status |= 0x08u;
        }
        if (nes->apu.dmc_bytes_remaining != 0) {
            status |= 0x10u;
        }
        if (nes->apu.frame_irq) {
            status |= 0x40u;
            nes->apu.frame_irq = 0;
            nes_update_irq(nes);
        }
        if (nes->apu.dmc_irq) {
            status |= 0x80u;
        }
        return status;
    }
    return 0;
}

static void apu_clock_length_counters(NesApu *apu)
{
    if (apu->length_counter[0] != 0 && (apu->regs[0] & 0x20u) == 0) {
        apu->length_counter[0]--;
    }
    if (apu->length_counter[1] != 0 && (apu->regs[4] & 0x20u) == 0) {
        apu->length_counter[1]--;
    }
    if (apu->length_counter[2] != 0 && (apu->regs[8] & 0x80u) == 0) {
        apu->length_counter[2]--;
    }
    if (apu->length_counter[3] != 0 && (apu->regs[12] & 0x20u) == 0) {
        apu->length_counter[3]--;
    }
}

static uint8_t apu_envelope_volume(const NesApu *apu, int channel)
{
    uint8_t reg = apu->regs[apu_envelope_reg_index[channel]];

    if ((reg & 0x10u) != 0) {
        return (uint8_t)(reg & 0x0Fu);
    }
    return apu->envelope_decay[channel];
}

static void apu_clock_envelope(NesApu *apu, int channel)
{
    uint8_t reg = apu->regs[apu_envelope_reg_index[channel]];
    uint8_t period = (uint8_t)(reg & 0x0Fu);

    if (apu->envelope_start[channel]) {
        apu->envelope_start[channel] = 0;
        apu->envelope_decay[channel] = 15;
        apu->envelope_divider[channel] = period;
    } else if (apu->envelope_divider[channel] == 0) {
        apu->envelope_divider[channel] = period;
        if (apu->envelope_decay[channel] != 0) {
            apu->envelope_decay[channel]--;
        } else if ((reg & 0x20u) != 0) {
            apu->envelope_decay[channel] = 15;
        }
    } else {
        apu->envelope_divider[channel]--;
    }
}

static void apu_clock_triangle_linear(NesApu *apu)
{
    if (apu->triangle_linear_reload) {
        apu->triangle_linear_counter = (uint8_t)(apu->regs[8] & 0x7Fu);
    } else if (apu->triangle_linear_counter != 0) {
        apu->triangle_linear_counter--;
    }
    if ((apu->regs[8] & 0x80u) == 0) {
        apu->triangle_linear_reload = 0;
    }
}

static void apu_clock_quarter_frame(NesApu *apu)
{
    apu_clock_envelope(apu, 0);
    apu_clock_envelope(apu, 1);
    apu_clock_envelope(apu, 2);
    apu_clock_triangle_linear(apu);
}

void nes_apu_clock_frame_counter(NesEmu *nes, int cycles)
{
    NesApu *apu = &nes->apu;
    const double quarter_frame_cycles = (double)CPU_CLOCK_NTSC / 240.0;

    if (cycles <= 0) {
        return;
    }
    apu->frame_counter_accumulator += (double)cycles;
    while (apu->frame_counter_accumulator >= quarter_frame_cycles) {
        apu_clock_quarter_frame(apu);
        if ((apu->regs[0x17] & 0x80u) != 0) {
            if (apu->frame_step == 1u || apu->frame_step == 4u) {
                apu_clock_length_counters(apu);
                apu_clock_sweeps(apu);
            }
            apu->frame_step = (uint8_t)((apu->frame_step + 1u) % 5u);
        } else {
            if (apu->frame_step == 1u || apu->frame_step == 3u) {
                apu_clock_length_counters(apu);
                apu_clock_sweeps(apu);
            }
            if (apu->frame_step == 3u && (apu->regs[0x17] & 0x40u) == 0) {
                apu->frame_irq = 1;
                nes_update_irq(nes);
            }
            apu->frame_step = (uint8_t)((apu->frame_step + 1u) & 3u);
        }
        apu->frame_half_step = (uint8_t)(apu->frame_step & 1u);
        apu->frame_counter_accumulator -= quarter_frame_cycles;
    }
}

static double pulse_sample(NesApu *apu, int channel, int sample_rate)
{
    const uint8_t *r = &apu->regs[channel ? 4 : 0];
    int enabled = (apu->status & (channel ? 0x02u : 0x01u)) != 0;
    int timer = r[2] | ((r[3] & 0x07) << 8);
    int volume = apu_envelope_volume(apu, channel);
    int duty = (r[0] >> 6) & 3;
    static const double duty_ratio[4] = { 0.125, 0.25, 0.5, 0.75 };
    double freq;

    if (!enabled || apu->length_counter[channel] == 0 || pulse_sweep_mutes(apu, channel) || volume == 0) {
        return 0.0;
    }
    freq = (double)CPU_CLOCK_NTSC / (16.0 * (double)(timer + 1));
    apu->pulse_phase[channel] += freq / (double)sample_rate;
    while (apu->pulse_phase[channel] >= 1.0) {
        apu->pulse_phase[channel] -= 1.0;
    }
    return apu->pulse_phase[channel] < duty_ratio[duty] ? (double)volume : 0.0;
}

static int pulse_timer(const NesApu *apu, int channel)
{
    int index = channel ? 4 : 0;

    return apu->regs[index + 2] | ((apu->regs[index + 3] & 0x07) << 8);
}

static void pulse_set_timer(NesApu *apu, int channel, int timer)
{
    int index = channel ? 4 : 0;

    timer &= 0x07FF;
    apu->regs[index + 2] = (uint8_t)(timer & 0xFF);
    apu->regs[index + 3] = (uint8_t)((apu->regs[index + 3] & 0xF8u) | ((uint8_t)(timer >> 8) & 0x07u));
}

static int pulse_sweep_target(const NesApu *apu, int channel)
{
    int index = channel ? 4 : 0;
    uint8_t reg = apu->regs[index + 1];
    int timer = pulse_timer(apu, channel);
    int change = timer >> (reg & 0x07u);

    if ((reg & 0x08u) != 0) {
        timer -= change;
        if (channel == 0) {
            timer--;
        }
    } else {
        timer += change;
    }
    return timer;
}

static int pulse_sweep_mutes(const NesApu *apu, int channel)
{
    int index = channel ? 4 : 0;
    uint8_t reg = apu->regs[index + 1];

    if (pulse_timer(apu, channel) < 8) {
        return 1;
    }
    if ((reg & 0x80u) != 0 && (reg & 0x07u) != 0 && pulse_sweep_target(apu, channel) > 0x07FF) {
        return 1;
    }
    return 0;
}

static void apu_clock_sweep(NesApu *apu, int channel)
{
    int index = channel ? 4 : 0;
    uint8_t reg = apu->regs[index + 1];
    uint8_t period = (uint8_t)((reg >> 4) & 0x07u);
    int divider_zero = apu->pulse_sweep_divider[channel] == 0;

    if (divider_zero) {
        if ((reg & 0x80u) != 0 && (reg & 0x07u) != 0 && !pulse_sweep_mutes(apu, channel)) {
            pulse_set_timer(apu, channel, pulse_sweep_target(apu, channel));
        }
        apu->pulse_sweep_divider[channel] = period;
    } else {
        apu->pulse_sweep_divider[channel]--;
    }
    if (apu->pulse_sweep_reload[channel]) {
        apu->pulse_sweep_divider[channel] = period;
        apu->pulse_sweep_reload[channel] = 0;
    }
}

static void apu_clock_sweeps(NesApu *apu)
{
    apu_clock_sweep(apu, 0);
    apu_clock_sweep(apu, 1);
}

static double triangle_sample(NesApu *apu, int sample_rate)
{
    const uint8_t *r = &apu->regs[8];
    int enabled = (apu->status & 0x04u) != 0;
    int timer = r[2] | ((r[3] & 0x07) << 8);
    double freq;
    double p;

    if (!enabled || apu->length_counter[2] == 0 || apu->triangle_linear_counter == 0 || timer < 2) {
        return 0.0;
    }
    freq = (double)CPU_CLOCK_NTSC / (32.0 * (double)(timer + 1));
    apu->triangle_phase += freq / (double)sample_rate;
    while (apu->triangle_phase >= 1.0) {
        apu->triangle_phase -= 1.0;
    }
    p = apu->triangle_phase;
    return p < 0.5 ? (15.0 - p * 30.0) : ((p - 0.5) * 30.0);
}

static double noise_sample(NesApu *apu, int sample_rate)
{
    const uint8_t *r = &apu->regs[12];
    int enabled = (apu->status & 0x08u) != 0;
    int volume = apu_envelope_volume(apu, 2);
    int period = noise_periods[r[2] & 0x0F];
    double freq;

    if (!enabled || apu->length_counter[3] == 0 || volume == 0) {
        return 0.0;
    }
    freq = (double)CPU_CLOCK_NTSC / (double)period;
    apu->noise_phase += freq / (double)sample_rate;
    while (apu->noise_phase >= 1.0) {
        uint16_t feedback = (uint16_t)((apu->noise_lfsr ^ (apu->noise_lfsr >> ((r[2] & 0x80u) ? 6 : 1))) & 1u);
        apu->noise_lfsr = (uint16_t)((apu->noise_lfsr >> 1) | (feedback << 14));
        apu->noise_phase -= 1.0;
    }
    return (apu->noise_lfsr & 1u) ? 0.0 : (double)volume;
}

static void dmc_fetch_byte(NesEmu *nes)
{
    NesApu *apu = &nes->apu;

    if (apu->dmc_bytes_remaining == 0) {
        if ((apu->regs[0x10] & 0x40u) != 0 && (apu->status & 0x10u) != 0) {
            dmc_start_sample(apu);
        } else {
            apu->dmc_silence = 1;
            return;
        }
    }

    apu->dmc_shift = nes_cpu_bus_read(nes, apu->dmc_current_address);
    nes->cpu.extra_cycles += 4;
    apu->dmc_current_address++;
    if (apu->dmc_current_address == 0) {
        apu->dmc_current_address = 0x8000u;
    }
    apu->dmc_bytes_remaining--;
    if (apu->dmc_bytes_remaining == 0 && (apu->regs[0x10] & 0xC0u) == 0x80u) {
        apu->dmc_irq = 1;
        nes_update_irq(nes);
    }
    apu->dmc_bits_remaining = 8;
    apu->dmc_silence = 0;
}

static void apu_clock_dmc(NesEmu *nes, int cycles)
{
    NesApu *apu = &nes->apu;
    int period = dmc_periods[apu->regs[0x10] & 0x0Fu];

    if (cycles <= 0 || (apu->status & 0x10u) == 0) {
        return;
    }
    apu->dmc_phase += (double)cycles;
    while (apu->dmc_phase >= (double)period) {
        if (apu->dmc_bits_remaining == 0) {
            dmc_fetch_byte(nes);
        }
        if (apu->dmc_bits_remaining != 0) {
            if (!apu->dmc_silence) {
                if ((apu->dmc_shift & 1u) != 0) {
                    if (apu->dmc_output <= 125u) {
                        apu->dmc_output = (uint8_t)(apu->dmc_output + 2u);
                    }
                } else if (apu->dmc_output >= 2u) {
                    apu->dmc_output = (uint8_t)(apu->dmc_output - 2u);
                }
            }
            apu->dmc_shift >>= 1;
            apu->dmc_bits_remaining--;
        }
        apu->dmc_phase -= (double)period;
    }
}

static int16_t apu_mix_sample(NesEmu *nes, int sample_rate)
{
    double pulse1 = pulse_sample(&nes->apu, 0, sample_rate);
    double pulse2 = pulse_sample(&nes->apu, 1, sample_rate);
    double triangle = triangle_sample(&nes->apu, sample_rate);
    double noise = noise_sample(&nes->apu, sample_rate);
    double dmc = (double)nes->apu.dmc_output;
    double namco163 = nes_mapper19_audio_sample(nes);
    double pulse_sum = pulse1 + pulse2;
    double tnd_sum = triangle / 8227.0 + noise / 12241.0 + dmc / 22638.0;
    double mix = 0.0;
    double filtered;
    int value;

    if (pulse_sum > 0.0) {
        mix += 95.88 / (8128.0 / pulse_sum + 100.0);
    }
    if (tnd_sum > 0.0) {
        mix += 159.79 / (1.0 / tnd_sum + 100.0);
    }
    mix += namco163 / 360.0;
    filtered = mix - nes->apu.highpass_prev_input + 0.995 * nes->apu.highpass_prev_output;
    nes->apu.highpass_prev_input = mix;
    nes->apu.highpass_prev_output = filtered;
    if (filtered > 1.0) {
        filtered = 1.0;
    } else if (filtered < -1.0) {
        filtered = -1.0;
    }
    value = (int)(filtered * 32000.0);
    return (int16_t)value;
}

static void apu_queue_sample(NesApu *apu, int16_t sample)
{
    if (apu->sample_count == NESEMU_AUDIO_BUFFER_SAMPLES) {
        apu->sample_read_pos = (apu->sample_read_pos + 1u) % NESEMU_AUDIO_BUFFER_SAMPLES;
        apu->sample_count--;
    }
    apu->sample_buffer[apu->sample_write_pos] = sample;
    apu->sample_write_pos = (apu->sample_write_pos + 1u) % NESEMU_AUDIO_BUFFER_SAMPLES;
    apu->sample_count++;
}

void nes_apu_clock_audio(NesEmu *nes, int cycles)
{
    NesApu *apu = &nes->apu;

    if (cycles <= 0) {
        return;
    }
    apu_clock_dmc(nes, cycles);
    nes_mapper19_clock_audio(nes, cycles);
    apu->sample_accumulator += (double)cycles * (double)NESEMU_AUDIO_RATE;
    while (apu->sample_accumulator >= (double)CPU_CLOCK_NTSC) {
        apu_queue_sample(apu, apu_mix_sample(nes, NESEMU_AUDIO_RATE));
        apu->sample_accumulator -= (double)CPU_CLOCK_NTSC;
    }
}

static int apu_pop_sample(NesApu *apu, int16_t *sample)
{
    if (apu->sample_count == 0) {
        return 0;
    }
    *sample = apu->sample_buffer[apu->sample_read_pos];
    apu->sample_read_pos = (apu->sample_read_pos + 1u) % NESEMU_AUDIO_BUFFER_SAMPLES;
    apu->sample_count--;
    return 1;
}

void nes_render_audio(NesEmu *nes, int16_t *samples, size_t sample_count, int sample_rate)
{
    size_t i;

    if (samples == NULL || sample_rate <= 0) {
        return;
    }
    if (nes == NULL || !nes->rom_loaded) {
        memset(samples, 0, sample_count * sizeof(samples[0]));
        return;
    }
    if (sample_rate != (int)NESEMU_AUDIO_RATE) {
        for (i = 0; i < sample_count; ++i) {
            samples[i] = apu_mix_sample(nes, sample_rate);
        }
        return;
    }
    for (i = 0; i < sample_count; ++i) {
        if (!apu_pop_sample(&nes->apu, &samples[i])) {
            samples[i] = 0;
        }
    }
}

