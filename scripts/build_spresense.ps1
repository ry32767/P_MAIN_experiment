param([string]$Cli = 'arduino-cli', [string]$Config = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $cliArgs = @()
    if ($Config) { $cliArgs += @('--config-file', $Config) }
    & $Cli @cliArgs compile --fqbn 'SPRESENSE:spresense:spresense' --libraries common --build-path build/spresense firmware/spresense/logger
    if ($LASTEXITCODE -ne 0) { throw 'Spresense build failed' }
} finally { Pop-Location }
