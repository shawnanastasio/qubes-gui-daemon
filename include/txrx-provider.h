#ifndef _TXRX_PROVIDER_H
#define _TXRX_PROVIDER_H

/*
 * This file defines the interface for shared memory providers.
 * All calls to libvchan, for example, should go through here.
 */

#include <sys/select.h>

struct txrx_provider {
    void (*register_eof_callback)(struct txrx_provider *, void (*at_eof)(void));
    int (*write_data)(struct txrx_provider *, char *buf, int size);
    int (*write_data_exact)(struct txrx_provider *, char *buf, int size);
    int (*read_data)(struct txrx_provider *, char *buf, int size);
    int (*wait_for_txrx_or_argfd_once)(struct txrx_provider *, int nfd, int *fd, fd_set *retset);
    int (*data_ready)(struct txrx_provider *);
    void (*destruct)(struct txrx_provider *);
};

static inline void register_eof_callback(struct txrx_provider *p, void (*at_eof)(void))
{
    p->register_eof_callback(p, at_eof);
}

static inline void write_data(struct txrx_provider *p, char *buf, int size)
{
    p->write_data(p, buf, size);
}

static inline void write_data_exact(struct txrx_provider *p, char *buf, int size)
{
    p->write_data_exact(p, buf, size);
}

static inline int read_data(struct txrx_provider *p, char *buf, int size)
{
    return p->read_data(p, buf, size);
}

static inline int wait_for_txrx_or_argfd_once(struct txrx_provider *p, int nfd, int *fd, fd_set *retset)
{
    return p->wait_for_txrx_or_argfd_once(p, nfd, fd, retset);
}

static inline int txrx_data_ready(struct txrx_provider *p)
{
    return p->data_ready(p);
}

static inline void txrx_destruct(struct txrx_provider *p)
{
    p->destruct(p);
}

static inline int real_write_message(struct txrx_provider *p, char *hdr, int size, char *data, int datasize)
{
    p->write_data(p, hdr, size);
    p->write_data(p, data, datasize);
    return 0;
}

static inline int wait_for_txrx_or_argfd(struct txrx_provider *p, int nfd, int *fd, fd_set *retset)
{
    int ret;
    while ((ret = wait_for_txrx_or_argfd_once(p, nfd, fd, retset)) == 0);
    return ret;
}

#define read_struct(provider, x) read_data(provider, (char*)&x, sizeof(x))
#define write_struct(provider, x) write_data(provider, (char*)&x, sizeof(x))
#define write_message(provider,x,y) do {\
    x.untrusted_len = sizeof(y); \
    real_write_message(provider, (char*)&x, sizeof(x), (char*)&y, sizeof(y)); \
    } while(0)

#endif // _TXRX_PROVIDER_H
