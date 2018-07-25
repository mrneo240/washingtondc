/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2016-2018 snickerbockers
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

#ifndef MEMORYMAP_H_
#define MEMORYMAP_H_

#include <stdint.h>
#include <stdbool.h>

#include "mem_areas.h"

typedef
float(*memory_map_readfloat_func)(uint32_t addr, void *ctxt);
typedef
double(*memory_map_readdouble_func)(uint32_t addr, void *ctxt);
typedef
uint32_t(*memory_map_read32_func)(uint32_t addr, void *ctxt);
typedef
uint16_t(*memory_map_read16_func)(uint32_t addr, void *ctxt);
typedef
uint8_t(*memory_map_read8_func)(uint32_t addr, void *ctxt);

typedef
void(*memory_map_writefloat_func)(uint32_t addr, float val, void *ctxt);
typedef
void(*memory_map_writedouble_func)(uint32_t addr, double val, void *ctxt);
typedef
void(*memory_map_write32_func)(uint32_t addr, uint32_t val, void *ctxt);
typedef
void(*memory_map_write16_func)(uint32_t addr, uint16_t val, void *ctxt);
typedef
void(*memory_map_write8_func)(uint32_t addr, uint8_t val, void *ctxt);

/*
 * read/write functions which will return an error instead of crashing if the
 * requested address has not been implemented.
 *
 * These functions don't need to be fast because they're primarily intended for
 * the debugger's benefit; this is why they take variable lengths instead of
 * having a special case for each variable type like the real read/write
 * handlers do.
 *
 * return 0 on success, nonzero on error
 */
typedef
float(*memory_map_readfloat_func)(uint32_t addr, void *ctxt);
typedef
double(*memory_map_readdouble_func)(uint32_t addr, void *ctxt);
typedef
uint32_t(*memory_map_read32_func)(uint32_t addr, void *ctxt);
typedef
uint16_t(*memory_map_read16_func)(uint32_t addr, void *ctxt);
typedef
uint8_t(*memory_map_read8_func)(uint32_t addr, void *ctxt);

typedef
void(*memory_map_writefloat_func)(uint32_t addr, float val, void *ctxt);
typedef
void(*memory_map_writedouble_func)(uint32_t addr, double val, void *ctxt);
typedef
void(*memory_map_write32_func)(uint32_t addr, uint32_t val, void *ctxt);
typedef
void(*memory_map_write16_func)(uint32_t addr, uint16_t val, void *ctxt);
typedef
void(*memory_map_write8_func)(uint32_t addr, uint8_t val, void *ctxt);

enum memory_map_region_id {
    MEMORY_MAP_REGION_UNKNOWN,
    MEMORY_MAP_REGION_RAM
};


#ifdef DETECT_SMC
/*
 * SELF-MODIFYING CODE DETECTION
 *
 * Each memory region has a flag set at initialization which determines whether
 * or not that region is executable.  This flag is never changed.
 *
 * Each executable region is divided into a number of equally-sized pages.
 *
 * The map as a whole has a "stamp" which denotes the number of times that
 * executable memory has been written to.  This stamp gets incremented on every
 * write to executable memory.  Each page has a copy of what the stamp was last
 * time it was written to.  Each code block (in the jit source) also has a copy
 * of what the stamp was when that code block was created.
 *
 * Each time a new code block is fetched, every page it spans is checked to see
 * if it that page's stamp is newer then the code block's stamp.  If so, the
 * code block is invalidated so that it can be regenerated.   After it is
 * regenerated, its stamp is updated to the current stamp.
 *
 * This leaves the problem of what to do when the stamp overflows.  Most obvious
 * solution is to invalidate all code blocks.
 *
 * This scheme puts the onus to prevent self-modifying code on the dispatch
 * code.  This is suboptimal because it requires an O(N) search through
 * executable pages.  If I prevent code blocks from spanning multiple pages
 * then it becomes an O(1) search, which lessens the impact.  I could also
 * move the work into the memory write function by having a linked-list node in
 * every code block that's used to link it into a per-page list of code blocks.
 * This might be a better system and it might even mean I don't need the stamps.
 */

// 4-kilobyte pages
#define MAP_PAGE_SHIFT 12
#define MAP_PAGE_SIZE (1 << MAP_PAGE_SHIFT)
#define MAP_PAGE_MASK (MAP_PAGE_SIZE - 1)

