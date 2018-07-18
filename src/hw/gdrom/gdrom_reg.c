/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2017, 2018 snickerbockers
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

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "mount.h"
#include "error.h"
#include "mem_code.h"
#include "types.h"
#include "MemoryMap.h"
#include "hw/sys/holly_intc.h"
#include "hw/sh4/sh4.h"
#include "hw/sh4/sh4_dmac.h"
#include "hw/g1/g1_reg.h"
#include "cdrom.h"
#include "dreamcast.h"
#include "gdrom_response.h"
#include "gdrom.h"
#include "mmio.h"

#include "gdrom_reg.h"

static uint32_t
gdrom_gdapro_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt);
static void
gdrom_gdapro_mmio_write(struct mmio_region_g1_reg_32 *region,
                        unsigned idx, uint32_t val, void *ctxt);
static uint32_t
gdrom_g1gdrc_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt);
static void
gdrom_g1gdrc_mmio_write(struct mmio_region_g1_reg_32 *region,
                        unsigned idx, uint32_t val, void *ctxt);
static uint32_t
gdrom_gdstar_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt);
static void
gdrom_gdstar_mmio_write(struct mmio_region_g1_reg_32 *region,
                        unsigned idx, uint32_t val, void *ctxt);
static uint32_t
gdrom_gdlen_mmio_read(struct mmio_region_g1_reg_32 *region,
                      unsigned idx, void *ctxt);
static void
gdrom_gdlen_mmio_write(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, uint32_t val, void *ctxt);
static uint32_t
gdrom_gddir_mmio_read(struct mmio_region_g1_reg_32 *region,
                      unsigned idx, void *ctxt);
static void
gdrom_gddir_mmio_write(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, uint32_t val, void *ctxt);
static uint32_t
gdrom_gden_mmio_read(struct mmio_region_g1_reg_32 *region,
                     unsigned idx, void *ctxt);
static void
gdrom_gden_mmio_write(struct mmio_region_g1_reg_32 *region,
                      unsigned idx, uint32_t val, void *ctxt);
static uint32_t
gdrom_gdst_reg_read_handler(struct mmio_region_g1_reg_32 *region,
                            unsigned idx, void *ctxt);
static void
gdrom_gdst_reg_write_handler(struct mmio_region_g1_reg_32 *region,
                             unsigned idx, uint32_t val, void *ctxt);
static uint32_t
gdrom_gdlend_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt);

static reg32_t
gdrom_get_status_reg(struct gdrom_status const *stat_in);
static reg32_t
gdrom_get_error_reg(struct gdrom_error const *error_in);
static reg32_t
gdrom_get_int_reason_reg(struct gdrom_int_reason *int_reason_in);

static void
gdrom_set_features_reg(struct gdrom_features *features_out, reg32_t feat_reg);
static void
gdrom_set_sect_cnt_reg(struct gdrom_sector_count *sect_cnt_out,
                       reg32_t sect_cnt_reg);
static void
gdrom_set_dev_ctrl_reg(struct gdrom_dev_ctrl *dev_ctrl_out,
                       reg32_t dev_ctrl_reg);

////////////////////////////////////////////////////////////////////////////////
//
// status flags (for REQ_STAT and the sector-number register)
//
////////////////////////////////////////////////////////////////////////////////

#define SEC_NUM_STATUS_SHIFT 0
#define SEC_NUM_STATUS_MASK (0xf << SEC_NUM_STATUS_SHIFT)

#define SEC_NUM_DISC_TYPE_SHIFT 4
#define SEC_NUM_DISC_TYPE_MASK (0xf << SEC_NUM_DISC_TYPE_SHIFT)

#define SEC_NUM_FMT_SHIFT 4
#define SEC_NUM_FMT_MASK (0xf << SEC_NUM_FMT_SHIFT)

#define GDROM_REG_BASE 0x5f7080

#define ATA_REG_ALT_STATUS_ADDR 0x5f7018

#define ATA_REG_RW_DATA        0
#define ATA_REG_W_FEAT         1
#define ATA_REG_R_ERROR        1
#define ATA_REG_R_INT_REASON   2
#define ATA_REG_W_SEC_CNT      2
#define ATA_REG_R_SEC_NUM      3
#define ATA_REG_RW_BYTE_CNT_LO 4
#define ATA_REG_RW_BYTE_CNT_HI 5
#define ATA_REG_RW_DRIVE_SEL   6
#define ATA_REG_R_STATUS       7
#define ATA_REG_W_CMD          7

