/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2018, 2019 snickerbockers
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 ******************************************************************************/

#ifndef ARM7_H_
#define ARM7_H_

#include <stdbool.h>
#include <assert.h>

#include "washdc/error.h"
#include "dc_sched.h"
#include "washdc/MemoryMap.h"
#include "hw/aica/aica_wave_mem.h"
#include "washdc/hw/arm7/arm7_reg_idx.h"

/*
 * XXX  all available documentation on the dreamcast states that the ARM7 is
 * clocked at 45MHz.
 * I have run some very primitive tests and found that it appears to be more
 * like 5MHz.
 * For now I'm putting it at 10MHz just to be safe (the test I ran was VERY
 * primitive and did not cover all possible cases), but I do believe that this
 * could go all the way down to 5MHz.
 */
#define ARM7_CLOCK_SCALE (SCHED_FREQUENCY / (10 * 1000 * 1000))
static_assert(SCHED_FREQUENCY % (10 * 1000 * 1000) == 0,
              "scheduler frequency does not cleanly divide by SH4 frequency");

// negative/less-than
#define ARM7_CPSR_N_SHIFT 31
#define ARM7_CPSR_N_MASK (1 << ARM7_CPSR_N_SHIFT)

// zero
#define ARM7_CPSR_Z_SHIFT 30
#define ARM7_CPSR_Z_MASK (1 << ARM7_CPSR_Z_SHIFT)

// carry/borrow/extend
#define ARM7_CPSR_C_SHIFT 29
#define ARM7_CPSR_C_MASK (1 << ARM7_CPSR_C_SHIFT)

// overflow
#define ARM7_CPSR_V_SHIFT 28
#define ARM7_CPSR_V_MASK (1 << ARM7_CPSR_V_SHIFT)

// IRQ disable
#define ARM7_CPSR_I_SHIFT 7
#define ARM7_CPSR_I_MASK (1 << ARM7_CPSR_I_SHIFT)

// FIQ disable
#define ARM7_CPSR_F_SHIFT 6
#define ARM7_CPSR_F_MASK (1 << ARM7_CPSR_F_SHIFT)

// CPU mode
#define ARM7_CPSR_M_SHIFT 0
#define ARM7_CPSR_M_MASK (0x1f << ARM7_CPSR_M_SHIFT)

enum arm7_mode {
    ARM7_MODE_USER = (0x10 << ARM7_CPSR_M_SHIFT),
    ARM7_MODE_FIQ  = (0x11 << ARM7_CPSR_M_SHIFT),
    ARM7_MODE_IRQ  = (0x12 << ARM7_CPSR_M_SHIFT),
    ARM7_MODE_SVC  = (0x13 << ARM7_CPSR_M_SHIFT),
    ARM7_MODE_ABT  = (0x17 << ARM7_CPSR_M_SHIFT),
    ARM7_MODE_UND  = (0x1b << ARM7_CPSR_M_SHIFT)
};

/*
 * ARM7DI-type CPU wired into the AICA sound system.
 *
 * Like the SH4, it supports both little-endian and big-endian byte orders.
 * AFAIK, this is always little-endian on the Dreamcast.  Documentation seems to
 * indicate the endianess is set by an external pin on the CPU, and that is
 * hopefully hardwired into LE mode.
 */

enum arm7_excp {
    ARM7_EXCP_NONE = 0,
    ARM7_EXCP_RESET = 1,
    ARM7_EXCP_DATA_ABORT = 2,
    ARM7_EXCP_FIQ = 4,
    ARM7_EXCP_IRQ = 8,
    ARM7_EXCP_PREF_ABORT = 16,
    ARM7_EXCP_SWI = 32
};

typedef uint32_t arm7_inst;

typedef bool(*arm7_irq_fn)(void *dat);

struct arm7 {
    /*
     * For the sake of instruction-fetching, ARM7 disregards the memory_map and
     * goes straight here.  This is less modular than going to the memory_map
     * since it hardcodes for AICA's memory map but needs must.
     */
    struct aica_wave_mem *inst_mem;
    struct dc_clock *clk;
    struct memory_map *map;

    uint32_t reg[ARM7_REGISTER_COUNT];

    unsigned extra_cycles;

    /*
     * One oddity about ARM7 (compared to saner CPUs like x86 and SH4) is that
     * the CPU does not hide its pipelining from software.  The Program Counter
     * register (ARM7_REG_R15) always points to the instruction being fetched;
     * since there's a 3-stage pipeline which is *not* hidden from software,
     * that means that ARM7_REG_R15 always points two instructions ahead of the
     * instruction being executed.
     *
     * For the sake of simplicity, this interpreter will actually mimic this
     * design by buffering three instructions in a fake "pipeline".  pipeline[2]
     * buffers the execution stage (ARM7_REG_R15 - 8), pipeline[1] buffers the
     * decoding stage (ARM7_REG_R15 - 4), and pipeline[0] buffers the fetch
     * stage (ARM7_REG_R15).  Instructions are actually fetched two cycles ahead
     * of their execution like in a real ARM, but the decoding isn't done until
     * it's at the execution stage.
     */
    arm7_inst pipeline[2];
    uint32_t pipeline_pc[2];

