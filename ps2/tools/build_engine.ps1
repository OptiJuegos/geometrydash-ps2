param(
    [string]$ToolchainRoot = '',
    [string]$Output = '',
    [switch]$PackageRomfs,
    [switch]$PackagePak,
    [switch]$DiagnosticAutoInput,
    [switch]$DiagnosticAudioTrace,
    [switch]$DiagnosticMenuStages,
    [switch]$DiagnosticFrameTiming,
    [switch]$DiagnosticBindDedup,
    [switch]$DiagnosticNoClipLevel,
    [switch]$DiagnosticMemoryMp3,
    [switch]$DiagnosticMp3Priority,
    [switch]$DiagnosticUsbLog,
    [switch]$DiagnosticNoMixerThread,
    [switch]$DiagnosticFastFixedMixer,
    [int]$DiagnosticMixerPriority = -1,
    [int]$DiagnosticMainPriority = -1,
    [int]$DiagnosticLevelIndex = -1
)

# Build an *experimental* ELF with source/main.c, not the menu demo.
# A successful link does not imply that the original atlas/audio/ROMFS files
# have been packaged, or that the code has passed a PCSX2 runtime test.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ($DiagnosticAutoInput -and -not $Output) {
    throw 'DiagnosticAutoInput requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticAudioTrace -and -not $Output) {
    throw 'DiagnosticAudioTrace requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticMenuStages -and -not $Output) {
    throw 'DiagnosticMenuStages requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticFrameTiming -and -not $Output) {
    throw 'DiagnosticFrameTiming requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticBindDedup -and -not $Output) {
    throw 'DiagnosticBindDedup requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticNoClipLevel -and -not $Output) {
    throw 'DiagnosticNoClipLevel requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticNoClipLevel -and -not $DiagnosticAutoInput) {
    throw 'DiagnosticNoClipLevel requires DiagnosticAutoInput and a separate diagnostic ELF.'
}
if ($DiagnosticMemoryMp3 -and -not $Output) {
    throw 'DiagnosticMemoryMp3 requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticMp3Priority -and -not $Output) {
    throw 'DiagnosticMp3Priority requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticUsbLog -and -not $Output) {
    throw 'DiagnosticUsbLog requires -Output with a separate debug ELF path.'
}
if ($DiagnosticNoMixerThread -and -not $Output) {
    throw 'DiagnosticNoMixerThread requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticFastFixedMixer -and -not $Output) {
    throw 'DiagnosticFastFixedMixer requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticMixerPriority -ge 1 -and -not $Output) {
    throw 'DiagnosticMixerPriority requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticMainPriority -ge 1 -and -not $Output) {
    throw 'DiagnosticMainPriority requires -Output with a separate diagnostic ELF path.'
}
if ($DiagnosticLevelIndex -ge 0 -and -not $DiagnosticAutoInput) { throw 'DiagnosticLevelIndex requires DiagnosticAutoInput.' }
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $root 'psdevwindows' }
if (-not $Output) { $Output = Join-Path $root 'ps2/out/gdps2-engine-experimental.elf' }
$Output = [IO.Path]::GetFullPath($Output)
$engine = Join-Path $root 'ps2/out/engine'

$buildArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    (Join-Path $PSScriptRoot 'check_engine.ps1'), '-ToolchainRoot', $ToolchainRoot)
if ($DiagnosticAudioTrace) { $buildArgs += '-DiagnosticAudioTrace' }
if ($DiagnosticMenuStages) { $buildArgs += '-DiagnosticMenuStages' }
if ($DiagnosticFrameTiming) { $buildArgs += '-DiagnosticFrameTiming' }
if ($DiagnosticNoClipLevel) { $buildArgs += '-DiagnosticNoClipLevel' }
if ($DiagnosticMemoryMp3) { $buildArgs += '-DiagnosticMemoryMp3' }
if ($DiagnosticMp3Priority) { $buildArgs += '-DiagnosticMp3Priority' }
& powershell @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Engine translation-unit build failed ($LASTEXITCODE)" }

$linkArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    (Join-Path $PSScriptRoot 'check_link.ps1'), '-ToolchainRoot', $ToolchainRoot)
if ($DiagnosticAutoInput) { $linkArgs += '-DiagnosticAutoInput' }
if ($DiagnosticUsbLog) { $linkArgs += '-DiagnosticUsbLog' }
if ($DiagnosticAudioTrace) { $linkArgs += '-DiagnosticAudioTrace' }
if ($DiagnosticMenuStages) { $linkArgs += '-DiagnosticMenuStages' }
if ($DiagnosticFrameTiming) { $linkArgs += '-DiagnosticFrameTiming' }
if ($DiagnosticBindDedup) { $linkArgs += '-DiagnosticBindDedup' }
if ($DiagnosticNoMixerThread) { $linkArgs += '-DiagnosticNoMixerThread' }
if ($DiagnosticFastFixedMixer) { $linkArgs += '-DiagnosticFastFixedMixer' }
if ($DiagnosticMixerPriority -ge 1) { $linkArgs += @('-DiagnosticMixerPriority', "$DiagnosticMixerPriority") }
if ($DiagnosticMainPriority -ge 1) { $linkArgs += @('-DiagnosticMainPriority', "$DiagnosticMainPriority") }
if ($DiagnosticLevelIndex -ge 0) { $linkArgs += @('-DiagnosticLevelIndex', "$DiagnosticLevelIndex") }
& powershell @linkArgs
if ($LASTEXITCODE -ne 0) { throw "Engine link checker failed ($LASTEXITCODE)" }

$report = Get-Content -LiteralPath (Join-Path $engine 'link_report.json') -Raw |
    ConvertFrom-Json
if (-not $report.link_succeeded -or $report.unresolved_unique -ne 0) {
    throw "Unresolved link: $($report.unresolved_unique) symbols; see link_report.json"
}
$source = Join-Path $engine 'link_probe.elf'
if (-not (Test-Path -LiteralPath $source) -or (Get-Item $source).Length -lt 128) {
    throw "Missing or empty ELF from link step"
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
Copy-Item -LiteralPath $source -Destination $Output -Force
Write-Host "EXPERIMENTAL PS2 ELF: $Output"
if ($PackageRomfs) {
    $packer = Join-Path $PSScriptRoot 'package_romfs.py'
    & python $packer
    if ($LASTEXITCODE -ne 0) { throw "PS2 ROMFS package failed ($LASTEXITCODE)" }
}
if ($PackagePak) {
    $packer = Join-Path $PSScriptRoot 'make_assets_pak.py'
    # PS2 CD/DVD: arrange boot assets in actual load order, with 2048-byte
    # sector-aligned extents. MCPK v1 hash index remains compatible.
    & python $packer --layout boot --align 2048
    if ($LASTEXITCODE -ne 0) { throw "PS2 assets.pak packaging failed ($LASTEXITCODE)" }
    # Pak releases always ship a bootable ISO alongside the ELF and assets.pak.
    # Its SYSTEM.CNF boots the ELF from the disc root; romfs/ is NOT duplicated.
    $buildDir = Split-Path -Parent $Output
    $pakSource = Join-Path $root 'ps2/out/package/assets.pak'
    $pakOutput = Join-Path $buildDir 'assets.pak'
    if (-not [string]::Equals(
            [IO.Path]::GetFullPath($pakSource),
            [IO.Path]::GetFullPath($pakOutput),
            [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $pakSource -Destination $pakOutput -Force
    }
    # Keep the packaged edition distinct from any loose-ROMFS ISO which may
    # already be mounted in PCSX2 (Windows locks its image while running).
    $isoOutput = Join-Path $buildDir 'GeometryDashPS2-PAK.iso'
    $isoMaker = Join-Path $PSScriptRoot 'make_iso.py'
    & python $isoMaker --elf $Output --pak $pakOutput --out $isoOutput
    if ($LASTEXITCODE -ne 0) {
        throw "PS2 pak ISO generation failed ($LASTEXITCODE); check pycdlib and make_iso.py"
    }
    if (-not (Test-Path -LiteralPath $isoOutput)) { throw "ISO not generated: $isoOutput" }
    Write-Host "PS2 PAK RELEASE: ELF=$Output"
    Write-Host "PS2 PAK RELEASE: PAK=$pakOutput"
    Write-Host "PS2 PAK RELEASE: ISO=$isoOutput"
    Write-Host "ISO creation is verified; PS2 hardware boot still needs testing."
} else {
    Write-Host "No PCSX2 test performed; place the staged ROMFS beside the ELF host root."
}
