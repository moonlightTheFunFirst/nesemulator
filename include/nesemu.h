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

typedef struct NesMapper {
    uint8_t *prg_rom;
    size_t prg_rom_size;
    uint8_t *chr_mem;
    size_t chr_mem_size;
    uint8_t chr_is_ram;
    uint8_t prg_ram[NESEMU_PRG_RAM_SIZE];
} NesMapper;

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
    int extra_cycles;
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
    uint64_t frame;
    uint8_t frame_ready;
    uint32_t framebuffer[NESEMU_SCREEN_WIDTH * NESEMU_SCREEN_HEIGHT];
} NesPpu;

typedef struct NesApu {
    uint8_t regs[0x18];
    uint8_t status;
    double pulse_phase[2];
    double triangle_phase;
    double noise_phase;
    uint16_t noise_lfsr;
    uint8_t length_counter[4];
    double dmc_phase;
    uint16_t dmc_sample_address;
    uint16_t dmc_sample_length;
    uint16_t dmc_current_address;
    uint16_t dmc_bytes_remaining;
    uint8_t dmc_shift;
    uint8_t dmc_bits_remaining;
    uint8_t dmc_output;
    uint8_t dmc_silence;
} NesApu;

typedef struct NesJoypad {
    uint8_t state;
    uint8_t shift;
    uint8_t strobe;
} NesJoypad;

typedef struct NesEmu {
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
} NesEmu;

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
uint8_t nes_ppu_read(const NesEmu *nes, uint16_t address);
void nes_ppu_write(NesEmu *nes, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
