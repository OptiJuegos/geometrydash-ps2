#!/usr/bin/env python3
"""Static diagnostic build invariants, no emulator, no Memory Card writes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def main():
    bat = read("build_debug.bat")
    release = read("build.bat")
    trace = read("ps2/src/boot_trace.c")
    main_c = read("source/main.c")
    backend = read("ps2/tools/check_link.ps1")
    assert "-DiagnosticUsbLog" in bat and '[switch]$DiagnosticUsbLog' in backend
    assert "-DiagnosticUsbLog" not in release
    assert "DiagnosticAutoInput" not in bat
    assert "-DiagnosticNoClipLevel" not in bat
    assert "-DiagnosticMenuStages" not in bat  # it also disables input
    assert "ps2\\out\\debug" in bat
    assert 'GeometryDashPS2_DEBUG.elf' in bat and 'GeometryDashPS2.elf"' not in bat
    assert 'PS2_BootInitUsbLog();' in main_c
    assert 'GDPS2 BOOT %02u' in trace
    assert 'mass:/GDPS2/debug.log' in trace and 'mass0:/GDPS2/debug.log' in trace
    assert '"ab"' in trace and '"wb"' not in trace
    assert 'SifExecModuleBuffer' in trace
    assert 'fclose(out)' in trace and 'fflush(out)' in trace
    assert '#ifdef PS2_DIAG_USB_LOG' in trace
    audio = read('ps2/src/ndsp_ps2.c')
    for marker in ('AUDIO sbv_patch_enable_lmb', 'AUDIO LIBSD module id',
                   'AUDIO embedded audsrv.irx bytes', 'AUDIO audsrv.irx module id',
                   'AUDIO audsrv.irx module result', 'AUDIO audsrv_init',
                   'AUDIO audsrv_set_format', 'AUDIO audsrv_set_volume',
                   'AUDIO CreateSema id', 'AUDIO CreateThread id',
                   'before StartThread mixer'):
        assert marker in audio, marker
    assert 'PS2_BootValue' in trace
    assert 'audsrv.irx' in bat and 'copy /Y' in bat
    assert 'PS2_DIAG_USB_LOG' in audio
    assert 'mass:/GDPS2/audsrv.irx' in audio
    assert 'mass0:/GDPS2/audsrv.irx' in audio
    assert 'unable to load audsrv.irx by file or buffer' in audio
    assert "($src -eq 'ps2/src/boot_trace.c' -or $src -eq 'ps2/src/ndsp_ps2.c')" in backend
    assert '#ifndef PS2_DIAG_USB_LOG\n            return -1;' in audio
    # The former 3DS DSP-firmware help screen is not valid on a physical PS2.
    assert 'PS2_BootFatal("PS2 audsrv/SPU2 initialization failed' in main_c
    assert 'return 1;\n#else\n        no_dsp_firmware();' in main_c

    debug = ROOT / 'ps2/out/debug'
    if debug.exists() and (debug / 'GeometryDashPS2_DEBUG.elf').exists():
        assert (debug / 'GeometryDashPS2_DEBUG.elf').stat().st_size > 128000
        assert (debug / 'audsrv.irx').stat().st_size > 1024
        for name in ('menus/main_menu.txt', 'gfx/ui.p2sheet', 'songs/menuLoop.mp3'):
            assert (debug / 'romfs' / name).exists(), name
    print("PASS: isolated diagnostic USB logging, append-only, stage trace and original ROMFS")


if __name__ == '__main__':
    main()
