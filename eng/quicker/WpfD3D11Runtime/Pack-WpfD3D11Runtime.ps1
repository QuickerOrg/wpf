#requires -Version 5.1
# Compatibility name only. All production logic lives in the single producer entry.
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string]$ConfigurationFile,
    [bool]$RequireSigned=$true,
    [string]$SignedPayloadRoot,
    [string]$Python='python'
)
& "$PSScriptRoot/Invoke-WpfD3D11RuntimeProducer.ps1" -ConfigurationFile $ConfigurationFile -Phase Pack -RequireSigned:$RequireSigned -SignedPayloadRoot $SignedPayloadRoot -Python $Python -WhatIf:$WhatIfPreference
