param([string]$ToolchainRoot = '')

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $root 'psdevwindows' }
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
$sdk = Join-Path $ToolchainRoot 'ps2sdk'
$cc = Join-Path $ToolchainRoot 'ee/bin/mips64r5900el-ps2-elf-gcc.exe'
$env:PATH = (Join-Path $ToolchainRoot 'ee/bin') + ';' + $env:PATH
$out = Join-Path $root 'ps2/out/gdps2-mp3-decode-probe.elf'
$args_cc = @('-D_EE', '-G0', '-O2', '-Wall', '-Wextra',
    "-I$(Join-Path $sdk 'ee/include')", "-I$(Join-Path $sdk 'common/include')",
    "-I$(Join-Path $sdk 'ports/include')",
    "-T$(Join-Path $sdk 'ee/startup/linkfile')", '-Wl,-zmax-page-size=128',
    "-L$(Join-Path $sdk 'ee/lib')", "-L$(Join-Path $sdk 'ports/lib')",
    (Join-Path $root 'ps2/tests/mp3_decode_ps2_probe.c'),
    '-o', $out, '-lmpg123', '-lm', '-lc', '-lkernel')
& $cc @args_cc
if ($LASTEXITCODE -ne 0) { throw "Standalone MP3 probe failed to build ($LASTEXITCODE)" }
Write-Host "Standalone MP3 decode ELF: $out"
