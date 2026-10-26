param(
 [string]$Cli = "$env:LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe",
 [string]$Config = "$PSScriptRoot/../../P_MAIN_experiment/.tools/arduino-cli.yaml"
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$library=Join-Path $root 'firmware/spresense/libraries/AquaBeacon/src'
# Shared protocol/bit engine are canonical in common/, generated copies are
# needed for Arduino's library dependency resolver. Never edit the original project.
foreach($name in @('spresense_protocol.h','single_wire_uart.h','spresense_control_protocol.h','spresense_calibration.h','sd_sector_write.h','sensor_time.h','spresense_pins.h')) {
 Copy-Item -LiteralPath (Join-Path $root "common/$name") -Destination (Join-Path $library $name) -Force
}
Set-Content -LiteralPath (Join-Path $library 'SpresenseControl.h') -Value '#pragma once', '#include "spresense_control_protocol.h"', '#include "spresense_calibration.h"' -Encoding utf8
Push-Location $root
try {
 foreach($target in @('formatter','logger')) {
  $fqbn=if($target -eq 'formatter'){'SPRESENSE:spresense:spresense:Core=Sub1'}else{'SPRESENSE:spresense:spresense'}
  & $Cli --config-file $Config compile --fqbn $fqbn --libraries firmware/spresense/libraries --build-path "build/spresense/$target" "firmware/spresense/$target"
  if($LASTEXITCODE -ne 0){throw "Spresense $target build failed"}
 }
} finally {Pop-Location}
