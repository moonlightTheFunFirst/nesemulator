#include "nesemu.h"

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

int main(void)
{
    int ok = 1;
    ok &= test_mapper0_load_and_map();
    ok &= test_unsupported_mapper();
    if (!ok) {
        return 1;
    }
    puts("core tests passed");
    return 0;
}
