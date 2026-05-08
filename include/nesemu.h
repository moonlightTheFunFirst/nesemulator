#ifndef NESEMU_H
#define NESEMU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NESEMU_PRG_BANK_SIZE 0x4000u
#define NESEMU_CHR_BANK_SIZE 0x2000u
#define NESEMU_PRG_RAM_SIZE  0x2000u
#define NESEMU_SCREEN_WIDTH  256u
#define NESEMU_SCREEN_HEIGHT 240u
#define NESEMU_AUDIO_RATE    44100u
#define NESEMU_AUDIO_BUFFER_SAMPLES NESEMU_AUDIO_RATE

typedef enum NesResult {
    NES_RESULT_OK = 0,
    NES_RESULT_INVALID_ARGUMENT,
    NES_RESULT_OUT_OF_MEMORY,
    NES_RESULT_INVALID_ROM,
    NES_RESULT_UNSUPPORTED_MAPPER,
    NES_RESULT_IO_ERROR
} NesResult;

typedef enum NesMirroring {
    NES_MIRROR_HORIZONTAL = 0,
    NES_MIRROR_VERTICAL,
    NES_MIRROR_FOUR_SCREEN
} NesMirroring;

typedef enum NesButton {
    NES_BUTTON_A = 0,
    NES_BUTTON_B,
    NES_BUTTON_SELECT,
    NES_BUTTON_START,
    NES_BUTTON_UP,
    NES_BUTTON_DOWN,
    NES_BUTTON_LEFT,
    NES_BUTTON_RIGHT,
    NES_BUTTON_COUNT
} NesButton;

typedef struct NesRomInfo {
    uint8_t mapper_id;
    uint8_t prg_banks;
    uint8_t chr_banks;
    uint8_t has_trainer;
    uint8_t has_battery_ram;
    NesMirroring mirroring;
} NesRomInfo;

typedef struct NesEmu NesEmu;
typedef struct NesMapper NesMapper;

typedef int (*NesMapperCpuReadFunc)(NesEmu *nes, uint16_t address, uint8_t *value);
typedef int (*NesMapperCpuWriteFunc)(NesEmu *nes, uint16_t address, uint8_t value);
typedef int (*NesMapperPpuReadFunc)(NesEmu *nes, uint16_t address, uint8_t *value);
typedef int (*NesMapperPpuWriteFunc)(NesEmu *nes, uint16_t address, uint8_t value);

typedef struct NesMapperOps {
    uint8_t mapper_id;
    void (*init)(NesMapper *mapper, NesMirroring mirroring);
    NesMapperCpuReadFunc cpu_read;
    NesMapperCpuWriteFunc cpu_write;
    NesMapperPpuReadFunc ppu_read;
    NesMapperPpuWriteFunc ppu_write;
} NesMapperOps;

struct NesMapper {
    const NesMapperOps *ops;
    uint8_t *prg_rom;
    size_t prg_rom_size;
    uint8_t *chr_mem;
    size_t chr_mem_size;
    uint8_t chr_is_ram;
    uint8_t chr_bank;
    uint8_t mapper4_bank_select;
    uint8_t mapper4_regs[8];
    uint8_t mapper4_prg_mode;
    uint8_t mapper4_chr_mode;
    uint8_t mapper4_irq_latch;
    uint8_t mapper4_irq_counter;
    uint8_t mapper4_irq_reload;
    uint8_t mapper4_irq_enabled;
    uint8_t mapper4_irq_pending;
    uint8_t mapper76_bank_select;
    uint8_t mapper76_regs[8];
    uint8_t mapper10_prg_bank;
    uint8_t mapper10_chr_banks[4];
    uint8_t mapper10_latch0;
    uint8_t mapper10_latch1;
    uint8_t mapper95_bank_select;
    uint8_t mapper95_regs[8];
    uint8_t mapper154_bank_select;
    uint8_t mapper154_regs[8];
    uint8_t mapper154_mirroring;
    uint8_t mapper206_bank_select;
    uint8_t mapper206_regs[8];
    uint8_t mapper88_bank_select;
    uint8_t mapper88_regs[8];
    uint8_t mapper19_chr_regs[12];
    uint8_t mapper19_prg_regs[3];
    uint8_t mapper19_e800;
    uint8_t mapper19_f800;
    uint16_t mapper19_irq_counter;
    uint8_t mapper19_irq_enabled;
    uint8_t mapper19_irq_pending;
    uint8_t mapper19_ram_addr;
    uint8_t mapper19_ram_auto_increment;
    uint8_t mapper19_sound_disabled;
    int mapper19_audio_cycle_accumulator;
    uint8_t mapper19_audio_channel;
    int16_t mapper19_audio_output[8];
    uint8_t mapper19_internal_ram[128];
    uint8_t prg_ram[NESEMU_PRG_RAM_SIZE];
};

