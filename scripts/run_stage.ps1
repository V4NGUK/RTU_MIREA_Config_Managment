param(
    [ValidateSet(2, 3, 4)][int]$Stage = 4,
    [string]$Emulator = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
if (-not $Emulator) {
    foreach ($relative in @('build/emulator.exe', 'build/Release/emulator.exe', 'cmake-build-debug/emulator.exe')) {
        $candidate = Join-Path $root $relative
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Emulator = $candidate; break }
    }
}
if (-not $Emulator) { throw 'Сначала соберите проект или укажите -Emulator.' }
$Emulator = (Resolve-Path -LiteralPath $Emulator).Path
$fixtures = Join-Path (Split-Path $Emulator) 'vfs'
if (-not (Test-Path -LiteralPath $fixtures)) {
    $fixtures = Join-Path (Split-Path (Split-Path $Emulator)) 'vfs'
}
$previousOutput = $env:SHELL_EMULATOR_OUTPUT
$previousFixtures = $env:SHELL_EMULATOR_FIXTURES
$env:SHELL_EMULATOR_FIXTURES = $fixtures.Replace('\', '/')
$env:SHELL_EMULATOR_OUTPUT = (Join-Path $root 'build/stage-output').Replace('\', '/')
New-Item -ItemType Directory -Path $env:SHELL_EMULATOR_OUTPUT -Force | Out-Null
Push-Location $root
try {
    if ($Stage -eq 2) {
        & $Emulator --vfs-path "$fixtures/minimal.zip" --startup-script scripts/stage2/minimal.emu
    } elseif ($Stage -eq 3) {
        & $Emulator --vfs "$fixtures/nested.zip" --script scripts/stage3/full_demo.emu
    } else {
        & $Emulator --vfs "$fixtures/commands.zip" --script "scripts/stage$Stage/full_demo.emu"
    }
    if ($LASTEXITCODE -ne 0) { throw "Эмулятор завершился с кодом $LASTEXITCODE" }
    if ($Stage -eq 4) {
        "one`ntwo" | & $Emulator --vfs "$fixtures/commands.zip" --script scripts/stage4/stdin_tac.emu
        if ($LASTEXITCODE -ne 0) { throw 'Ошибка примера tac' }
        'hello' | & $Emulator --vfs "$fixtures/commands.zip" --script scripts/stage4/stdin_rev.emu
        if ($LASTEXITCODE -ne 0) { throw 'Ошибка примера rev' }
    }
} finally {
    Pop-Location
    $env:SHELL_EMULATOR_OUTPUT = $previousOutput
    $env:SHELL_EMULATOR_FIXTURES = $previousFixtures
}
