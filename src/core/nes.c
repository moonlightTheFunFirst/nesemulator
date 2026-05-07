#include "nesemu.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    INES_HEADER_SIZE = 16,
    INES_TRAINER_SIZE = 512,
    CPU_CLOCK_NTSC = 1789773
};

enum {
    CPU_C = 0x01,
    CPU_Z = 0x02,
    CPU_I = 0x04,
    CPU_D = 0x08,
    CPU_B = 0x10,
    CPU_U = 0x20,
    CPU_V = 0x40,
    CPU_N = 0x80
};

static const uint32_t nes_palette_rgb[64] = {
    0xFF666666u, 0xFF002A88u, 0xFF1412A7u, 0xFF3B00A4u,
    0xFF5C007Eu, 0xFF6E0040u, 0xFF6C0700u, 0xFF561D00u,
    0xFF333500u, 0xFF0B4800u, 0xFF005200u, 0xFF004F08u,
    0xFF00404Du, 0xFF000000u, 0xFF000000u, 0xFF000000u,
    0xFFADADADu, 0xFF155FD9u, 0xFF4240FFu, 0xFF7527FEu,
    0xFFA01ACCu, 0xFFB71E7Bu, 0xFFB53120u, 0xFF994E00u,
    0xFF6B6D00u, 0xFF388700u, 0xFF0D9300u, 0xFF008F32u,
    0xFF007C8Du, 0xFF000000u, 0xFF000000u, 0xFF000000u,
    0xFFFFFFFFu, 0xFF64B0FFu, 0xFF9290FFu, 0xFFC676FFu,
    0xFFF36AFFu, 0xFFFE6ECCu, 0xFFFE8170u, 0xFFEA9E22u,
    0xFFBCBE00u, 0xFF88D800u, 0xFF5CE430u, 0xFF45E082u,
    0xFF48CDDEu, 0xFF4F4F4Fu, 0xFF000000u, 0xFF000000u,
    0xFFFFFFFFu, 0xFFC0DFFFu, 0xFFD3D2FFu, 0xFFE8C8FFu,
    0xFFFBC2FFu, 0xFFFEC4EAu, 0xFFFECCC5u, 0xFFF7D8A5u,
    0xFFE4E594u, 0xFFCFEE96u, 0xFFBDF4ABu, 0xFFB3F3CCu,
    0xFFB5EBF2u, 0xFFB8B8B8u, 0xFF000000u, 0xFF000000u
};

static const int noise_periods[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

static void nes_mapper_clear(NesMapper *mapper)
{
    if (mapper->prg_rom != NULL) {
        free(mapper->prg_rom);
    }
    if (mapper->chr_mem != NULL) {
        free(mapper->chr_mem);
    }
    memset(mapper, 0, sizeof(*mapper));
}

static void cpu_set_zn(NesEmu *nes, uint8_t value)
{
    if (value == 0) {
        nes->cpu.p |= CPU_Z;
    } else {
        nes->cpu.p &= (uint8_t)~CPU_Z;
    }
    if ((value & 0x80u) != 0) {
        nes->cpu.p |= CPU_N;
    } else {
        nes->cpu.p &= (uint8_t)~CPU_N;
    }
}

static void cpu_set_flag(NesEmu *nes, uint8_t flag, int value)
{
    if (value) {
        nes->cpu.p |= flag;
    } else {
        nes->cpu.p &= (uint8_t)~flag;
    }
    nes->cpu.p |= CPU_U;
}

static uint8_t cpu_get_flag(const NesEmu *nes, uint8_t flag)
{
    return (uint8_t)((nes->cpu.p & flag) != 0);
}

static uint8_t palette_read(const NesPpu *ppu, uint16_t address)
{
    uint16_t index = address & 0x1Fu;

    if (index == 0x10u) {
        index = 0x00u;
    } else if (index == 0x14u) {
        index = 0x04u;
    } else if (index == 0x18u) {
        index = 0x08u;
    } else if (index == 0x1Cu) {
        index = 0x0Cu;
    }
    return ppu->palette[index] & 0x3Fu;
}

static void palette_write(NesPpu *ppu, uint16_t address, uint8_t value)
{
    uint16_t index = address & 0x1Fu;

    if (index == 0x10u) {
        index = 0x00u;
    } else if (index == 0x14u) {
        index = 0x04u;
    } else if (index == 0x18u) {
        index = 0x08u;
    } else if (index == 0x1Cu) {
        index = 0x0Cu;
    }
    ppu->palette[index] = (uint8_t)(value & 0x3Fu);
}

static uint16_t nametable_index(const NesEmu *nes, uint16_t address)
{
    uint16_t offset = (uint16_t)((address - 0x2000u) & 0x0FFFu);
    uint16_t table = offset / 0x400u;
    uint16_t inner = offset & 0x03FFu;

    if (nes->rom.mirroring == NES_MIRROR_FOUR_SCREEN) {
        return (uint16_t)(offset & 0x07FFu);
    }
    if (nes->rom.mirroring == NES_MIRROR_VERTICAL) {
        table &= 1u;
    } else {
        table = (uint16_t)((table >> 1) & 1u);
    }
    return (uint16_t)(table * 0x400u + inner);
}

uint8_t nes_ppu_read(const NesEmu *nes, uint16_t address)
{
    address &= 0x3FFFu;
    if (address < 0x2000u) {
        if (!nes->rom_loaded || nes->mapper.chr_mem_size == 0) {
            return 0xFFu;
        }
        return nes->mapper.chr_mem[address % nes->mapper.chr_mem_size];
    }
    if (address < 0x3F00u) {
        return nes->ppu.nametable[nametable_index(nes, address)];
    }
    return palette_read(&nes->ppu, address);
}

void nes_ppu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    address &= 0x3FFFu;
    if (address < 0x2000u) {
        if (nes->rom_loaded && nes->mapper.chr_is_ram && nes->mapper.chr_mem_size != 0) {
            nes->mapper.chr_mem[address % nes->mapper.chr_mem_size] = value;
        }
        return;
    }
    if (address < 0x3F00u) {
        nes->ppu.nametable[nametable_index(nes, address)] = value;
        return;
    }
    palette_write(&nes->ppu, address, value);
}

