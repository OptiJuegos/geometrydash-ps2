param(
    [string]$ToolchainRoot = '',
    [string]$OutputDir = '',
    [switch]$SyntaxOnly,
    [switch]$DiagnosticAudioTrace,
    [switch]$DiagnosticMenuStages,
    [switch]$DiagnosticFrameTiming,
    [switch]$DiagnosticNoClipLevel,
    [switch]$DiagnosticMemoryMp3,
    [switch]$DiagnosticMp3Priority
)

# Compile every source translation unit of the original offline game, keeping
# errors isolated per file. This is intentionally NOT an ELF link step: the
# original main() still has unresolved 3DS/audio/filesystem dependencies.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $root 'psdevwindows' }
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
if (-not $OutputDir) { $OutputDir = Join-Path $root 'ps2/out/engine' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$sdk = if ($env:PS2SDK) { $env:PS2SDK } else { Join-Path $ToolchainRoot 'ps2sdk' }
$gskit = if ($env:GSKIT) { $env:GSKIT } else { Join-Path $ToolchainRoot 'gsKit' }
$cc = Join-Path $ToolchainRoot 'ee/bin/mips64r5900el-ps2-elf-gcc.exe'
if (-not (Test-Path -LiteralPath $cc)) { throw "Missing PS2 compiler: $cc" }

$env:PATH = (Join-Path $ToolchainRoot 'ee/bin') + ';' + $env:PATH
$sourcePaths = @(Get-ChildItem (Join-Path $root 'source') -Recurse -File -Filter '*.c' |
    Where-Object { $_.FullName -notlike '*\source\utils\network.c' } |
    Sort-Object FullName | ForEach-Object { $_.FullName })
$sourcePaths += (Join-Path $root 'libraries/color.c')
$sourcePaths += (Join-Path $root 'ps2/third_party/cjson/cJSON.c')

$includes = @(
    "-I$(Join-Path $root 'source')",
    "-I$(Join-Path $root 'libraries')",
    "-I$(Join-Path $root 'psp/include')",
    "-I$(Join-Path $root 'ps2/third_party')",
    "-I$(Join-Path $root 'ps2/src')",
    "-I$(Join-Path $sdk 'ee/include')",
    "-I$(Join-Path $sdk 'common/include')",
    "-I$(Join-Path $sdk 'ports/include')",
    "-I$(Join-Path $gskit 'include')"
)
$flags = @('-D_EE', '-D__PS2__', '-G0', '-O2', '-std=gnu11', '-Wall',
           '-Wno-unused-function', '-Wno-unused-variable',
           '-Wno-unused-but-set-variable') + $includes
# Experiment hook: extra compiler flags (e.g. '-O3'), appended so they win.
if ($env:GDPS2_EXTRA_CFLAGS) { $flags += @($env:GDPS2_EXTRA_CFLAGS -split '\s+' | Where-Object { $_ }) }

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$objectsDir = Join-Path $OutputDir 'objects'
$logsDir = Join-Path $OutputDir 'logs'
New-Item -ItemType Directory -Force -Path $objectsDir, $logsDir | Out-Null
$entries = New-Object System.Collections.ArrayList
$failed = 0
Push-Location -LiteralPath $root
try {
    foreach ($path in $sourcePaths) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Missing source: $path" }
        $relative = $path.Substring($root.Length).TrimStart('\','/') -replace '\\','/'
        $stem = $relative -replace '\.c$', ''
        $objectPath = Join-Path $objectsDir ($stem.Replace('/', [IO.Path]::DirectorySeparatorChar) + '.o')
        $logPath = Join-Path $logsDir ($stem.Replace('/', [IO.Path]::DirectorySeparatorChar) + '.log')
        $parent = Split-Path -Parent $objectPath
        $logParent = Split-Path -Parent $logPath
        New-Item -ItemType Directory -Force -Path $parent, $logParent | Out-Null
        $extra = @()
        if ($DiagnosticAudioTrace -and $relative -eq 'source/music_player.c') {
            $extra += '-DPS2_MP3_DIAGNOSTIC'
        }
        if ($DiagnosticMenuStages -and $relative -eq 'source/menus/main_menu.c') {
            $extra += '-DPS2_DIAG_MENU_STAGES'
        }
        if ($DiagnosticFrameTiming -and $relative -eq 'source/main.c') {
            $extra += '-DPS2_DIAG_FRAME_TIMING'
        }
        if ($DiagnosticNoClipLevel -and $relative -eq 'source/main.c') {
            $extra += '-DPS2_DIAG_NOCLIP_LEVEL'
        }
        if ($DiagnosticMenuStages -and $relative -eq 'source/music_player.c') {
            $extra += '-DPS2_DIAG_MENU_STAGES'
        }
        if ($DiagnosticMemoryMp3 -and $relative -eq 'source/music_player.c') {
            $extra += '-DPS2_DIAG_MP3_MEMORY'
        }
        if ($DiagnosticMp3Priority -and $relative -eq 'source/music_player.c') {
            $extra += '-DPS2_DIAG_MP3_PRIORITY'
        }
        $args = $flags + $extra + $(if ($SyntaxOnly) { @('-fsyntax-only') } else { @('-c') }) + @($path)
        if (-not $SyntaxOnly) { $args += @('-o', $objectPath) }

        # GCC diagnostics on stderr are expected for a work-in-progress port.
        # Do not convert them to PowerShell terminating errors.
        $ErrorActionPreference = 'Continue'
        $outputLines = @(& $cc @args 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $outputText = $outputLines -join "`n"
        [IO.File]::WriteAllText($logPath, $outputText, [Text.Encoding]::UTF8)
        if ($exitCode -ne 0) { $failed++ }
        $detail = ($outputLines | Where-Object { $_ -match '(fatal error:|error:|warning:)' } | Select-Object -First 3) -join ' | '
        [void]$entries.Add([pscustomobject]@{
            source = $relative
            success = ($exitCode -eq 0)
            exit_code = $exitCode
            object = $(if ($SyntaxOnly -or $exitCode -ne 0) { $null } else { $objectPath.Substring($root.Length).TrimStart('\','/') -replace '\\','/' })
            log = ($logPath.Substring($root.Length).TrimStart('\','/') -replace '\\','/')
            detail = $detail
        })
        if ($exitCode -ne 0) { Write-Host "FAIL $relative : $detail" }
    }
} finally {
    Pop-Location
}

$report = [pscustomobject]@{
    compiler = $cc
    mode = $(if ($SyntaxOnly) { 'syntax' } else { 'objects' })
    total = $entries.Count
    succeeded = $entries.Count - $failed
    failed = $failed
    excluded = @('source/utils/network.c')
    files = @($entries.ToArray())
}
$reportPath = Join-Path $OutputDir 'compile_report.json'
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "Engine compile: $($report.succeeded)/$($report.total) succeeded; $failed failed"
Write-Host "Report: $reportPath"
if ($failed) { exit 1 }
