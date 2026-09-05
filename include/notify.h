#ifndef NVFD_NOTIFY_H
#define NVFD_NOTIFY_H

/*
 * Minimal sd_notify(3) without linking libsystemd: sends one state string
 * ("READY=1", "WATCHDOG=1", "STOPPING=1", ...) to $NOTIFY_SOCKET.
 *
 * Returns 1 when the message was sent, 0 when NOTIFY_SOCKET is unset (not
 * running under a notify-aware supervisor; nothing to do), and -1 with errno
 * set when NOTIFY_SOCKET is set but the message could not be delivered.
 */
int notify_send(const char *state);

#endif /* NVFD_NOTIFY_H */
