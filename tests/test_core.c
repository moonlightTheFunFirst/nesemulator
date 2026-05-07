#include "nesemu.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TEST_ROM_SIZE (16u + NESEMU_PRG_BANK_SIZE + NESEMU_CHR_BANK_SIZE)

static int expect_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        printf("FAIL %s: actual=%d expected=%d\n", name, actual, expected);
        return 0;
    }
    return 1;
}

static int test_mapper0_load_and_map(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = 0;
    rom[7] = 0;
    rom[16] = 0xA5;
    rom[16 + 0x3FFC] = 0x34;
    rom[16 + 0x3FFD] = 0x12;
    rom[16 + NESEMU_PRG_BANK_SIZE] = 0x5A;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper0", result, NES_RESULT_OK);
    ok &= expect_int("mapper id", nes.rom.mapper_id, 0);
    ok &= expect_int("prg banks", nes.rom.prg_banks, 1);
    ok &= expect_int("chr banks", nes.rom.chr_banks, 1);
    ok &= expect_int("cpu read 8000", nes_cpu_read(&nes, 0x8000), 0xA5);
    ok &= expect_int("cpu read mirrored C000", nes_cpu_read(&nes, 0xC000), 0xA5);
    ok &= expect_int("reset vector", nes.reset_vector, 0x1234);
    ok &= expect_int("ppu read chr", nes_ppu_read(&nes, 0x0000), 0x5A);
    nes_cpu_write(&nes, 0x6000, 0x77);
    ok &= expect_int("prg ram", nes_cpu_read(&nes, 0x6000), 0x77);

    nes_set_button(&nes, NES_BUTTON_A, 1);
    ok &= expect_int("button on", nes_get_button(&nes, NES_BUTTON_A), 1);
    nes_set_button(&nes, NES_BUTTON_A, 0);
    ok &= expect_int("button off", nes_get_button(&nes, NES_BUTTON_A), 0);

    nes_shutdown(&nes);
    return ok;
}

static int test_cpu_executes_program(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0xA9, 0x42,       /* LDA #$42 */
        0x85, 0x10,       /* STA $10 */
        0xA2, 0x05,       /* LDX #$05 */
        0xE8,             /* INX */
        0x86, 0x11,       /* STX $11 */
        0x4C, 0x09, 0x80  /* JMP $8009 */
    };

    memset(rom, 0xEA, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = 0;
    rom[7] = 0;
    memcpy(&rom[16], program, sizeof(program));
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load cpu rom", result, NES_RESULT_OK);
    nes_run_frame(&nes);
    ok &= expect_int("cpu sta", nes_cpu_read(&nes, 0x0010), 0x42);
    ok &= expect_int("cpu stx", nes_cpu_read(&nes, 0x0011), 0x06);
    nes_shutdown(&nes);
    return ok;
}

static int test_joypad_shift(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load joypad rom", result, NES_RESULT_OK);
    nes_set_button(&nes, NES_BUTTON_A, 1);
    nes_set_button(&nes, NES_BUTTON_START, 1);
    nes_cpu_write(&nes, 0x4016, 1);
    nes_cpu_write(&nes, 0x4016, 0);
    ok &= expect_int("joypad A", nes_cpu_read(&nes, 0x4016) & 1, 1);
    ok &= expect_int("joypad B", nes_cpu_read(&nes, 0x4016) & 1, 0);
    ok &= expect_int("joypad SELECT", nes_cpu_read(&nes, 0x4016) & 1, 0);
    ok &= expect_int("joypad START", nes_cpu_read(&nes, 0x4016) & 1, 1);
    nes_shutdown(&nes);
    return ok;
}

static int test_unsupported_mapper(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = 0x10;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("unsupported mapper", result, NES_RESULT_UNSUPPORTED_MAPPER);
    nes_shutdown(&nes);
    return ok;
}

static int run_rom_smoke_test(const char *path)
{
    FILE *file;
    long size;
    uint8_t *data;
    NesEmu nes;
    NesResult result;
    int frame;
    int i;
    int unique_colors = 0;
    uint32_t colors[32];
    const uint32_t *framebuffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        printf("FAIL open rom: %s\n", path);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        fclose(file);
        return 0;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);

    nes_init(&nes);
    result = nes_load_rom_image(&nes, data, (size_t)size);
    free(data);
    if (result != NES_RESULT_OK) {
        printf("SKIP %s: %s\n", path, nes_result_string(result));
        nes_shutdown(&nes);
        return 1;
    }
    for (frame = 0; frame < 120; ++frame) {
        nes_run_frame(&nes);
    }
    memset(colors, 0, sizeof(colors));
    framebuffer = nes_get_framebuffer(&nes);
    for (i = 0; framebuffer != NULL && i < (int)(NESEMU_SCREEN_WIDTH * NESEMU_SCREEN_HEIGHT); ++i) {
        int found = 0;
        int j;

        for (j = 0; j < unique_colors; ++j) {
            if (colors[j] == framebuffer[i]) {
                found = 1;
                break;
            }
        }
        if (!found && unique_colors < (int)(sizeof(colors) / sizeof(colors[0]))) {
            colors[unique_colors++] = framebuffer[i];
        }
    }
    printf("smoke %s: pc=$%04X frame=%llu cycles=%llu colors=%d\n",
           path,
           (unsigned int)nes.cpu.pc,
           (unsigned long long)nes.ppu.frame,
           (unsigned long long)nes.cpu.cycles,
           unique_colors);
    nes_shutdown(&nes);
    return 1;
}

int main(int argc, char **argv)
{
    int ok = 1;
    int i;

    ok &= test_mapper0_load_and_map();
    ok &= test_cpu_executes_program();
    ok &= test_joypad_shift();
    ok &= test_unsupported_mapper();
    for (i = 1; i < argc; ++i) {
        ok &= run_rom_smoke_test(argv[i]);
    }
    if (!ok) {
        return 1;
    }
    puts("core tests passed");
    return 0;
}