DEF_MMIO_REGION(gdrom_reg_32, N_GDROM_REGS, ADDR_GDROM_FIRST, uint32_t)
DEF_MMIO_REGION(gdrom_reg_8, N_GDROM_REGS, ADDR_GDROM_FIRST, uint8_t)

float gdrom_reg_read_float(addr32_t addr, void *ctxt) {
    uint32_t tmp = gdrom_reg_read_32(addr, ctxt);
    float val;
    memcpy(&val, &tmp, sizeof(val));
    return val;
}

void gdrom_reg_write_float(addr32_t addr, float val, void *ctxt) {
    uint32_t tmp;
    memcpy(&tmp, &val, sizeof(tmp));
    gdrom_reg_write_32(addr, tmp, ctxt);
}

double gdrom_reg_read_double(addr32_t addr, void *ctxt) {
    error_set_length(8);
    error_set_address(addr);
    RAISE_ERROR(ERROR_UNIMPLEMENTED);
}

void gdrom_reg_write_double(addr32_t addr, double val, void *ctxt) {
    error_set_length(8);
    error_set_address(addr);
    RAISE_ERROR(ERROR_UNIMPLEMENTED);
}

uint8_t gdrom_reg_read_8(addr32_t addr, void *ctxt) {
    unsigned idx = (addr - GDROM_REG_BASE) / 4;
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    uint8_t buf;

    switch (idx) {
    case ATA_REG_RW_DATA:
        gdrom_read_data(gdrom_ctxt, (uint8_t*)&buf, sizeof(buf));
        return buf;
    case ATA_REG_R_ERROR:
        buf = gdrom_get_error_reg(&gdrom_ctxt->error_reg);
        GDROM_TRACE("read 0x%02x from error register\n", (unsigned)buf);
        return buf;
    case ATA_REG_R_INT_REASON:
        buf = gdrom_get_int_reason_reg(&gdrom_ctxt->int_reason_reg);
        GDROM_TRACE("int_reason is 0x%08x\n", (unsigned)buf);
        return buf;
    case ATA_REG_R_SEC_NUM:
        return ((uint8_t)gdrom_get_drive_state() << SEC_NUM_STATUS_SHIFT) |
            ((uint8_t)gdrom_get_disc_type() << SEC_NUM_DISC_TYPE_SHIFT);
    case ATA_REG_RW_BYTE_CNT_LO:
        buf = gdrom_ctxt->data_byte_count & 0xff;
        GDROM_TRACE("read 0x%02x from byte_count_low\n", (unsigned)buf);
        if (gdrom_ctxt->data_byte_count > UINT16_MAX) {
            error_set_feature("reading more than 64 kilobytes from GD-ROM");
            RAISE_ERROR(ERROR_UNIMPLEMENTED);
        }
        return buf;
    case ATA_REG_RW_BYTE_CNT_HI:
        buf = (gdrom_ctxt->data_byte_count & 0xff00) >> 8;
        GDROM_TRACE("read 0x%02x from byte_count_high\n", (unsigned)buf);
        if (gdrom_ctxt->data_byte_count > UINT16_MAX) {
            error_set_feature("reading more than 64 kilobytes from GD-ROM");
            RAISE_ERROR(ERROR_UNIMPLEMENTED);
        }
        return buf;
        break;
    case ATA_REG_RW_DRIVE_SEL:
        return gdrom_ctxt->drive_sel_reg;
    case ATA_REG_R_STATUS:
        /*
         * XXX
         * For the most part, I try to keep all the logic in gdrom.c and all the
         * encoding/decoding here in gdrom_reg.c (ie, gdrom.c manages the system
         * state and gdrom_reg.c translates data into/from the format the guest
         * software expects it to be in).
         *
         * This part here where I clear the interrupt flag is an exception to that
         * rule because I didn't it was worth it to add a layer of indirection to
         * this single function call.  If this function did more than just read from
         * a register and clear the interrupt flag, then I would have some
         * infrastructure in place to do that on its behalf in gdrom.c
         */
        holly_clear_ext_int(HOLLY_EXT_INT_GDROM);

        buf = gdrom_get_status_reg(&gdrom_ctxt->stat_reg);
        GDROM_TRACE("read 0x%02x from status register\n", (unsigned)buf);
        return buf;
    }

    if (addr == ATA_REG_ALT_STATUS_ADDR) {
        uint8_t stat_bin = gdrom_get_status_reg(&gdrom_ctxt->stat_reg);

        GDROM_TRACE("read 0x%02x from alternate status register\n",
                    (unsigned)stat_bin);
        return stat_bin;
    }

    error_set_address(addr);
    error_set_length(1);
    RAISE_ERROR(ERROR_UNIMPLEMENTED);
}