typedef struct NesCpu {
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t p;
    uint8_t sp;
    uint16_t pc;
    uint64_t cycles;
    int nmi_pending;
    int nmi_delay;
    int irq_pending;
    int extra_cycles;
    int io_write_delay;
    int stopped;
} NesCpu;

typedef struct NesPpu {
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oam_addr;
    uint8_t write_latch;
    uint8_t fine_x;
    uint8_t data_buffer;
    uint8_t oam[256];
    uint8_t nametable[2048];
    uint8_t palette[32];
    uint16_t v;
    uint16_t t;
    uint8_t scroll_x;
    uint8_t scroll_y;
    int scanline;
    int cycle;
    int sprite0_hit_position;
    int pending_write_position;
    uint16_t pending_write_address;
    uint8_t pending_write_value;
    uint8_t pending_write;
    int mapper4_irq_cache_scanline;
    int mapper4_irq_cache_cycle;
    uint8_t mapper4_irq_cache_valid;
    uint64_t frame;
    uint8_t frame_ready;
    uint32_t framebuffer[NESEMU_SCREEN_WIDTH * NESEMU_SCREEN_HEIGHT];
} NesPpu;

typedef struct NesApu {
    uint8_t regs[0x18];
    uint8_t status;
    int pulse_timer_counter[2];
    uint8_t pulse_sequence_step[2];
    uint8_t pulse_sweep_divider[2];
    uint8_t pulse_sweep_reload[2];
    int triangle_timer_counter;
    uint8_t triangle_sequence_step;
    int noise_timer_counter;
    uint16_t noise_lfsr;
    uint8_t length_counter[4];
    int dmc_timer_counter;
    uint16_t dmc_sample_address;
    uint16_t dmc_sample_length;
    uint16_t dmc_current_address;
    uint16_t dmc_bytes_remaining;
    uint8_t dmc_sample_buffer;
    uint8_t dmc_sample_buffer_empty;
    uint8_t dmc_shift;
    uint8_t dmc_bits_remaining;
    uint8_t dmc_output;
    uint8_t dmc_silence;
    uint8_t dmc_irq;
    uint8_t envelope_start[3];
    uint8_t envelope_decay[3];
    uint8_t envelope_divider[3];
    uint8_t triangle_linear_counter;
    uint8_t triangle_linear_reload;
    uint8_t frame_step;
    int frame_counter_cycles;
    uint8_t frame_counter_mode;
    uint8_t frame_irq_inhibit;
    uint8_t frame_irq;
    uint64_t sample_accumulator;
    double namco163_lowpass_output;
    double namco163_lowpass_output2;
    double highpass_prev_input;
    double highpass_prev_output;
    size_t sample_read_pos;
    size_t sample_write_pos;
    size_t sample_count;
    int16_t last_render_sample;
    int16_t sample_buffer[NESEMU_AUDIO_BUFFER_SAMPLES];
} NesApu;

typedef struct NesJoypad {
    uint8_t state;
    uint8_t shift;
    uint8_t strobe;
} NesJoypad;

struct NesEmu {
    NesRomInfo rom;
    NesMapper mapper;
    NesCpu cpu;
    NesPpu ppu;
    NesApu apu;
    NesJoypad joypad;
    NesJoypad joypad2;
    uint8_t ram[2048];
    uint16_t reset_vector;
    uint8_t rom_loaded;
};

void nes_init(NesEmu *nes);
void nes_shutdown(NesEmu *nes);
void nes_reset(NesEmu *nes);

NesResult nes_load_rom_image(NesEmu *nes, const uint8_t *data, size_t size);
const char *nes_result_string(NesResult result);

void nes_set_button(NesEmu *nes, NesButton button, int pressed);
int nes_get_button(const NesEmu *nes, NesButton button);

void nes_run_frame(NesEmu *nes);
const uint32_t *nes_get_framebuffer(const NesEmu *nes);
void nes_render_audio(NesEmu *nes, int16_t *samples, size_t sample_count, int sample_rate);

uint8_t nes_cpu_read(NesEmu *nes, uint16_t address);
void nes_cpu_write(NesEmu *nes, uint16_t address, uint8_t value);
uint8_t nes_ppu_read(NesEmu *nes, uint16_t address);
void nes_ppu_write(NesEmu *nes, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