    enum arm7_excp excp;

    bool enabled;

    bool fiq_line;
};

void arm7_init(struct arm7 *arm7, struct dc_clock *clk, struct aica_wave_mem *inst_mem);
void arm7_cleanup(struct arm7 *arm7);

void arm7_set_mem_map(struct arm7 *arm7, struct memory_map *arm7_mem_map);

void arm7_reset(struct arm7 *arm7, bool val);

void arm7_get_regs(struct arm7 *arm7, void *dat_out);

uint32_t arm7_pc_next(struct arm7 *arm7);

void arm7_set_fiq(struct arm7 *arm7);
void arm7_clear_fiq(struct arm7 *arm7);

void arm7_excp_refresh(struct arm7 *arm7);

inline static uint32_t *arm7_gen_reg(struct arm7 *arm7, unsigned reg) {
    unsigned idx_actual;
    switch (arm7->reg[ARM7_REG_CPSR] & ARM7_CPSR_M_MASK) {
    case ARM7_MODE_USER:
        idx_actual = reg + ARM7_REG_R0;
        break;
    case ARM7_MODE_FIQ:
        if (reg >= 8 && reg <= 14)
            idx_actual = (reg - 8) + ARM7_REG_R8_FIQ;
        else
            idx_actual = reg + ARM7_REG_R0;
        break;
    case ARM7_MODE_IRQ:
        if (reg >= 13 && reg <= 14)
            idx_actual = (reg - 13) + ARM7_REG_R13_IRQ;
        else
            idx_actual = reg + ARM7_REG_R0;
        break;
    case ARM7_MODE_SVC:
        if (reg >= 13 && reg <= 14)
            idx_actual = (reg - 13) + ARM7_REG_R13_SVC;
        else
            idx_actual = reg + ARM7_REG_R0;
        break;
    case ARM7_MODE_ABT:
        if (reg >= 13 && reg <= 14)
            idx_actual = (reg - 13) + ARM7_REG_R13_ABT;
        else
            idx_actual = reg + ARM7_REG_R0;
        break;
    case ARM7_MODE_UND:
        if (reg >= 13 && reg <= 14)
            idx_actual = (reg - 13) + ARM7_REG_R13_UND;
        else
            idx_actual = reg + ARM7_REG_R0;
        break;
    default:
        RAISE_ERROR(ERROR_UNIMPLEMENTED);
    }

    return arm7->reg + idx_actual;
}

typedef unsigned(*arm7_op_fn)(struct arm7*,arm7_inst);
arm7_op_fn arm7_decode(struct arm7 *arm7, arm7_inst inst);

static inline uint32_t arm7_do_fetch_inst(struct arm7 *arm7, uint32_t addr);

/*
 * call this when something like a branch or exception happens that invalidates
 * instructions in the pipeline.
 *
 * This won't effect the PC, but it will clear out anything already in the
 * pipeline.  What that means is that anything in the pipeline which hasn't
 * been executed yet will get trashed.  The upshot of this is that it's only
 * safe to call arm7_reset_pipeline when the PC has actually changed.
 */
static inline void arm7_reset_pipeline(struct arm7 *arm7) {
    uint32_t pc = arm7->reg[ARM7_REG_PC];

    arm7->extra_cycles = 2;

    arm7->pipeline_pc[0] = pc + 4;
    arm7->pipeline[0] = arm7_do_fetch_inst(arm7, pc + 4);

    arm7->pipeline_pc[1] = pc;
    arm7->pipeline[1] = arm7_do_fetch_inst(arm7, pc);

    pc += 8;
    arm7->reg[ARM7_REG_PC] = pc;
}

static inline uint32_t arm7_do_fetch_inst(struct arm7 *arm7, uint32_t addr) {
    if (addr <= 0x007fffff)
        return aica_wave_mem_read_32(addr & 0x001fffff, arm7->inst_mem);
    return ~0;
}

static inline arm7_inst arm7_fetch_inst(struct arm7 *arm7, int *extra_cycles) {
    uint32_t pc = arm7->reg[ARM7_REG_PC];

    arm7_inst inst_fetched = arm7_do_fetch_inst(arm7, pc);
    uint32_t newpc = arm7->pipeline_pc[0];
    arm7_inst newinst = arm7->pipeline[0];
    arm7_inst ret = arm7->pipeline[1];

    arm7->pipeline_pc[0] = pc;
    arm7->pipeline[0] = inst_fetched;
    arm7->pipeline_pc[1] = newpc;
    arm7->pipeline[1] = newinst;

    *extra_cycles = arm7->extra_cycles;
    arm7->extra_cycles = 0;

    return ret;
}

#endif
