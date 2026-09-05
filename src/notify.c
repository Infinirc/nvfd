#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "notify.h"

int notify_send(const char *state) {
    const char *path = getenv("NOTIFY_SOCKET");
    if (!path || !*path)
        return 0;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    size_t path_len = strlen(path);
    if (path_len >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(addr.sun_path, path, path_len);
    /* A leading '@' denotes the Linux abstract socket namespace. */
    if (addr.sun_path[0] == '@')
        addr.sun_path[0] = '\0';
    socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len);

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    ssize_t sent = sendto(fd, state, strlen(state), MSG_NOSIGNAL,
                          (const struct sockaddr *)&addr, addr_len);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return sent < 0 ? -1 : 1;
}