void gdrom_reg_write_8(addr32_t addr, uint8_t val, void *ctxt) {
    unsigned idx = (addr - GDROM_REG_BASE) / 4;
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;

    switch (idx) {
    case ATA_REG_RW_DATA:
        gdrom_write_data(gdrom_ctxt, (uint8_t*)&val, sizeof(val));
        return;
    case ATA_REG_W_FEAT:
        GDROM_TRACE("write 0x%08x to the features register\n", (unsigned)val);
        gdrom_set_features_reg(&gdrom_ctxt->feat_reg, val);
        return;
    case ATA_REG_W_SEC_CNT:
        GDROM_TRACE("Write %08x to sec_cnt_reg\n", (unsigned)val);
        gdrom_set_sect_cnt_reg(&gdrom_ctxt->sect_cnt_reg, val);
        return;
    case ATA_REG_RW_BYTE_CNT_LO:
        GDROM_TRACE("write 0x%02x to byte_count_low\n", (unsigned)(val & 0xff));
        gdrom_ctxt->data_byte_count =
            (gdrom_ctxt->data_byte_count & ~0xff) | (val & 0xff);
        return;
    case ATA_REG_RW_BYTE_CNT_HI:
        GDROM_TRACE("write 0x%02x to byte_count_high\n",
                    (unsigned)((val & 0xff) << 8));
        gdrom_ctxt->data_byte_count =
            (gdrom_ctxt->data_byte_count & ~0xff00) | ((val & 0xff) << 8);
        return;
    case ATA_REG_RW_DRIVE_SEL:
        gdrom_ctxt->drive_sel_reg = val;
        return;
    case ATA_REG_W_CMD:
        GDROM_TRACE("write 0x%x to command register (4 bytes)\n",
                    (unsigned)val);
        gdrom_input_cmd(gdrom_ctxt, val);
        return;
    }

    if (addr == ATA_REG_ALT_STATUS_ADDR) {
        gdrom_set_dev_ctrl_reg(&gdrom_ctxt->dev_ctrl_reg, val);
        GDROM_TRACE("Write %08x to dev_ctrl_reg\n", (unsigned)val);
        return;
    }

    error_set_address(addr);
    error_set_length(1);
    RAISE_ERROR(ERROR_UNIMPLEMENTED);
}

uint16_t gdrom_reg_read_16(addr32_t addr, void *ctxt) {
    unsigned idx = (addr - GDROM_REG_BASE) / 4;
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    uint16_t buf;

    if (idx == ATA_REG_RW_DATA) {
        gdrom_read_data(gdrom_ctxt, (uint8_t*)&buf, sizeof(buf));
        return buf;
    } else {
        error_set_address(addr);
        error_set_length(4);
        RAISE_ERROR(ERROR_UNIMPLEMENTED);
    }
}

void gdrom_reg_write_16(addr32_t addr, uint16_t val, void *ctxt) {
    unsigned idx = (addr - GDROM_REG_BASE) / 4;
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;

    if (idx == ATA_REG_RW_DATA) {
        gdrom_write_data(gdrom_ctxt, (uint8_t*)&val, sizeof(val));
    } else {
        error_set_address(addr);
        error_set_length(4);
        RAISE_ERROR(ERROR_UNIMPLEMENTED);
    }
}

