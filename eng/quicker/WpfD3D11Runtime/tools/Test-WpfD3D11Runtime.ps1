#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Input','Output')][string]$Mode,
    [Parameter(Mandatory)][string]$PackageRoot,
    [string]$RuntimeIdentifier,
    [string]$OutputDirectory,
    [string]$DepsFile,
    [ValidateSet('true','false')][string]$SelfContained='false',
    [string]$ReportPath
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/Payload.Common.ps1"
try {
    if($Mode -eq 'Input') {
        $m=Test-WpfInput $PackageRoot $RuntimeIdentifier
        $result=@{mode='input';rid=$RuntimeIdentifier;files=$m.files.Count;passed=$true}
    } else { $result=Test-WpfOutput $PackageRoot $RuntimeIdentifier $OutputDirectory $DepsFile ($SelfContained -eq 'true') }
    $json=$result | ConvertTo-Json -Depth 10
    if($ReportPath) { [IO.File]::WriteAllText($ReportPath,$json,[Text.UTF8Encoding]::new($false)) }
    Write-Output $json
} catch { Write-Error $_.Exception.Message -ErrorAction Continue; exit 1 }
