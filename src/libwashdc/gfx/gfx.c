/*******************************************************************************
 *
 *
 *    WashingtonDC Dreamcast Emulator
 *    Copyright (C) 2017-2019 snickerbockers
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#ifndef __MINGW32__
#include <err.h>
#else

#define err(retval, ...) do { \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "Undefined error: %d\n", errno); \
    exit(retval); \
} while(0)

#endif
#include <stdbool.h>

#define GL3_PROTOTYPES 1
#include <GL/glew.h>
#include <GL/gl.h>

#include "washdc/win.h"
#include "dreamcast.h"
#include "gfx/rend_common.h"
#include "gfx/gfx_tex_cache.h"
#include "log.h"
#include "config.h"

// for the palette_tp stuff
//#include "hw/pvr2/pvr2_core_reg.h"

#include "gfx/gfx.h"

static unsigned win_width, win_height;

static unsigned frame_counter;

static struct washdc_overlay_intf const *overlay_intf;

// Only call gfx_thread_signal and gfx_thread_wait when you hold the lock.
static void gfx_do_init(void);

void gfx_init(unsigned int width, unsigned int height) {
    win_width = width;
    win_height = height;

    LOG_INFO("GFX: rendering graphics from within the main emulation thread\n");

    if(win_width && win_height)
        gfx_do_init();
}

void gfx_cleanup(void) {
    if(win_width && win_height)
        rend_cleanup();
}

void gfx_expose(void) {
    if(win_width && win_height)
        gfx_redraw();
}

void gfx_redraw(void) {
    if(win_width && win_height){
        gfx_rend_ifp->video_present();
        if (overlay_intf->overlay_draw)
            overlay_intf->overlay_draw();
        win_update();
    }
}

void gfx_resize(int xres, int yres) {
    if(win_width && win_height) {
        gfx_rend_ifp->video_present();
        if (overlay_intf->overlay_draw)
            overlay_intf->overlay_draw();
        win_update();
    }
}
#include <signal.h>

static void gfx_do_init(void) {
    win_make_context_current();

    glewExperimental = GL_TRUE;
    {
        GLenum glewError = glewInit();
        if (glewError != GLEW_OK) {
            fprintf(stdout, "Error initializing GLEW! %s\n", glewGetErrorString(glewError)); fflush(stdout);
            raise(SIGINT);
        }
    }
    glViewport(0, 0, win_width, win_height);

    gfx_tex_cache_init();
    rend_init();

    glClear(GL_COLOR_BUFFER_BIT);
}

void gfx_post_framebuffer(int obj_handle,
                          unsigned fb_new_width,
                          unsigned fb_new_height, bool do_flip) {
    gfx_rend_ifp->video_new_framebuffer(obj_handle, fb_new_width, fb_new_height,
                                        do_flip);
    if(win_width && win_height) {
        gfx_rend_ifp->video_present();
        if (overlay_intf->overlay_draw)
            overlay_intf->overlay_draw();
        win_update();
    }
    frame_counter++;
}

void gfx_toggle_output_filter(void) {
    if(win_width && win_height)
        gfx_rend_ifp->video_toggle_filter();
}

void gfx_set_overlay_intf(struct washdc_overlay_intf const *intf) {
    overlay_intf = intf;
}