uint32_t gdrom_reg_read_32(addr32_t addr, void *ctxt) {
    unsigned idx = (addr - GDROM_REG_BASE) / 4;
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    uint32_t buf;

    switch (idx) {
    case ATA_REG_RW_DATA:
        gdrom_read_data(gdrom_ctxt, (uint8_t*)&buf, sizeof(buf));
        return buf;
    case ATA_REG_R_ERROR:
        buf = gdrom_get_error_reg(&gdrom_ctxt->error_reg);
        GDROM_TRACE("read 0x%02x from error register\n", (unsigned)buf);
        return buf;
    case ATA_REG_R_INT_REASON:
        buf = gdrom_get_int_reason_reg(&gdrom_ctxt->int_reason_reg);
        GDROM_TRACE("int_reason is 0x%08x\n", (unsigned)buf);
        return buf;
    case ATA_REG_R_SEC_NUM:
        return ((uint32_t)gdrom_get_drive_state() << SEC_NUM_STATUS_SHIFT) |
            ((uint32_t)gdrom_get_disc_type() << SEC_NUM_DISC_TYPE_SHIFT);
    case ATA_REG_RW_BYTE_CNT_LO:
        buf = gdrom_ctxt->data_byte_count & 0xff;
        GDROM_TRACE("read 0x%02x from byte_count_low\n", (unsigned)buf);
        return buf;
    case ATA_REG_RW_BYTE_CNT_HI:
        buf = (gdrom_ctxt->data_byte_count & 0xff00) >> 8;
        GDROM_TRACE("read 0x%02x from byte_count_high\n", (unsigned)buf);
        return buf;
        break;
    case ATA_REG_RW_DRIVE_SEL:
        return gdrom_ctxt->drive_sel_reg;
    case ATA_REG_R_STATUS:
        /*
         * XXX
         * For the most part, I try to keep all the logic in gdrom.c and all the
         * encoding/decoding here in gdrom_reg.c (ie, gdrom.c manages the system
         * state and gdrom_reg.c translates data into/from the format the guest
         * software expects it to be in).
         *
         * This part here where I clear the interrupt flag is an exception to that
         * rule because I didn't it was worth it to add a layer of indirection to
         * this single function call.  If this function did more than just read from
         * a register and clear the interrupt flag, then I would have some
         * infrastructure in place to do that on its behalf in gdrom.c
         */
        holly_clear_ext_int(HOLLY_EXT_INT_GDROM);

        buf = gdrom_get_status_reg(&gdrom_ctxt->stat_reg);
        GDROM_TRACE("read 0x%02x from status register\n", (unsigned)buf);
        return buf;
    }

    error_set_address(addr);
    error_set_length(4);
    RAISE_ERROR(ERROR_UNIMPLEMENTED);
}

void gdrom_reg_write_32(addr32_t addr, uint32_t val, void *ctxt) {
    unsigned idx = (addr - GDROM_REG_BASE) / 4;
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;

    switch (idx) {
    case ATA_REG_RW_DATA:
        gdrom_write_data(gdrom_ctxt, (uint8_t*)&val, sizeof(val));
        return;
    case ATA_REG_W_FEAT:
        GDROM_TRACE("write 0x%08x to the features register\n", (unsigned)val);
        gdrom_set_features_reg(&gdrom_ctxt->feat_reg, val);
        return;
    case ATA_REG_W_SEC_CNT:
        GDROM_TRACE("Write %08x to sec_cnt_reg\n", (unsigned)val);
        gdrom_set_sect_cnt_reg(&gdrom_ctxt->sect_cnt_reg, val);
        return;
    case ATA_REG_RW_BYTE_CNT_LO:
        GDROM_TRACE("write 0x%02x to byte_count_low\n", (unsigned)(val & 0xff));
        gdrom_ctxt->data_byte_count =
            (gdrom_ctxt->data_byte_count & ~0xff) | (val & 0xff);
        return;
    case ATA_REG_RW_BYTE_CNT_HI:
        GDROM_TRACE("write 0x%02x to byte_count_high\n",
                    (unsigned)((val & 0xff) << 8));
        gdrom_ctxt->data_byte_count =
            (gdrom_ctxt->data_byte_count & ~0xff00) | ((val & 0xff) << 8);
        return;
    case ATA_REG_RW_DRIVE_SEL:
        gdrom_ctxt->drive_sel_reg = val;
        return;
    case ATA_REG_W_CMD:
        GDROM_TRACE("write 0x%x to command register (4 bytes)\n",
                    (unsigned)val);
        gdrom_input_cmd(gdrom_ctxt, val);
        return;
    }

    error_set_address(addr);
    error_set_length(4);
    RAISE_ERROR(ERROR_UNIMPLEMENTED);
}

