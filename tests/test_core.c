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

static int test_ora_opcodes(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0xA9, 0x05,       /* LDA #$05 */
        0x85, 0x20,       /* STA $20 */
        0xA9, 0x03,       /* LDA #$03 */
        0x05, 0x20,       /* ORA $20 */
        0x85, 0x30,       /* STA $30 */
        0xA9, 0x00,       /* LDA #$00 */
        0x85, 0x14,       /* STA $14 */
        0xA9, 0x02,       /* LDA #$02 */
        0x85, 0x15,       /* STA $15 */
        0xA9, 0x05,       /* LDA #$05 */
        0x8D, 0x00, 0x02, /* STA $0200 */
        0xA2, 0x04,       /* LDX #$04 */
        0xA9, 0x03,       /* LDA #$03 */
        0x01, 0x10,       /* ORA ($10,X) */
        0x85, 0x31,       /* STA $31 */
        0x4C, 0x22, 0x80  /* JMP $8022 */
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
    ok &= expect_int("load ora rom", result, NES_RESULT_OK);
    nes_run_frame(&nes);
    ok &= expect_int("ora zp", nes_cpu_read(&nes, 0x0030), 0x07);
    ok &= expect_int("ora indx", nes_cpu_read(&nes, 0x0031), 0x07);
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
    nes_cpu_write(&nes, 0x4016, 1);
    nes_cpu_write(&nes, 0x4016, 0);
    ok &= expect_int("joypad2 A released", nes_cpu_read(&nes, 0x4017) & 1, 0);
    ok &= expect_int("joypad2 B released", nes_cpu_read(&nes, 0x4017) & 1, 0);
    ok &= expect_int("joypad2 SELECT released", nes_cpu_read(&nes, 0x4017) & 1, 0);
    ok &= expect_int("joypad2 START released", nes_cpu_read(&nes, 0x4017) & 1, 0);
    nes_shutdown(&nes);
    return ok;
}

static uint8_t balloon_style_controller_read(NesEmu *nes, uint16_t address)
{
    uint8_t value = nes_cpu_read(nes, address);

    return (uint8_t)(((value >> 1) | value) & 1u);
}

static int test_balloon_fight_controller_poll_shape(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int i;
    uint8_t p1 = 0;
    uint8_t p2 = 0;

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
    ok &= expect_int("load balloon joypad rom", result, NES_RESULT_OK);
    nes_set_button(&nes, NES_BUTTON_A, 1);
    nes_set_button(&nes, NES_BUTTON_START, 1);
    nes_cpu_write(&nes, 0x4016, 1);
    nes_cpu_write(&nes, 0x4016, 0);
    for (i = 0; i < 8; ++i) {
        p1 = (uint8_t)((p1 << 1) | balloon_style_controller_read(&nes, 0x4016));
        p2 = (uint8_t)((p2 << 1) | balloon_style_controller_read(&nes, 0x4017));
    }
    ok &= expect_int("balloon p1 A+START bits", p1, 0x90);
    ok &= expect_int("balloon p2 empty", p2, 0);
    nes_shutdown(&nes);
    return ok;
}

static int test_balloon_fight_controller_routine(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0xA2, 0x00,       /* LDX #$00 */
        0xA9, 0x01,       /* LDA #$01 */
        0x8D, 0x16, 0x40, /* STA $4016 */
        0xA9, 0x00,       /* LDA #$00 */
        0x8D, 0x16, 0x40, /* STA $4016 */
        0xA0, 0x07,       /* LDY #$07 */
        0xBD, 0x16, 0x40, /* LDA $4016,X */
        0x85, 0x12,       /* STA $12 */
        0x4A,             /* LSR A */
        0x05, 0x12,       /* ORA $12 */
        0x4A,             /* LSR A */
        0x3E, 0x1C, 0x06, /* ROL $061C,X */
        0x88,             /* DEY */
        0x10, 0xF1,       /* BPL loop */
        0x4C, 0x20, 0x80  /* JMP $8020 */
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
    ok &= expect_int("load balloon routine rom", result, NES_RESULT_OK);
    nes_set_button(&nes, NES_BUTTON_A, 1);
    nes_set_button(&nes, NES_BUTTON_START, 1);
    nes_run_frame(&nes);
    ok &= expect_int("balloon routine RAM bits", nes_cpu_read(&nes, 0x061C), 0x90);
    nes_shutdown(&nes);
    return ok;
}

static int test_unofficial_opcodes(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0xA9, 0x0F,       /* LDA #$0F */
        0xA2, 0xF0,       /* LDX #$F0 */
        0x87, 0x20,       /* SAX $20 */
        0xA9, 0x01,       /* LDA #$01 */
        0x85, 0x21,       /* STA $21 */
        0x07, 0x21,       /* SLO $21 */
        0xA7, 0x21,       /* LAX $21 */
        0x4C, 0x0E, 0x80  /* JMP $800E */
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
    ok &= expect_int("load unofficial rom", result, NES_RESULT_OK);
    nes_run_frame(&nes);
    ok &= expect_int("sax stores a&x", nes_cpu_read(&nes, 0x0020), 0x00);
    ok &= expect_int("slo shifts memory", nes_cpu_read(&nes, 0x0021), 0x02);
    ok &= expect_int("lax sets a", nes.cpu.a, 0x02);
    ok &= expect_int("lax sets x", nes.cpu.x, 0x02);
    nes_shutdown(&nes);
    return ok;
}

static int test_apu_length_counter(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int frame;

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
    ok &= expect_int("load apu rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4015, 0x01);
    nes_cpu_write(&nes, 0x4003, 0x08);
    ok &= expect_int("apu length active", nes_cpu_read(&nes, 0x4015) & 1, 1);
    for (frame = 0; frame < 130; ++frame) {
        nes_run_frame(&nes);
    }
    ok &= expect_int("apu length expires", nes_cpu_read(&nes, 0x4015) & 1, 0);
    nes_shutdown(&nes);
    return ok;
}

static int test_dmc_playback_progresses(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    int16_t samples[512];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int i;
    int nonzero = 0;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[16] = 0xFF;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load dmc rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4010, 0x0F);
    nes_cpu_write(&nes, 0x4011, 0x40);
    nes_cpu_write(&nes, 0x4012, 0x00);
    nes_cpu_write(&nes, 0x4013, 0x00);
    nes_cpu_write(&nes, 0x4015, 0x10);
    nes_render_audio(&nes, samples, sizeof(samples) / sizeof(samples[0]), NESEMU_AUDIO_RATE);
    for (i = 0; i < (int)(sizeof(samples) / sizeof(samples[0])); ++i) {
        if (samples[i] != 0) {
            nonzero = 1;
            break;
        }
    }
    ok &= expect_int("dmc produced sample", nonzero, 1);
    nes_shutdown(&nes);
    return ok;
}

static int test_kil_opcode_stops_cpu(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;

    memset(rom, 0xEA, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = 0;
    rom[7] = 0;
    rom[16] = 0x02;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load kil rom", result, NES_RESULT_OK);
    nes_run_frame(&nes);
    ok &= expect_int("kil stops", nes.cpu.stopped, 1);
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
    ok &= test_ora_opcodes();
    ok &= test_joypad_shift();
    ok &= test_balloon_fight_controller_poll_shape();
    ok &= test_balloon_fight_controller_routine();
    ok &= test_unofficial_opcodes();
    ok &= test_apu_length_counter();
    ok &= test_dmc_playback_progresses();
    ok &= test_kil_opcode_stops_cpu();
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