static uint8_t ppu_read_register(NesEmu *nes, uint16_t address)
{
    NesPpu *ppu = &nes->ppu;
    uint8_t value;

    switch (address & 7u) {
    case 2:
        value = ppu->status;
        ppu->status &= (uint8_t)~0x80u;
        ppu->write_latch = 0;
        nes->cpu.nmi_pending = 0;
        return value;
    case 4:
        return ppu->oam[ppu->oam_addr];
    case 7:
        value = nes_ppu_read(nes, ppu->v);
        if ((ppu->v & 0x3FFFu) < 0x3F00u) {
            uint8_t buffered = ppu->data_buffer;
            ppu->data_buffer = value;
            value = buffered;
        } else {
            ppu->data_buffer = nes_ppu_read(nes, (uint16_t)(ppu->v - 0x1000u));
        }
        ppu->v = (uint16_t)(ppu->v + ((ppu->ctrl & 0x04u) ? 32u : 1u));
        return value;
    default:
        return 0;
    }
}

static void ppu_write_register(NesEmu *nes, uint16_t address, uint8_t value)
{
    NesPpu *ppu = &nes->ppu;

    switch (address & 7u) {
    case 0:
        ppu->ctrl = value;
        ppu->t = (uint16_t)((ppu->t & 0xF3FFu) | ((uint16_t)(value & 0x03u) << 10));
        if ((value & 0x80u) != 0 && (ppu->status & 0x80u) != 0) {
            nes->cpu.nmi_pending = 1;
        }
        break;
    case 1:
        ppu->mask = value;
        break;
    case 3:
        ppu->oam_addr = value;
        break;
    case 4:
        ppu->oam[ppu->oam_addr++] = value;
        break;
    case 5:
        if (!ppu->write_latch) {
            ppu->fine_x = (uint8_t)(value & 0x07u);
            ppu->scroll_x = value;
            ppu->t = (uint16_t)((ppu->t & 0xFFE0u) | ((uint16_t)value >> 3));
            ppu->write_latch = 1;
        } else {
            ppu->scroll_y = value;
            ppu->t = (uint16_t)((ppu->t & 0x8C1Fu) |
                                (((uint16_t)value & 0x07u) << 12) |
                                (((uint16_t)value & 0xF8u) << 2));
            ppu->write_latch = 0;
        }
        break;
    case 6:
        if (!ppu->write_latch) {
            ppu->t = (uint16_t)((ppu->t & 0x00FFu) | (((uint16_t)value & 0x3Fu) << 8));
            ppu->write_latch = 1;
        } else {
            ppu->t = (uint16_t)((ppu->t & 0x7F00u) | value);
            ppu->v = ppu->t;
            ppu->write_latch = 0;
        }
        break;
    case 7:
        nes_ppu_write(nes, ppu->v, value);
        ppu->v = (uint16_t)(ppu->v + ((ppu->ctrl & 0x04u) ? 32u : 1u));
        break;
    default:
        break;
    }
}

static void apu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (address >= 0x4000u && address <= 0x4017u) {
        nes->apu.regs[address - 0x4000u] = value;
    }
    if (address == 0x4015u) {
        nes->apu.status = value;
    }
}

static uint8_t apu_read(NesEmu *nes, uint16_t address)
{
    if (address == 0x4015u) {
        return (uint8_t)(nes->apu.status & 0x1Fu);
    }
    return 0;
}

static uint8_t cpu_read_bus(NesEmu *nes, uint16_t address)
{
    if (address < 0x2000u) {
        return nes->ram[address & 0x07FFu];
    }
    if (address < 0x4000u) {
        return ppu_read_register(nes, address);
    }
    if (address == 0x4015u) {
        return apu_read(nes, address);
    }
    if (address == 0x4016u) {
        uint8_t value = (uint8_t)(0x40u | (nes->joypad.shift & 1u));
        if (!nes->joypad.strobe) {
            nes->joypad.shift = (uint8_t)(0x80u | (nes->joypad.shift >> 1));
        }
        return value;
    }
    if (address >= 0x6000u && address <= 0x7FFFu) {
        return nes->mapper.prg_ram[address - 0x6000u];
    }
    if (address >= 0x8000u && nes->mapper.prg_rom_size > 0) {
        size_t offset = (size_t)(address - 0x8000u);
        if (nes->mapper.prg_rom_size == NESEMU_PRG_BANK_SIZE) {
            offset %= NESEMU_PRG_BANK_SIZE;
        }
        if (offset < nes->mapper.prg_rom_size) {
            return nes->mapper.prg_rom[offset];
        }
    }
    return 0xFFu;
}

static void cpu_write_bus(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (address < 0x2000u) {
        nes->ram[address & 0x07FFu] = value;
        return;
    }
    if (address < 0x4000u) {
        ppu_write_register(nes, address, value);
        return;
    }
    if (address >= 0x4000u && address <= 0x4013u) {
        apu_write(nes, address, value);
        return;
    }
    if (address == 0x4014u) {
        uint16_t source = (uint16_t)value << 8;
        int i;

        for (i = 0; i < 256; ++i) {
            nes->ppu.oam[nes->ppu.oam_addr++] = cpu_read_bus(nes, (uint16_t)(source + i));
        }
        nes->cpu.extra_cycles += 513;
        return;
    }
    if (address == 0x4015u || address == 0x4017u) {
        apu_write(nes, address, value);
        return;
    }
    if (address == 0x4016u) {
        nes->joypad.strobe = (uint8_t)(value & 1u);
        if (nes->joypad.strobe) {
            nes->joypad.shift = nes->joypad.state;
        }
        return;
    }
    if (address >= 0x6000u && address <= 0x7FFFu) {
        nes->mapper.prg_ram[address - 0x6000u] = value;
    }
}

uint8_t nes_cpu_read(NesEmu *nes, uint16_t address)
{
    if (nes == NULL || !nes->rom_loaded) {
        return 0xFFu;
    }
    return cpu_read_bus(nes, address);
}

void nes_cpu_write(NesEmu *nes, uint16_t address, uint8_t value)
{
    if (nes == NULL || !nes->rom_loaded) {
        return;
    }
    cpu_write_bus(nes, address, value);
}

static uint8_t cpu_fetch8(NesEmu *nes)
{
    uint8_t value = cpu_read_bus(nes, nes->cpu.pc);
    nes->cpu.pc++;
    return value;
}