static uint32_t
gdrom_gdapro_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from GDAPRO\n", gdrom_ctxt->gdapro_reg);
    return gdrom_ctxt->gdapro_reg;
}

static void
gdrom_gdapro_mmio_write(struct mmio_region_g1_reg_32 *region,
                        unsigned idx, uint32_t val, void *ctxt) {
    // check security code
    if ((val & 0xffff0000) != 0x88430000)
        return;

    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    gdrom_ctxt->gdapro_reg = val;

    GDROM_TRACE("GDAPRO (0x%08x) - allowing writes from 0x%08x through "
                "0x%08x\n",
                gdrom_ctxt->gdapro_reg,
                gdrom_dma_prot_top(gdrom_ctxt), gdrom_dma_prot_bot(gdrom_ctxt));
}

static uint32_t
gdrom_g1gdrc_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from G1GDRC\n", gdrom_ctxt->g1gdrc_reg);
    return gdrom_ctxt->g1gdrc_reg;
}

static void
gdrom_g1gdrc_mmio_write(struct mmio_region_g1_reg_32 *region,
                        unsigned idx, uint32_t val, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("write %08x to G1GDRC\n", gdrom_ctxt->g1gdrc_reg);
    gdrom_ctxt->g1gdrc_reg = val;
}

static uint32_t
gdrom_gdstar_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from GDSTAR\n", gdrom_ctxt->dma_start_addr_reg);
    return gdrom_ctxt->dma_start_addr_reg;
}

static void
gdrom_gdstar_mmio_write(struct mmio_region_g1_reg_32 *region,
                        unsigned idx, uint32_t val, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    gdrom_ctxt->dma_start_addr_reg = val;
    gdrom_ctxt->dma_start_addr_reg &= ~0xe0000000;
    GDROM_TRACE("write %08x to GDSTAR\n", gdrom_ctxt->dma_start_addr_reg);
}

static uint32_t
gdrom_gdlen_mmio_read(struct mmio_region_g1_reg_32 *region,
                      unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from GDLEN\n", gdrom_ctxt->dma_len_reg);
    return gdrom_ctxt->dma_len_reg;
}

static void
gdrom_gdlen_mmio_write(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, uint32_t val, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    gdrom_ctxt->dma_len_reg = val;
    GDROM_TRACE("write %08x to GDLEN\n", gdrom_ctxt->dma_len_reg);
}

static uint32_t
gdrom_gddir_mmio_read(struct mmio_region_g1_reg_32 *region,
                      unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from GDDIR\n", gdrom_ctxt->dma_dir_reg);
    return gdrom_ctxt->dma_dir_reg;
}

static void
gdrom_gddir_mmio_write(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, uint32_t val, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    gdrom_ctxt->dma_dir_reg = val;
    GDROM_TRACE("write %08x to GDDIR\n", gdrom_ctxt->dma_dir_reg);
}

static uint32_t
gdrom_gden_mmio_read(struct mmio_region_g1_reg_32 *region,
                     unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from GDEN\n", gdrom_ctxt->dma_en_reg);
    return gdrom_ctxt->dma_en_reg;
}

static void
gdrom_gden_mmio_write(struct mmio_region_g1_reg_32 *region,
                      unsigned idx, uint32_t val, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    gdrom_ctxt->dma_en_reg = val;
    GDROM_TRACE("write %08x to GDEN\n", gdrom_ctxt->dma_en_reg);
}

static uint32_t
gdrom_gdst_reg_read_handler(struct mmio_region_g1_reg_32 *region,
                            unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from GDST\n", gdrom_ctxt->dma_start_reg);
    return gdrom_ctxt->dma_start_reg;
}

static void
gdrom_gdst_reg_write_handler(struct mmio_region_g1_reg_32 *region,
                             unsigned idx, uint32_t val, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    gdrom_ctxt->dma_start_reg = val;
    GDROM_TRACE("write %08x to GDST\n", gdrom_ctxt->dma_start_reg);
    gdrom_start_dma(gdrom_ctxt);
}

