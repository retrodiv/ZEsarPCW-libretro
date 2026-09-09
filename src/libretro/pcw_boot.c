/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 retrodiv <retrodiv@proton.me> */
/* Native PCW 8256/8512 sector bootstrap. See docs/BOOTSTRAP.md for the
 * functional interface and provenance. No firmware image is executed or
 * deposited in guest RAM. The existing uPD765 handles the actual disk read. */
#include "pcw_boot.h"
#include "cpu.h"
#include "pcw.h"
#include "pd765.h"
#include "pcw_debug.h"

enum boot_phase {
    BOOT_OFF, BOOT_INIT, BOOT_SPIN, BOOT_SPECIFY, BOOT_RECALIBRATE,
    BOOT_SEEK, BOOT_SENSE, BOOT_SENSE_RESULT, BOOT_READ,
    BOOT_TRANSFER, BOOT_RESULT, BOOT_WAIT
};

enum { BOOT_SLICE = 4, BOOT_SPIN_CYCLES = 400000,
       BOOT_TIMEOUT_CYCLES = 4000000 * 3 };

struct boot_state {
    int phase, elapsed, cursor, received, checksum, calibrated, status;
};
static struct boot_state boot;

void pcw_boot_begin(void)
{
    memset(&boot, 0, sizeof boot);
    boot.phase = BOOT_INIT;
}

static void boot_phase(int phase)
{
    boot.phase = phase;
    boot.cursor = 0;
}

static void boot_fail(void)
{
    boot_phase(BOOT_WAIT);
    pd765_motor_off();
    debug_printf(VERBOSE_INFO, "Native PCW bootstrap: no valid boot sector");
    /* The machine layer owns the optional OpenPCW disk substitution. It
     * rearms us through reset_cpu(), or leaves us waiting if that also fails. */
    pcw_boot_failed();
}

static void boot_command(const unsigned char *bytes, int length, int next,
                         unsigned status)
{
    if ((status & 0xc0) != 0x80) return;
    pd765_out_port_data_register(bytes[boot.cursor]);
    if (++boot.cursor == length) boot_phase(next);
}

