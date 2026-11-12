#pragma once

/* EE stdout diagnostics for the experimental full-engine PS2 ELF.
 * This header is included only under __PS2__ in source/main.c. */
void PS2_BootStage(unsigned number, const char *description);
void PS2_BootResult(const char *operation, int result);
void PS2_BootValue(const char *operation, int value);
void PS2_BootInitRpc(void);
/* USB logging is enabled only by the separate -DiagnosticUsbLog build.
 * The regular release retains the stdout markers but writes no debug.log. */
void PS2_BootInitUsbLog(void);
void PS2_BootEvent(const char *message);
void PS2_BootFatal(const char *message, int value);
void PS2_BootPathProbe(const char *path, int error, int success);
