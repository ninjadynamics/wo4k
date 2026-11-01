# Compatibility entry point for earlier local probes.
param([string]$GameRoot=(Join-Path $PSScriptRoot '../wobble'),[switch]$Probe)
& (Join-Path $PSScriptRoot 'Show-Video.ps1') -GameRoot $GameRoot -Movie intro -Probe:$Probe
