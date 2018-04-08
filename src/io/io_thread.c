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

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/thread.h>

#include "async_io.h"
#include "serial_server.h"
#include "dreamcast.h"
#include "cmd_tcp.h"
#include "log.h"

#ifdef ENABLE_DEBUGGER
#include "gdb_stub.h"
#endif

#include "io_thread.h"

struct event_base *io_thread_event_base;

/*
 * event that gets invoked whenever somebody calls io_thread_kick
 * to tell the io_thread that it has work to do
 */
static struct event *io_thread_work_event;

static pthread_t io_thread;

static pthread_mutex_t io_thread_create_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_thread_create_condition = PTHREAD_COND_INITIALIZER;

static void *io_main(void *arg);
static void io_work_callback(evutil_socket_t fd, short ev, void *arg);
static void io_on_kick(int fd);

static int evfd = -1;

void io_thread_launch(void) {
    int err_code;

    if (pthread_mutex_lock(&io_thread_create_mutex) != 0)
        abort(); // TODO: error handling

    if ((err_code = pthread_create(&io_thread, NULL, io_main, NULL)) != 0)
        err(errno, "Unable to launch io thread");

    if (pthread_cond_wait(&io_thread_create_condition,
                          &io_thread_create_mutex) != 0) {
            abort(); // TODO: error handling
    }

    if (pthread_mutex_unlock(&io_thread_create_mutex) != 0)
        abort(); // TODO: error handling
}

void io_thread_join(void) {
    pthread_join(io_thread, NULL);
}

static void *io_main(void *arg) {
    if (pthread_mutex_lock(&io_thread_create_mutex) != 0)
        abort(); // TODO: error handling

    async_io_init();

    evthread_use_pthreads();

    io_thread_event_base = event_base_new();
    if (!io_thread_event_base)
        errx(1, "event_base_new returned -1!");

    io_thread_work_event = event_new(io_thread_event_base, -1, EV_PERSIST,
                                     io_work_callback, NULL);
    if (!io_thread_work_event)
        errx(1, "event_new returned NULL!");

    evfd = eventfd(0, 0);
    async_io_set_fd(ASYNC_IO_FD_INTERNAL, evfd);
    async_io_set_read_callback(ASYNC_IO_FD_INTERNAL, io_on_kick);

    cmd_tcp_init();

    serial_server_init(dreamcast_get_cpu());

#ifdef ENABLE_DEBUGGER
    gdb_init();
#endif

    if (pthread_cond_signal(&io_thread_create_condition) != 0)
        abort(); // TODO: error handling

    if (pthread_mutex_unlock(&io_thread_create_mutex) != 0)
        abort(); // TODO: error handling

    /* int const evflags = EVLOOP_NO_EXIT_ON_EMPTY; */
    /* while (event_base_loop(io_thread_event_base, evflags) >= 0) { */
    /*     if (!dc_is_running()) */
    /*         break; */

    /*     serial_server_run(); */
    /* } */

    while (dc_is_running()) {
        async_io_run();
        serial_server_run();
    }

    LOG_WARN("io thread finished\n");

    close(evfd);

    event_free(io_thread_work_event);

#ifdef ENABLE_DEBUGGER
    gdb_cleanup();
#endif

    serial_server_cleanup();

    cmd_tcp_cleanup();

    event_base_free(io_thread_event_base);

    pthread_exit(NULL);
    return NULL; // this line never executes
}

void io_thread_kick(void) {
    uint64_t val = 1;

    ssize_t n_bytes = write(evfd, &val, sizeof(val));
    if (n_bytes != sizeof(val)) {
        LOG_WARN("%s - failed to read %u bytes (write returned %d)\n",
                 __func__, (unsigned)sizeof(val), (int)n_bytes);
    }

    /* event_active(io_thread_work_event, 0, 0); */
}

static void io_work_callback(evutil_socket_t fd, short ev, void *arg) {
    if (!dc_is_running())
        event_base_loopbreak(io_thread_event_base);

    serial_server_run();
}

static void io_on_kick(int fd) {
    uint64_t val;

    printf("kick received!\n");
    ssize_t n_bytes = read(fd, &val, sizeof(val));

    if (n_bytes != sizeof(val)) {
        LOG_WARN("%s - failed to read %u bytes (read returned %d)\n",
                 __func__, (unsigned)sizeof(val), (int)n_bytes);
    }

    /* if (!dc_is_running()) */
    /*     event_base_loopbreak(io_thread_event_base); */

    /* serial_server_run(); */
}
