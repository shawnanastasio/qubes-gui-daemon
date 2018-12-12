/*
 * The Qubes OS Project, http://www.qubes-os.org
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <libvchan.h>
#include <sys/select.h>
#include <errno.h>
#include "double-buffer.h"
#include "txrx-provider.h"

struct txrx_provider_vchan {
    struct txrx_provider super;
    libvchan_t *vchan;
    void (*vchan_at_eof)(void);
    int vchan_is_closed;
    /* double buffered in gui-daemon to deal with deadlock
     * during send large clipboard content
     */
    int double_buffered;
};

static void vchan_register_at_eof(struct txrx_provider *_provider, void (*new_vchan_at_eof)(void))
{
    struct txrx_provider_vchan *provider = (struct txrx_provider_vchan *)_provider;
    provider->vchan_at_eof = new_vchan_at_eof;
}

void handle_vchan_error(libvchan_t *vchan, const char *op)
{
    if (!libvchan_is_open(vchan)) {
        fprintf(stderr, "EOF\n");
        exit(0);
    } else {
        fprintf(stderr, "Error while vchan %s\n, terminating", op);
        exit(1);
    }
}

static int vchan_write_data_exact(struct txrx_provider *_provider, char *buf, int size)
{
    struct txrx_provider_vchan *provider = (struct txrx_provider_vchan *)_provider;
    int written = 0;
    int ret;

    while (written < size) {
        ret = libvchan_write(provider->vchan, buf + written, size - written);
        if (ret <= 0)
            handle_vchan_error(provider->vchan, "write data");
        written += ret;
    }
//      fprintf(stderr, "sent %d bytes\n", size);
    return size;
}

static int vchan_write_data(struct txrx_provider *_provider, char *buf, int size)
{
    struct txrx_provider_vchan *provider = (struct txrx_provider_vchan *)_provider;

    int count;
    if (!provider->double_buffered)
        return vchan_write_data_exact(_provider, buf, size); // this may block
    double_buffer_append(buf, size);
    count = libvchan_buffer_space(provider->vchan);
    if (count > double_buffer_datacount())
        count = double_buffer_datacount();
        // below, we write only as much data as possible without
        // blocking; remainder of data stays in the double buffer
    vchan_write_data_exact(_provider, double_buffer_data(), count);
    double_buffer_substract(count);
    return size;
}

static int vchan_read_data(struct txrx_provider *_provider, char *buf, int size)
{
    struct txrx_provider_vchan *provider = (struct txrx_provider_vchan *)_provider;

    int written = 0;
    int ret;
    while (written < size) {
        ret = libvchan_read(provider->vchan, buf + written, size - written);
        if (ret <= 0)
            handle_vchan_error(provider->vchan, "read data");
        written += ret;
    }
//      fprintf(stderr, "read %d bytes\n", size);
    return size;
}

static int wait_for_vchan_or_argfd_once(struct txrx_provider *_provider, int nfd, int *fd, fd_set * retset)
{
    struct txrx_provider_vchan *provider = (struct txrx_provider_vchan *)_provider;

    fd_set rfds;
    int vfd, max = 0, ret, i;
    struct timeval tv = { 0, 1000000 };
    vchan_write_data(_provider, NULL, 0);    // trigger write of queued data, if any present
    vfd = libvchan_fd_for_select(provider->vchan);
    FD_ZERO(&rfds);
    for (i = 0; i < nfd; i++) {
        int cfd = fd[i];
        FD_SET(cfd, &rfds);
        if (cfd > max)
            max = cfd;
    }
    FD_SET(vfd, &rfds);
    if (vfd > max)
        max = vfd;
    max++;
    ret = select(max, &rfds, NULL, NULL, &tv);
    if (ret < 0 && errno == EINTR)
        return -1;
    if (ret < 0) {
        perror("select");
        exit(1);
    }
    if (!libvchan_is_open(provider->vchan)) {
        fprintf(stderr, "libvchan_is_eof\n");
        libvchan_close(provider->vchan);
        if (provider->vchan_at_eof != NULL) {
            provider->vchan_at_eof();
            return -1;
        } else
            exit(0);
    }
    if (FD_ISSET(vfd, &rfds))
        // the following will never block; we need to do this to
        // clear libvchan_fd pending state
        libvchan_wait(provider->vchan);
    if (retset)
        *retset = rfds;
    return ret;
}

static int vchan_data_ready(struct txrx_provider *_provider)
{
    struct txrx_provider_vchan *provider = (struct txrx_provider_vchan *)_provider;
    return libvchan_data_ready(provider->vchan);
}

static void vchan_destruct(struct txrx_provider *_provider)
{
    struct txrx_provider_vchan *provider = (struct txrx_provider_vchan *)_provider;
    libvchan_close(provider->vchan);
}

struct txrx_provider *txrx_provider_vchan_new_client(int domain, int port)
{
    struct txrx_provider_vchan *provider = malloc(sizeof(struct txrx_provider_vchan));
    if (!provider)
        return NULL;

    // Set default values
    provider->vchan_is_closed = 0;
    provider->double_buffered = 1;

    // Create new libvchan_t *
    provider->vchan = libvchan_client_init(domain, port);
    if (!provider->vchan) {
        free(provider);
        return NULL;
    }

    // Set function pointers
    provider->super.register_eof_callback = &vchan_register_at_eof;
    provider->super.write_data = &vchan_write_data;
    provider->super.write_data_exact = &vchan_write_data_exact;
    provider->super.read_data = &vchan_read_data;
    provider->super.wait_for_txrx_or_argfd_once = &wait_for_vchan_or_argfd_once;
    provider->super.data_ready = &vchan_data_ready;
    provider->super.destruct = &vchan_destruct;

    return (struct txrx_provider *)provider;
}