static uint32_t
gdrom_gdlend_mmio_read(struct mmio_region_g1_reg_32 *region,
                       unsigned idx, void *ctxt) {
    struct gdrom_ctxt *gdrom_ctxt = (struct gdrom_ctxt*)ctxt;
    GDROM_TRACE("read %08x from GDLEND\n", gdrom_ctxt->gdlend_reg);
    return gdrom_ctxt->gdlend_reg;
}

////////////////////////////////////////////////////////////////////////////////
//
// Error register flags
//
////////////////////////////////////////////////////////////////////////////////

#define GDROM_ERROR_SENSE_KEY_SHIFT 4
#define GDROM_ERROR_SENSE_KEY_MASK (0xf << GDROM_ERROR_SENSE_KEY_SHIFT)

#define GDROM_ERROR_MCR_SHIFT 3
#define GDROM_ERROR_MCR_MASK (1 << GDROM_ERROR_MCR_SHIFT)

#define GDROM_ERROR_ABRT_SHIFT 2
#define GDROM_ERROR_ABRT_MASK (1 << GDROM_ERROR_ABRT_SHIFT)

#define GDROM_ERROR_EOMF_SHIFT 1
#define GDROM_ERROR_EOMF_MASK (1 << GDROM_ERROR_EOMF_SHIFT)

#define GDROM_ERROR_ILI_SHIFT 0
#define GDROM_ERROR_ILI_MASK (1 << GDROM_ERROR_ILI_SHIFT)

static reg32_t gdrom_get_error_reg(struct gdrom_error const *error_in) {
    reg32_t error_reg =
        (((reg32_t)error_in->sense_key) << GDROM_ERROR_SENSE_KEY_SHIFT) &
        GDROM_ERROR_SENSE_KEY_MASK;

    if (error_in->ili)
        error_reg |= GDROM_ERROR_ILI_MASK;
    if (error_in->eomf)
        error_reg |= GDROM_ERROR_EOMF_MASK;
    if (error_in->abrt)
        error_reg |= GDROM_ERROR_ABRT_MASK;
    if (error_in->mcr)
        error_reg |= GDROM_ERROR_MCR_MASK;

    return error_reg;
}

////////////////////////////////////////////////////////////////////////////////
//
// Status register flags
//
////////////////////////////////////////////////////////////////////////////////

// the drive is processing a command
#define GDROM_STAT_BSY_SHIFT 7
#define GDROM_STAT_BSY_MASK (1 << GDROM_STAT_BSY_SHIFT)

// response to ATA command is possible
#define GDROM_STAT_DRDY_SHIFT 6
#define GDROM_STAT_DRDY_MASK (1 << GDROM_STAT_DRDY_SHIFT)

// drive fault
#define GDROM_STAT_DF_SHIFT 5
#define GDROM_STAT_DF_MASK (1 << GDROM_STAT_DF_SHIFT)

// seek processing is complete
#define GDROM_STAT_DSC_SHIFT 4
#define GDROM_STAT_DSC_MASK (1 << GDROM_STAT_DSC_SHIFT)

// data transfer possible
#define GDROM_STAT_DRQ_SHIFT 3
#define GDROM_STAT_DRQ_MASK (1 << GDROM_STAT_DRQ_SHIFT)

// correctable error flag
#define GDROM_STAT_CORR_SHIFT 2
#define GDROM_STAT_CORR_MASK (1 << GDROM_STAT_CORR_SHIFT)

// error flag
#define GDROM_STAT_CHECK_SHIFT 0
#define GDROM_STAT_CHECK_MASK (1 << GDROM_STAT_CHECK_SHIFT)

static reg32_t gdrom_get_status_reg(struct gdrom_status const *stat_in) {
    reg32_t stat_reg = 0;

    if (stat_in->bsy)
        stat_reg |= GDROM_STAT_BSY_MASK;
    if (stat_in->drdy)
        stat_reg |= GDROM_STAT_DRDY_MASK;
    if (stat_in->df)
        stat_reg |= GDROM_STAT_DF_MASK;
    if (stat_in->dsc)
        stat_reg |= GDROM_STAT_DSC_MASK;
    if (stat_in->drq)
        stat_reg |= GDROM_STAT_DRQ_MASK;
    if (stat_in->corr)
        stat_reg |= GDROM_STAT_CORR_MASK;
    if (stat_in->check)
        stat_reg |= GDROM_STAT_CHECK_MASK;

    return stat_reg;
}

