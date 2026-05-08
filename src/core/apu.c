#include "apu.h"
#include "nes_internal.h"
#include "mapper/mapper19.h"

#include <string.h>

enum {
    CPU_CLOCK_NTSC = 1789773,
    APU_MIX_SCALE = 1 << 24
};

/* The multiplexed N163 output relies on downstream analog rolloff, especially in 8ch games. */
#define NAMCO163_LOWPASS_ALPHA 0.68
#define NAMCO163_MIX_SCALE 200.0

static void apu_clock_length_counters(NesApu *apu);
static void apu_clock_half_frame(NesApu *apu);
static void apu_clock_quarter_frame(NesApu *apu);
static void apu_clock_sweeps(NesApu *apu);
static void apu_clock_dmc(NesEmu *nes, int cycles);
static void apu_clock_channel_timers(NesEmu *nes, int cycles);
static int pulse_sweep_mutes(const NesApu *apu, int channel);
static double namco163_filtered_sample(NesApu *apu, double sample);

void nes_apu_init(NesApu *apu)
{
    if (apu == NULL) {
        return;
    }
    apu->noise_lfsr = 1;
    apu->dmc_sample_buffer_empty = 1;
    apu->dmc_silence = 1;
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
    apu->dmc_sample_buffer_empty = 1;
    apu->dmc_silence = 1;
}

static const int noise_periods[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

static const int dmc_periods[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 85, 72, 54
};

static const uint8_t pulse_duty_sequences[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 1, 1, 1, 1 }
};

static const uint8_t triangle_sequence[32] = {
    15, 14, 13, 12, 11, 10, 9, 8,
    7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15
};

static const int frame_counter_4_step_events[4] = { 7457, 14913, 22371, 29829 };
static const int frame_counter_5_step_events[4] = { 7457, 14913, 22371, 37281 };

static const uint8_t apu_length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

static const int32_t apu_pulse_table[31] = {
    0, 195503, 386311, 572591, 754503, 932197, 1105820, 1275509,
    1441397, 1603610, 1762269, 1917490, 2069382, 2218052, 2363601, 2506127,
    2645723, 2782478, 2916478, 3047805, 3176539, 3302756, 3426529, 3547927,
    3667020, 3783872, 3898545, 4011100, 4121595, 4230086, 4336627
};

static const int32_t apu_tnd_table[203] = {
    0, 109740, 218585, 326546, 433634, 539859, 645232, 749763,
    853462, 956339, 1058404, 1159666, 1260135, 1359819, 1458729, 1556873,
    1654260, 1750898, 1846797, 1941965, 2036410, 2130140, 2223163, 2315488,
    2407122, 2498072, 2588348, 2677955, 2766902, 2855195, 2942842, 3029851,
    3116227, 3201977, 3287110, 3371630, 3455545, 3538862, 3621586, 3703724,
    3785282, 3866266, 3946683, 4026538, 4105837, 4184585, 4262790, 4340455,
    4417587, 4494192, 4570274, 4645839, 4720892, 4795439, 4869484, 4943033,
    5016090, 5088661, 5160749, 5232361, 5303501, 5374173, 5444382, 5514133,
    5583430, 5652277, 5720679, 5788640, 5856164, 5923256, 5989920, 6056159,
    6121978, 6187381, 6252372, 6316955, 6381133, 6444910, 6508290, 6571277,
    6633874, 6696085, 6757914, 6819363, 6880437, 6941139, 7001473, 7061441,
    7121047, 7180294, 7239186, 7297725, 7355915, 7413759, 7471261, 7528422,
    7585246, 7641736, 7697895, 7753726, 7809232, 7864416, 7919279, 7973826,
    8028059, 8081980, 8135593, 8188899, 8241902, 8294604, 8347008, 8399116,
    8450931, 8502454, 8553690, 8604639, 8655305, 8705689, 8755794, 8805623,
    8855178, 8904461, 8953473, 9002218, 9050698, 9098914, 9146870, 9194566,
    9242005, 9289190, 9336121, 9382802, 9429234, 9475420, 9521360, 9567058,
    9612515, 9657732, 9702713, 9747458, 9791969, 9836249, 9880299, 9924121,
    9967717, 10011088, 10054237, 10097164, 10139872, 10182362, 10224636, 10266695,
    10308542, 10350178, 10391604, 10432822, 10473833, 10514640, 10555243, 10595645,
    10635846, 10675848, 10715652, 10755261, 10794676, 10833897, 10872926, 10911766,
    10950416, 10988879, 11027156, 11065248, 11103157, 11140883, 11178429, 11215795,
    11252983, 11289994, 11326829, 11363490, 11399977, 11436293, 11472438, 11508413,
    11544220, 11579859, 11615333, 11650642, 11685787, 11720769, 11755590, 11790250,
    11824752, 11859095, 11893281, 11927311, 11961186, 11994907, 12028476, 12061892,
    12095158, 12128274, 12161242
};