typedef uint32_t map_stamp_type;
#define MAP_STAMP_MAX UINT32_MAX

#endif

struct memory_interface {
    /*
     * TODO: there should also be separate try_read/try_write handlers so we
     * don t crash when the debugger tries to access an invalid address that
     * resolves to a valid memory_map_region.
     */
    memory_map_readdouble_func readdouble;
    memory_map_readfloat_func readfloat;
    memory_map_read32_func read32;
    memory_map_read16_func read16;
    memory_map_read8_func read8;

    memory_map_writedouble_func writedouble;
    memory_map_writefloat_func writefloat;
    memory_map_write32_func write32;
    memory_map_write16_func write16;
    memory_map_write8_func write8;
};

struct memory_map_region {
    uint32_t first_addr, last_addr;
    uint32_t range_mask;
    uint32_t mask;

    bool executable;

    // Pointer where regions can store whatever context they may need.
    void *ctxt;

    enum memory_map_region_id id;

    struct memory_interface const *intf;
};

#define MAX_MEM_MAP_REGIONS 32

struct memory_map {
    struct memory_map_region regions[MAX_MEM_MAP_REGIONS];
    unsigned n_regions;

#ifdef DETECT_SMC
    map_stamp_type cur_stamp;
#endif
};

void memory_map_init(struct memory_map *map);
void memory_map_cleanup(struct memory_map *map);

void
memory_map_add(struct memory_map *map,
               uint32_t addr_first,
               uint32_t addr_last,
               bool executable,
               uint32_t range_mask,
               uint32_t mask,
               enum memory_map_region_id id,
               struct memory_interface const *intf, void *ctxt);

uint8_t
memory_map_read_8_exec(struct memory_map *map, uint32_t addr);
uint16_t
memory_map_read_16_exec(struct memory_map *map, uint32_t addr);
uint32_t
memory_map_read_32_exec(struct memory_map *map, uint32_t addr);
float
memory_map_read_float_exec(struct memory_map *map, uint32_t addr);
double
memory_map_read_double_exec(struct memory_map *map, uint32_t addr);

uint8_t
memory_map_read_8(struct memory_map *map, uint32_t addr);
uint16_t
memory_map_read_16(struct memory_map *map, uint32_t addr);
uint32_t
memory_map_read_32(struct memory_map *map, uint32_t addr);
float
memory_map_read_float(struct memory_map *map, uint32_t addr);
double
memory_map_read_double(struct memory_map *map, uint32_t addr);

void
memory_map_write_8(struct memory_map *map, uint32_t addr, uint8_t val);
void
memory_map_write_16(struct memory_map *map, uint32_t addr, uint16_t val);
void
memory_map_write_32(struct memory_map *map, uint32_t addr, uint32_t val);
void
memory_map_write_float(struct memory_map *map, uint32_t addr, float val);
void
memory_map_write_double(struct memory_map *map, uint32_t addr, double val);

/*
 * These functions will return zero if the write was successful and nonzero if
 * it wasn't.  memory_map_write_* would just panic the emulator if something had
 * gone wrong.  This is primarily intended for the benefit of the debugger so
 * that an invalid read coming from the remote GDB frontend doesn't needlessly
 * crash the system.
 */
int
memory_map_try_write_8(struct memory_map *map, uint32_t addr, uint8_t val);
int
memory_map_try_write_16(struct memory_map *map, uint32_t addr, uint16_t val);
int
memory_map_try_write_32(struct memory_map *map, uint32_t addr, uint32_t val);
int
memory_map_try_write_float(struct memory_map *map, uint32_t addr, float val);
int
memory_map_try_write_double(struct memory_map *map, uint32_t addr, double val);

/*
 * These functions will return zero if the read was successful and nonzero if
 * it wasn't.  memory_map_read_* would just panic the emulator if something had
 * gone wrong.  This is primarily intended for the benefit of the debugger so
 * that an invalid read coming from the remote GDB frontend doesn't needlessly
 * crash the system.
 */
int
memory_map_try_read_8(struct memory_map *map, uint32_t addr, uint8_t *val);
int
memory_map_try_read_16(struct memory_map *map, uint32_t addr, uint16_t *val);
int
memory_map_try_read_32(struct memory_map *map, uint32_t addr, uint32_t *val);
int
memory_map_try_read_float(struct memory_map *map, uint32_t addr, float *val);
int
memory_map_try_read_double(struct memory_map *map, uint32_t addr, double *val);

#endif
