@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem One-command Geometry Dash PS2 MCPK release: ELF + assets.pak + bootable ISO.
rem Relative to this script; no hardcoded game installation path.
pushd "%~dp0" || exit /b 1

where python.exe >nul 2>&1 || goto missing_python
where powershell.exe >nul 2>&1 || goto missing_powershell

echo.
echo ========================================
echo  Geometry Dash PS2 - MCPK pak release
echo ========================================
echo  Building engine, resources, pak and ISO
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "ps2\tools\build_engine.ps1" -PackageRomfs -PackagePak -Output "ps2\out\pak-release\GeometryDashPS2.elf"
if errorlevel 1 goto failed
if not exist "ps2\out\pak-release\GeometryDashPS2.elf" goto failed
if not exist "ps2\out\pak-release\assets.pak" goto failed
if not exist "ps2\out\pak-release\GeometryDashPS2-PAK.iso" goto failed

echo.
echo OK: ps2\out\pak-release\GeometryDashPS2.elf
echo OK: ps2\out\pak-release\assets.pak
echo OK: ps2\out\pak-release\GeometryDashPS2-PAK.iso
popd
exit /b 0

:missing_python
echo [ERROR] Python not found in PATH.
goto failed
:missing_powershell
echo [ERROR] PowerShell not found in PATH.
:failed
echo [ERROR] MCPK release build failed; see output above.
popd
exit /b 1