bool pcw_boot_step(void)
{
    /* These are uPD765 command parameters, not Z80 instructions. SPECIFY
     * selects a 3 ms step, 240 ms unload, 2 ms load and non-DMA transfers. */
    static const unsigned char specify[] = { 0x03, 0xdf, 0x03 };
    static const unsigned char recalibrate[] = { 0x07, 0x00 };
    static const unsigned char sense[] = { 0x08 };
    static const unsigned char read_sector[] = {
        0x66, 0, 0, 0, 1, 2, 1, 0x2a, 0xff
    };
    unsigned status;
    if (boot.phase == BOOT_OFF) return false;
    t_estados += BOOT_SLICE;
    if (boot.phase != BOOT_WAIT &&
        (boot.elapsed += BOOT_SLICE) > BOOT_TIMEOUT_CYCLES) {
        boot.elapsed = BOOT_TIMEOUT_CYCLES;
        boot_fail();
        return true;
    }
    status = pd765_read_status_register();
    switch (boot.phase) {
    case BOOT_INIT:
        /* Also used for the guest's OUT (F8),1 soft boot. */
        for (unsigned bank = 0; bank < 4; ++bank)
            pcw_out_port_bank(0xf0 + bank, 0x80 + bank);
        memset(memoria_spectrum, 0, 0x200);
        reg_pc = 0;
        reg_sp = 0xfff0;
        iff1.v = iff2.v = 0;
        z80_halt_signal.v = z80_wait_signal.v = 0;
        interrupcion_maskable_generada.v = 0;
        interrupcion_non_maskable_generada.v = 0;
        pcw_out_port_f8(4);
        pcw_out_port_f7(0x80);
        pd765_reset();
        pd765_motor_on();
        boot_phase(BOOT_SPIN);
        break;
    case BOOT_SPIN:
        if (boot.elapsed >= BOOT_SPIN_CYCLES) boot_phase(BOOT_SPECIFY);
        break;
    case BOOT_SPECIFY:
        /* A motor READY event precedes the seek event. Acknowledge it before
         * recalibration so no stale event leaks into the guest's first SIS. */
        if (!boot.cursor && pd765_interrupt_pending) {
            boot_phase(BOOT_SENSE);
            break;
        }
        boot_command(specify, sizeof specify, BOOT_RECALIBRATE, status);
        break;
    case BOOT_RECALIBRATE:
        boot_command(recalibrate, sizeof recalibrate, BOOT_SEEK, status);
        break;
    case BOOT_SEEK:
        if (pd765_interrupt_pending) boot_phase(BOOT_SENSE);
        else if (boot.calibrated) boot_phase(BOOT_READ);
        break;
    case BOOT_SENSE:
        boot_command(sense, sizeof sense, BOOT_SENSE_RESULT, status);
        break;
    case BOOT_SENSE_RESULT:
        if ((status & 0xc0) == 0xc0) {
            unsigned value = pd765_read();
            if (boot.cursor++ == 0) boot.status = value;
        } else if ((status & 0xc0) == 0x80 && boot.cursor) {
            if (boot.cursor == 2 && (boot.status & 0xe0) == 0x20)
                boot.calibrated = 1;
            if ((boot.status & 0xc0) == 0x40) boot_fail();
            else if ((boot.status & 0xc0) == 0xc0) boot_phase(BOOT_SPECIFY);
            else boot_phase(BOOT_SEEK);
        }
        break;
    case BOOT_READ:
        boot_command(read_sector, sizeof read_sector, BOOT_TRANSFER, status);
        break;
    case BOOT_TRANSFER:
        if ((status & 0xc0) != 0xc0) break;
        if (!(status & PD765_MAIN_STATUS_REGISTER_EXM_MASK)) {
            boot_phase(BOOT_RESULT);
            break;
        }
        {
            unsigned value = pd765_read();
            *pcw_get_memory_offset_write(0xf000 + boot.received) = value;
            boot.checksum = (boot.checksum + value) & 0xff;
            if (++boot.received == 512) {
                pd765_set_terminal_count_signal();
                boot_phase(BOOT_RESULT);
            }
        }
        break;
    case BOOT_RESULT:
        if ((status & 0xc0) == 0xc0) {
            unsigned value = pd765_read();
            if (boot.cursor++ == 0) boot.status = value;
        } else if ((status & 0xc0) == 0x80) {
            if (boot.cursor != 7 || (boot.status & 0xcb) ||
                boot.received != 512 || boot.checksum != 0xff) {
                boot_fail();
            } else {
                reg_a = 0;
                /* Successful boot reports A=0, Z set and carry clear. */
                Z80_FLAGS = FLAG_Z | FLAG_H;
                reg_bc = 1;
                reg_hl = reg_pc = 0xf010;
                boot_phase(BOOT_OFF);
                debug_printf(VERBOSE_INFO,
                             "Native PCW bootstrap: executing sector at F010");
            }
        }
        break;
    case BOOT_WAIT:
        /* Space retries after failure; reset and disk autoload also rearm us. */
        if (pcw_read_keyboard(0x3ff5) & 0x80) pcw_boot_begin();
        break;
    default:
        break;
    }
    return true;
}

bool pcw_boot_state(struct pcw_state_io *io)
{
    (void)pcw_state_value(io, 0x50434231, 0x50434231, 0x50434231);
    struct boot_state restored = boot;
#define FIELD(name, maximum) \
    restored.name = pcw_state_value(io, boot.name, 0, maximum)
    FIELD(phase, BOOT_WAIT);
    FIELD(elapsed, BOOT_TIMEOUT_CYCLES);
    FIELD(cursor, 9);
    FIELD(received, 512);
    FIELD(checksum, 255);
    FIELD(calibrated, 1);
    FIELD(status, 255);
#undef FIELD
    const int cursor_limits[] = { 0, 0, 0, 2, 1, 0, 0, 2, 8, 0, 7, 0 };
    if (io->ok && (restored.cursor > cursor_limits[restored.phase] ||
        (restored.phase > BOOT_OFF && restored.phase < BOOT_TRANSFER &&
         restored.received != 0) ||
        (restored.phase == BOOT_TRANSFER && restored.received >= 512)))
        io->ok = false;
    if (io->apply && io->ok) boot = restored;
    return io->ok;
}
