#include "nesemu.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TEST_ROM_SIZE (16u + NESEMU_PRG_BANK_SIZE + NESEMU_CHR_BANK_SIZE)
#define TEST_MAPPER3_ROM_SIZE (16u + NESEMU_PRG_BANK_SIZE * 2u + NESEMU_CHR_BANK_SIZE * 4u)
#define TEST_MAPPER4_PRG_BANKS 4u
#define TEST_MAPPER4_CHR_BANKS 2u
#define TEST_MAPPER4_ROM_SIZE \
    (16u + NESEMU_PRG_BANK_SIZE * TEST_MAPPER4_PRG_BANKS + NESEMU_CHR_BANK_SIZE * TEST_MAPPER4_CHR_BANKS)
#define TEST_MAPPER10_PRG_BANKS 4u
#define TEST_MAPPER10_CHR_BANKS 4u
#define TEST_MAPPER10_ROM_SIZE \
    (16u + NESEMU_PRG_BANK_SIZE * TEST_MAPPER10_PRG_BANKS + NESEMU_CHR_BANK_SIZE * TEST_MAPPER10_CHR_BANKS)
#define TEST_MAPPER19_PRG_BANKS 4u
#define TEST_MAPPER19_CHR_BANKS 2u
#define TEST_MAPPER19_ROM_SIZE \
    (16u + NESEMU_PRG_BANK_SIZE * TEST_MAPPER19_PRG_BANKS + NESEMU_CHR_BANK_SIZE * TEST_MAPPER19_CHR_BANKS)
#define TEST_MAPPER88_PRG_BANKS 4u
#define TEST_MAPPER88_CHR_BANKS 16u
#define TEST_MAPPER88_ROM_SIZE \
    (16u + NESEMU_PRG_BANK_SIZE * TEST_MAPPER88_PRG_BANKS + NESEMU_CHR_BANK_SIZE * TEST_MAPPER88_CHR_BANKS)
#define TEST_MAPPER88_PRG_MASK_PRG_BANKS 3u
#define TEST_MAPPER88_PRG_MASK_ROM_SIZE \
    (16u + NESEMU_PRG_BANK_SIZE * TEST_MAPPER88_PRG_MASK_PRG_BANKS + NESEMU_CHR_BANK_SIZE)

static int expect_int(const char *name, int actual, int expected)
{
    if (actual != expected) {
        printf("FAIL %s: actual=%d expected=%d\n", name, actual, expected);
        return 0;
    }
    return 1;
}

static void mapper19_write_chip_ram(NesEmu *nes, uint8_t address, const uint8_t *values, size_t count)
{
    size_t i;

    nes_cpu_write(nes, 0xF800u, (uint8_t)(address | 0x80u));
    for (i = 0; i < count; ++i) {
        nes_cpu_write(nes, 0x4800u, values[i]);
    }
}

static int audio_has_signal(const int16_t *samples, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (samples[i] != 0) {
            return 1;
        }
    }
    return 0;
}

void nes_apu_clock_frame_counter(NesEmu *nes, int cycles);
void nes_apu_clock_audio(NesEmu *nes, int cycles);
void nes_apu_write(NesEmu *nes, uint16_t address, uint8_t value);
void nes_mapper19_clock_audio(NesEmu *nes, int cycles);
int nes_cpu_step(NesEmu *nes);

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

