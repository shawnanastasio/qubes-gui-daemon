#ifndef _TXRX_PROVIDER_H
#define _TXRX_PROVIDER_H

/*
 * This file defines the interface for virtual memory providers.
 * All calls to libvchan, for example, should go through here.
 */

struct txrx_provider {
    void (*write_data)(struct txrx_provider *, char *buf, int size);
    void (*write_data_exact)(struct txrx_provider *, char *buf, int size);
    void (*wait_for_rxtx_or_argfd_once)(struct txrx_provider *, int nfd, int *fd, fd_set *retset);
    void (*register_eof_callback)(void (*at_eof)(void));
};

#endif // _TXRX_PROVIDER_H
