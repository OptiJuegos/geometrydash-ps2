@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Geometry Dash PS2 - normal/release build of the ORIGINAL game engine.
rem No diagnostic macros, automatic input, tests, emulator or prototype demos.
rem Run from anywhere; all paths are relative to this batch file.

pushd "%~dp0" || (
    echo [ERROR] No se pudo entrar en la carpeta del proyecto.
    exit /b 1
)

set "RELEASE_DIR=%CD%\ps2\out\release"
set "RELEASE_ELF=%RELEASE_DIR%\GeometryDashPS2.elf"
set "BUILD_ELF=%RELEASE_DIR%\GeometryDashPS2.build.elf"
set "RELEASE_ISO=%RELEASE_DIR%\GeometryDashPS2.iso"
set "BUILDER=%CD%\ps2\tools\build_engine.ps1"
set "PACKER=%CD%\ps2\tools\package_romfs.py"
set "ISO_MAKER=%CD%\ps2\tools\make_iso.py"

if not exist "%BUILDER%" goto missing_builder
if not exist "%PACKER%" goto missing_packer
if not exist "%ISO_MAKER%" goto missing_iso_maker
where powershell.exe >nul 2>&1 || goto missing_powershell
where python.exe >nul 2>&1 || goto missing_python

if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%" || goto failed

echo.
echo ========================================
echo  Geometry Dash PS2 - Release build
echo ========================================
echo  Motor original, optimizacion -O2
echo  Sin auto-input, sin macros de debug
echo  Sin pruebas ni ejecucion de PCSX2
echo.

rem Use a staging ELF so a failed compile does not replace the last release.
if exist "%BUILD_ELF%" del /f /q "%BUILD_ELF%" || goto failed
echo [1/4] Compilando motor completo...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%BUILDER%" -Output "%BUILD_ELF%"
if errorlevel 1 goto failed
if not exist "%BUILD_ELF%" goto missing_elf

rem Asset packer converts the atlases to PS2 T8 and the MP3 songs to IMA-ADPCM WAV.
echo.
echo [2/4] Preparando recursos originales ROMFS...
python.exe "%PACKER%" --output "%RELEASE_DIR%"
if errorlevel 1 goto failed
if not exist "%RELEASE_DIR%\romfs\menus\main_menu.txt" goto missing_assets
if not exist "%RELEASE_DIR%\romfs\gfx\ui.p2sheet" goto missing_assets
if not exist "%RELEASE_DIR%\romfs\songs\menuLoop.wav" goto missing_assets

echo.
echo [3/4] Publicando ELF normal...
move /Y "%BUILD_ELF%" "%RELEASE_ELF%" >nul
if errorlevel 1 goto failed

rem The ISO (SYSTEM.CNF + ELF + romfs) boots in PCSX2, OPL or from a burned disc.
echo.
echo [4/4] Generando ISO...
python.exe "%ISO_MAKER%" --elf "%RELEASE_ELF%" --romfs "%RELEASE_DIR%\romfs" --out "%RELEASE_ISO%"
if errorlevel 1 goto iso_failed
if not exist "%RELEASE_ISO%" goto iso_failed

echo.
echo ========================================
echo  COMPILACION RELEASE COMPLETADA
echo ========================================
echo  ELF:   "%RELEASE_ELF%"
echo  ROMFS: "%RELEASE_DIR%\romfs"
echo  ISO:   "%RELEASE_ISO%"
echo.
echo  Para PCSX2, abre la ISO, o el ELF con HostFs habilitado.
echo  Para PS2 real aun falta verificar los drivers de USB/disco.
echo.
popd
exit /b 0

:missing_builder
echo [ERROR] No existe "%BUILDER%".
goto failed
:missing_packer
echo [ERROR] No existe "%PACKER%".
goto failed
:missing_iso_maker
echo [ERROR] No existe "%ISO_MAKER%".
goto failed
:iso_failed
echo.
echo [ERROR] No se pudo generar la ISO (falta pycdlib? pip install pycdlib).
echo  El ELF y el ROMFS release si quedaron actualizados.
popd
exit /b 1
:missing_powershell
echo [ERROR] PowerShell no esta disponible.
goto failed
:missing_python
echo [ERROR] Python no esta disponible en PATH.
goto failed
:missing_elf
echo [ERROR] El compilador no genero el ELF temporal.
goto failed
:missing_assets
echo [ERROR] Faltan archivos esenciales del ROMFS empaquetado.
:failed
echo.
echo [ERROR] El build no termino. El ultimo ELF release no se reemplazo.
popd
exit /b 1