static int test_mapper3_chr_bank_switch(void)
{
    uint8_t rom[TEST_MAPPER3_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t chr_offset = 16u + NESEMU_PRG_BANK_SIZE * 2u;
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int bank;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 2;
    rom[5] = 4;
    rom[6] = 0x30;
    rom[7] = 0;
    rom[prg_offset] = 0xA5;
    rom[prg_offset + 0x4000] = 0x5A;
    rom[prg_offset + 0x7FFC] = 0x00;
    rom[prg_offset + 0x7FFD] = 0x80;
    for (bank = 0; bank < 4; ++bank) {
        rom[chr_offset + (size_t)bank * NESEMU_CHR_BANK_SIZE] = (uint8_t)(0x10 + bank);
        rom[chr_offset + (size_t)bank * NESEMU_CHR_BANK_SIZE + 0x1FFFu] = (uint8_t)(0x80 + bank);
    }

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper3", result, NES_RESULT_OK);
    ok &= expect_int("mapper3 id", nes.rom.mapper_id, 3);
    ok &= expect_int("mapper3 cpu read 8000", nes_cpu_read(&nes, 0x8000), 0xA5);
    ok &= expect_int("mapper3 cpu read C000", nes_cpu_read(&nes, 0xC000), 0x5A);
    ok &= expect_int("mapper3 initial chr", nes_ppu_read(&nes, 0x0000), 0x10);
    nes_cpu_write(&nes, 0x8000, 2);
    ok &= expect_int("mapper3 selected bank", nes.mapper.chr_bank, 2);
    ok &= expect_int("mapper3 chr bank 2 start", nes_ppu_read(&nes, 0x0000), 0x12);
    ok &= expect_int("mapper3 chr bank 2 end", nes_ppu_read(&nes, 0x1FFF), 0x82);
    nes_cpu_write(&nes, 0x8000, 3);
    nes_ppu_write(&nes, 0x0000, 0xEE);
    ok &= expect_int("mapper3 chr rom ignores writes", nes_ppu_read(&nes, 0x0000), 0x13);

    nes_shutdown(&nes);
    return ok;
}

static int test_mapper4_bank_switch_and_irq(void)
{
    uint8_t rom[TEST_MAPPER4_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_size = NESEMU_PRG_BANK_SIZE * TEST_MAPPER4_PRG_BANKS;
    size_t chr_offset = 16u + prg_size;
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int bank;
    uint8_t program[] = {
        0x58,                   /* CLI */
        0xA9, 0x01,             /* LDA #$01 */
        0x8D, 0x00, 0xC0,       /* STA $C000: IRQ latch */
        0xA9, 0x00,             /* LDA #$00 */
        0x8D, 0x01, 0xC0,       /* STA $C001: reload */
        0x8D, 0x01, 0xE0,       /* STA $E001: enable */
        0xA9, 0x08,             /* LDA #$08 */
        0x8D, 0x00, 0x20,       /* STA $2000: sprite pattern table at $1000 */
        0xA9, 0x18,             /* LDA #$18 */
        0x8D, 0x01, 0x20,       /* STA $2001: show bg/sprites */
        0x4C, 0x18, 0xE0        /* JMP $E018 */
    };
    uint8_t irq_handler[] = {
        0xA9, 0x00,             /* LDA #$00 */
        0x8D, 0x00, 0xE0,       /* STA $E000: disable/ack IRQ */
        0xE6, 0x20,             /* INC $20 */
        0x40                    /* RTI */
    };

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER4_PRG_BANKS;
    rom[5] = TEST_MAPPER4_CHR_BANKS;
    rom[6] = 0x40;
    rom[7] = 0;
    for (bank = 0; bank < 8; ++bank) {
        rom[prg_offset + (size_t)bank * 0x2000u] = (uint8_t)(0x80 + bank);
    }
    for (bank = 0; bank < 16; ++bank) {
        rom[chr_offset + (size_t)bank * 0x0400u] = (uint8_t)(0x40 + bank);
        rom[chr_offset + (size_t)bank * 0x0400u + 0x03FFu] = (uint8_t)(0xC0 + bank);
    }
    memcpy(&rom[prg_offset + 7u * 0x2000u], program, sizeof(program));
    memcpy(&rom[prg_offset + 7u * 0x2000u + 0x0100u], irq_handler, sizeof(irq_handler));
    rom[prg_offset + 7u * 0x2000u + 0x1FFCu] = 0x00;
    rom[prg_offset + 7u * 0x2000u + 0x1FFDu] = 0xE0;
    rom[prg_offset + 7u * 0x2000u + 0x1FFEu] = 0x00;
    rom[prg_offset + 7u * 0x2000u + 0x1FFFu] = 0xE1;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper4", result, NES_RESULT_OK);
    ok &= expect_int("mapper4 id", nes.rom.mapper_id, 4);
    ok &= expect_int("mapper4 initial 8000", nes_cpu_read(&nes, 0x8000), 0x80);
    ok &= expect_int("mapper4 initial A000", nes_cpu_read(&nes, 0xA000), 0x81);
    ok &= expect_int("mapper4 initial C000", nes_cpu_read(&nes, 0xC000), 0x86);
    ok &= expect_int("mapper4 initial E000", nes_cpu_read(&nes, 0xE000), 0x58);
    nes_cpu_write(&nes, 0x8000, 0x06);
    nes_cpu_write(&nes, 0x8001, 0x03);
    ok &= expect_int("mapper4 prg r6 bank", nes_cpu_read(&nes, 0x8000), 0x83);
    nes_cpu_write(&nes, 0x8000, 0x07);
    nes_cpu_write(&nes, 0x8001, 0x04);
    ok &= expect_int("mapper4 prg r7 bank", nes_cpu_read(&nes, 0xA000), 0x84);
    nes_cpu_write(&nes, 0x8000, 0x46);
    nes_cpu_write(&nes, 0x8001, 0x02);
    ok &= expect_int("mapper4 prg mode fixed 8000", nes_cpu_read(&nes, 0x8000), 0x86);
    ok &= expect_int("mapper4 prg mode r6 C000", nes_cpu_read(&nes, 0xC000), 0x82);
    nes_cpu_write(&nes, 0xA000, 0x00);
    ok &= expect_int("mapper4 mirroring vertical", nes.rom.mirroring, NES_MIRROR_VERTICAL);
    nes_cpu_write(&nes, 0xA000, 0x01);
    ok &= expect_int("mapper4 mirroring horizontal", nes.rom.mirroring, NES_MIRROR_HORIZONTAL);
    nes_cpu_write(&nes, 0x8000, 0x00);
    nes_cpu_write(&nes, 0x8001, 0x04);
    ok &= expect_int("mapper4 chr r0 low", nes_ppu_read(&nes, 0x0000), 0x44);
    ok &= expect_int("mapper4 chr r0 high", nes_ppu_read(&nes, 0x0400), 0x45);
    nes_cpu_write(&nes, 0x8000, 0x02);
    nes_cpu_write(&nes, 0x8001, 0x09);
    ok &= expect_int("mapper4 chr r2", nes_ppu_read(&nes, 0x1000), 0x49);
    nes_cpu_write(&nes, 0x8000, 0x82);
    ok &= expect_int("mapper4 chr mode r2 low", nes_ppu_read(&nes, 0x0000), 0x49);
    ok &= expect_int("mapper4 chr mode r0 high", nes_ppu_read(&nes, 0x1000), 0x44);

    nes_reset(&nes);
    nes_run_frame(&nes);
    ok &= expect_int("mapper4 scanline irq reached handler", nes_cpu_read(&nes, 0x0020), 1);
    ok &= expect_int("mapper4 irq acknowledged", nes.mapper.mapper4_irq_pending, 0);

    nes_shutdown(&nes);
    return ok;
}

static int test_mapper4_irq_requires_a12_rise(void)
{
    uint8_t rom[TEST_MAPPER4_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_8k_banks = TEST_MAPPER4_PRG_BANKS * 2u;
    size_t fixed_offset = prg_offset + (prg_8k_banks - 1u) * 0x2000u;
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0x58,                   /* CLI */
        0xA9, 0x01,             /* LDA #$01 */
        0x8D, 0x00, 0xC0,       /* STA $C000: IRQ latch */
        0xA9, 0x00,             /* LDA #$00 */
        0x8D, 0x01, 0xC0,       /* STA $C001: reload */
        0x8D, 0x01, 0xE0,       /* STA $E001: enable */
        0xA9, 0x18,             /* LDA #$18 */
        0x8D, 0x01, 0x20,       /* STA $2001: show bg/sprites */
        0x4C, 0x13, 0xE0        /* JMP $E013 */
    };
    uint8_t irq_handler[] = {
        0xA9, 0x00,             /* LDA #$00 */
        0x8D, 0x00, 0xE0,       /* STA $E000: disable/ack IRQ */
        0xE6, 0x20,             /* INC $20 */
        0x40                    /* RTI */
    };

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER4_PRG_BANKS;
    rom[5] = TEST_MAPPER4_CHR_BANKS;
    rom[6] = 0x40;
    rom[7] = 0;
    memcpy(&rom[fixed_offset], program, sizeof(program));
    memcpy(&rom[fixed_offset + 0x0100u], irq_handler, sizeof(irq_handler));
    rom[fixed_offset + 0x1FFCu] = 0x00;
    rom[fixed_offset + 0x1FFDu] = 0xE0;
    rom[fixed_offset + 0x1FFEu] = 0x00;
    rom[fixed_offset + 0x1FFFu] = 0xE1;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper4 no a12 rom", result, NES_RESULT_OK);
    nes_run_frame(&nes);
    ok &= expect_int("mapper4 irq waits for a12 rise", nes_cpu_read(&nes, 0x0020), 0);
    nes_shutdown(&nes);
    return ok;
}

static int test_mapper10_mmc4_latches(void)
{
    uint8_t rom[TEST_MAPPER10_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_size = NESEMU_PRG_BANK_SIZE * TEST_MAPPER10_PRG_BANKS;
    size_t chr_offset = 16u + prg_size;
    NesEmu nes;
    NesResult result;
    int ok = 1;
    size_t bank;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER10_PRG_BANKS;
    rom[5] = TEST_MAPPER10_CHR_BANKS;
    rom[6] = 0xA0;
    rom[7] = 0x00;
    for (bank = 0; bank < TEST_MAPPER10_PRG_BANKS; ++bank) {
        rom[prg_offset + bank * NESEMU_PRG_BANK_SIZE] = (uint8_t)(0x80u + bank);
    }
    for (bank = 0; bank < TEST_MAPPER10_CHR_BANKS * 2u; ++bank) {
        rom[chr_offset + bank * 0x1000u] = (uint8_t)(0x10u + bank);
        rom[chr_offset + bank * 0x1000u + 0x0FD8u] = (uint8_t)(0x20u + bank);
        rom[chr_offset + bank * 0x1000u + 0x0FE8u] = (uint8_t)(0x30u + bank);
    }
    rom[prg_offset + (TEST_MAPPER10_PRG_BANKS - 1u) * NESEMU_PRG_BANK_SIZE + 0x3FFCu] = 0x00;
    rom[prg_offset + (TEST_MAPPER10_PRG_BANKS - 1u) * NESEMU_PRG_BANK_SIZE + 0x3FFDu] = 0xC0;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper10", result, NES_RESULT_OK);
    ok &= expect_int("mapper10 id", nes.rom.mapper_id, 10);
    ok &= expect_int("mapper10 initial 8000", nes_cpu_read(&nes, 0x8000), 0x80);
    ok &= expect_int("mapper10 fixed C000", nes_cpu_read(&nes, 0xC000), 0x83);
    nes_cpu_write(&nes, 0xA000, 0x02);
    ok &= expect_int("mapper10 prg switch 8000", nes_cpu_read(&nes, 0x8000), 0x82);

    nes_cpu_write(&nes, 0xB000, 0x01);
    nes_cpu_write(&nes, 0xC000, 0x02);
    nes_cpu_write(&nes, 0xD000, 0x03);
    nes_cpu_write(&nes, 0xE000, 0x04);
    ok &= expect_int("mapper10 latch0 starts FE", nes.mapper.mapper10_latch0, 0xFE);
    ok &= expect_int("mapper10 latch1 starts FE", nes.mapper.mapper10_latch1, 0xFE);
    ok &= expect_int("mapper10 latch0 FE bank", nes_ppu_read(&nes, 0x0000), 0x12);
    ok &= expect_int("mapper10 latch1 FE bank", nes_ppu_read(&nes, 0x1000), 0x14);

    ok &= expect_int("mapper10 FD trigger uses old bank", nes_ppu_read(&nes, 0x0FD8), 0x22);
    ok &= expect_int("mapper10 latch0 set FD", nes.mapper.mapper10_latch0, 0xFD);
    ok &= expect_int("mapper10 latch0 FD bank", nes_ppu_read(&nes, 0x0000), 0x11);
    ok &= expect_int("mapper10 FE trigger uses old bank", nes_ppu_read(&nes, 0x0FE8), 0x31);
    ok &= expect_int("mapper10 latch0 set FE", nes.mapper.mapper10_latch0, 0xFE);
    ok &= expect_int("mapper10 latch0 back to FE bank", nes_ppu_read(&nes, 0x0000), 0x12);

    ok &= expect_int("mapper10 high FD trigger uses old bank", nes_ppu_read(&nes, 0x1FD8), 0x24);
    ok &= expect_int("mapper10 latch1 set FD", nes.mapper.mapper10_latch1, 0xFD);
    ok &= expect_int("mapper10 latch1 FD bank", nes_ppu_read(&nes, 0x1000), 0x13);
    ok &= expect_int("mapper10 high FE trigger uses old bank", nes_ppu_read(&nes, 0x1FE8), 0x33);
    ok &= expect_int("mapper10 latch1 set FE", nes.mapper.mapper10_latch1, 0xFE);
    ok &= expect_int("mapper10 latch1 back to FE bank", nes_ppu_read(&nes, 0x1000), 0x14);

    nes.ppu.nametable[0] = 0xAAu;
    nes.ppu.nametable[0x400] = 0xBBu;
    nes_cpu_write(&nes, 0xF000, 0x00);
    ok &= expect_int("mapper10 mirroring vertical A", nes_ppu_read(&nes, 0x2800), 0xAA);
    ok &= expect_int("mapper10 mirroring vertical B", nes_ppu_read(&nes, 0x2400), 0xBB);
    nes_cpu_write(&nes, 0xF000, 0x01);
    ok &= expect_int("mapper10 mirroring horizontal A", nes_ppu_read(&nes, 0x2400), 0xAA);
    ok &= expect_int("mapper10 mirroring horizontal B", nes_ppu_read(&nes, 0x2800), 0xBB);

    nes_shutdown(&nes);
    return ok;
}

static int test_mapper19_bank_switch_nt_and_irq(void)
{
    uint8_t rom[TEST_MAPPER19_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_size = NESEMU_PRG_BANK_SIZE * TEST_MAPPER19_PRG_BANKS;
    size_t chr_offset = 16u + prg_size;
    size_t prg_8k_banks = TEST_MAPPER19_PRG_BANKS * 2u;
    size_t chr_1k_banks = TEST_MAPPER19_CHR_BANKS * 8u;
    size_t fixed_offset = prg_offset + (prg_8k_banks - 1u) * 0x2000u;
    NesEmu nes;
    NesResult result;
    int ok = 1;
    size_t bank;
    uint8_t program[] = {
        0x58,                   /* CLI */
        0xA9, 0xFC,             /* LDA #$FC */
        0x8D, 0x00, 0x50,       /* STA $5000: IRQ counter low */
        0xA9, 0xFF,             /* LDA #$FF */
        0x8D, 0x00, 0x58,       /* STA $5800: IRQ high and enable */
        0x4C, 0x0B, 0xE0        /* JMP $E00B */
    };
    uint8_t irq_handler[] = {
        0xA9, 0x00,             /* LDA #$00 */
        0x8D, 0x00, 0x58,       /* STA $5800: disable/ack IRQ */
        0xE6, 0x20,             /* INC $20 */
        0x40                    /* RTI */
    };

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER19_PRG_BANKS;
    rom[5] = TEST_MAPPER19_CHR_BANKS;
    rom[6] = 0x31;
    rom[7] = 0x10;

    for (bank = 0; bank < prg_8k_banks; ++bank) {
        rom[prg_offset + bank * 0x2000u] = (uint8_t)(0x80u + bank);
    }
    for (bank = 0; bank < chr_1k_banks; ++bank) {
        rom[chr_offset + bank * 0x0400u] = (uint8_t)(0x10u + bank);
        rom[chr_offset + bank * 0x0400u + 0x03FFu] = (uint8_t)(0x40u + bank);
    }
    memcpy(&rom[fixed_offset], program, sizeof(program));
    memcpy(&rom[fixed_offset + 0x0100u], irq_handler, sizeof(irq_handler));
    rom[fixed_offset + 0x1FFAu] = 0x00;
    rom[fixed_offset + 0x1FFBu] = 0xE1;
    rom[fixed_offset + 0x1FFCu] = 0x00;
    rom[fixed_offset + 0x1FFDu] = 0xE0;
    rom[fixed_offset + 0x1FFEu] = 0x00;
    rom[fixed_offset + 0x1FFFu] = 0xE1;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper19", result, NES_RESULT_OK);
    ok &= expect_int("mapper19 id", nes.rom.mapper_id, 19);
    ok &= expect_int("mapper19 initial 8000", nes_cpu_read(&nes, 0x8000), 0x80);
    ok &= expect_int("mapper19 initial A000", nes_cpu_read(&nes, 0xA000), 0x81);
    ok &= expect_int("mapper19 initial C000", nes_cpu_read(&nes, 0xC000), 0x82);
    ok &= expect_int("mapper19 fixed E000", nes_cpu_read(&nes, 0xE000), 0x58);
    nes.ppu.nametable[0] = 0x12u;
    nes.ppu.nametable[0x400] = 0x34u;
    ok &= expect_int("mapper19 vertical nt initial A", nes_ppu_read(&nes, 0x2000), 0x12);
    ok &= expect_int("mapper19 vertical nt initial B", nes_ppu_read(&nes, 0x2400), 0x34);
    ok &= expect_int("mapper19 vertical nt mirror A", nes_ppu_read(&nes, 0x2800), 0x12);
    ok &= expect_int("mapper19 vertical nt mirror B", nes_ppu_read(&nes, 0x2C00), 0x34);

    nes_cpu_write(&nes, 0xE000, 3);
    ok &= expect_int("mapper19 prg 8000 bank", nes_cpu_read(&nes, 0x8000), 0x83);
    nes_cpu_write(&nes, 0xE800, 4);
    ok &= expect_int("mapper19 prg A000 bank", nes_cpu_read(&nes, 0xA000), 0x84);
    nes_cpu_write(&nes, 0xF000, 5);
    ok &= expect_int("mapper19 prg C000 bank", nes_cpu_read(&nes, 0xC000), 0x85);

    nes_cpu_write(&nes, 0x8000, 4);
    ok &= expect_int("mapper19 chr low bank", nes_ppu_read(&nes, 0x0000), 0x14);
    ok &= expect_int("mapper19 chr low bank end", nes_ppu_read(&nes, 0x03FF), 0x44);
    nes_cpu_write(&nes, 0xA000, 5);
    ok &= expect_int("mapper19 chr high bank", nes_ppu_read(&nes, 0x1000), 0x15);

    nes.ppu.nametable[0] = 0xAAu;
    nes_cpu_write(&nes, 0x8000, 0xE0);
    ok &= expect_int("mapper19 low chr can select nt ram", nes_ppu_read(&nes, 0x0000), 0xAA);
    nes_ppu_write(&nes, 0x0000, 0xCC);
    ok &= expect_int("mapper19 low chr nt ram write", nes.ppu.nametable[0], 0xCC);
    nes_cpu_write(&nes, 0xE800, 0x40);
    ok &= expect_int("mapper19 e800 can force low chr rom", nes_ppu_read(&nes, 0x0000), 0x10);

    nes.ppu.nametable[0] = 0x11u;
    nes.ppu.nametable[0x400] = 0x22u;
    nes_cpu_write(&nes, 0xC000, 0xE0);
    nes_cpu_write(&nes, 0xC800, 0xE1);
    ok &= expect_int("mapper19 nt bank A", nes_ppu_read(&nes, 0x2000), 0x11);
    ok &= expect_int("mapper19 nt bank B", nes_ppu_read(&nes, 0x2400), 0x22);
    nes_ppu_write(&nes, 0x2400, 0x33);
    ok &= expect_int("mapper19 nt bank write", nes.ppu.nametable[0x400], 0x33);

    nes_cpu_write(&nes, 0xF800, 0x80);
    nes_cpu_write(&nes, 0x4800, 0x5A);
    nes_cpu_write(&nes, 0x4800, 0xA5);
    nes_cpu_write(&nes, 0xF800, 0x80);
    ok &= expect_int("mapper19 chip ram read 0", nes_cpu_read(&nes, 0x4800), 0x5A);
    ok &= expect_int("mapper19 chip ram read 1", nes_cpu_read(&nes, 0x4800), 0xA5);

    nes_cpu_write(&nes, 0x6000, 0x77);
    ok &= expect_int("mapper19 prg ram protected", nes_cpu_read(&nes, 0x6000), 0);
    nes_cpu_write(&nes, 0xF800, 0x40);
    nes_cpu_write(&nes, 0x6000, 0x77);
    ok &= expect_int("mapper19 prg ram write enabled", nes_cpu_read(&nes, 0x6000), 0x77);
    nes_cpu_write(&nes, 0xF800, 0x41);
    nes_cpu_write(&nes, 0x6000, 0x55);
    ok &= expect_int("mapper19 prg ram window protected", nes_cpu_read(&nes, 0x6000), 0x77);

    nes_reset(&nes);
    nes_run_frame(&nes);
    ok &= expect_int("mapper19 cpu irq reached handler", nes_cpu_read(&nes, 0x0020), 1);
    ok &= expect_int("mapper19 irq acknowledged", nes.mapper.mapper19_irq_pending, 0);

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
    rom[6] = 0;
    rom[7] = 0;
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
    rom[6] = 0;
    rom[7] = 0;
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

static int test_mach_rider_controller_routine(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0xA0, 0x01,       /* LDY #$01 */
        0x8C, 0x16, 0x40, /* STY $4016 */
        0x88,             /* DEY */
        0x8C, 0x16, 0x40, /* STY $4016 */
        0xAD, 0x16, 0x40, /* LDA $4016 */
        0x29, 0x03,       /* AND #$03 */
        0x85, 0x54,       /* STA $54 */
        0xAD, 0x16, 0x40, /* LDA $4016 */
        0x29, 0x03,       /* AND #$03 */
        0x85, 0x55,       /* STA $55 */
        0xAD, 0x16, 0x40, /* LDA $4016 */
        0x29, 0x03,       /* AND #$03 */
        0x85, 0x51,       /* STA $51 */
        0xAD, 0x16, 0x40, /* LDA $4016 */
        0x29, 0x03,       /* AND #$03 */
        0x85, 0x52,       /* STA $52 */
        0x4C, 0x2D, 0x80  /* JMP $802D */
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
    ok &= expect_int("load mach routine rom", result, NES_RESULT_OK);
    nes_set_button(&nes, NES_BUTTON_START, 1);
    nes_run_frame(&nes);
    ok &= expect_int("mach routine A released", nes_cpu_read(&nes, 0x0054), 0);
    ok &= expect_int("mach routine B released", nes_cpu_read(&nes, 0x0055), 0);
    ok &= expect_int("mach routine SELECT released", nes_cpu_read(&nes, 0x0051), 0);
    ok &= expect_int("mach routine START pressed", nes_cpu_read(&nes, 0x0052), 1);
    nes_shutdown(&nes);
    return ok;
}

static int test_ppuctrl_nmi_rising_edge(void)
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
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load nmi edge rom", result, NES_RESULT_OK);

    nes.ppu.status = 0x80u;
    nes.ppu.ctrl = 0x00u;
    nes.cpu.nmi_pending = 0;
    nes_cpu_write(&nes, 0x2000, 0x80);
    ok &= expect_int("ppuctrl nmi rising edge", nes.cpu.nmi_pending, 1);

    nes.cpu.nmi_pending = 0;
    nes_cpu_write(&nes, 0x2000, 0x80);
    ok &= expect_int("ppuctrl nmi held high", nes.cpu.nmi_pending, 0);

    nes_cpu_write(&nes, 0x2000, 0x00);
    nes.cpu.nmi_pending = 0;
    nes_cpu_write(&nes, 0x2000, 0x80);
    ok &= expect_int("ppuctrl nmi re-enabled", nes.cpu.nmi_pending, 1);

    nes_shutdown(&nes);
    return ok;
}

static int test_vblank_poll_can_suppress_pending_nmi(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0xA9, 0x80,       /* LDA #$80 */
        0x8D, 0x00, 0x20, /* STA $2000 */
        0xAD, 0x02, 0x20, /* wait: LDA $2002 */
        0x10, 0xFB,       /* BPL wait */
        0xA9, 0x5A,       /* LDA #$5A */
        0x85, 0x30,       /* STA $30 */
        0x4C, 0x0E, 0x80  /* JMP $800E */
    };
    uint8_t nmi_handler[] = {
        0xA2, 0x00,       /* LDX #$00 */
        0xA0, 0xFF,       /* outer: LDY #$FF */
        0x88,             /* inner: DEY */
        0xD0, 0xFD,       /* BNE inner */
        0xCA,             /* DEX */
        0xD0, 0xF8,       /* BNE outer */
        0x40              /* RTI */
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
    memcpy(&rom[16 + 0x0100], nmi_handler, sizeof(nmi_handler));
    rom[16 + 0x3FFA] = 0x00;
    rom[16 + 0x3FFB] = 0x81;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load vblank poll rom", result, NES_RESULT_OK);
    nes.cpu.pc = 0x8008u;
    nes.cpu.p = 0x20u;
    nes.ppu.ctrl = 0x80u;
    nes.ppu.scanline = 240;
    nes.ppu.cycle = 338;
    nes_run_frame(&nes);
    ok &= expect_int("vblank poll nmi pending", nes.cpu.nmi_pending, 1);
    nes_run_frame(&nes);
    ok &= expect_int("vblank poll continues", nes_cpu_read(&nes, 0x0030), 0x5A);
    nes_shutdown(&nes);
    return ok;
}

static int test_oam_dma_cycle_parity(void)
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
    ok &= expect_int("load oam dma rom", result, NES_RESULT_OK);
    nes.cpu.cycles = 0;
    nes_cpu_write(&nes, 0x4014, 0x00);
    ok &= expect_int("oam dma even cycles", nes.cpu.extra_cycles, 513);
    nes.cpu.extra_cycles = 0;
    nes.cpu.cycles = 1;
    nes_cpu_write(&nes, 0x4014, 0x00);
    ok &= expect_int("oam dma odd cycles", nes.cpu.extra_cycles, 514);
    nes.cpu.extra_cycles = 0;
    nes.cpu.cycles = 2;
    nes.cpu.io_write_delay = 3;
    nes_cpu_write(&nes, 0x4014, 0x00);
    ok &= expect_int("oam dma delayed odd write", nes.cpu.extra_cycles, 514);
    nes.cpu.extra_cycles = 0;
    nes.cpu.cycles = 1;
    nes.cpu.io_write_delay = 3;
    nes_cpu_write(&nes, 0x4014, 0x00);
    ok &= expect_int("oam dma delayed even write", nes.cpu.extra_cycles, 513);
    nes_shutdown(&nes);
    return ok;
}

static int test_ppu_delays_writes_only_during_visible_rendering(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int visible_position;

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
    ok &= expect_int("load ppu delay rom", result, NES_RESULT_OK);

    nes.ppu.mask = 0x1Eu;
    nes.ppu.scanline = 248;
    nes.ppu.cycle = 300;
    nes.cpu.io_write_delay = 3;
    nes_cpu_write(&nes, 0x2006, 0x20);
    nes_cpu_write(&nes, 0x2006, 0x00);
    nes_cpu_write(&nes, 0x2007, 0xAA);
    nes_cpu_write(&nes, 0x2007, 0xBB);
    ok &= expect_int("vblank ppu write is immediate", nes.ppu.pending_write, 0);
    ok &= expect_int("vblank ppu write first byte", nes.ppu.nametable[0], 0xAA);
    ok &= expect_int("vblank ppu write second byte", nes.ppu.nametable[1], 0xBB);

    nes.ppu.scanline = 100;
    nes.ppu.cycle = 20;
    nes.cpu.io_write_delay = 3;
    nes_cpu_write(&nes, 0x2005, 0x12);
    visible_position = 100 * 341 + 20 + 9;
    ok &= expect_int("visible ppu write is delayed", nes.ppu.pending_write, 1);
    ok &= expect_int("visible ppu write position", nes.ppu.pending_write_position, visible_position);
    ok &= expect_int("visible ppu write not yet applied", nes.ppu.scroll_x, 0);

    nes_shutdown(&nes);
    return ok;
}

static int test_ppu_render_uses_v_scroll_address(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int row;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[16] = 0x02;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;
    for (row = 0; row < 8; ++row) {
        rom[16 + NESEMU_PRG_BANK_SIZE + 16 + row] = 0xFF;
    }

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load v scroll render rom", result, NES_RESULT_OK);
    nes.ppu.mask = 0x0Au;
    nes.ppu.v = 0x2001u;
    nes.ppu.t = 0x2001u;
    nes.ppu.scanline = 0;
    nes.ppu.cycle = 0;
    nes.ppu.nametable[0] = 0;
    nes.ppu.nametable[1] = 1;
    nes.ppu.palette[0] = 0x0Fu;
    nes.ppu.palette[1] = 0x30u;
    nes_run_frame(&nes);
    ok &= expect_int("v scroll starts from coarse x", nes.ppu.framebuffer[0] != nes.ppu.framebuffer[8], 1);
    nes_shutdown(&nes);
    return ok;
}

static int test_sprite_behind_background_blocks_later_sprites(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int row;
    int tile;

    memset(rom, 0xEA, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = 0;
    rom[7] = 0;
    rom[16] = 0x4C;
    rom[17] = 0x00;
    rom[18] = 0x80;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;
    for (row = 0; row < 8; ++row) {
        rom[16 + NESEMU_PRG_BANK_SIZE + 1u * 16u + row] = 0xFFu;
        rom[16 + NESEMU_PRG_BANK_SIZE + 1u * 16u + 8u + row] = 0x00u;
        rom[16 + NESEMU_PRG_BANK_SIZE + 2u * 16u + row] = 0xFFu;
        rom[16 + NESEMU_PRG_BANK_SIZE + 2u * 16u + 8u + row] = 0x00u;
        rom[16 + NESEMU_PRG_BANK_SIZE + 3u * 16u + row] = 0xFFu;
        rom[16 + NESEMU_PRG_BANK_SIZE + 3u * 16u + 8u + row] = 0x00u;
    }

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load sprite priority rom", result, NES_RESULT_OK);
    nes.ppu.mask = 0x1Eu;
    for (tile = 0; tile < 2048; ++tile) {
        nes.ppu.nametable[tile] = 1;
    }
    nes.ppu.palette[1] = 0x30u;
    nes.ppu.palette[5] = 0x30u;
    nes.ppu.palette[9] = 0x30u;
    nes.ppu.palette[13] = 0x30u;
    nes.ppu.palette[0x11] = 0x16u;
    memset(nes.ppu.oam, 0xFF, sizeof(nes.ppu.oam));
    nes.ppu.oam[0] = 0;
    nes.ppu.oam[1] = 2;
    nes.ppu.oam[2] = 0x20u;
    nes.ppu.oam[3] = 0;
    nes.ppu.oam[4] = 0;
    nes.ppu.oam[5] = 3;
    nes.ppu.oam[6] = 0x00u;
    nes.ppu.oam[7] = 0;
    nes_run_frame(&nes);
    nes_run_frame(&nes);
    ok &= expect_int("hidden lower sprite restores background",
                     nes.ppu.framebuffer[1u * NESEMU_SCREEN_WIDTH],
                     0xFFFFFFFFu);
    nes_shutdown(&nes);
    return ok;
}

static int test_ppustatus_read_uses_instruction_cycle(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0x2C, 0x02, 0x20, /* BIT $2002 */
        0x08,             /* PHP */
        0x68,             /* PLA */
        0x85, 0x20,       /* STA $20 */
        0x02              /* KIL */
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
    ok &= expect_int("load ppustatus timing rom", result, NES_RESULT_OK);
    nes.ppu.scanline = 40;
    nes.ppu.cycle = 20;
    nes.ppu.mask = 0x18u;
    nes.ppu.status = 0;
    nes.ppu.sprite0_hit_position = 40 * 341 + 29;
    nes_run_frame(&nes);
    ok &= expect_int("bit sees sprite0 at read cycle", nes_cpu_read(&nes, 0x0020) & 0x40, 0x40);
    nes_shutdown(&nes);
    return ok;
}

static int test_ppustatus_read_sees_vblank_start_during_instruction(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t program[] = {
        0x2C, 0x02, 0x20, /* BIT $2002 */
        0x02              /* KIL */
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
    ok &= expect_int("load ppustatus vblank timing rom", result, NES_RESULT_OK);
    nes.ppu.scanline = 240;
    nes.ppu.cycle = 337;
    nes.ppu.ctrl = 0x80u;
    nes.ppu.status = 0;
    nes.ppu.frame_ready = 0;
    ok &= expect_int("bit vblank cycles", nes_cpu_step(&nes), 4);
    ok &= expect_int("bit sees vblank at read cycle", nes.cpu.p & 0x80, 0x80);
    ok &= expect_int("vblank read marks frame ready", nes.ppu.frame_ready, 1);
    ok &= expect_int("vblank read clears status", nes.ppu.status & 0x80, 0);
    ok &= expect_int("vblank read suppresses pending nmi", nes.cpu.nmi_pending, 0);
    nes_shutdown(&nes);
    return ok;
}

static int test_mapper88_namco118_banks(void)
{
    uint8_t rom[TEST_MAPPER88_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_size = NESEMU_PRG_BANK_SIZE * TEST_MAPPER88_PRG_BANKS;
    size_t chr_offset = 16u + prg_size;
    size_t prg_8k_banks = TEST_MAPPER88_PRG_BANKS * 2u;
    size_t chr_1k_banks = TEST_MAPPER88_CHR_BANKS * 8u;
    NesEmu nes;
    NesResult result;
    int ok = 1;
    size_t bank;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER88_PRG_BANKS;
    rom[5] = TEST_MAPPER88_CHR_BANKS;
    rom[6] = 0x81;
    rom[7] = 0x50;
    for (bank = 0; bank < prg_8k_banks; ++bank) {
        rom[prg_offset + bank * 0x2000u] = (uint8_t)(0x80u + bank);
    }
    for (bank = 0; bank < chr_1k_banks; ++bank) {
        rom[chr_offset + bank * 0x0400u] = (uint8_t)bank;
        rom[chr_offset + bank * 0x0400u + 0x03FFu] = (uint8_t)(0x80u | (bank & 0x7Fu));
    }
    rom[prg_offset + (prg_8k_banks - 1u) * 0x2000u + 0x1FFCu] = 0x00;
    rom[prg_offset + (prg_8k_banks - 1u) * 0x2000u + 0x1FFDu] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper88", result, NES_RESULT_OK);
    ok &= expect_int("mapper88 id", nes.rom.mapper_id, 88);
    ok &= expect_int("mapper88 initial mirroring", nes.rom.mirroring, NES_MIRROR_VERTICAL);
    ok &= expect_int("mapper88 initial 8000", nes_cpu_read(&nes, 0x8000), 0x80);
    ok &= expect_int("mapper88 initial A000", nes_cpu_read(&nes, 0xA000), 0x81);
    ok &= expect_int("mapper88 fixed C000", nes_cpu_read(&nes, 0xC000), 0x86);
    ok &= expect_int("mapper88 fixed E000", nes_cpu_read(&nes, 0xE000), 0x87);

    nes_cpu_write(&nes, 0x8000, 0x06);
    nes_cpu_write(&nes, 0x8001, 0x03);
    ok &= expect_int("mapper88 prg r6 bank", nes_cpu_read(&nes, 0x8000), 0x83);
    nes_cpu_write(&nes, 0x8000, 0x07);
    nes_cpu_write(&nes, 0x8001, 0x04);
    ok &= expect_int("mapper88 prg r7 bank", nes_cpu_read(&nes, 0xA000), 0x84);
    nes_cpu_write(&nes, 0x8000, 0x46);
    nes_cpu_write(&nes, 0x8001, 0x02);
    ok &= expect_int("mapper88 ignores prg mode bit", nes_cpu_read(&nes, 0x8000), 0x82);
    ok &= expect_int("mapper88 fixed C000 after prg mode write", nes_cpu_read(&nes, 0xC000), 0x86);
    nes_cpu_write(&nes, 0xA000, 0x00);
    nes_cpu_write(&nes, 0xA001, 0x01);
    ok &= expect_int("mapper88 ignores mirror writes", nes.rom.mirroring, NES_MIRROR_VERTICAL);

    nes_cpu_write(&nes, 0x8000, 0x00);
    nes_cpu_write(&nes, 0x8001, 0x05);
    ok &= expect_int("mapper88 chr r0 even bank", nes_ppu_read(&nes, 0x0000), 0x04);
    ok &= expect_int("mapper88 chr r0 second bank", nes_ppu_read(&nes, 0x0400), 0x05);
    nes_cpu_write(&nes, 0x8000, 0x01);
    nes_cpu_write(&nes, 0x8001, 0x06);
    ok &= expect_int("mapper88 chr r1 even bank", nes_ppu_read(&nes, 0x0800), 0x06);
    ok &= expect_int("mapper88 chr r1 second bank", nes_ppu_read(&nes, 0x0C00), 0x07);
    nes_cpu_write(&nes, 0x8000, 0x02);
    nes_cpu_write(&nes, 0x8001, 0x09);
    ok &= expect_int("mapper88 chr r2 upper half", nes_ppu_read(&nes, 0x1000), 0x49);
    nes_cpu_write(&nes, 0x8000, 0x05);
    nes_cpu_write(&nes, 0x8001, 0x3E);
    ok &= expect_int("mapper88 chr r5 upper half", nes_ppu_read(&nes, 0x1C00), 0x7E);
    ok &= expect_int("mapper88 chr r5 end", nes_ppu_read(&nes, 0x1FFF), 0xFE);

    nes_shutdown(&nes);
    return ok;
}

static int test_mapper88_prg_bank_mask(void)
{
    uint8_t rom[TEST_MAPPER88_PRG_MASK_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_8k_banks = TEST_MAPPER88_PRG_MASK_PRG_BANKS * 2u;
    NesEmu nes;
    NesResult result;
    int ok = 1;
    size_t bank;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER88_PRG_MASK_PRG_BANKS;
    rom[5] = 1;
    rom[6] = 0x80;
    rom[7] = 0x50;
    for (bank = 0; bank < prg_8k_banks; ++bank) {
        rom[prg_offset + bank * 0x2000u] = (uint8_t)(0x80u + bank);
    }
    rom[prg_offset + (prg_8k_banks - 1u) * 0x2000u + 0x1FFCu] = 0x00;
    rom[prg_offset + (prg_8k_banks - 1u) * 0x2000u + 0x1FFDu] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load mapper88 prg mask", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x8000, 0x06);
    nes_cpu_write(&nes, 0x8001, 0x10);
    ok &= expect_int("mapper88 prg r6 masks to 4 bits", nes_cpu_read(&nes, 0x8000), 0x80);
    nes_cpu_write(&nes, 0x8000, 0x07);
    nes_cpu_write(&nes, 0x8001, 0x11);
    ok &= expect_int("mapper88 prg r7 masks to 4 bits", nes_cpu_read(&nes, 0xA000), 0x81);

    nes_shutdown(&nes);
    return ok;
}

static void prepare_ppustatus_timing_rom(uint8_t *rom, const uint8_t *program, size_t program_size)
{
    memset(rom, 0xEA, TEST_ROM_SIZE);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = 0;
    rom[7] = 0;
    memcpy(&rom[16], program, program_size);
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;
}

static void prepare_ppustatus_sprite0_event(NesEmu *nes, int ppu_cycles_until_read)
{
    nes->ppu.scanline = 40;
    nes->ppu.cycle = 20;
    nes->ppu.mask = 0x18u;
    nes->ppu.status = 0;
    nes->ppu.sprite0_hit_position = 40 * 341 + 20 + ppu_cycles_until_read;
}

static int test_ppustatus_load_reads_use_instruction_cycle(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t ldx_abs[] = {
        0xAE, 0x02, 0x20, /* LDX $2002 */
        0x02
    };
    uint8_t ldy_abs[] = {
        0xAC, 0x02, 0x20, /* LDY $2002 */
        0x02
    };
    uint8_t lda_absx[] = {
        0xBD, 0x02, 0x20, /* LDA $2002,X */
        0x02
    };
    uint8_t lda_indy[] = {
        0xB1, 0x10,       /* LDA ($10),Y */
        0x02
    };

    prepare_ppustatus_timing_rom(rom, ldx_abs, sizeof(ldx_abs));
    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load ldx ppustatus timing rom", result, NES_RESULT_OK);
    prepare_ppustatus_sprite0_event(&nes, 9);
    ok &= expect_int("ldx ppustatus cycles", nes_cpu_step(&nes), 4);
    ok &= expect_int("ldx sees sprite0 at read cycle", nes.cpu.x & 0x40, 0x40);
    nes_shutdown(&nes);

    prepare_ppustatus_timing_rom(rom, ldy_abs, sizeof(ldy_abs));
    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load ldy ppustatus timing rom", result, NES_RESULT_OK);
    prepare_ppustatus_sprite0_event(&nes, 9);
    ok &= expect_int("ldy ppustatus cycles", nes_cpu_step(&nes), 4);
    ok &= expect_int("ldy sees sprite0 at read cycle", nes.cpu.y & 0x40, 0x40);
    nes_shutdown(&nes);

    prepare_ppustatus_timing_rom(rom, lda_absx, sizeof(lda_absx));
    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load lda absx ppustatus timing rom", result, NES_RESULT_OK);
    nes.cpu.x = 0;
    prepare_ppustatus_sprite0_event(&nes, 9);
    ok &= expect_int("lda absx ppustatus cycles", nes_cpu_step(&nes), 4);
    ok &= expect_int("lda absx sees sprite0 at read cycle", nes.cpu.a & 0x40, 0x40);
    nes_shutdown(&nes);

    prepare_ppustatus_timing_rom(rom, lda_indy, sizeof(lda_indy));
    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load lda indy ppustatus timing rom", result, NES_RESULT_OK);
    nes.ram[0x10] = 0x02;
    nes.ram[0x11] = 0x20;
    nes.cpu.y = 0;
    prepare_ppustatus_sprite0_event(&nes, 12);
    ok &= expect_int("lda indy ppustatus cycles", nes_cpu_step(&nes), 5);
    ok &= expect_int("lda indy sees sprite0 at read cycle", nes.cpu.a & 0x40, 0x40);
    nes_shutdown(&nes);
    return ok;
}

static int test_inc_dec_flags_use_written_value(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    uint8_t inc_program[] = {
        0xEE, 0x02, 0x20, /* INC $2002 */
        0x08,             /* PHP */
        0x68,             /* PLA */
        0x85, 0x20,       /* STA $20 */
        0x02              /* KIL */
    };
    uint8_t dec_program[] = {
        0xCE, 0x02, 0x20, /* DEC $2002 */
        0x08,             /* PHP */
        0x68,             /* PLA */
        0x85, 0x20,       /* STA $20 */
        0x02              /* KIL */
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
    memcpy(&rom[16], inc_program, sizeof(inc_program));
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load inc ppustatus rom", result, NES_RESULT_OK);
    nes.ppu.status = 0x80u;
    nes_cpu_step(&nes);
    nes_cpu_step(&nes);
    nes_cpu_step(&nes);
    nes_cpu_step(&nes);
    ok &= expect_int("inc flags use written value", nes_cpu_read(&nes, 0x0020) & 0x82, 0x80);
    nes_shutdown(&nes);

    memcpy(&rom[16], dec_program, sizeof(dec_program));
    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load dec ppustatus rom", result, NES_RESULT_OK);
    nes.ppu.status = 0x01u;
    nes_cpu_step(&nes);
    nes_cpu_step(&nes);
    nes_cpu_step(&nes);
    nes_cpu_step(&nes);
    ok &= expect_int("dec flags use written value", nes_cpu_read(&nes, 0x0020) & 0x82, 0x02);
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

static int test_apu_envelope_and_linear_counters(void)
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
    ok &= expect_int("load apu envelope rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4015, 0x05);
    nes_cpu_write(&nes, 0x4000, 0x00);
    nes_cpu_write(&nes, 0x4003, 0x08);
    nes_cpu_write(&nes, 0x4008, 0x02);
    nes_cpu_write(&nes, 0x400B, 0x08);
    nes_run_frame(&nes);
    ok &= expect_int("pulse envelope decays", nes.apu.envelope_decay[0] < 15, 1);
    ok &= expect_int("triangle linear counts down", nes.apu.triangle_linear_counter < 2, 1);
    nes_shutdown(&nes);
    return ok;
}

static int test_pulse_sweep_updates_timer(void)
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
    ok &= expect_int("load pulse sweep rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4015, 0x01);
    nes_cpu_write(&nes, 0x4002, 0x00);
    nes_cpu_write(&nes, 0x4003, 0x09);
    nes_cpu_write(&nes, 0x4001, 0x81);
    nes_cpu_write(&nes, 0x4017, 0x80);
    ok &= expect_int("pulse positive sweep low", nes.apu.regs[2], 0x80);
    ok &= expect_int("pulse positive sweep high", nes.apu.regs[3] & 0x07, 0x01);

    nes_cpu_write(&nes, 0x4002, 0x00);
    nes_cpu_write(&nes, 0x4003, 0x09);
    nes_cpu_write(&nes, 0x4001, 0x89);
    nes_cpu_write(&nes, 0x4017, 0x80);
    ok &= expect_int("pulse channel 1 negative sweep low", nes.apu.regs[2], 0x7F);
    ok &= expect_int("pulse channel 1 negative sweep high", nes.apu.regs[3] & 0x07, 0x00);
    nes_shutdown(&nes);
    return ok;
}

static int test_apu_timer_boundary_and_status_mask(void)
{
    NesEmu nes;
    int ok = 1;

    nes_init(&nes);
    nes.apu.regs[0x02] = 0;
    nes.apu.regs[0x03] = 0;
    nes.apu.regs[0x0E] = 0;
    nes_apu_clock_audio(&nes, 4);
    ok &= expect_int("pulse timer exact boundary steps", nes.apu.pulse_sequence_step[0], 2);
    ok &= expect_int("noise timer exact boundary steps", nes.apu.noise_lfsr, 0x4000);
    nes_shutdown(&nes);

    nes_init(&nes);
    nes.apu.regs[0x0A] = 2;
    nes.apu.regs[0x0B] = 0;
    nes.apu.status = 0x04u;
    nes.apu.length_counter[2] = 1;
    nes.apu.triangle_linear_counter = 1;
    nes_apu_clock_audio(&nes, 6);
    ok &= expect_int("triangle timer exact boundary steps", nes.apu.triangle_sequence_step, 2);
    nes_shutdown(&nes);

    nes_init(&nes);
    nes_apu_write(&nes, 0x4015u, 0xE0u);
    ok &= expect_int("apu status masks write-only bits", nes.apu.status, 0);
    nes_apu_write(&nes, 0x4015u, 0xFFu);
    ok &= expect_int("apu status keeps channel enable bits", nes.apu.status, 0x1F);
    nes_shutdown(&nes);
    return ok;
}

static int test_apu_frame_irq_drives_irq_vector(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    NesEmu nes;
    NesResult result;
    int ok = 1;
    int frame;
    uint8_t program[] = {
        0x58,             /* CLI */
        0xA9, 0x00,       /* LDA #$00 */
        0x8D, 0x17, 0x40, /* STA $4017: 4-step frame IRQ enabled */
        0x4C, 0x06, 0x80  /* JMP $8006 */
    };
    uint8_t irq_handler[] = {
        0xAD, 0x15, 0x40, /* LDA $4015: acknowledge frame IRQ */
        0xE6, 0x20,       /* INC $20 */
        0x40              /* RTI */
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
    memcpy(&rom[16 + 0x0100], irq_handler, sizeof(irq_handler));
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;
    rom[16 + 0x3FFE] = 0x00;
    rom[16 + 0x3FFF] = 0x81;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load apu irq rom", result, NES_RESULT_OK);
    for (frame = 0; frame < 8; ++frame) {
        nes_run_frame(&nes);
    }
    ok &= expect_int("apu frame irq reached handler", nes_cpu_read(&nes, 0x0020) != 0, 1);
    ok &= expect_int("apu frame irq acknowledged", nes.apu.frame_irq, 0);
    nes_shutdown(&nes);
    return ok;
}

static int test_apu_frame_counter_event_timing(void)
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
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load apu frame timing rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4017, 0x00);
    nes_apu_clock_frame_counter(&nes, 29827);
    ok &= expect_int("frame irq before event", nes.apu.frame_irq, 0);
    nes_apu_clock_frame_counter(&nes, 1);
    ok &= expect_int("frame irq first event", nes.apu.frame_irq, 1);
    ok &= expect_int("frame irq status bit", nes_cpu_read(&nes, 0x4015) & 0x40, 0x40);
    ok &= expect_int("frame irq acknowledged by status read", nes.apu.frame_irq, 0);
    nes_apu_clock_frame_counter(&nes, 1);
    ok &= expect_int("frame irq second event", nes.apu.frame_irq, 1);
    ok &= expect_int("frame irq second status bit", nes_cpu_read(&nes, 0x4015) & 0x40, 0x40);
    ok &= expect_int("frame irq second acknowledge", nes.apu.frame_irq, 0);
    nes_apu_clock_frame_counter(&nes, 1);
    ok &= expect_int("frame irq third event", nes.apu.frame_irq, 1);
    ok &= expect_int("frame irq third status bit", nes_cpu_read(&nes, 0x4015) & 0x40, 0x40);
    ok &= expect_int("frame irq third acknowledge", nes.apu.frame_irq, 0);
    ok &= expect_int("frame counter wraps after third irq", nes.apu.frame_counter_cycles, 0);

    nes_cpu_write(&nes, 0x4017, 0xC0);
    nes_apu_clock_frame_counter(&nes, 37282 * 2);
    ok &= expect_int("5-step frame irq inhibited", nes.apu.frame_irq, 0);
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

    memset(rom, 0xEA, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 1;
    rom[6] = 0;
    rom[7] = 0;
    rom[16 + 0x40] = 0xFF;
    rom[16 + 0x41] = 0xFF;
    rom[16 + 0x42] = 0xFF;
    rom[16 + 0x43] = 0xFF;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load dmc rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4010, 0x0F);
    nes_cpu_write(&nes, 0x4011, 0x40);
    nes_cpu_write(&nes, 0x4012, 0x01);
    nes_cpu_write(&nes, 0x4013, 0x00);
    nes_cpu_write(&nes, 0x4015, 0x10);
    nes_run_frame(&nes);
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

static int test_dmc_irq_status_and_acknowledge(void)
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
    rom[16 + 0x40] = 0xFF;
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load dmc irq rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4017, 0x40);
    nes_cpu_write(&nes, 0x4010, 0x8F);
    nes_cpu_write(&nes, 0x4012, 0x01);
    nes_cpu_write(&nes, 0x4013, 0x00);
    nes_cpu_write(&nes, 0x4015, 0x10);
    nes_run_frame(&nes);
    ok &= expect_int("dmc irq pending", nes.apu.dmc_irq, 1);
    ok &= expect_int("dmc irq drives cpu irq", nes.cpu.irq_pending, 1);
    ok &= expect_int("dmc irq status bit", nes_cpu_read(&nes, 0x4015) & 0x80, 0x80);
    ok &= expect_int("dmc irq status read does not ack", nes_cpu_read(&nes, 0x4015) & 0x80, 0x80);
    nes_cpu_write(&nes, 0x4010, 0x0F);
    ok &= expect_int("dmc irq cleared by irq disable", nes_cpu_read(&nes, 0x4015) & 0x80, 0);

    nes_cpu_write(&nes, 0x4010, 0x8F);
    nes_cpu_write(&nes, 0x4015, 0x10);
    nes_run_frame(&nes);
    ok &= expect_int("dmc irq pending again", nes.apu.dmc_irq, 1);
    nes_cpu_write(&nes, 0x4015, 0x10);
    ok &= expect_int("dmc irq cleared by status write", nes.apu.dmc_irq, 0);
    ok &= expect_int("dmc irq line cleared", nes.cpu.irq_pending, 0);
    nes_shutdown(&nes);
    return ok;
}

static int test_apu_timer_sequences_drive_channels(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    int16_t samples[512];
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
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load apu timer rom", result, NES_RESULT_OK);
    nes_cpu_write(&nes, 0x4017, 0xC0);
    nes_cpu_write(&nes, 0x4015, 0x0F);
    nes_cpu_write(&nes, 0x4000, 0x3F);
    nes_cpu_write(&nes, 0x4002, 0x20);
    nes_cpu_write(&nes, 0x4003, 0x08);
    nes_cpu_write(&nes, 0x4008, 0xFF);
    nes_cpu_write(&nes, 0x400A, 0x08);
    nes_cpu_write(&nes, 0x400B, 0x08);
    nes_cpu_write(&nes, 0x400C, 0x1F);
    nes_cpu_write(&nes, 0x400E, 0x00);
    nes_cpu_write(&nes, 0x400F, 0x08);
    nes_run_frame(&nes);
    nes_render_audio(&nes, samples, sizeof(samples) / sizeof(samples[0]), NESEMU_AUDIO_RATE);
    ok &= expect_int("pulse sequencer advanced", nes.apu.pulse_sequence_step[0] != 0, 1);
    ok &= expect_int("triangle sequencer advanced", nes.apu.triangle_sequence_step != 0, 1);
    ok &= expect_int("noise lfsr advanced", nes.apu.noise_lfsr != 1, 1);
    ok &= expect_int("timer channels produced sample",
                     audio_has_signal(samples, sizeof(samples) / sizeof(samples[0])),
                     1);
    nes_shutdown(&nes);
    return ok;
}

static int test_audio_underrun_holds_last_sample(void)
{
    uint8_t rom[TEST_ROM_SIZE];
    int16_t samples[4];
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
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load audio underrun rom", result, NES_RESULT_OK);
    nes.apu.sample_count = 0;
    nes.apu.last_render_sample = 1234;
    nes_render_audio(&nes, samples, sizeof(samples) / sizeof(samples[0]), NESEMU_AUDIO_RATE);
    ok &= expect_int("underrun sample 0", samples[0], 1234);
    ok &= expect_int("underrun sample 1", samples[1], 1234);
    ok &= expect_int("underrun sample 2", samples[2], 1234);
    ok &= expect_int("underrun sample 3", samples[3], 1234);
    nes_shutdown(&nes);
    return ok;
}

static int test_mapper19_n163_audio(void)
{
    uint8_t rom[TEST_MAPPER19_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_8k_banks = TEST_MAPPER19_PRG_BANKS * 2u;
    size_t fixed_offset = prg_offset + (prg_8k_banks - 1u) * 0x2000u;
    uint8_t waveform[] = { 0xF0, 0xF0 };
    uint8_t channel8[] = {
        0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x0F
    };
    int16_t samples[512];
    NesEmu nes;
    NesResult result;
    int ok = 1;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER19_PRG_BANKS;
    rom[5] = TEST_MAPPER19_CHR_BANKS;
    rom[6] = 0x31;
    rom[7] = 0x10;
    rom[fixed_offset] = 0x4C;
    rom[fixed_offset + 1] = 0x00;
    rom[fixed_offset + 2] = 0xE0;
    rom[fixed_offset + 0x1FFCu] = 0x00;
    rom[fixed_offset + 0x1FFDu] = 0xE0;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load n163 audio rom", result, NES_RESULT_OK);
    mapper19_write_chip_ram(&nes, 0x00u, waveform, sizeof(waveform));
    mapper19_write_chip_ram(&nes, 0x78u, channel8, sizeof(channel8));
    nes_cpu_write(&nes, 0xE000u, 0x00u);
    nes_run_frame(&nes);
    nes_render_audio(&nes, samples, sizeof(samples) / sizeof(samples[0]), NESEMU_AUDIO_RATE);
    ok &= expect_int("n163 audio produced sample",
                     audio_has_signal(samples, sizeof(samples) / sizeof(samples[0])),
                     1);
    nes_shutdown(&nes);

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load n163 mute rom", result, NES_RESULT_OK);
    mapper19_write_chip_ram(&nes, 0x00u, waveform, sizeof(waveform));
    mapper19_write_chip_ram(&nes, 0x78u, channel8, sizeof(channel8));
    nes_cpu_write(&nes, 0xE000u, 0x40u);
    nes_run_frame(&nes);
    nes_render_audio(&nes, samples, sizeof(samples) / sizeof(samples[0]), NESEMU_AUDIO_RATE);
    ok &= expect_int("n163 disable bit mutes",
                     audio_has_signal(samples, sizeof(samples) / sizeof(samples[0])),
                     0);
    nes_shutdown(&nes);
    return ok;
}

static int test_mapper19_n163_channel_count_encoding(void)
{
    uint8_t rom[TEST_MAPPER19_ROM_SIZE];
    size_t prg_offset = 16u;
    size_t prg_8k_banks = TEST_MAPPER19_PRG_BANKS * 2u;
    size_t fixed_offset = prg_offset + (prg_8k_banks - 1u) * 0x2000u;
    uint8_t waveform[] = { 0xFF, 0xFF };
    uint8_t first_channel[] = {
        0x01, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x0F
    };
    uint8_t second_channel[] = {
        0x01, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x0F
    };
    uint8_t two_channel_count = 0x1F;
    NesEmu nes;
    NesResult result;
    int ok = 1;

    memset(rom, 0, sizeof(rom));
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = TEST_MAPPER19_PRG_BANKS;
    rom[5] = TEST_MAPPER19_CHR_BANKS;
    rom[6] = 0x31;
    rom[7] = 0x10;
    rom[fixed_offset] = 0x4C;
    rom[fixed_offset + 1] = 0x00;
    rom[fixed_offset + 2] = 0xE0;
    rom[fixed_offset + 0x1FFCu] = 0x00;
    rom[fixed_offset + 0x1FFDu] = 0xE0;

    nes_init(&nes);
    result = nes_load_rom_image(&nes, rom, sizeof(rom));
    ok &= expect_int("load n163 channel count rom", result, NES_RESULT_OK);
    mapper19_write_chip_ram(&nes, 0x00u, waveform, sizeof(waveform));
    mapper19_write_chip_ram(&nes, 0x70u, second_channel, sizeof(second_channel));
    mapper19_write_chip_ram(&nes, 0x78u, first_channel, sizeof(first_channel));
    nes_mapper19_clock_audio(&nes, 30);
    ok &= expect_int("n163 C0 keeps channel 7 muted",
                     nes.mapper.mapper19_audio_output[1],
                     0);
    ok &= expect_int("n163 C0 clocks channel 8",
                     nes.mapper.mapper19_audio_output[0] != 0,
                     1);
    mapper19_write_chip_ram(&nes, 0x7Fu, &two_channel_count, 1);
    nes_mapper19_clock_audio(&nes, 30);
    ok &= expect_int("n163 C1 clocks channel 7",
                     nes.mapper.mapper19_audio_output[1] != 0,
                     1);
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
    ok &= test_mapper3_chr_bank_switch();
    ok &= test_mapper4_bank_switch_and_irq();
    ok &= test_mapper4_irq_requires_a12_rise();
    ok &= test_mapper10_mmc4_latches();
    ok &= test_mapper19_bank_switch_nt_and_irq();
    ok &= test_mapper88_namco118_banks();
    ok &= test_mapper88_prg_bank_mask();
    ok &= test_cpu_executes_program();
    ok &= test_ora_opcodes();
    ok &= test_joypad_shift();
    ok &= test_balloon_fight_controller_poll_shape();
    ok &= test_balloon_fight_controller_routine();
    ok &= test_mach_rider_controller_routine();
    ok &= test_ppuctrl_nmi_rising_edge();
    ok &= test_vblank_poll_can_suppress_pending_nmi();
    ok &= test_oam_dma_cycle_parity();
    ok &= test_ppu_delays_writes_only_during_visible_rendering();
    ok &= test_ppu_render_uses_v_scroll_address();
    ok &= test_sprite_behind_background_blocks_later_sprites();
    ok &= test_ppustatus_read_uses_instruction_cycle();
    ok &= test_ppustatus_read_sees_vblank_start_during_instruction();
    ok &= test_ppustatus_load_reads_use_instruction_cycle();
    ok &= test_inc_dec_flags_use_written_value();
    ok &= test_unofficial_opcodes();
    ok &= test_apu_length_counter();
    ok &= test_apu_envelope_and_linear_counters();
    ok &= test_pulse_sweep_updates_timer();
    ok &= test_apu_timer_boundary_and_status_mask();
    ok &= test_apu_frame_irq_drives_irq_vector();
    ok &= test_apu_frame_counter_event_timing();
    ok &= test_dmc_playback_progresses();
    ok &= test_dmc_irq_status_and_acknowledge();
    ok &= test_apu_timer_sequences_drive_channels();
    ok &= test_audio_underrun_holds_last_sample();
    ok &= test_mapper19_n163_audio();
    ok &= test_mapper19_n163_channel_count_encoding();
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
