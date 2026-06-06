/*
    ceracoder - live video encoder with dynamic bitrate control

    Copyright (C) 2026 CERALIVE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef CERACODER_SD_NOTIFY_H
#define CERACODER_SD_NOTIFY_H

/*
  Minimal, dependency-free sd_notify(3) implementation.

  Talks directly to the AF_UNIX datagram socket named by $NOTIFY_SOCKET, which
  systemd sets for units running with Type=notify and/or WatchdogSec=. This
  avoids linking libsystemd, keeping the device build (Makefile, no extra
  pkg-config dep) unchanged.

  All functions are safe no-ops when the process is NOT supervised by systemd
  (NOTIFY_SOCKET unset), so ceracoder still runs identically when launched
  directly (e.g. from a shell or, today, child-spawned by the CeraUI backend).

  See ADR-0005 (device supervision & restart-authority model): systemd owns
  process restart; ceracoder pets WatchdogSec from its main loop so a hung or
  zombie process is killed and respawned.
*/

/* Send "READY=1": tells systemd that startup has completed. Required for
   Type=notify units. Returns >0 on success, 0 if not under systemd, <0 on
   error. */
int sd_notify_ready(void);

/* Send "WATCHDOG=1": keep-alive ping that resets the systemd watchdog timer.
   Same return convention as sd_notify_ready(). */
int sd_notify_watchdog(void);

/* Return the configured watchdog interval in microseconds (from
   $WATCHDOG_USEC), or 0 if the watchdog is not enabled for this unit. */
unsigned long long sd_watchdog_usec(void);

#endif /* CERACODER_SD_NOTIFY_H */