////////////////////////////////////////////////////////////////////////////////
//
// feature register flags
//
////////////////////////////////////////////////////////////////////////////////

#define FEAT_REG_DMA_SHIFT 0
#define FEAT_REG_DMA_MASK (1 << FEAT_REG_DMA_SHIFT)

static void
gdrom_set_features_reg(struct gdrom_features *features_out, reg32_t feat_reg) {
    if (feat_reg & FEAT_REG_DMA_MASK)
        features_out->dma_enable = true;
    else
        features_out->dma_enable = false;

    if ((feat_reg & 0x7f) == 3)
        features_out->set_feat_enable = true;
    else
        features_out->set_feat_enable = false;
}

////////////////////////////////////////////////////////////////////////////////
//
// Transfer Modes (for the sector count register in GDROM_CMD_SEAT_FEAT)
//
////////////////////////////////////////////////////////////////////////////////

#define TRANS_MODE_PIO_DFLT_MASK        0xfe
#define TRANS_MODE_PIO_DFLT_VAL         0x00

#define TRANS_MODE_PIO_FLOW_CTRL_MASK   0xf8
#define TRANS_MODE_PIO_FLOW_CTRL_VAL    0x08

#define TRANS_MODE_SINGLE_WORD_DMA_MASK 0xf8
#define TRANS_MODE_SINGLE_WORD_DMA_VAL  0x10

#define TRANS_MODE_MULTI_WORD_DMA_MASK  0xf8
#define TRANS_MODE_MULTI_WORD_DMA_VAL   0x20

#define TRANS_MODE_PSEUDO_DMA_MASK      0xf8
#define TRANS_MODE_PSEUDO_DMA_VAL       0x18

#define SECT_CNT_MODE_VAL_SHIFT 0
#define SECT_CNT_MODE_VAL_MASK (0xf << SECT_CNT_MODE_VAL_SHIFT)

static void
gdrom_set_sect_cnt_reg(struct gdrom_sector_count *sect_cnt_out,
                       reg32_t sect_cnt_reg) {
    unsigned mode_val =
        (sect_cnt_reg & SECT_CNT_MODE_VAL_MASK) >> SECT_CNT_MODE_VAL_SHIFT;
    if ((sect_cnt_reg & TRANS_MODE_PIO_DFLT_MASK) ==
        TRANS_MODE_PIO_DFLT_VAL) {
        sect_cnt_out->trans_mode = TRANS_MODE_PIO_DFLT;
    } else if ((sect_cnt_reg & TRANS_MODE_PIO_FLOW_CTRL_MASK) ==
               TRANS_MODE_PIO_FLOW_CTRL_VAL) {
        sect_cnt_out->trans_mode = TRANS_MODE_PIO_FLOW_CTRL;
    } else if ((sect_cnt_reg & TRANS_MODE_SINGLE_WORD_DMA_MASK) ==
               TRANS_MODE_SINGLE_WORD_DMA_VAL) {
        sect_cnt_out->trans_mode = TRANS_MODE_SINGLE_WORD_DMA;
    } else if ((sect_cnt_reg & TRANS_MODE_MULTI_WORD_DMA_MASK) ==
               TRANS_MODE_MULTI_WORD_DMA_VAL) {
        sect_cnt_out->trans_mode = TRANS_MODE_MULTI_WORD_DMA;
    } else if ((sect_cnt_reg & TRANS_MODE_PSEUDO_DMA_MASK) ==
               TRANS_MODE_PSEUDO_DMA_VAL) {
        sect_cnt_out->trans_mode = TRANS_MODE_PSEUDO_DMA;
    } else {
        // TODO: maybe this should be a soft warning instead of an error
        GDROM_TRACE("unrecognized transfer mode (sec_cnt_reg is 0x%08x)\n",
                    sect_cnt_reg);
        error_set_feature("unrecognized transfer mode\n");
        RAISE_ERROR(ERROR_UNIMPLEMENTED);
    }

    sect_cnt_out->mode_val = mode_val;
}

////////////////////////////////////////////////////////////////////////////////
//
// Interrupt Reason register flags
//
////////////////////////////////////////////////////////////////////////////////

