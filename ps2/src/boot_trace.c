#include "boot_trace.h"
#include <sifrpc.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef PS2_DIAG_USB_LOG
#include <loadfile.h>
#include <delaythread.h>

/* Linked only in the separate debug ELF; neither IRX is in release.
 * They are copied from the local PS2SDK, never extracted from user saves. */
extern unsigned char gdps2_debug_usbd_start[], gdps2_debug_usbd_end[];
extern unsigned char gdps2_debug_mass_start[], gdps2_debug_mass_end[];

static const char *const destinations[] = {
    "mass:/GDPS2/debug.log", "mass0:/GDPS2/debug.log", "host:debug.log"
};
static const char *log_path;
static char pending[8192];
static unsigned pending_len;

static void emit_line(const char *line)
{
    FILE *out;
    const size_t len = strlen(line);
    /* Preserve early boot evidence in RAM until mass: becomes available. */
    if (!log_path) {
        if (pending_len + len + 2 < sizeof(pending)) {
            memcpy(pending + pending_len, line, len);
            pending_len += (unsigned)len;
            pending[pending_len++] = '\n';
            pending[pending_len] = '\0';
        }
        return;
    }
    /* Open/append/flush/close EACH event. A sudden crash after one of the
     * stages should not lose buffered stdio writes. No writes to MC saves. */
    out = fopen(log_path, "ab");
    if (!out) {
        log_path = NULL;
        emit_line(line);
        return;
    }
    fwrite(line, 1, len, out);
    fwrite("\n", 1, 1, out);
    fflush(out);
    fclose(out);
}

static void probe_log_device(void)
{
    /* host: can become ready before the USB driver finishes enumeration.
     * Reprobe and prefer mass: if USB appears later. */
    if (log_path && strncmp(log_path, "host:", 5) != 0) return;
    /* Prefer the USB folder explicitly requested by the user. Do not
     * truncate existing logs, and never create/format a partition. */
    for (unsigned i = 0; i < sizeof(destinations)/sizeof(destinations[0]); i++) {
        if (log_path && !strncmp(destinations[i], "host:", 5)) break;
        FILE *out = fopen(destinations[i], "ab");
        if (!out) continue;
        log_path = destinations[i];
        fwrite("\n========== GDPS2 DEBUG SESSION ==========\n", 1,
               sizeof("\n========== GDPS2 DEBUG SESSION ==========\n") - 1, out);
        if (pending_len) fwrite(pending, 1, pending_len, out);
        fflush(out);
        fclose(out);
        pending_len = 0;
        printf("GDPS2 DEBUG: persistent log -> %s\n", log_path);
        return;
    }
}

static void log_message(const char *message)
{
    emit_line(message);
    puts(message);
    fflush(stdout);
}

static void load_usb_module(const char *name, void *start, void *end)
{
    char message[160];
    int mod_result = -999;
    int size = (int)((unsigned char *)end - (unsigned char *)start);
    int id = SifExecModuleBuffer(start, (unsigned)size, 0, NULL, &mod_result);
    snprintf(message, sizeof(message), "GDPS2 DEBUG IRX %s bytes=%d id=%d result=%d",
             name, size, id, mod_result);
    log_message(message);
}
#endif

/* Do not require host: for logs: the asset root might not be mounted yet.
 * PCSX2/ps2link can capture the EE stdout from this program. */
void PS2_BootStage(unsigned number, const char *description)
{
    static int stdout_configured;
    if (!stdout_configured) {
        /* A crash in the next stage must not hide buffered diagnostic lines. */
        setvbuf(stdout, NULL, _IONBF, 0);
        stdout_configured = 1;
    }
    printf("GDPS2 BOOT %02u: %s\n", number, description ? description : "(null)");
    fflush(stdout);
#ifdef PS2_DIAG_USB_LOG
    char message[220];
    snprintf(message, sizeof(message), "GDPS2 BOOT %02u: %s", number,
             description ? description : "(null)");
    emit_line(message);
#endif
}

void PS2_BootResult(const char *operation, int result)
{
    printf("GDPS2 RESULT %s: %d (%s)\n", operation ? operation : "(null)",
           result, result == 0 ? "OK" : "FAIL");
    fflush(stdout);
#ifdef PS2_DIAG_USB_LOG
    char message[220];
    snprintf(message, sizeof(message), "GDPS2 RESULT %s: %d (%s)",
             operation ? operation : "(null)", result, result == 0 ? "OK" : "FAIL");
    emit_line(message);
#endif
}

void PS2_BootValue(const char *operation, int value)
{
    printf("GDPS2 VALUE %s: %d\n", operation ? operation : "(null)", value);
    fflush(stdout);
#ifdef PS2_DIAG_USB_LOG
    char message[220];
    snprintf(message, sizeof(message), "GDPS2 VALUE %s: %d",
             operation ? operation : "(null)", value);
    emit_line(message);
#endif
}

void PS2_BootInitRpc(void)
{
    /* ROMFS probes host: before gfxInitDefault(), so initialize RPC here. */
    sceSifInitRpc(0);
    /* sceSifInitRpc has no success result: do not report a fabricated OK. */
    puts("GDPS2 SIF: sceSifInitRpc(0) returned");
}

void PS2_BootInitUsbLog(void)
{
#ifdef PS2_DIAG_USB_LOG
    log_message("GDPS2 DEBUG: probe existing USB mount before IRX load");
    probe_log_device();
    if (!log_path || !strncmp(log_path, "host:", 5)) {
        char line[100];
        int sif_file = SifLoadFileInit();
        snprintf(line, sizeof(line), "GDPS2 DEBUG: SifLoadFileInit=%d", sif_file);
        log_message(line);
        /* uLaunchELF may have unloaded its IOP modules when starting ELF.
         * Do not reset the IOP: that would disrupt other device drivers. */
        load_usb_module("usbd.irx", gdps2_debug_usbd_start, gdps2_debug_usbd_end);
        load_usb_module("usbhdfsd.irx", gdps2_debug_mass_start, gdps2_debug_mass_end);
        for (int attempt = 0; attempt < 25 && (!log_path || !strncmp(log_path, "host:", 5)); attempt++) {
            probe_log_device();
            if (!log_path || !strncmp(log_path, "host:", 5)) DelayThread(200000);
        }
    }
    if (!log_path) log_message("GDPS2 DEBUG WARNING: mass:/GDPS2 unavailable; no persistent USB log");
    else log_message("GDPS2 DEBUG: boot logging initialized");
#endif
}

void PS2_BootEvent(const char *message)
{
#ifdef PS2_DIAG_USB_LOG
    log_message(message ? message : "GDPS2 DEBUG EVENT (null)");
#else
    (void)message;
#endif
}

void PS2_BootFatal(const char *message, int value)
{
#ifdef PS2_DIAG_USB_LOG
    char line[220];
    snprintf(line, sizeof(line), "GDPS2 FATAL: %s code=%d last stage above",
             message ? message : "unknown", value);
    log_message(line);
#else
    (void)message; (void)value;
#endif
}

void PS2_BootPathProbe(const char *path, int error, int success)
{
#ifdef PS2_DIAG_USB_LOG
    char line[256];
    snprintf(line, sizeof(line), "GDPS2 ROMFS: %s %s errno=%d",
             path ? path : "(null)", success ? "FOUND" : "missing", error);
    log_message(line);
#else
    (void)path; (void)error; (void)success;
#endif
}
