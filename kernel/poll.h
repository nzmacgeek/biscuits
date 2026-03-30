#pragma once
// BlueyOS Poll - event multiplexing for process supervision and sockets
// "Who's next? Bandit watches all the doors at once!" - Takeaway
// Episode ref: "Takeaway" - juggling many things simultaneously.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

/* Event flags for pollfd.events / pollfd.revents */
#define POLLIN    0x0001   /* data available to read       */
#define POLLOUT   0x0004   /* ready to write               */
#define POLLERR   0x0008   /* error condition              */
#define POLLHUP   0x0010   /* peer closed / hang-up        */
#define POLLNVAL  0x0020   /* invalid / closed fd          */

typedef struct {
    int32_t  fd;       /* file descriptor to watch   */
    int16_t  events;   /* requested events (POLL*)   */
    int16_t  revents;  /* returned events (filled by kernel) */
} pollfd_t;

/*
 * kernel_poll - check a set of fds for readiness.
 *
 * Returns the number of fds with non-zero revents, 0 on timeout, or
 * -EAGAIN when the call should be retried (process has been put to sleep).
 * Callers that need true blocking must loop and retry on -EAGAIN.
 */
int kernel_poll(pollfd_t *fds, uint32_t nfds, int32_t timeout_ms);