// ready to receive command
#define INT_REASON_COD_SHIFT 0
#define INT_REASON_COD_MASK (1 << INT_REASON_COD_SHIFT)

/*
 * ready to receive data from software to drive if set
 * ready to send data from drive to software if not set
 */
#define INT_REASON_IO_SHIFT 1
#define INT_REASON_IO_MASK (1 << INT_REASON_IO_SHIFT)

static reg32_t gdrom_get_int_reason_reg(struct gdrom_int_reason *int_reason_in) {
    reg32_t reg_out = 0;

    if (int_reason_in->cod)
        reg_out |= INT_REASON_COD_MASK;
    if (int_reason_in->io)
        reg_out |= INT_REASON_IO_MASK;

    return reg_out;
}

////////////////////////////////////////////////////////////////////////////////
//
// Device control register flags
//
////////////////////////////////////////////////////////////////////////////////

#define DEV_CTRL_NIEN_SHIFT 1
#define DEV_CTRL_NIEN_MASK (1 << DEV_CTRL_NIEN_SHIFT)

#define DEV_CTRL_SRST_SHIFT 2
#define DEV_CTRL_SRST_MASK (1 << DEV_CTRL_SRST_SHIFT)

static void
gdrom_set_dev_ctrl_reg(struct gdrom_dev_ctrl *dev_ctrl_out,
                       reg32_t dev_ctrl_reg) {
    dev_ctrl_out->nien = (bool)(dev_ctrl_reg & DEV_CTRL_NIEN_MASK);
    dev_ctrl_out->srst = (bool)(dev_ctrl_reg & DEV_CTRL_SRST_MASK);
}

void gdrom_reg_init(struct gdrom_ctxt *gdrom) {
    /* GD-ROM DMA registers */
    g1_mmio_cell_init_32("SB_GDAPRO", 0x5f74b8,
                         gdrom_gdapro_mmio_read,
                         gdrom_gdapro_mmio_write, gdrom);
    g1_mmio_cell_init_32("SB_G1GDRC", 0x5f74a0,
                         gdrom_g1gdrc_mmio_read,
                         gdrom_g1gdrc_mmio_write, gdrom);
    g1_mmio_cell_init_32("SB_G1GDWC", 0x5f74a4,
                         mmio_region_g1_reg_32_warn_read_handler,
                         mmio_region_g1_reg_32_warn_write_handler,
                         gdrom);
    g1_mmio_cell_init_32("SB_GDSTAR", 0x5f7404,
                         gdrom_gdstar_mmio_read,
                         gdrom_gdstar_mmio_write,
                         gdrom);
    g1_mmio_cell_init_32("SB_GDLEN", 0x5f7408,
                         gdrom_gdlen_mmio_read,
                         gdrom_gdlen_mmio_write,
                         gdrom);
    g1_mmio_cell_init_32("SB_GDDIR", 0x5f740c,
                         gdrom_gddir_mmio_read,
                         gdrom_gddir_mmio_write,
                         gdrom);
    g1_mmio_cell_init_32("SB_GDEN", 0x5f7414,
                         gdrom_gden_mmio_read,
                         gdrom_gden_mmio_write, gdrom);
    g1_mmio_cell_init_32("SB_GDST", 0x5f7418,
                         gdrom_gdst_reg_read_handler,
                         gdrom_gdst_reg_write_handler, gdrom);
    g1_mmio_cell_init_32("SB_GDLEND", 0x005f74f8,
                         gdrom_gdlend_mmio_read,
                         mmio_region_g1_reg_32_readonly_write_error,
                         gdrom);
}

void gdrom_reg_cleanup(struct gdrom_ctxt *gdrom) {
}

struct memory_interface gdrom_reg_intf = {
    .read32 = gdrom_reg_read_32,
    .read16 = gdrom_reg_read_16,
    .read8 = gdrom_reg_read_8,
    .readfloat = gdrom_reg_read_float,
    .readdouble = gdrom_reg_read_double,

    .write32 = gdrom_reg_write_32,
    .write16 = gdrom_reg_write_16,
    .write8 = gdrom_reg_write_8,
    .writefloat = gdrom_reg_write_float,
    .writedouble = gdrom_reg_write_double
};
