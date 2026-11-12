param([string]$ToolchainRoot = '', [switch]$PartialWriteDiagnostic)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $root 'psdevwindows' }
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
$sdk = if ($env:PS2SDK) { $env:PS2SDK } else { Join-Path $ToolchainRoot 'ps2sdk' }
$cc = Join-Path $ToolchainRoot 'ee/bin/mips64r5900el-ps2-elf-gcc.exe'
$linker = Join-Path $sdk 'ee/startup/linkfile'
$env:PATH = (Join-Path $ToolchainRoot 'ee/bin') + ';' + $env:PATH
$out = Join-Path $root 'ps2/out/audio'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$includeFlags = @("-I$(Join-Path $sdk 'ee/include')",
                  "-I$(Join-Path $sdk 'common/include')",
                  "-I$(Join-Path $root 'psp/include')",
                  "-I$(Join-Path $root 'ps2/src')")
$sourceList = @('ps2/audio_test.c', 'ps2/src/ndsp_mixer.c',
                'ps2/src/ndsp_ps2.c', 'ps2/src/audsrv_stream.c', 'ps2/audio_irx.S')
$objects = @()
foreach ($src in $sourceList) {
    $inputPath = Join-Path $root $src
    $outputPath = Join-Path $out ((Split-Path -Leaf $src) -replace '\.(c|S)$', '.o')
    Push-Location -LiteralPath $root
    try {
        $flags = @('-D_EE', '-D__PS2__', '-G0', '-O2', '-Wall', '-Wextra') +
                 $includeFlags + @('-c', $inputPath, '-o', $outputPath)
        if ($PartialWriteDiagnostic -and $src -eq 'ps2/src/ndsp_ps2.c') {
            $flags = @('-DPS2_AUDIO_TEST_PARTIAL_SEND') + $flags
        }
        & $cc @flags
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile $src ($LASTEXITCODE)" }
    $objects += $outputPath
}
$elfName = if ($PartialWriteDiagnostic) { 'gdps2-audio-partial-test.elf' } `
           else { 'gdps2-audio-test.elf' }
$elf = Join-Path (Join-Path $root 'ps2/out') $elfName
$args_link = @("-T$linker", '-Wl,-zmax-page-size=128',
    "-L$(Join-Path $sdk 'ee/lib')", '-o', $elf) +
    $objects + @('-laudsrv', '-lpatches', '-lm', '-lc', '-lkernel')
& $cc @args_link
if ($LASTEXITCODE -ne 0) { throw "Audio test link failed ($LASTEXITCODE)" }
Write-Host "Audio diagnostic ELF: $elf"
Write-Host 'Listen for a 440 Hz left / 660 Hz right chord in PCSX2; runtime not verified.'
