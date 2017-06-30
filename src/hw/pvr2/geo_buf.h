/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2017 snickerbockers
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

#ifndef GEO_BUF_H_
#define GEO_BUF_H_

#include "pvr2_tex_cache.h"

/*
 * a geo_buf is a pre-allocated buffer used to pass data from the emulation
 * thread to the gfx_thread.  They are stored in a ringbuffer in which the
 * emulation code produces and the rendering code consumes.  Currently this code
 * supports only triangles, but it will eventually grow to encapsulate
 * everything.
 */

/*
 * max number of triangles for a single geo_buf.  Maybe it doesn't need to be
 * this big, or maybe it isn't big enough.  Who is John Galt?
 */
#define GEO_BUF_TRIANGLE_COUNT 131072
#define GEO_BUF_VERT_COUNT (GEO_BUF_TRIANGLE_COUNT * 3)

/*
 * offsets to vertex components within the geo_buf's vert array
 * these are in terms of sizeof(float)
 */
#define GEO_BUF_POS_OFFSET 0
#define GEO_BUF_COLOR_OFFSET 3
#define GEO_BUF_TEX_COORD_OFFSET 7

/*
 * the number of elements per vertex.  Currently this means 3 floats for the
 * coordinates and 4 floats for the color.
 */
#define GEO_BUF_VERT_LEN 9

/*
 * TODO: due to my own oversights, there is currently no way to use more than
 * one texture at a time.  This is because there's no infra to mark the
 * beginning and end of a group of polygons.  This will obviously need to be
 * fixed in the future.
 */
struct geo_buf {
    struct pvr2_tex tex_cache[PVR2_TEX_CACHE_SIZE];

    float verts[GEO_BUF_VERT_COUNT * GEO_BUF_VERT_LEN];
    unsigned n_verts;
    unsigned frame_stamp;

    // render dimensions
    unsigned screen_width, screen_height;

    float bgcolor[4];
    float bgdepth;

    // which texture in the tex cache to bind
    int tex_no;

    bool tex_enable;
    unsigned tex_idx; // only valid if tex_enable=true
};

/*
 * return the next geo_buf to be consumed, or NULL if there are none.
 * This function never blocks.
 */
struct geo_buf *geo_buf_get_cons(void);

/*
 * return the next geo_buf to be produced.  This function never returns NULL.
 */
struct geo_buf *geo_buf_get_prod(void);

// consume the current geo_buf (which is the one returned by geo_buf_get_cons)
void geo_buf_consume(void);

/*
 * mark the current geo_buf as having been consumed.
 *
 * This function can block if the buffer is full; this is not ideal
 * and I would like to find a way to revisit this some time in the future.  For
 * now, stability trumps performance.
 */
void geo_buf_produce(void);

// return the frame stamp of the last geo_buf to be produced
unsigned geo_buf_latest_frame_stamp(void);

#endif
