#requires -Version 5.1
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string]$ConfigurationFile,
    [ValidateSet('Validate','Build','Prepare','ExportSigningBundle','Pack','Check')][string]$Phase='Validate',
    [bool]$RequireSigned=$true,
    [string]$SignedPayloadRoot,
    [string]$Python='python'
)
$ErrorActionPreference='Stop'
$arguments=@("$PSScriptRoot/tools/producer.py",'--config',$ConfigurationFile,'--phase',$Phase,
    '--require-signed',$RequireSigned.ToString().ToLowerInvariant())
if($WhatIfPreference) { $arguments+='--dry-run' }
if($SignedPayloadRoot) { $arguments+=@('--signed-payload-root',$SignedPayloadRoot) }
& $Python @arguments
if($LASTEXITCODE) { throw "Producer failed ($LASTEXITCODE): $Phase" }
