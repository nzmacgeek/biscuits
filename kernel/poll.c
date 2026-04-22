// BlueyOS Poll - "Bandit watches all the doors at once"
// Episode ref: "Takeaway" - managing many things simultaneously.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "poll.h"
#include "devev.h"
#include "socket.h"
#include "process.h"
#include "timer.h"
#include "tty.h"
#include "../fs/vfs.h"

#define BLUEY_EAGAIN  11
#define BLUEY_EINVAL  22
/* Kernel-internal: must match the value in syscall.c */
#define BLUEY_ERESTART 512

/*
 * Determine readiness of a single fd.
 * Returns the set of POLL* flags that are currently true for that fd.
 */
static int16_t poll_check_fd(int32_t fd, int16_t events) {
    int16_t ready = 0;

    if (fd < 0) return POLLNVAL;

    /* Standard streams */
    if (fd == 0) {
        /* stdin — readable when tty input is pending */
        if ((events & POLLIN) && tty_input_pending()) ready |= POLLIN;
        return ready;
    }
    if (fd == 1 || fd == 2) {
        /* stdout/stderr — always writable */
        if (events & POLLOUT) ready |= POLLOUT;
        return ready;
    }

    /* VFS / device fds */
    if (vfs_fd_is_tty(fd)) {
        if ((events & POLLIN) && tty_input_pending()) ready |= POLLIN;
        if (events & POLLOUT) ready |= POLLOUT;
        return ready;
    }

    if (vfs_fd_is_devev(fd)) {
        if ((events & POLLIN) && devev_pending()) ready |= POLLIN;
        return ready;
    }

    if (vfs_fd_is_socket(fd)) {
        int socket_id = vfs_socket_id(fd);
        if ((events & POLLIN) && socket_is_readable(socket_id)) ready |= POLLIN;
        if ((events & POLLOUT) && socket_is_writable(socket_id)) ready |= POLLOUT;
        return ready;
    }

    if (vfs_fd_is_pipe(fd)) {
        /* Read end: data available or EOF (all writers closed). */
        if (events & POLLIN) {
            if (vfs_pipe_readable(fd)) ready |= POLLIN;
        }
        /* Write end: space available and at least one reader open. */
        if (events & POLLOUT) {
            if (vfs_pipe_writable(fd)) ready |= POLLOUT;
        }
        return ready;
    }

    /* Generic VFS file: assume readable/writable (basic approximation) */
    if (events & POLLIN)  ready |= POLLIN;
    if (events & POLLOUT) ready |= POLLOUT;
    return ready;
}

int kernel_poll(pollfd_t *fds, uint32_t nfds, int32_t timeout_ms) {
    int ready;
    uint32_t i;

    if (!fds || nfds == 0) return -BLUEY_EINVAL;

    /* Single-shot readiness check */
    ready = 0;
    for (i = 0; i < nfds; i++) {
        fds[i].revents = poll_check_fd(fds[i].fd, fds[i].events);
        if (fds[i].revents) ready++;
    }

    if (ready > 0 || timeout_ms == 0) return ready;

    /*
     * Nothing is ready and the caller wants to wait.  Sleep for
     * min(|timeout_ms|, 50) ms so other processes can run, then signal
     * the syscall dispatcher to retry the entire poll syscall via the
     * kernel-internal ERESTART mechanism (invisible to user space).
     */
    {
        uint32_t sleep_ms = (timeout_ms < 0) ? 50u : (uint32_t)timeout_ms;
        if (sleep_ms > 50u) sleep_ms = 50u;
        process_sleep(sleep_ms);
    }

    return -BLUEY_ERESTART;
}
