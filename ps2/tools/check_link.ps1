param(
    [string]$ToolchainRoot = '',
    [string]$OutputDir = '',
    [switch]$DiagnosticAutoInput,
    [switch]$DiagnosticAudioTrace,
    [switch]$DiagnosticMenuStages,
    [switch]$DiagnosticFrameTiming,
    [switch]$DiagnosticBindDedup,
    [switch]$DiagnosticNoMixerThread,
    [switch]$DiagnosticFastFixedMixer,
    [switch]$DiagnosticUsbLog,
    [int]$DiagnosticMixerPriority = -1,
    [int]$DiagnosticMainPriority = -1,
    [int]$DiagnosticLevelIndex = -1
)

# Diagnostic-only whole-engine link. It inventories *real* unresolved symbols
# and NEVER promotes link_probe.elf into the playable GDPS2 executable.
$ErrorActionPreference = 'Stop'
if ($DiagnosticLevelIndex -lt -1 -or $DiagnosticLevelIndex -gt 19) { throw 'DiagnosticLevelIndex must be between 0 and 19' }
if ($DiagnosticLevelIndex -ge 0 -and -not $DiagnosticAutoInput) { throw 'DiagnosticLevelIndex requires DiagnosticAutoInput' }
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $root 'psdevwindows' }
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
if (-not $OutputDir) { $OutputDir = Join-Path $root 'ps2/out/engine' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$sdk = if ($env:PS2SDK) { $env:PS2SDK } else { Join-Path $ToolchainRoot 'ps2sdk' }
$gskit = if ($env:GSKIT) { $env:GSKIT } else { Join-Path $ToolchainRoot 'gsKit' }
$eeBin = Join-Path $ToolchainRoot 'ee/bin'
$cc = Join-Path $eeBin 'mips64r5900el-ps2-elf-gcc.exe'
$linker = Join-Path $sdk 'ee/startup/linkfile'
$env:PATH = $eeBin + ';' + $env:PATH
$sourceObjects = @(Get-ChildItem (Join-Path $OutputDir 'objects') -Recurse -File -Filter '*.o' |
    Sort-Object FullName | ForEach-Object { $_.FullName })
$compileReport = Join-Path $OutputDir 'compile_report.json'
if (-not (Test-Path -LiteralPath $compileReport)) { throw 'Run check_engine.ps1 first' }
$report = Get-Content -LiteralPath $compileReport -Raw | ConvertFrom-Json
if ($report.failed -ne 0 -or $sourceObjects.Count -ne $report.total) {
    throw "Expected $($report.total) successful engine objects, found $($sourceObjects.Count)"
}

$backendSources = @('ps2/src/boot_trace.c','ps2/src/ctru_ps2.c','ps2/src/c2d_ps2.c',
                    'ps2/src/ctru_ps2_input.c','ps2/src/romfs_paths.c','ps2/src/disc_names.c',
                    'ps2/src/romfs_ps2.c','ps2/src/asset_locator.c','ps2/src/asset_pak.c','ps2/src/ndsp_mixer.c','ps2/src/audsrv_stream.c',
                    'ps2/src/ndsp_ps2.c','ps2/src/swkbd_edit.c',
                    'ps2/src/swkbd_ps2.c','ps2/src/startup_ps2.c','ps2/audio_irx.S')
$backendObjects = @()
$includes = @(
    "-I$(Join-Path $root 'psp/include')",
    "-I$(Join-Path $root 'ps2/src')",
    "-I$(Join-Path $sdk 'ee/include')",
    "-I$(Join-Path $sdk 'common/include')",
    "-I$(Join-Path $gskit 'include')"
)
foreach ($src in $backendSources) {
    $sourcePath = Join-Path $root $src
    $objectPath = Join-Path $OutputDir ((Split-Path $src -Leaf) -replace '\.(c|S)$', '.o')
    if ($src.EndsWith('.S')) {
        Push-Location -LiteralPath $root
        try { & $cc '-D_EE' '-G0' @includes '-c' $sourcePath '-o' $objectPath }
        finally { Pop-Location }
    } else {
        $extraFlags = @()
        if ($DiagnosticUsbLog -and ($src -eq 'ps2/src/boot_trace.c' -or $src -eq 'ps2/src/ndsp_ps2.c')) {
            $extraFlags += '-DPS2_DIAG_USB_LOG'
        }
        if ($DiagnosticAutoInput -and $src -eq 'ps2/src/ctru_ps2_input.c') {
            $extraFlags += '-DPS2_DIAG_AUTONAV'
            if ($DiagnosticLevelIndex -ge 0) { $extraFlags += "-DPS2_DIAG_LEVEL_INDEX=$DiagnosticLevelIndex" }
        }
        if (($DiagnosticAudioTrace -or $DiagnosticMenuStages) -and -not $DiagnosticAutoInput -and
            $src -eq 'ps2/src/ctru_ps2_input.c') {
            $extraFlags += '-DPS2_DIAG_NO_INPUT'
        }
        if ($DiagnosticAudioTrace -and ($src -eq 'ps2/src/ndsp_ps2.c' -or
                                         $src -eq 'ps2/src/ndsp_mixer.c')) {
            $extraFlags += '-DPS2_AUDIO_DIAGNOSTIC'
        }
        if ($DiagnosticNoMixerThread -and $src -eq 'ps2/src/ndsp_ps2.c') {
            $extraFlags += '-DPS2_DIAG_NO_MIXER_THREAD'
        }
        if ($DiagnosticFastFixedMixer -and $src -eq 'ps2/src/ndsp_mixer.c') {
            $extraFlags += '-DPS2_MIXER_FAST_FIXED'
        }
        if ($DiagnosticMixerPriority -ge 1 -and $src -eq 'ps2/src/ndsp_ps2.c') {
            $extraFlags += "-DPS2_DIAG_MIXER_PRIORITY=$DiagnosticMixerPriority"
        }
        if ($DiagnosticMainPriority -ge 1 -and $src -eq 'ps2/src/ndsp_ps2.c') {
            $extraFlags += "-DPS2_DIAG_MAIN_PRIORITY=$DiagnosticMainPriority"
        }
        if ($DiagnosticMenuStages -and $src -eq 'ps2/src/ndsp_ps2.c') {
            $extraFlags += '-DPS2_DIAG_PCM_HEARTBEAT'
        }
        if ($DiagnosticFrameTiming -and $src -eq 'ps2/src/c2d_ps2.c') {
            $extraFlags += '-DPS2_DIAG_FRAME_TIMING'
        }
        if ($DiagnosticBindDedup -and $src -eq 'ps2/src/c2d_ps2.c') {
            $extraFlags += '-DPS2_DIAG_BIND_DEDUP'
        }
        if ($env:GDPS2_EXTRA_CFLAGS) { $extraFlags += @($env:GDPS2_EXTRA_CFLAGS -split '\s+' | Where-Object { $_ }) }
        & $cc '-D_EE' '-D__PS2__' '-G0' '-O2' '-std=gnu11' '-Wall' '-Wextra' @extraFlags @includes '-c' $sourcePath '-o' $objectPath
    }
    if ($LASTEXITCODE -ne 0) { throw "Backend compilation failed: $src" }
    $backendObjects += $objectPath
}

if ($DiagnosticUsbLog) {
    $generatedUsb = Join-Path $root 'ps2/out/debug/debug_usb_irx.S'
    if (-not (Test-Path -LiteralPath $generatedUsb)) {
        throw 'Debug USB IRX assembly missing. Run build_debug.bat first.'
    }
    $debugUsbObject = Join-Path $OutputDir 'debug_usb_irx.o'
    & $cc '-D_EE' '-G0' '-c' $generatedUsb '-o' $debugUsbObject
    if ($LASTEXITCODE -ne 0) { throw 'Debug USB IRX embedding failed' }
    $backendObjects += $debugUsbObject
}

$elf = Join-Path $OutputDir 'link_probe.elf'
$args = @("-T$linker", '-Wl,-zmax-page-size=128',
          "-L$(Join-Path $sdk 'ee/lib')",
          "-L$(Join-Path $sdk 'ports/lib')",
          "-L$(Join-Path $gskit 'lib')", '-o', $elf) +
        @('-Wl,--wrap=fopen','-Wl,--wrap=open','-Wl,--wrap=stat',
          '-Wl,--wrap=opendir','-Wl,--wrap=access','-Wl,--wrap=remove',
          '-Wl,--wrap=unlink','-Wl,--wrap=rename','-Wl,--wrap=mkdir') +
        $sourceObjects + $backendObjects +
        @('-lgskit','-ldmakit','-lpad','-lmc','-laudsrv','-lpatches','-lmpg123','-lz','-lm','-lc','-lkernel')
$ErrorActionPreference = 'Continue'
$diagnostics = @(& $cc @args 2>&1 | ForEach-Object { $_.ToString() })
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$log = Join-Path $OutputDir 'link_probe.log'
[IO.File]::WriteAllLines($log, $diagnostics, [Text.Encoding]::UTF8)
$symbolCounts = @{}
foreach ($line in $diagnostics) {
    if ($line -match 'undefined reference to [`'']([^`'']+)[`'']') {
        $name = $Matches[1]
        if (-not $symbolCounts.ContainsKey($name)) { $symbolCounts[$name] = 0 }
        $symbolCounts[$name]++
    }
}
$missing = @($symbolCounts.GetEnumerator() | Sort-Object Value -Descending |
    ForEach-Object { [pscustomobject]@{ name = $_.Key; sites = $_.Value } })
$summary = [pscustomobject]@{
    link_succeeded = ($code -eq 0)
    runnable_verified = $false
    objects = ($sourceObjects.Count + $backendObjects.Count)
    unresolved_unique = $missing.Count
    unresolved = $missing
    log = 'ps2/out/engine/link_probe.log'
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDir 'link_report.json') -Encoding UTF8
Write-Host "Link probe: $($summary.objects) objects, $($summary.unresolved_unique) missing symbols, status=$code"
$missing | Select-Object -First 30 | Format-Table -AutoSize
Write-Host "Log: $log"
Write-Host 'Diagnostic only. No playable ELF verified.'
