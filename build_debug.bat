@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Independent physical-PS2 diagnostic ELF. NEVER overwrites build.bat release.
rem No autonav / autojump / noclip, no PCSX2 launch and no Memory Card edits.
pushd "%~dp0" || exit /b 1
set "DEBUG_DIR=%CD%\ps2\out\debug"
set "BUILD_ELF=%DEBUG_DIR%\GeometryDashPS2_DEBUG.build.elf"
set "DEBUG_ELF=%DEBUG_DIR%\GeometryDashPS2_DEBUG.elf"
set "SDK=%CD%\psdevwindows\ps2sdk"
if not exist "%DEBUG_DIR%" mkdir "%DEBUG_DIR%" || goto failed

echo.
echo ========================================
echo  Geometry Dash PS2 - USB Debug build
echo ========================================
echo  Auto-input OFF / Sin modificar release
echo  El juego intenta escribir mass:/GDPS2/debug.log
echo.

echo [1/3] Preparando drivers USB embebidos desde el SDK local...
python.exe "%CD%\ps2\tools\embed_debug_irx.py" --sdk "%SDK%" --output "%DEBUG_DIR%\debug_usb_irx.S"
if errorlevel 1 goto failed

echo [2/3] Compilando ELF con logging persistente...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\ps2\tools\build_engine.ps1" -Output "%BUILD_ELF%" -DiagnosticUsbLog
if errorlevel 1 goto failed
if not exist "%BUILD_ELF%" goto failed

echo [3/3] Empaquetando ROMFS debug separado...
python.exe "%CD%\ps2\tools\package_romfs.py" --output "%DEBUG_DIR%"
if errorlevel 1 goto failed
rem On hardware with unsupported LOADFILE buffer patch, try audsrv from USB.
copy /Y "%SDK%\iop\irx\audsrv.irx" "%DEBUG_DIR%\audsrv.irx" >nul
if errorlevel 1 goto failed
if not exist "%DEBUG_DIR%\romfs\menus\main_menu.txt" goto failed
if not exist "%DEBUG_DIR%\romfs\gfx\ui.p2sheet" goto failed
if not exist "%DEBUG_DIR%\romfs\songs\menuLoop.wav" goto failed
move /Y "%BUILD_ELF%" "%DEBUG_ELF%" >nul
if errorlevel 1 goto failed

echo.
echo [OK] DEBUG ELF: "%DEBUG_ELF%"
echo [OK] DEBUG ROMFS: "%DEBUG_DIR%\romfs"
echo Para USB, copiar ELF como mass:/GDPS2/GeometryDashPS2_DEBUG.elf
echo y copiar audsrv.irx como mass:/GDPS2/audsrv.irx.
echo y toda la carpeta romfs junto al ELF.
echo Crear la carpeta GDPS2 en FAT32 antes de ejecutarlo.
echo Si USB inicializa, revisar mass:/GDPS2/debug.log despues del fallo.
echo El release normal permanece intacto.
popd
exit /b 0

:failed
echo.
echo [ERROR] Debug build fallido. No se toco GeometryDashPS2.elf release.
popd
exit /b 1
