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

typedef struct NesEmu {
    NesRomInfo rom;
    NesMapper mapper;
    uint8_t buttons;
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

uint8_t nes_cpu_read(const NesEmu *nes, uint16_t address);
void nes_cpu_write(NesEmu *nes, uint16_t address, uint8_t value);
uint8_t nes_ppu_read(const NesEmu *nes, uint16_t address);
void nes_ppu_write(NesEmu *nes, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