static uint16_t cpu_fetch16(NesEmu *nes)
{
    uint8_t lo = cpu_fetch8(nes);
    uint8_t hi = cpu_fetch8(nes);
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t cpu_read16(NesEmu *nes, uint16_t address)
{
    uint8_t lo = cpu_read_bus(nes, address);
    uint8_t hi = cpu_read_bus(nes, (uint16_t)(address + 1));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t cpu_read16_bug(NesEmu *nes, uint16_t address)
{
    uint8_t lo = cpu_read_bus(nes, address);
    uint8_t hi = cpu_read_bus(nes, (uint16_t)((address & 0xFF00u) | ((address + 1u) & 0x00FFu)));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static void cpu_push(NesEmu *nes, uint8_t value)
{
    cpu_write_bus(nes, (uint16_t)(0x0100u | nes->cpu.sp), value);
    nes->cpu.sp--;
}

static uint8_t cpu_pull(NesEmu *nes)
{
    nes->cpu.sp++;
    return cpu_read_bus(nes, (uint16_t)(0x0100u | nes->cpu.sp));
}

static uint16_t addr_zp(NesEmu *nes)
{
    return cpu_fetch8(nes);
}

static uint16_t addr_zpx(NesEmu *nes)
{
    return (uint8_t)(cpu_fetch8(nes) + nes->cpu.x);
}

static uint16_t addr_zpy(NesEmu *nes)
{
    return (uint8_t)(cpu_fetch8(nes) + nes->cpu.y);
}

static uint16_t addr_abs(NesEmu *nes)
{
    return cpu_fetch16(nes);
}

static uint16_t addr_absx(NesEmu *nes, int *page_crossed)
{
    uint16_t base = cpu_fetch16(nes);
    uint16_t address = (uint16_t)(base + nes->cpu.x);
    if (page_crossed != NULL) {
        *page_crossed = ((base ^ address) & 0xFF00u) != 0;
    }
    return address;
}

static uint16_t addr_absy(NesEmu *nes, int *page_crossed)
{
    uint16_t base = cpu_fetch16(nes);
    uint16_t address = (uint16_t)(base + nes->cpu.y);
    if (page_crossed != NULL) {
        *page_crossed = ((base ^ address) & 0xFF00u) != 0;
    }
    return address;
}

static uint16_t addr_indx(NesEmu *nes)
{
    uint8_t zp = (uint8_t)(cpu_fetch8(nes) + nes->cpu.x);
    uint8_t lo = cpu_read_bus(nes, zp);
    uint8_t hi = cpu_read_bus(nes, (uint8_t)(zp + 1));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t addr_indy(NesEmu *nes, int *page_crossed)
{
    uint8_t zp = cpu_fetch8(nes);
    uint16_t base = (uint16_t)(cpu_read_bus(nes, zp) | ((uint16_t)cpu_read_bus(nes, (uint8_t)(zp + 1)) << 8));
    uint16_t address = (uint16_t)(base + nes->cpu.y);
    if (page_crossed != NULL) {
        *page_crossed = ((base ^ address) & 0xFF00u) != 0;
    }
    return address;
}

static void op_adc(NesEmu *nes, uint8_t value)
{
    uint16_t sum = (uint16_t)nes->cpu.a + value + cpu_get_flag(nes, CPU_C);
    uint8_t result = (uint8_t)sum;

    cpu_set_flag(nes, CPU_C, sum > 0xFFu);
    cpu_set_flag(nes, CPU_V, (~(nes->cpu.a ^ value) & (nes->cpu.a ^ result) & 0x80u) != 0);
    nes->cpu.a = result;
    cpu_set_zn(nes, nes->cpu.a);
}

static void op_sbc(NesEmu *nes, uint8_t value)
{
    op_adc(nes, (uint8_t)(value ^ 0xFFu));
}

static void op_cmp(NesEmu *nes, uint8_t reg, uint8_t value)
{
    uint8_t result = (uint8_t)(reg - value);

    cpu_set_flag(nes, CPU_C, reg >= value);
    cpu_set_zn(nes, result);
}

static uint8_t op_asl(NesEmu *nes, uint8_t value)
{
    cpu_set_flag(nes, CPU_C, (value & 0x80u) != 0);
    value = (uint8_t)(value << 1);
    cpu_set_zn(nes, value);
    return value;
}

static uint8_t op_lsr(NesEmu *nes, uint8_t value)
{
    cpu_set_flag(nes, CPU_C, (value & 0x01u) != 0);
    value = (uint8_t)(value >> 1);
    cpu_set_zn(nes, value);
    return value;
}

static uint8_t op_rol(NesEmu *nes, uint8_t value)
{
    uint8_t carry = cpu_get_flag(nes, CPU_C);

    cpu_set_flag(nes, CPU_C, (value & 0x80u) != 0);
    value = (uint8_t)((value << 1) | carry);
    cpu_set_zn(nes, value);
    return value;
}

static uint8_t op_ror(NesEmu *nes, uint8_t value)
{
    uint8_t carry = cpu_get_flag(nes, CPU_C);

    cpu_set_flag(nes, CPU_C, (value & 0x01u) != 0);
    value = (uint8_t)((value >> 1) | (carry << 7));
    cpu_set_zn(nes, value);
    return value;
}

static int op_branch(NesEmu *nes, int condition)
{
    int8_t offset = (int8_t)cpu_fetch8(nes);

    if (condition) {
        uint16_t old_pc = nes->cpu.pc;
        nes->cpu.pc = (uint16_t)(nes->cpu.pc + offset);
        return 1 + ((((old_pc ^ nes->cpu.pc) & 0xFF00u) != 0) ? 1 : 0);
    }
    return 0;
}

static void cpu_service_nmi(NesEmu *nes)
{
    nes->cpu.nmi_pending = 0;
    cpu_push(nes, (uint8_t)(nes->cpu.pc >> 8));
    cpu_push(nes, (uint8_t)nes->cpu.pc);
    cpu_push(nes, (uint8_t)((nes->cpu.p & (uint8_t)~CPU_B) | CPU_U));
    cpu_set_flag(nes, CPU_I, 1);
    nes->cpu.pc = cpu_read16(nes, 0xFFFAu);
}

static int cpu_step(NesEmu *nes)
{
    uint8_t opcode;
    uint16_t address;
    int page_crossed = 0;
    int cycles = 0;

    if (nes->cpu.nmi_pending) {
        cpu_service_nmi(nes);
        return 7;
    }

    opcode = cpu_fetch8(nes);
    switch (opcode) {
    case 0x00:
        nes->cpu.pc++;
        cpu_push(nes, (uint8_t)(nes->cpu.pc >> 8));
        cpu_push(nes, (uint8_t)nes->cpu.pc);
        cpu_push(nes, (uint8_t)(nes->cpu.p | CPU_B | CPU_U));
        cpu_set_flag(nes, CPU_I, 1);
        nes->cpu.pc = cpu_read16(nes, 0xFFFEu);
        cycles = 7;
        break;
    case 0x01:
        op_adc(nes, cpu_read_bus(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0x05:
        op_adc(nes, cpu_read_bus(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0x06:
        address = addr_zp(nes);
        cpu_write_bus(nes, address, op_asl(nes, cpu_read_bus(nes, address)));
        cycles = 5;
        break;
    case 0x08:
        cpu_push(nes, (uint8_t)(nes->cpu.p | CPU_B | CPU_U));
        cycles = 3;
        break;
    case 0x09:
        nes->cpu.a |= cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x0A:
        nes->cpu.a = op_asl(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x0D:
        nes->cpu.a |= cpu_read_bus(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x0E:
        address = addr_abs(nes);
        cpu_write_bus(nes, address, op_asl(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x10:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_N));
        break;
    case 0x11:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a |= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0x15:
        nes->cpu.a |= cpu_read_bus(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x16:
        address = addr_zpx(nes);
        cpu_write_bus(nes, address, op_asl(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x18:
        cpu_set_flag(nes, CPU_C, 0);
        cycles = 2;
        break;
    case 0x19:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a |= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x1D:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a |= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x1E:
        address = addr_absx(nes, NULL);
        cpu_write_bus(nes, address, op_asl(nes, cpu_read_bus(nes, address)));
        cycles = 7;
        break;
    case 0x20:
        address = cpu_fetch16(nes);
        cpu_push(nes, (uint8_t)((nes->cpu.pc - 1u) >> 8));
        cpu_push(nes, (uint8_t)(nes->cpu.pc - 1u));
        nes->cpu.pc = address;
        cycles = 6;
        break;
    case 0x21:
        nes->cpu.a &= cpu_read_bus(nes, addr_indx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 6;
        break;
    case 0x24:
        {
            uint8_t value = cpu_read_bus(nes, addr_zp(nes));
            cpu_set_flag(nes, CPU_Z, (nes->cpu.a & value) == 0);
            cpu_set_flag(nes, CPU_N, (value & 0x80u) != 0);
            cpu_set_flag(nes, CPU_V, (value & 0x40u) != 0);
        }
        cycles = 3;
        break;
    case 0x25:
        nes->cpu.a &= cpu_read_bus(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0x26:
        address = addr_zp(nes);
        cpu_write_bus(nes, address, op_rol(nes, cpu_read_bus(nes, address)));
        cycles = 5;
        break;
    case 0x28:
        nes->cpu.p = (uint8_t)((cpu_pull(nes) & (uint8_t)~CPU_B) | CPU_U);
        cycles = 4;
        break;
    case 0x29:
        nes->cpu.a &= cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x2A:
        nes->cpu.a = op_rol(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x2C:
        {
            uint8_t value = cpu_read_bus(nes, addr_abs(nes));
            cpu_set_flag(nes, CPU_Z, (nes->cpu.a & value) == 0);
            cpu_set_flag(nes, CPU_N, (value & 0x80u) != 0);
            cpu_set_flag(nes, CPU_V, (value & 0x40u) != 0);
        }
        cycles = 4;
        break;
    case 0x2D:
        nes->cpu.a &= cpu_read_bus(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x2E:
        address = addr_abs(nes);
        cpu_write_bus(nes, address, op_rol(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x30:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_N));
        break;
    case 0x31:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a &= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0x35:
        nes->cpu.a &= cpu_read_bus(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x36:
        address = addr_zpx(nes);
        cpu_write_bus(nes, address, op_rol(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x38:
        cpu_set_flag(nes, CPU_C, 1);
        cycles = 2;
        break;
    case 0x39:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a &= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x3D:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a &= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x3E:
        address = addr_absx(nes, NULL);
        cpu_write_bus(nes, address, op_rol(nes, cpu_read_bus(nes, address)));
        cycles = 7;
        break;
    case 0x40:
        nes->cpu.p = (uint8_t)((cpu_pull(nes) & (uint8_t)~CPU_B) | CPU_U);
        nes->cpu.pc = cpu_pull(nes);
        nes->cpu.pc |= (uint16_t)cpu_pull(nes) << 8;
        cycles = 6;
        break;
    case 0x41:
        nes->cpu.a ^= cpu_read_bus(nes, addr_indx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 6;
        break;
    case 0x45:
        nes->cpu.a ^= cpu_read_bus(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0x46:
        address = addr_zp(nes);
        cpu_write_bus(nes, address, op_lsr(nes, cpu_read_bus(nes, address)));
        cycles = 5;
        break;
    case 0x48:
        cpu_push(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0x49:
        nes->cpu.a ^= cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x4A:
        nes->cpu.a = op_lsr(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x4C:
        nes->cpu.pc = addr_abs(nes);
        cycles = 3;
        break;
    case 0x4D:
        nes->cpu.a ^= cpu_read_bus(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x4E:
        address = addr_abs(nes);
        cpu_write_bus(nes, address, op_lsr(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x50:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_V));
        break;
    case 0x51:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a ^= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0x55:
        nes->cpu.a ^= cpu_read_bus(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x56:
        address = addr_zpx(nes);
        cpu_write_bus(nes, address, op_lsr(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x58:
        cpu_set_flag(nes, CPU_I, 0);
        cycles = 2;
        break;
    case 0x59:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a ^= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x5D:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a ^= cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x5E:
        address = addr_absx(nes, NULL);
        cpu_write_bus(nes, address, op_lsr(nes, cpu_read_bus(nes, address)));
        cycles = 7;
        break;
    case 0x60:
        nes->cpu.pc = cpu_pull(nes);
        nes->cpu.pc |= (uint16_t)cpu_pull(nes) << 8;
        nes->cpu.pc++;
        cycles = 6;
        break;
    case 0x61:
        op_adc(nes, cpu_read_bus(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0x65:
        op_adc(nes, cpu_read_bus(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0x66:
        address = addr_zp(nes);
        cpu_write_bus(nes, address, op_ror(nes, cpu_read_bus(nes, address)));
        cycles = 5;
        break;
    case 0x68:
        nes->cpu.a = cpu_pull(nes);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x69:
        op_adc(nes, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0x6A:
        nes->cpu.a = op_ror(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x6C:
        nes->cpu.pc = cpu_read16_bug(nes, addr_abs(nes));
        cycles = 5;
        break;
    case 0x6D:
        op_adc(nes, cpu_read_bus(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0x6E:
        address = addr_abs(nes);
        cpu_write_bus(nes, address, op_ror(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x70:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_V));
        break;
    case 0x71:
        address = addr_indy(nes, &page_crossed);
        op_adc(nes, cpu_read_bus(nes, address));
        cycles = 5 + page_crossed;
        break;
    case 0x75:
        op_adc(nes, cpu_read_bus(nes, addr_zpx(nes)));
        cycles = 4;
        break;
    case 0x76:
        address = addr_zpx(nes);
        cpu_write_bus(nes, address, op_ror(nes, cpu_read_bus(nes, address)));
        cycles = 6;
        break;
    case 0x78:
        cpu_set_flag(nes, CPU_I, 1);
        cycles = 2;
        break;
    case 0x79:
        address = addr_absy(nes, &page_crossed);
        op_adc(nes, cpu_read_bus(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0x7D:
        address = addr_absx(nes, &page_crossed);
        op_adc(nes, cpu_read_bus(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0x7E:
        address = addr_absx(nes, NULL);
        cpu_write_bus(nes, address, op_ror(nes, cpu_read_bus(nes, address)));
        cycles = 7;
        break;
    case 0x81:
        cpu_write_bus(nes, addr_indx(nes), nes->cpu.a);
        cycles = 6;
        break;
    case 0x84:
        cpu_write_bus(nes, addr_zp(nes), nes->cpu.y);
        cycles = 3;
        break;
    case 0x85:
        cpu_write_bus(nes, addr_zp(nes), nes->cpu.a);
        cycles = 3;
        break;
    case 0x86:
        cpu_write_bus(nes, addr_zp(nes), nes->cpu.x);
        cycles = 3;
        break;
    case 0x88:
        nes->cpu.y--;
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 2;
        break;
    case 0x8A:
        nes->cpu.a = nes->cpu.x;
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x8C:
        cpu_write_bus(nes, addr_abs(nes), nes->cpu.y);
        cycles = 4;
        break;
    case 0x8D:
        cpu_write_bus(nes, addr_abs(nes), nes->cpu.a);
        cycles = 4;
        break;
    case 0x8E:
        cpu_write_bus(nes, addr_abs(nes), nes->cpu.x);
        cycles = 4;
        break;
    case 0x90:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_C));
        break;
    case 0x91:
        cpu_write_bus(nes, addr_indy(nes, NULL), nes->cpu.a);
        cycles = 6;
        break;
    case 0x94:
        cpu_write_bus(nes, addr_zpx(nes), nes->cpu.y);
        cycles = 4;
        break;
    case 0x95:
        cpu_write_bus(nes, addr_zpx(nes), nes->cpu.a);
        cycles = 4;
        break;
    case 0x96:
        cpu_write_bus(nes, addr_zpy(nes), nes->cpu.x);
        cycles = 4;
        break;
    case 0x98:
        nes->cpu.a = nes->cpu.y;
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x99:
        cpu_write_bus(nes, addr_absy(nes, NULL), nes->cpu.a);
        cycles = 5;
        break;
    case 0x9A:
        nes->cpu.sp = nes->cpu.x;
        cycles = 2;
        break;
    case 0x9D:
        cpu_write_bus(nes, addr_absx(nes, NULL), nes->cpu.a);
        cycles = 5;
        break;
    case 0xA0:
        nes->cpu.y = cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 2;
        break;
    case 0xA1:
        nes->cpu.a = cpu_read_bus(nes, addr_indx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 6;
        break;
    case 0xA2:
        nes->cpu.x = cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 2;
        break;
    case 0xA4:
        nes->cpu.y = cpu_read_bus(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 3;
        break;
    case 0xA5:
        nes->cpu.a = cpu_read_bus(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0xA6:
        nes->cpu.x = cpu_read_bus(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 3;
        break;
    case 0xA8:
        nes->cpu.y = nes->cpu.a;
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 2;
        break;
    case 0xA9:
        nes->cpu.a = cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0xAA:
        nes->cpu.x = nes->cpu.a;
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 2;
        break;
    case 0xAC:
        nes->cpu.y = cpu_read_bus(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 4;
        break;
    case 0xAD:
        nes->cpu.a = cpu_read_bus(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0xAE:
        nes->cpu.x = cpu_read_bus(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 4;
        break;
    case 0xB0:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_C));
        break;
    case 0xB1:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a = cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0xB4:
        nes->cpu.y = cpu_read_bus(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 4;
        break;
    case 0xB5:
        nes->cpu.a = cpu_read_bus(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0xB6:
        nes->cpu.x = cpu_read_bus(nes, addr_zpy(nes));
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 4;
        break;
    case 0xB8:
        cpu_set_flag(nes, CPU_V, 0);
        cycles = 2;
        break;
    case 0xB9:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a = cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0xBA:
        nes->cpu.x = nes->cpu.sp;
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 2;
        break;
    case 0xBC:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.y = cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 4 + page_crossed;
        break;
    case 0xBD:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a = cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0xBE:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.x = cpu_read_bus(nes, address);
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 4 + page_crossed;
        break;
    case 0xC0:
        op_cmp(nes, nes->cpu.y, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0xC1:
        op_cmp(nes, nes->cpu.a, cpu_read_bus(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0xC4:
        op_cmp(nes, nes->cpu.y, cpu_read_bus(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xC5:
        op_cmp(nes, nes->cpu.a, cpu_read_bus(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xC6:
        address = addr_zp(nes);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) - 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 5;
        break;
    case 0xC8:
        nes->cpu.y++;
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 2;
        break;
    case 0xC9:
        op_cmp(nes, nes->cpu.a, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0xCA:
        nes->cpu.x--;
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 2;
        break;
    case 0xCC:
        op_cmp(nes, nes->cpu.y, cpu_read_bus(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xCD:
        op_cmp(nes, nes->cpu.a, cpu_read_bus(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xCE:
        address = addr_abs(nes);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) - 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 6;
        break;
    case 0xD0:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_Z));
        break;
    case 0xD1:
        address = addr_indy(nes, &page_crossed);
        op_cmp(nes, nes->cpu.a, cpu_read_bus(nes, address));
        cycles = 5 + page_crossed;
        break;
    case 0xD5:
        op_cmp(nes, nes->cpu.a, cpu_read_bus(nes, addr_zpx(nes)));
        cycles = 4;
        break;
    case 0xD6:
        address = addr_zpx(nes);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) - 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 6;
        break;
    case 0xD8:
        cpu_set_flag(nes, CPU_D, 0);
        cycles = 2;
        break;
    case 0xD9:
        address = addr_absy(nes, &page_crossed);
        op_cmp(nes, nes->cpu.a, cpu_read_bus(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xDD:
        address = addr_absx(nes, &page_crossed);
        op_cmp(nes, nes->cpu.a, cpu_read_bus(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xDE:
        address = addr_absx(nes, NULL);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) - 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 7;
        break;
    case 0xE0:
        op_cmp(nes, nes->cpu.x, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0xE1:
        op_sbc(nes, cpu_read_bus(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0xE4:
        op_cmp(nes, nes->cpu.x, cpu_read_bus(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xE5:
        op_sbc(nes, cpu_read_bus(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xE6:
        address = addr_zp(nes);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) + 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 5;
        break;
    case 0xE8:
        nes->cpu.x++;
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 2;
        break;
    case 0xE9:
    case 0xEB:
        op_sbc(nes, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0xEA:
        cycles = 2;
        break;
    case 0xEC:
        op_cmp(nes, nes->cpu.x, cpu_read_bus(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xED:
        op_sbc(nes, cpu_read_bus(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xEE:
        address = addr_abs(nes);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) + 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 6;
        break;
    case 0xF0:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_Z));
        break;
    case 0xF1:
        address = addr_indy(nes, &page_crossed);
        op_sbc(nes, cpu_read_bus(nes, address));
        cycles = 5 + page_crossed;
        break;
    case 0xF5:
        op_sbc(nes, cpu_read_bus(nes, addr_zpx(nes)));
        cycles = 4;
        break;
    case 0xF6:
        address = addr_zpx(nes);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) + 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 6;
        break;
    case 0xF8:
        cpu_set_flag(nes, CPU_D, 1);
        cycles = 2;
        break;
    case 0xF9:
        address = addr_absy(nes, &page_crossed);
        op_sbc(nes, cpu_read_bus(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xFD:
        address = addr_absx(nes, &page_crossed);
        op_sbc(nes, cpu_read_bus(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xFE:
        address = addr_absx(nes, NULL);
        cpu_write_bus(nes, address, (uint8_t)(cpu_read_bus(nes, address) + 1u));
        cpu_set_zn(nes, cpu_read_bus(nes, address));
        cycles = 7;
        break;
    case 0x04:
    case 0x44:
    case 0x64:
        cpu_fetch8(nes);
        cycles = 3;
        break;
    case 0x0C:
        cpu_fetch16(nes);
        cycles = 4;
        break;
    case 0x14:
    case 0x34:
    case 0x54:
    case 0x74:
    case 0xD4:
    case 0xF4:
        cpu_fetch8(nes);
        cycles = 4;
        break;
    case 0x1A:
    case 0x3A:
    case 0x5A:
    case 0x7A:
    case 0xDA:
    case 0xFA:
        cycles = 2;
        break;
    case 0x1C:
    case 0x3C:
    case 0x5C:
    case 0x7C:
    case 0xDC:
    case 0xFC:
        addr_absx(nes, &page_crossed);
        cycles = 4 + page_crossed;
        break;
    case 0x80:
    case 0x82:
    case 0x89:
    case 0xC2:
    case 0xE2:
        cpu_fetch8(nes);
        cycles = 2;
        break;
    default:
        cycles = 2;
        break;
    }

    if (nes->cpu.extra_cycles > 0) {
        cycles += nes->cpu.extra_cycles;
        nes->cpu.extra_cycles = 0;
    }
    nes->cpu.p |= CPU_U;
    return cycles;
}

static uint8_t background_pixel(NesEmu *nes, int x, int y, uint8_t *palette_slot)
{
    int base_nt = nes->ppu.ctrl & 0x03;
    int global_x = ((base_nt & 1) * 256 + x + nes->ppu.scroll_x) & 0x1FF;
    int global_y = (((base_nt >> 1) & 1) * 240 + y + nes->ppu.scroll_y) % 480;
    int nt = (global_x / 256) + (global_y / 240) * 2;
    int tile_x = (global_x & 0xFF) / 8;
    int tile_y = (global_y % 240) / 8;
    int fine_y = global_y & 7;
    int fine_x = global_x & 7;
    uint16_t nt_base = (uint16_t)(0x2000u + nt * 0x400u);
    uint8_t tile = nes_ppu_read(nes, (uint16_t)(nt_base + tile_y * 32 + tile_x));
    uint8_t attr = nes_ppu_read(nes, (uint16_t)(nt_base + 0x03C0u + (tile_y / 4) * 8 + (tile_x / 4)));
    int attr_shift = ((tile_y & 2) ? 4 : 0) + ((tile_x & 2) ? 2 : 0);
    int attr_palette = (attr >> attr_shift) & 0x03;
    uint16_t pattern = (uint16_t)(((nes->ppu.ctrl & 0x10u) ? 0x1000u : 0x0000u) + tile * 16 + fine_y);
    uint8_t lo = nes_ppu_read(nes, pattern);
    uint8_t hi = nes_ppu_read(nes, (uint16_t)(pattern + 8));
    uint8_t bit = (uint8_t)(7 - fine_x);
    uint8_t color = (uint8_t)(((lo >> bit) & 1u) | (((hi >> bit) & 1u) << 1));

    *palette_slot = (uint8_t)(attr_palette * 4 + color);
    return color;
}

static uint8_t sprite_pixel(NesEmu *nes, int sprite, int x, int y, uint8_t *palette_slot, int *priority)
{
    const uint8_t *oam = &nes->ppu.oam[sprite * 4];
    int sprite_y = (int)oam[0] + 1;
    int tile = oam[1];
    int attr = oam[2];
    int sprite_x = oam[3];
    int height = (nes->ppu.ctrl & 0x20u) ? 16 : 8;
    int row;
    int col;
    uint16_t pattern;
    uint8_t lo;
    uint8_t hi;
    uint8_t bit;
    uint8_t color;

    if (x < sprite_x || x >= sprite_x + 8 || y < sprite_y || y >= sprite_y + height) {
        return 0;
    }
    row = y - sprite_y;
    col = x - sprite_x;
    if ((attr & 0x80u) != 0) {
        row = height - 1 - row;
    }
    if ((attr & 0x40u) != 0) {
        col = 7 - col;
    }

    if (height == 16) {
        pattern = (uint16_t)((tile & 1) * 0x1000u + (tile & 0xFE) * 16u + (row & 7));
        if (row >= 8) {
            pattern = (uint16_t)(pattern + 16u);
        }
    } else {
        pattern = (uint16_t)(((nes->ppu.ctrl & 0x08u) ? 0x1000u : 0x0000u) + tile * 16 + row);
    }
    lo = nes_ppu_read(nes, pattern);
    hi = nes_ppu_read(nes, (uint16_t)(pattern + 8));
    bit = (uint8_t)(7 - col);
    color = (uint8_t)(((lo >> bit) & 1u) | (((hi >> bit) & 1u) << 1));
    if (color == 0) {
        return 0;
    }

    *palette_slot = (uint8_t)(0x10u + (attr & 0x03u) * 4u + color);
    *priority = (attr & 0x20u) != 0;
    return color;
}

static void render_scanline(NesEmu *nes, int y)
{
    int x;
    uint8_t bg_opaque[NESEMU_SCREEN_WIDTH];

    for (x = 0; x < (int)NESEMU_SCREEN_WIDTH; ++x) {
        uint8_t slot = 0;
        uint8_t color = 0;

        if ((nes->ppu.mask & 0x08u) != 0 && (x >= 8 || (nes->ppu.mask & 0x02u) != 0)) {
            color = background_pixel(nes, x, y, &slot);
        }
        bg_opaque[x] = (uint8_t)(color != 0);
        if (color == 0) {
            slot = 0;
        }
        nes->ppu.framebuffer[y * NESEMU_SCREEN_WIDTH + x] =
            nes_palette_rgb[palette_read(&nes->ppu, (uint16_t)(0x3F00u + slot)) & 0x3Fu];
    }

    if ((nes->ppu.mask & 0x10u) != 0) {
        int sprite;
        for (sprite = 63; sprite >= 0; --sprite) {
            for (x = 0; x < (int)NESEMU_SCREEN_WIDTH; ++x) {
                uint8_t slot = 0;
                int behind_bg = 0;
                uint8_t color;

                if (x < 8 && (nes->ppu.mask & 0x04u) == 0) {
                    continue;
                }
                color = sprite_pixel(nes, sprite, x, y, &slot, &behind_bg);
                if (color == 0) {
                    continue;
                }
                if (sprite == 0 && bg_opaque[x] && x < 255) {
                    nes->ppu.status |= 0x40u;
                }
                if (!behind_bg || !bg_opaque[x]) {
                    nes->ppu.framebuffer[y * NESEMU_SCREEN_WIDTH + x] =
                        nes_palette_rgb[palette_read(&nes->ppu, (uint16_t)(0x3F00u + slot)) & 0x3Fu];
                }
            }
        }
    }
}

static void ppu_step(NesEmu *nes, int ppu_cycles)
{
    while (ppu_cycles-- > 0) {
        if (nes->ppu.cycle == 0 && nes->ppu.scanline >= 0 && nes->ppu.scanline < 240) {
            render_scanline(nes, nes->ppu.scanline);
        }
        if (nes->ppu.scanline == 241 && nes->ppu.cycle == 1) {
            nes->ppu.status |= 0x80u;
            nes->ppu.frame_ready = 1;
            if ((nes->ppu.ctrl & 0x80u) != 0) {
                nes->cpu.nmi_pending = 1;
            }
        }
        if (nes->ppu.scanline == 261 && nes->ppu.cycle == 1) {
            nes->ppu.status &= (uint8_t)~0xC0u;
        }

        nes->ppu.cycle++;
        if (nes->ppu.cycle >= 341) {
            nes->ppu.cycle = 0;
            nes->ppu.scanline++;
            if (nes->ppu.scanline >= 262) {
                nes->ppu.scanline = 0;
                nes->ppu.frame++;
            }
        }
    }
}

static void clock_cpu_cycles(NesEmu *nes, int cycles)
{
    nes->cpu.cycles += (uint64_t)cycles;
    ppu_step(nes, cycles * 3);
}

void nes_run_frame(NesEmu *nes)
{
    int guard = 0;

    if (nes == NULL || !nes->rom_loaded) {
        return;
    }
    nes->ppu.frame_ready = 0;
    while (!nes->ppu.frame_ready && guard < 50000) {
        int cycles = cpu_step(nes);
        clock_cpu_cycles(nes, cycles);
        guard++;
    }
}

const uint32_t *nes_get_framebuffer(const NesEmu *nes)
{
    if (nes == NULL) {
        return NULL;
    }
    return nes->ppu.framebuffer;
}

static double pulse_sample(NesApu *apu, int channel, int sample_rate)
{
    const uint8_t *r = &apu->regs[channel ? 4 : 0];
    int enabled = (apu->status & (channel ? 0x02u : 0x01u)) != 0;
    int timer = r[2] | ((r[3] & 0x07) << 8);
    int volume = r[0] & 0x0F;
    int duty = (r[0] >> 6) & 3;
    static const double duty_ratio[4] = { 0.125, 0.25, 0.5, 0.75 };
    double freq;
    double sample;

    if (!enabled || timer < 8 || volume == 0) {
        return 0.0;
    }
    freq = (double)CPU_CLOCK_NTSC / (16.0 * (double)(timer + 1));
    apu->pulse_phase[channel] += freq / (double)sample_rate;
    while (apu->pulse_phase[channel] >= 1.0) {
        apu->pulse_phase[channel] -= 1.0;
    }
    sample = apu->pulse_phase[channel] < duty_ratio[duty] ? 1.0 : -1.0;
    return sample * ((double)volume / 15.0);
}

static double triangle_sample(NesApu *apu, int sample_rate)
{
    const uint8_t *r = &apu->regs[8];
    int enabled = (apu->status & 0x04u) != 0;
    int timer = r[2] | ((r[3] & 0x07) << 8);
    double freq;
    double p;

    if (!enabled || timer < 2) {
        return 0.0;
    }
    freq = (double)CPU_CLOCK_NTSC / (32.0 * (double)(timer + 1));
    apu->triangle_phase += freq / (double)sample_rate;
    while (apu->triangle_phase >= 1.0) {
        apu->triangle_phase -= 1.0;
    }
    p = apu->triangle_phase;
    return (p < 0.5 ? (p * 4.0 - 1.0) : (3.0 - p * 4.0)) * 0.75;
}

static double noise_sample(NesApu *apu, int sample_rate)
{
    const uint8_t *r = &apu->regs[12];
    int enabled = (apu->status & 0x08u) != 0;
    int volume = r[0] & 0x0F;
    int period = noise_periods[r[2] & 0x0F];
    double freq;

    if (!enabled || volume == 0) {
        return 0.0;
    }
    freq = (double)CPU_CLOCK_NTSC / (double)period;
    apu->noise_phase += freq / (double)sample_rate;
    while (apu->noise_phase >= 1.0) {
        uint16_t feedback = (uint16_t)((apu->noise_lfsr ^ (apu->noise_lfsr >> ((r[2] & 0x80u) ? 6 : 1))) & 1u);
        apu->noise_lfsr = (uint16_t)((apu->noise_lfsr >> 1) | (feedback << 14));
        apu->noise_phase -= 1.0;
    }
    return ((apu->noise_lfsr & 1u) ? -1.0 : 1.0) * ((double)volume / 15.0) * 0.45;
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
    for (i = 0; i < sample_count; ++i) {
        double mix = pulse_sample(&nes->apu, 0, sample_rate) * 0.22;
        int value;

        mix += pulse_sample(&nes->apu, 1, sample_rate) * 0.22;
        mix += triangle_sample(&nes->apu, sample_rate) * 0.18;
        mix += noise_sample(&nes->apu, sample_rate) * 0.12;
        if (mix > 1.0) {
            mix = 1.0;
        } else if (mix < -1.0) {
            mix = -1.0;
        }
        value = (int)(mix * 28000.0);
        samples[i] = (int16_t)value;
    }
}

void nes_init(NesEmu *nes)
{
    if (nes == NULL) {
        return;
    }
    memset(nes, 0, sizeof(*nes));
    nes->apu.noise_lfsr = 1;
}

void nes_shutdown(NesEmu *nes)
{
    if (nes == NULL) {
        return;
    }
    nes_mapper_clear(&nes->mapper);
    memset(nes, 0, sizeof(*nes));
}

void nes_reset(NesEmu *nes)
{
    if (nes == NULL) {
        return;
    }
    memset(nes->ram, 0, sizeof(nes->ram));
    memset(&nes->cpu, 0, sizeof(nes->cpu));
    memset(&nes->ppu, 0, sizeof(nes->ppu));
    memset(&nes->apu, 0, sizeof(nes->apu));
    nes->apu.noise_lfsr = 1;
    nes->cpu.p = CPU_I | CPU_U;
    nes->cpu.sp = 0xFDu;
    nes->ppu.scanline = 0;
    nes->ppu.cycle = 0;
    if (nes->rom_loaded) {
        nes->reset_vector = (uint16_t)cpu_read_bus(nes, 0xFFFCu);
        nes->reset_vector |= (uint16_t)cpu_read_bus(nes, 0xFFFDu) << 8;
        nes->cpu.pc = nes->reset_vector;
        nes->cpu.cycles = 7;
        ppu_step(nes, 21);
    } else {
        nes->reset_vector = 0;
    }
}

NesResult nes_load_rom_image(NesEmu *nes, const uint8_t *data, size_t size)
{
    NesRomInfo info;
    NesMapper mapper;
    size_t offset;
    size_t prg_size;
    size_t chr_size;
    uint8_t flags6;
    uint8_t flags7;
    uint8_t mapper_id;

    if (nes == NULL || data == NULL) {
        return NES_RESULT_INVALID_ARGUMENT;
    }
    if (size < INES_HEADER_SIZE) {
        return NES_RESULT_INVALID_ROM;
    }
    if (data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1Au) {
        return NES_RESULT_INVALID_ROM;
    }

    flags6 = data[6];
    flags7 = data[7];
    mapper_id = (uint8_t)((flags6 >> 4) | (flags7 & 0xF0u));
    if (mapper_id != 0) {
        return NES_RESULT_UNSUPPORTED_MAPPER;
    }

    prg_size = (size_t)data[4] * NESEMU_PRG_BANK_SIZE;
    chr_size = (size_t)data[5] * NESEMU_CHR_BANK_SIZE;
    if (prg_size == 0) {
        return NES_RESULT_INVALID_ROM;
    }

    offset = INES_HEADER_SIZE;
    if ((flags6 & 0x04u) != 0) {
        offset += INES_TRAINER_SIZE;
    }
    if (offset > size || prg_size > size - offset) {
        return NES_RESULT_INVALID_ROM;
    }
    if (chr_size > size - offset - prg_size) {
        return NES_RESULT_INVALID_ROM;
    }

    memset(&info, 0, sizeof(info));
    memset(&mapper, 0, sizeof(mapper));

    info.mapper_id = mapper_id;
    info.prg_banks = data[4];
    info.chr_banks = data[5];
    info.has_trainer = (uint8_t)((flags6 & 0x04u) != 0);
    info.has_battery_ram = (uint8_t)((flags6 & 0x02u) != 0);
    if ((flags6 & 0x08u) != 0) {
        info.mirroring = NES_MIRROR_FOUR_SCREEN;
    } else {
        info.mirroring = (flags6 & 0x01u) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL;
    }

    mapper.prg_rom = (uint8_t *)malloc(prg_size);
    if (mapper.prg_rom == NULL) {
        return NES_RESULT_OUT_OF_MEMORY;
    }
    memcpy(mapper.prg_rom, data + offset, prg_size);
    mapper.prg_rom_size = prg_size;
    offset += prg_size;

    if (chr_size == 0) {
        chr_size = NESEMU_CHR_BANK_SIZE;
        mapper.chr_is_ram = 1;
    }
    mapper.chr_mem = (uint8_t *)malloc(chr_size);
    if (mapper.chr_mem == NULL) {
        free(mapper.prg_rom);
        return NES_RESULT_OUT_OF_MEMORY;
    }
    if (info.chr_banks == 0) {
        memset(mapper.chr_mem, 0, chr_size);
    } else {
        memcpy(mapper.chr_mem, data + offset, chr_size);
    }
    mapper.chr_mem_size = chr_size;

    nes_mapper_clear(&nes->mapper);
    nes->rom = info;
    nes->mapper = mapper;
    nes->rom_loaded = 1;
    nes->joypad.state = 0;
    nes->joypad.shift = 0;
    nes->joypad.strobe = 0;
    nes_reset(nes);
    return NES_RESULT_OK;
}

const char *nes_result_string(NesResult result)
{
    switch (result) {
    case NES_RESULT_OK:
        return "ok";
    case NES_RESULT_INVALID_ARGUMENT:
        return "invalid argument";
    case NES_RESULT_OUT_OF_MEMORY:
        return "out of memory";
    case NES_RESULT_INVALID_ROM:
        return "invalid ROM";
    case NES_RESULT_UNSUPPORTED_MAPPER:
        return "unsupported mapper";
    case NES_RESULT_IO_ERROR:
        return "I/O error";
    default:
        return "unknown error";
    }
}

void nes_set_button(NesEmu *nes, NesButton button, int pressed)
{
    uint8_t mask;

    if (nes == NULL || button < 0 || button >= NES_BUTTON_COUNT) {
        return;
    }
    mask = (uint8_t)(1u << (unsigned int)button);
    if (pressed) {
        nes->joypad.state |= mask;
    } else {
        nes->joypad.state &= (uint8_t)~mask;
    }
    if (nes->joypad.strobe) {
        nes->joypad.shift = nes->joypad.state;
    }
}

int nes_get_button(const NesEmu *nes, NesButton button)
{
    if (nes == NULL || button < 0 || button >= NES_BUTTON_COUNT) {
        return 0;
    }
    return (nes->joypad.state & (uint8_t)(1u << (unsigned int)button)) != 0;
}
