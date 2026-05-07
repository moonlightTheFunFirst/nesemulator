#include "nes_internal.h"

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

static uint8_t cpu_fetch8(NesEmu *nes)
{
    uint8_t value = nes_cpu_bus_read(nes, nes->cpu.pc);
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
    uint8_t lo = nes_cpu_bus_read(nes, address);
    uint8_t hi = nes_cpu_bus_read(nes, (uint16_t)(address + 1));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t cpu_read16_bug(NesEmu *nes, uint16_t address)
{
    uint8_t lo = nes_cpu_bus_read(nes, address);
    uint8_t hi = nes_cpu_bus_read(nes, (uint16_t)((address & 0xFF00u) | ((address + 1u) & 0x00FFu)));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static void cpu_push(NesEmu *nes, uint8_t value)
{
    nes_cpu_bus_write(nes, (uint16_t)(0x0100u | nes->cpu.sp), value);
    nes->cpu.sp--;
}

static uint8_t cpu_pull(NesEmu *nes)
{
    nes->cpu.sp++;
    return nes_cpu_bus_read(nes, (uint16_t)(0x0100u | nes->cpu.sp));
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
    uint8_t lo = nes_cpu_bus_read(nes, zp);
    uint8_t hi = nes_cpu_bus_read(nes, (uint8_t)(zp + 1));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint16_t addr_indy(NesEmu *nes, int *page_crossed)
{
    uint8_t zp = cpu_fetch8(nes);
    uint16_t base = (uint16_t)(nes_cpu_bus_read(nes, zp) | ((uint16_t)nes_cpu_bus_read(nes, (uint8_t)(zp + 1)) << 8));
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

static void op_slo(NesEmu *nes, uint16_t address)
{
    uint8_t value = op_asl(nes, nes_cpu_bus_read(nes, address));

    nes_cpu_bus_write(nes, address, value);
    nes->cpu.a |= value;
    cpu_set_zn(nes, nes->cpu.a);
}

static void op_rla(NesEmu *nes, uint16_t address)
{
    uint8_t value = op_rol(nes, nes_cpu_bus_read(nes, address));

    nes_cpu_bus_write(nes, address, value);
    nes->cpu.a &= value;
    cpu_set_zn(nes, nes->cpu.a);
}

static void op_sre(NesEmu *nes, uint16_t address)
{
    uint8_t value = op_lsr(nes, nes_cpu_bus_read(nes, address));

    nes_cpu_bus_write(nes, address, value);
    nes->cpu.a ^= value;
    cpu_set_zn(nes, nes->cpu.a);
}

static void op_rra(NesEmu *nes, uint16_t address)
{
    uint8_t value = op_ror(nes, nes_cpu_bus_read(nes, address));

    nes_cpu_bus_write(nes, address, value);
    op_adc(nes, value);
}

static void op_dcp(NesEmu *nes, uint16_t address)
{
    uint8_t value = (uint8_t)(nes_cpu_bus_read(nes, address) - 1u);

    nes_cpu_bus_write(nes, address, value);
    op_cmp(nes, nes->cpu.a, value);
}

static void op_isc(NesEmu *nes, uint16_t address)
{
    uint8_t value = (uint8_t)(nes_cpu_bus_read(nes, address) + 1u);

    nes_cpu_bus_write(nes, address, value);
    op_sbc(nes, value);
}

static void op_lax(NesEmu *nes, uint8_t value)
{
    nes->cpu.a = value;
    nes->cpu.x = value;
    cpu_set_zn(nes, value);
}

static void op_sax(NesEmu *nes, uint16_t address)
{
    nes_cpu_bus_write(nes, address, (uint8_t)(nes->cpu.a & nes->cpu.x));
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
    nes->cpu.nmi_delay = 0;
    cpu_push(nes, (uint8_t)(nes->cpu.pc >> 8));
    cpu_push(nes, (uint8_t)nes->cpu.pc);
    cpu_push(nes, (uint8_t)((nes->cpu.p & (uint8_t)~CPU_B) | CPU_U));
    cpu_set_flag(nes, CPU_I, 1);
    nes->cpu.pc = cpu_read16(nes, 0xFFFAu);
}

int nes_cpu_step(NesEmu *nes)
{
    uint8_t opcode;
    uint16_t address;
    int page_crossed = 0;
    int cycles = 0;

    nes->cpu.io_write_delay = 0;
    if (nes->cpu.stopped) {
        return 1;
    }
    if (nes->cpu.nmi_pending && nes->cpu.nmi_delay <= 0) {
        cpu_service_nmi(nes);
        return 7;
    }
    if (nes->cpu.nmi_delay > 0) {
        nes->cpu.nmi_delay--;
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
    case 0x02:
    case 0x12:
    case 0x22:
    case 0x32:
    case 0x42:
    case 0x52:
    case 0x62:
    case 0x72:
    case 0x92:
    case 0xB2:
    case 0xD2:
    case 0xF2:
        nes->cpu.stopped = 1;
        cycles = 2;
        break;
    case 0x01:
        nes->cpu.a |= nes_cpu_bus_read(nes, addr_indx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 6;
        break;
    case 0x03:
        op_slo(nes, addr_indx(nes));
        cycles = 8;
        break;
    case 0x05:
        nes->cpu.a |= nes_cpu_bus_read(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0x06:
        address = addr_zp(nes);
        nes_cpu_bus_write(nes, address, op_asl(nes, nes_cpu_bus_read(nes, address)));
        cycles = 5;
        break;
    case 0x07:
        op_slo(nes, addr_zp(nes));
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
    case 0x0B:
        nes->cpu.a &= cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.a);
        cpu_set_flag(nes, CPU_C, (nes->cpu.a & 0x80u) != 0);
        cycles = 2;
        break;
    case 0x0A:
        nes->cpu.a = op_asl(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x0D:
        nes->cpu.a |= nes_cpu_bus_read(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x0E:
        address = addr_abs(nes);
        nes_cpu_bus_write(nes, address, op_asl(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x0F:
        op_slo(nes, addr_abs(nes));
        cycles = 6;
        break;
    case 0x10:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_N));
        break;
    case 0x11:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a |= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0x13:
        op_slo(nes, addr_indy(nes, NULL));
        cycles = 8;
        break;
    case 0x15:
        nes->cpu.a |= nes_cpu_bus_read(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x16:
        address = addr_zpx(nes);
        nes_cpu_bus_write(nes, address, op_asl(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x17:
        op_slo(nes, addr_zpx(nes));
        cycles = 6;
        break;
    case 0x18:
        cpu_set_flag(nes, CPU_C, 0);
        cycles = 2;
        break;
    case 0x19:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a |= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x1D:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a |= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x1E:
        address = addr_absx(nes, NULL);
        nes_cpu_bus_write(nes, address, op_asl(nes, nes_cpu_bus_read(nes, address)));
        cycles = 7;
        break;
    case 0x1B:
        op_slo(nes, addr_absy(nes, NULL));
        cycles = 7;
        break;
    case 0x1F:
        op_slo(nes, addr_absx(nes, NULL));
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
        nes->cpu.a &= nes_cpu_bus_read(nes, addr_indx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 6;
        break;
    case 0x23:
        op_rla(nes, addr_indx(nes));
        cycles = 8;
        break;
    case 0x24:
        {
            uint8_t value = nes_cpu_bus_read(nes, addr_zp(nes));
            cpu_set_flag(nes, CPU_Z, (nes->cpu.a & value) == 0);
            cpu_set_flag(nes, CPU_N, (value & 0x80u) != 0);
            cpu_set_flag(nes, CPU_V, (value & 0x40u) != 0);
        }
        cycles = 3;
        break;
    case 0x25:
        nes->cpu.a &= nes_cpu_bus_read(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0x26:
        address = addr_zp(nes);
        nes_cpu_bus_write(nes, address, op_rol(nes, nes_cpu_bus_read(nes, address)));
        cycles = 5;
        break;
    case 0x27:
        op_rla(nes, addr_zp(nes));
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
    case 0x2B:
        nes->cpu.a &= cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.a);
        cpu_set_flag(nes, CPU_C, (nes->cpu.a & 0x80u) != 0);
        cycles = 2;
        break;
    case 0x2A:
        nes->cpu.a = op_rol(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x2C:
        {
            uint16_t bit_address = addr_abs(nes);
            uint8_t value = nes_cpu_bus_read_delayed(nes, bit_address, 3);
            cpu_set_flag(nes, CPU_Z, (nes->cpu.a & value) == 0);
            cpu_set_flag(nes, CPU_N, (value & 0x80u) != 0);
            cpu_set_flag(nes, CPU_V, (value & 0x40u) != 0);
        }
        cycles = 4;
        break;
    case 0x2D:
        nes->cpu.a &= nes_cpu_bus_read(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x2E:
        address = addr_abs(nes);
        nes_cpu_bus_write(nes, address, op_rol(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x2F:
        op_rla(nes, addr_abs(nes));
        cycles = 6;
        break;
    case 0x30:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_N));
        break;
    case 0x31:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a &= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0x33:
        op_rla(nes, addr_indy(nes, NULL));
        cycles = 8;
        break;
    case 0x35:
        nes->cpu.a &= nes_cpu_bus_read(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x36:
        address = addr_zpx(nes);
        nes_cpu_bus_write(nes, address, op_rol(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x37:
        op_rla(nes, addr_zpx(nes));
        cycles = 6;
        break;
    case 0x38:
        cpu_set_flag(nes, CPU_C, 1);
        cycles = 2;
        break;
    case 0x39:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a &= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x3D:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a &= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x3E:
        address = addr_absx(nes, NULL);
        nes_cpu_bus_write(nes, address, op_rol(nes, nes_cpu_bus_read(nes, address)));
        cycles = 7;
        break;
    case 0x3B:
        op_rla(nes, addr_absy(nes, NULL));
        cycles = 7;
        break;
    case 0x3F:
        op_rla(nes, addr_absx(nes, NULL));
        cycles = 7;
        break;
    case 0x40:
        nes->cpu.p = (uint8_t)((cpu_pull(nes) & (uint8_t)~CPU_B) | CPU_U);
        nes->cpu.pc = cpu_pull(nes);
        nes->cpu.pc |= (uint16_t)cpu_pull(nes) << 8;
        cycles = 6;
        break;
    case 0x41:
        nes->cpu.a ^= nes_cpu_bus_read(nes, addr_indx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 6;
        break;
    case 0x43:
        op_sre(nes, addr_indx(nes));
        cycles = 8;
        break;
    case 0x45:
        nes->cpu.a ^= nes_cpu_bus_read(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0x46:
        address = addr_zp(nes);
        nes_cpu_bus_write(nes, address, op_lsr(nes, nes_cpu_bus_read(nes, address)));
        cycles = 5;
        break;
    case 0x47:
        op_sre(nes, addr_zp(nes));
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
    case 0x4B:
        nes->cpu.a &= cpu_fetch8(nes);
        nes->cpu.a = op_lsr(nes, nes->cpu.a);
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
        nes->cpu.a ^= nes_cpu_bus_read(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x4E:
        address = addr_abs(nes);
        nes_cpu_bus_write(nes, address, op_lsr(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x4F:
        op_sre(nes, addr_abs(nes));
        cycles = 6;
        break;
    case 0x50:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_V));
        break;
    case 0x51:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a ^= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0x53:
        op_sre(nes, addr_indy(nes, NULL));
        cycles = 8;
        break;
    case 0x55:
        nes->cpu.a ^= nes_cpu_bus_read(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0x56:
        address = addr_zpx(nes);
        nes_cpu_bus_write(nes, address, op_lsr(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x57:
        op_sre(nes, addr_zpx(nes));
        cycles = 6;
        break;
    case 0x58:
        cpu_set_flag(nes, CPU_I, 0);
        cycles = 2;
        break;
    case 0x59:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a ^= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x5D:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a ^= nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0x5E:
        address = addr_absx(nes, NULL);
        nes_cpu_bus_write(nes, address, op_lsr(nes, nes_cpu_bus_read(nes, address)));
        cycles = 7;
        break;
    case 0x5B:
        op_sre(nes, addr_absy(nes, NULL));
        cycles = 7;
        break;
    case 0x5F:
        op_sre(nes, addr_absx(nes, NULL));
        cycles = 7;
        break;
    case 0x60:
        nes->cpu.pc = cpu_pull(nes);
        nes->cpu.pc |= (uint16_t)cpu_pull(nes) << 8;
        nes->cpu.pc++;
        cycles = 6;
        break;
    case 0x61:
        op_adc(nes, nes_cpu_bus_read(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0x63:
        op_rra(nes, addr_indx(nes));
        cycles = 8;
        break;
    case 0x65:
        op_adc(nes, nes_cpu_bus_read(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0x66:
        address = addr_zp(nes);
        nes_cpu_bus_write(nes, address, op_ror(nes, nes_cpu_bus_read(nes, address)));
        cycles = 5;
        break;
    case 0x67:
        op_rra(nes, addr_zp(nes));
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
    case 0x6B:
        nes->cpu.a &= cpu_fetch8(nes);
        nes->cpu.a = (uint8_t)((nes->cpu.a >> 1) | (cpu_get_flag(nes, CPU_C) << 7));
        cpu_set_zn(nes, nes->cpu.a);
        cpu_set_flag(nes, CPU_C, (nes->cpu.a & 0x40u) != 0);
        cpu_set_flag(nes, CPU_V, ((nes->cpu.a >> 6) ^ (nes->cpu.a >> 5)) & 1u);
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
        op_adc(nes, nes_cpu_bus_read(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0x6E:
        address = addr_abs(nes);
        nes_cpu_bus_write(nes, address, op_ror(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x6F:
        op_rra(nes, addr_abs(nes));
        cycles = 6;
        break;
    case 0x70:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_V));
        break;
    case 0x71:
        address = addr_indy(nes, &page_crossed);
        op_adc(nes, nes_cpu_bus_read(nes, address));
        cycles = 5 + page_crossed;
        break;
    case 0x73:
        op_rra(nes, addr_indy(nes, NULL));
        cycles = 8;
        break;
    case 0x75:
        op_adc(nes, nes_cpu_bus_read(nes, addr_zpx(nes)));
        cycles = 4;
        break;
    case 0x76:
        address = addr_zpx(nes);
        nes_cpu_bus_write(nes, address, op_ror(nes, nes_cpu_bus_read(nes, address)));
        cycles = 6;
        break;
    case 0x77:
        op_rra(nes, addr_zpx(nes));
        cycles = 6;
        break;
    case 0x78:
        cpu_set_flag(nes, CPU_I, 1);
        cycles = 2;
        break;
    case 0x79:
        address = addr_absy(nes, &page_crossed);
        op_adc(nes, nes_cpu_bus_read(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0x7D:
        address = addr_absx(nes, &page_crossed);
        op_adc(nes, nes_cpu_bus_read(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0x7E:
        address = addr_absx(nes, NULL);
        nes_cpu_bus_write(nes, address, op_ror(nes, nes_cpu_bus_read(nes, address)));
        cycles = 7;
        break;
    case 0x7B:
        op_rra(nes, addr_absy(nes, NULL));
        cycles = 7;
        break;
    case 0x7F:
        op_rra(nes, addr_absx(nes, NULL));
        cycles = 7;
        break;
    case 0x81:
        nes_cpu_bus_write_delayed(nes, addr_indx(nes), nes->cpu.a, 5);
        cycles = 6;
        break;
    case 0x83:
        nes_cpu_bus_write_delayed(nes, addr_indx(nes), (uint8_t)(nes->cpu.a & nes->cpu.x), 5);
        cycles = 6;
        break;
    case 0x84:
        nes_cpu_bus_write(nes, addr_zp(nes), nes->cpu.y);
        cycles = 3;
        break;
    case 0x85:
        nes_cpu_bus_write(nes, addr_zp(nes), nes->cpu.a);
        cycles = 3;
        break;
    case 0x86:
        nes_cpu_bus_write(nes, addr_zp(nes), nes->cpu.x);
        cycles = 3;
        break;
    case 0x87:
        op_sax(nes, addr_zp(nes));
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
    case 0x8B:
        nes->cpu.a = (uint8_t)(nes->cpu.x & cpu_fetch8(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x8C:
        nes_cpu_bus_write_delayed(nes, addr_abs(nes), nes->cpu.y, 3);
        cycles = 4;
        break;
    case 0x8D:
        nes_cpu_bus_write_delayed(nes, addr_abs(nes), nes->cpu.a, 3);
        cycles = 4;
        break;
    case 0x8E:
        nes_cpu_bus_write_delayed(nes, addr_abs(nes), nes->cpu.x, 3);
        cycles = 4;
        break;
    case 0x8F:
        nes_cpu_bus_write_delayed(nes, addr_abs(nes), (uint8_t)(nes->cpu.a & nes->cpu.x), 3);
        cycles = 4;
        break;
    case 0x90:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_C));
        break;
    case 0x91:
        nes_cpu_bus_write_delayed(nes, addr_indy(nes, NULL), nes->cpu.a, 5);
        cycles = 6;
        break;
    case 0x93:
        address = addr_indy(nes, NULL);
        nes_cpu_bus_write_delayed(nes,
                              address,
                              (uint8_t)(nes->cpu.a & nes->cpu.x & (((address >> 8) + 1u) & 0xFFu)),
                              5);
        cycles = 6;
        break;
    case 0x94:
        nes_cpu_bus_write(nes, addr_zpx(nes), nes->cpu.y);
        cycles = 4;
        break;
    case 0x95:
        nes_cpu_bus_write(nes, addr_zpx(nes), nes->cpu.a);
        cycles = 4;
        break;
    case 0x96:
        nes_cpu_bus_write(nes, addr_zpy(nes), nes->cpu.x);
        cycles = 4;
        break;
    case 0x97:
        op_sax(nes, addr_zpy(nes));
        cycles = 4;
        break;
    case 0x98:
        nes->cpu.a = nes->cpu.y;
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 2;
        break;
    case 0x99:
        nes_cpu_bus_write_delayed(nes, addr_absy(nes, NULL), nes->cpu.a, 4);
        cycles = 5;
        break;
    case 0x9A:
        nes->cpu.sp = nes->cpu.x;
        cycles = 2;
        break;
    case 0x9D:
        nes_cpu_bus_write_delayed(nes, addr_absx(nes, NULL), nes->cpu.a, 4);
        cycles = 5;
        break;
    case 0x9B:
        address = addr_absy(nes, NULL);
        nes->cpu.sp = (uint8_t)(nes->cpu.a & nes->cpu.x);
        nes_cpu_bus_write_delayed(nes,
                              address,
                              (uint8_t)(nes->cpu.sp & (((address >> 8) + 1u) & 0xFFu)),
                              4);
        cycles = 5;
        break;
    case 0x9C:
        address = addr_absx(nes, NULL);
        nes_cpu_bus_write_delayed(nes,
                              address,
                              (uint8_t)(nes->cpu.y & (((address >> 8) + 1u) & 0xFFu)),
                              4);
        cycles = 5;
        break;
    case 0x9E:
        address = addr_absy(nes, NULL);
        nes_cpu_bus_write_delayed(nes,
                              address,
                              (uint8_t)(nes->cpu.x & (((address >> 8) + 1u) & 0xFFu)),
                              4);
        cycles = 5;
        break;
    case 0x9F:
        address = addr_absy(nes, NULL);
        nes_cpu_bus_write_delayed(nes,
                              address,
                              (uint8_t)(nes->cpu.a & nes->cpu.x & (((address >> 8) + 1u) & 0xFFu)),
                              4);
        cycles = 5;
        break;
    case 0xA0:
        nes->cpu.y = cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 2;
        break;
    case 0xA1:
        nes->cpu.a = nes_cpu_bus_read(nes, addr_indx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 6;
        break;
    case 0xA3:
        op_lax(nes, nes_cpu_bus_read(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0xA2:
        nes->cpu.x = cpu_fetch8(nes);
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 2;
        break;
    case 0xA4:
        nes->cpu.y = nes_cpu_bus_read(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 3;
        break;
    case 0xA5:
        nes->cpu.a = nes_cpu_bus_read(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 3;
        break;
    case 0xA6:
        nes->cpu.x = nes_cpu_bus_read(nes, addr_zp(nes));
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 3;
        break;
    case 0xA7:
        op_lax(nes, nes_cpu_bus_read(nes, addr_zp(nes)));
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
    case 0xAB:
        op_lax(nes, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0xAC:
        nes->cpu.y = nes_cpu_bus_read(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 4;
        break;
    case 0xAD:
        address = addr_abs(nes);
        nes->cpu.a = nes_cpu_bus_read_delayed(nes, address, 3);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0xAE:
        nes->cpu.x = nes_cpu_bus_read(nes, addr_abs(nes));
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 4;
        break;
    case 0xAF:
        op_lax(nes, nes_cpu_bus_read(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xB0:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_C));
        break;
    case 0xB1:
        address = addr_indy(nes, &page_crossed);
        nes->cpu.a = nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 5 + page_crossed;
        break;
    case 0xB3:
        address = addr_indy(nes, &page_crossed);
        op_lax(nes, nes_cpu_bus_read(nes, address));
        cycles = 5 + page_crossed;
        break;
    case 0xB4:
        nes->cpu.y = nes_cpu_bus_read(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 4;
        break;
    case 0xB5:
        nes->cpu.a = nes_cpu_bus_read(nes, addr_zpx(nes));
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4;
        break;
    case 0xB6:
        nes->cpu.x = nes_cpu_bus_read(nes, addr_zpy(nes));
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 4;
        break;
    case 0xB7:
        op_lax(nes, nes_cpu_bus_read(nes, addr_zpy(nes)));
        cycles = 4;
        break;
    case 0xB8:
        cpu_set_flag(nes, CPU_V, 0);
        cycles = 2;
        break;
    case 0xB9:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.a = nes_cpu_bus_read(nes, address);
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
        nes->cpu.y = nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.y);
        cycles = 4 + page_crossed;
        break;
    case 0xBD:
        address = addr_absx(nes, &page_crossed);
        nes->cpu.a = nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.a);
        cycles = 4 + page_crossed;
        break;
    case 0xBE:
        address = addr_absy(nes, &page_crossed);
        nes->cpu.x = nes_cpu_bus_read(nes, address);
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 4 + page_crossed;
        break;
    case 0xBB:
        address = addr_absy(nes, &page_crossed);
        op_lax(nes, (uint8_t)(nes_cpu_bus_read(nes, address) & nes->cpu.sp));
        nes->cpu.sp = nes->cpu.a;
        cycles = 4 + page_crossed;
        break;
    case 0xBF:
        address = addr_absy(nes, &page_crossed);
        op_lax(nes, nes_cpu_bus_read(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xC0:
        op_cmp(nes, nes->cpu.y, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0xC1:
        op_cmp(nes, nes->cpu.a, nes_cpu_bus_read(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0xC3:
        op_dcp(nes, addr_indx(nes));
        cycles = 8;
        break;
    case 0xC4:
        op_cmp(nes, nes->cpu.y, nes_cpu_bus_read(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xC5:
        op_cmp(nes, nes->cpu.a, nes_cpu_bus_read(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xC6:
        address = addr_zp(nes);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) - 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 5;
        break;
    case 0xC7:
        op_dcp(nes, addr_zp(nes));
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
    case 0xCB:
        {
            uint8_t value = (uint8_t)(nes->cpu.a & nes->cpu.x);
            uint8_t operand = cpu_fetch8(nes);
            uint8_t result = (uint8_t)(value - operand);

            cpu_set_flag(nes, CPU_C, value >= operand);
            nes->cpu.x = result;
            cpu_set_zn(nes, nes->cpu.x);
        }
        cycles = 2;
        break;
    case 0xCA:
        nes->cpu.x--;
        cpu_set_zn(nes, nes->cpu.x);
        cycles = 2;
        break;
    case 0xCC:
        op_cmp(nes, nes->cpu.y, nes_cpu_bus_read(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xCD:
        op_cmp(nes, nes->cpu.a, nes_cpu_bus_read(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xCE:
        address = addr_abs(nes);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) - 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 6;
        break;
    case 0xCF:
        op_dcp(nes, addr_abs(nes));
        cycles = 6;
        break;
    case 0xD0:
        cycles = 2 + op_branch(nes, !cpu_get_flag(nes, CPU_Z));
        break;
    case 0xD1:
        address = addr_indy(nes, &page_crossed);
        op_cmp(nes, nes->cpu.a, nes_cpu_bus_read(nes, address));
        cycles = 5 + page_crossed;
        break;
    case 0xD3:
        op_dcp(nes, addr_indy(nes, NULL));
        cycles = 8;
        break;
    case 0xD5:
        op_cmp(nes, nes->cpu.a, nes_cpu_bus_read(nes, addr_zpx(nes)));
        cycles = 4;
        break;
    case 0xD6:
        address = addr_zpx(nes);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) - 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 6;
        break;
    case 0xD7:
        op_dcp(nes, addr_zpx(nes));
        cycles = 6;
        break;
    case 0xD8:
        cpu_set_flag(nes, CPU_D, 0);
        cycles = 2;
        break;
    case 0xD9:
        address = addr_absy(nes, &page_crossed);
        op_cmp(nes, nes->cpu.a, nes_cpu_bus_read(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xDD:
        address = addr_absx(nes, &page_crossed);
        op_cmp(nes, nes->cpu.a, nes_cpu_bus_read(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xDE:
        address = addr_absx(nes, NULL);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) - 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 7;
        break;
    case 0xDB:
        op_dcp(nes, addr_absy(nes, NULL));
        cycles = 7;
        break;
    case 0xDF:
        op_dcp(nes, addr_absx(nes, NULL));
        cycles = 7;
        break;
    case 0xE0:
        op_cmp(nes, nes->cpu.x, cpu_fetch8(nes));
        cycles = 2;
        break;
    case 0xE1:
        op_sbc(nes, nes_cpu_bus_read(nes, addr_indx(nes)));
        cycles = 6;
        break;
    case 0xE3:
        op_isc(nes, addr_indx(nes));
        cycles = 8;
        break;
    case 0xE4:
        op_cmp(nes, nes->cpu.x, nes_cpu_bus_read(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xE5:
        op_sbc(nes, nes_cpu_bus_read(nes, addr_zp(nes)));
        cycles = 3;
        break;
    case 0xE6:
        address = addr_zp(nes);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) + 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 5;
        break;
    case 0xE7:
        op_isc(nes, addr_zp(nes));
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
        op_cmp(nes, nes->cpu.x, nes_cpu_bus_read(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xED:
        op_sbc(nes, nes_cpu_bus_read(nes, addr_abs(nes)));
        cycles = 4;
        break;
    case 0xEE:
        address = addr_abs(nes);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) + 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 6;
        break;
    case 0xEF:
        op_isc(nes, addr_abs(nes));
        cycles = 6;
        break;
    case 0xF0:
        cycles = 2 + op_branch(nes, cpu_get_flag(nes, CPU_Z));
        break;
    case 0xF1:
        address = addr_indy(nes, &page_crossed);
        op_sbc(nes, nes_cpu_bus_read(nes, address));
        cycles = 5 + page_crossed;
        break;
    case 0xF3:
        op_isc(nes, addr_indy(nes, NULL));
        cycles = 8;
        break;
    case 0xF5:
        op_sbc(nes, nes_cpu_bus_read(nes, addr_zpx(nes)));
        cycles = 4;
        break;
    case 0xF6:
        address = addr_zpx(nes);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) + 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 6;
        break;
    case 0xF7:
        op_isc(nes, addr_zpx(nes));
        cycles = 6;
        break;
    case 0xF8:
        cpu_set_flag(nes, CPU_D, 1);
        cycles = 2;
        break;
    case 0xF9:
        address = addr_absy(nes, &page_crossed);
        op_sbc(nes, nes_cpu_bus_read(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xFD:
        address = addr_absx(nes, &page_crossed);
        op_sbc(nes, nes_cpu_bus_read(nes, address));
        cycles = 4 + page_crossed;
        break;
    case 0xFE:
        address = addr_absx(nes, NULL);
        nes_cpu_bus_write(nes, address, (uint8_t)(nes_cpu_bus_read(nes, address) + 1u));
        cpu_set_zn(nes, nes_cpu_bus_read(nes, address));
        cycles = 7;
        break;
    case 0xFB:
        op_isc(nes, addr_absy(nes, NULL));
        cycles = 7;
        break;
    case 0xFF:
        op_isc(nes, addr_absx(nes, NULL));
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
