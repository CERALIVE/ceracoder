/*
    ceracoder - live video encoder with dynamic bitrate control

    Copyright (C) 2026 CERALIVE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "sd_notify.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/*
  Send a single sd_notify state string to $NOTIFY_SOCKET.

  The protocol (sd_notify(3)): connect-less AF_UNIX SOCK_DGRAM sendto() of a
  newline-separated set of VAR=value assignments. NOTIFY_SOCKET is either a
  filesystem path ("/run/...") or an abstract socket ("@...", first byte NUL).
*/
static int sd_notify_send(const char *state) {
  const char *path = getenv("NOTIFY_SOCKET");
  if (path == NULL || (path[0] != '/' && path[0] != '@')) {
    /* Not supervised by systemd notify: harmless no-op. */
    return 0;
  }

  size_t path_len = strlen(path);
  struct sockaddr_un sa;
  if (path_len >= sizeof(sa.sun_path)) {
    return -E2BIG;
  }

  int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return -errno;
  }

  memset(&sa, 0, sizeof(sa));
  sa.sun_family = AF_UNIX;
  memcpy(sa.sun_path, path, path_len + 1);

  socklen_t sa_len;
  if (sa.sun_path[0] == '@') {
    /* Abstract namespace: leading '@' becomes a NUL and is NOT counted with a
       trailing terminator. */
    sa.sun_path[0] = '\0';
    sa_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len);
  } else {
    sa_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1);
  }

  ssize_t n;
  do {
    n = sendto(fd, state, strlen(state), MSG_NOSIGNAL, (struct sockaddr *)&sa,
               sa_len);
  } while (n < 0 && errno == EINTR);

  int ret = (n < 0) ? -errno : 1;
  close(fd);
  return ret;
}

int sd_notify_ready(void) { return sd_notify_send("READY=1\n"); }

int sd_notify_watchdog(void) { return sd_notify_send("WATCHDOG=1\n"); }

unsigned long long sd_watchdog_usec(void) {
  const char *e = getenv("WATCHDOG_USEC");
  if (e == NULL || *e == '\0') {
    return 0ULL;
  }

  int saved_errno = errno;
  errno = 0;
  char *end = NULL;
  unsigned long long v = strtoull(e, &end, 10);
  if (errno != 0 || end == e) {
    errno = saved_errno;
    return 0ULL;
  }
  errno = saved_errno;
  return v;
}