static const uint8_t apu_envelope_reg_index[3] = { 0, 4, 12 };

static void dmc_start_sample(NesApu *apu)
{
    apu->dmc_current_address = apu->dmc_sample_address;
    apu->dmc_bytes_remaining = apu->dmc_sample_length;
    apu->dmc_sample_buffer_empty = 1;
    if (apu->dmc_timer_counter <= 0) {
        apu->dmc_timer_counter = dmc_periods[apu->regs[0x10] & 0x0Fu];
    }
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
        nes->apu.pulse_sequence_step[0] = 0;
        nes->apu.pulse_timer_counter[0] = 0;
        break;
    case 0x4007u:
        if ((nes->apu.status & 0x02u) != 0) {
            nes->apu.length_counter[1] = apu_length_table[value >> 3];
        }
        nes->apu.envelope_start[1] = 1;
        nes->apu.pulse_sequence_step[1] = 0;
        nes->apu.pulse_timer_counter[1] = 0;
        break;
    case 0x400Bu:
        if ((nes->apu.status & 0x04u) != 0) {
            nes->apu.length_counter[2] = apu_length_table[value >> 3];
        }
        nes->apu.triangle_linear_reload = 1;
        nes->apu.triangle_timer_counter = 0;
        break;
    case 0x400Fu:
        if ((nes->apu.status & 0x08u) != 0) {
            nes->apu.length_counter[3] = apu_length_table[value >> 3];
        }
        nes->apu.envelope_start[2] = 1;
        nes->apu.noise_timer_counter = 0;
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
            nes->apu.dmc_sample_buffer_empty = 1;
            nes->apu.dmc_bits_remaining = 0;
            nes->apu.dmc_silence = 1;
        } else if (nes->apu.dmc_bytes_remaining == 0) {
            dmc_start_sample(&nes->apu);
        }
        break;
    case 0x4017u:
        nes->apu.frame_counter_mode = (uint8_t)((value & 0x80u) != 0);
        nes->apu.frame_irq_inhibit = (uint8_t)((value & 0x40u) != 0);
        nes->apu.frame_counter_cycles = 0;
        nes->apu.frame_step = 0;
        if (nes->apu.frame_irq_inhibit) {
            nes->apu.frame_irq = 0;
            nes_update_irq(nes);
        }
        if (nes->apu.frame_counter_mode) {
            apu_clock_quarter_frame(&nes->apu);
            apu_clock_half_frame(&nes->apu);
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

static void apu_clock_half_frame(NesApu *apu)
{
    apu_clock_length_counters(apu);
    apu_clock_sweeps(apu);
}

static int apu_frame_counter_period(const NesApu *apu)
{
    return apu->frame_counter_mode ? 37282 : 29830;
}

static int apu_next_frame_counter_event(const NesApu *apu)
{
    const int *events = apu->frame_counter_mode ? frame_counter_5_step_events : frame_counter_4_step_events;
    int i;

    for (i = 0; i < 4; ++i) {
        if (apu->frame_counter_cycles < events[i]) {
            return events[i];
        }
    }
    return apu_frame_counter_period(apu);
}

static void apu_clock_frame_event(NesEmu *nes)
{
    NesApu *apu = &nes->apu;
    int cycle = apu->frame_counter_cycles;

    if (apu->frame_counter_mode) {
        if (cycle == frame_counter_5_step_events[0]) {
            apu_clock_quarter_frame(apu);
            apu->frame_step = 1;
        } else if (cycle == frame_counter_5_step_events[1]) {
            apu_clock_quarter_frame(apu);
            apu_clock_half_frame(apu);
            apu->frame_step = 2;
        } else if (cycle == frame_counter_5_step_events[2]) {
            apu_clock_quarter_frame(apu);
            apu->frame_step = 3;
        } else if (cycle == frame_counter_5_step_events[3]) {
            apu_clock_quarter_frame(apu);
            apu_clock_half_frame(apu);
            apu->frame_step = 4;
        }
        return;
    }

    if (cycle == frame_counter_4_step_events[0]) {
        apu_clock_quarter_frame(apu);
        apu->frame_step = 1;
    } else if (cycle == frame_counter_4_step_events[1]) {
        apu_clock_quarter_frame(apu);
        apu_clock_half_frame(apu);
        apu->frame_step = 2;
    } else if (cycle == frame_counter_4_step_events[2]) {
        apu_clock_quarter_frame(apu);
        apu->frame_step = 3;
    } else if (cycle == frame_counter_4_step_events[3]) {
        apu_clock_quarter_frame(apu);
        apu_clock_half_frame(apu);
        apu->frame_step = 4;
        if (!apu->frame_irq_inhibit) {
            apu->frame_irq = 1;
            nes_update_irq(nes);
        }
    }
}

void nes_apu_clock_frame_counter(NesEmu *nes, int cycles)
{
    NesApu *apu;

    if (nes == NULL || cycles <= 0) {
        return;
    }
    apu = &nes->apu;
    while (cycles > 0) {
        int next_event = apu_next_frame_counter_event(apu);
        int step = next_event - apu->frame_counter_cycles;

        if (step <= 0) {
            step = 1;
        }
        if (step > cycles) {
            apu->frame_counter_cycles += cycles;
            return;
        }
        apu->frame_counter_cycles += step;
        cycles -= step;
        if (apu->frame_counter_cycles >= apu_frame_counter_period(apu)) {
            apu->frame_counter_cycles = 0;
            apu->frame_step = 0;
        } else {
            apu_clock_frame_event(nes);
        }
    }
}

static int pulse_sample(const NesApu *apu, int channel)
{
    const uint8_t *r = &apu->regs[channel ? 4 : 0];
    int enabled = (apu->status & (channel ? 0x02u : 0x01u)) != 0;
    int volume = apu_envelope_volume(apu, channel);
    int duty = (r[0] >> 6) & 3;

    if (!enabled || apu->length_counter[channel] == 0 || pulse_sweep_mutes(apu, channel) || volume == 0) {
        return 0;
    }
    return pulse_duty_sequences[duty][apu->pulse_sequence_step[channel] & 7u] ? volume : 0;
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

static void apu_clock_pulse_timer(NesApu *apu, int channel, int cycles)
{
    int timer = pulse_timer(apu, channel);
    int period = (timer + 1) * 2;

    if (period <= 0) {
        period = 2;
    }
    apu->pulse_timer_counter[channel] -= cycles;
    while (apu->pulse_timer_counter[channel] <= 0) {
        apu->pulse_timer_counter[channel] += period;
        apu->pulse_sequence_step[channel] = (uint8_t)((apu->pulse_sequence_step[channel] + 1u) & 7u);
    }
}

static void apu_clock_triangle_timer(NesApu *apu, int cycles)
{
    const uint8_t *r = &apu->regs[8];
    int timer = r[2] | ((r[3] & 0x07) << 8);
    int period = timer + 1;

    if (period <= 0) {
        period = 1;
    }
    apu->triangle_timer_counter -= cycles;
    while (apu->triangle_timer_counter <= 0) {
        apu->triangle_timer_counter += period;
        if ((apu->status & 0x04u) != 0 &&
            apu->length_counter[2] != 0 &&
            apu->triangle_linear_counter != 0 &&
            timer >= 2) {
            apu->triangle_sequence_step = (uint8_t)((apu->triangle_sequence_step + 1u) & 31u);
        }
    }
}

static void apu_clock_noise_timer(NesApu *apu, int cycles)
{
    const uint8_t *r = &apu->regs[12];
    int period = noise_periods[r[2] & 0x0F];

    apu->noise_timer_counter -= cycles;
    while (apu->noise_timer_counter <= 0) {
        uint16_t tap = (uint16_t)(((r[2] & 0x80u) != 0) ? 6u : 1u);
        uint16_t feedback = (uint16_t)((apu->noise_lfsr ^ (apu->noise_lfsr >> tap)) & 1u);

        apu->noise_lfsr = (uint16_t)((apu->noise_lfsr >> 1) | (feedback << 14));
        apu->noise_timer_counter += period;
    }
}

static void apu_clock_channel_timers(NesEmu *nes, int cycles)
{
    NesApu *apu = &nes->apu;

    if (cycles <= 0) {
        return;
    }
    apu_clock_pulse_timer(apu, 0, cycles);
    apu_clock_pulse_timer(apu, 1, cycles);
    apu_clock_triangle_timer(apu, cycles);
    apu_clock_noise_timer(apu, cycles);
}

static int triangle_sample(const NesApu *apu)
{
    const uint8_t *r = &apu->regs[8];
    int enabled = (apu->status & 0x04u) != 0;
    int timer = r[2] | ((r[3] & 0x07) << 8);

    if (!enabled || apu->length_counter[2] == 0 || apu->triangle_linear_counter == 0 || timer < 2) {
        return 0;
    }
    return triangle_sequence[apu->triangle_sequence_step & 31u];
}

static int noise_sample(const NesApu *apu)
{
    int enabled = (apu->status & 0x08u) != 0;
    int volume = apu_envelope_volume(apu, 2);

    if (!enabled || apu->length_counter[3] == 0 || volume == 0) {
        return 0;
    }
    return (apu->noise_lfsr & 1u) ? 0 : volume;
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

    apu->dmc_sample_buffer = nes_cpu_bus_read(nes, apu->dmc_current_address);
    apu->dmc_sample_buffer_empty = 0;
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
}

static void dmc_reload_output_unit(NesApu *apu)
{
    apu->dmc_bits_remaining = 8;
    if (apu->dmc_sample_buffer_empty) {
        apu->dmc_silence = 1;
        return;
    }
    apu->dmc_shift = apu->dmc_sample_buffer;
    apu->dmc_sample_buffer_empty = 1;
    apu->dmc_silence = 0;
}

static void apu_clock_dmc(NesEmu *nes, int cycles)
{
    NesApu *apu = &nes->apu;
    int period = dmc_periods[apu->regs[0x10] & 0x0Fu];

    if (cycles <= 0 || (apu->status & 0x10u) == 0) {
        return;
    }
    if (apu->dmc_sample_buffer_empty && apu->dmc_bytes_remaining != 0) {
        dmc_fetch_byte(nes);
    }
    apu->dmc_timer_counter -= cycles;
    while (apu->dmc_timer_counter <= 0) {
        if (apu->dmc_bits_remaining == 0) {
            dmc_reload_output_unit(apu);
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
        if (apu->dmc_sample_buffer_empty && apu->dmc_bytes_remaining != 0) {
            dmc_fetch_byte(nes);
        }
        apu->dmc_timer_counter += period;
    }
}

static double namco163_filtered_sample(NesApu *apu, double sample)
{
    apu->namco163_lowpass_output +=
        NAMCO163_LOWPASS_ALPHA * (sample - apu->namco163_lowpass_output);
    apu->namco163_lowpass_output2 +=
        NAMCO163_LOWPASS_ALPHA * (apu->namco163_lowpass_output - apu->namco163_lowpass_output2);
    return apu->namco163_lowpass_output2;
}

static int16_t apu_mix_sample(NesEmu *nes)
{
    int pulse1 = pulse_sample(&nes->apu, 0);
    int pulse2 = pulse_sample(&nes->apu, 1);
    int triangle = triangle_sample(&nes->apu);
    int noise = noise_sample(&nes->apu);
    int dmc = nes->apu.dmc_output;
    int pulse_index = pulse1 + pulse2;
    int tnd_index = triangle * 3 + noise * 2 + dmc;
    int32_t internal_mix = apu_pulse_table[pulse_index] + apu_tnd_table[tnd_index];
    double namco163 = namco163_filtered_sample(&nes->apu, nes_mapper19_audio_sample(nes));
    double mix = (double)internal_mix / (double)APU_MIX_SCALE;
    double filtered;
    int value;

    mix += namco163 / NAMCO163_MIX_SCALE;
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
    apu_clock_channel_timers(nes, cycles);
    apu_clock_dmc(nes, cycles);
    nes_mapper19_clock_audio(nes, cycles);
    apu->sample_accumulator += (uint64_t)cycles * (uint64_t)NESEMU_AUDIO_RATE;
    while (apu->sample_accumulator >= (uint64_t)CPU_CLOCK_NTSC) {
        apu_queue_sample(apu, apu_mix_sample(nes));
        apu->sample_accumulator -= (uint64_t)CPU_CLOCK_NTSC;
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
    int16_t sample;

    if (samples == NULL || sample_rate <= 0) {
        return;
    }
    if (nes == NULL || !nes->rom_loaded) {
        memset(samples, 0, sample_count * sizeof(samples[0]));
        return;
    }
    if (sample_rate != (int)NESEMU_AUDIO_RATE) {
        for (i = 0; i < sample_count; ++i) {
            samples[i] = apu_mix_sample(nes);
            nes->apu.last_render_sample = samples[i];
        }
        return;
    }
    for (i = 0; i < sample_count; ++i) {
        if (apu_pop_sample(&nes->apu, &sample)) {
            nes->apu.last_render_sample = sample;
            samples[i] = sample;
        } else {
            samples[i] = nes->apu.last_render_sample;
        }
    }
}

