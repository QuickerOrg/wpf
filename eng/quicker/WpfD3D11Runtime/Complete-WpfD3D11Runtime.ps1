#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputRoot,
    [Parameter(Mandatory)][string]$QuickerRepository,
    [Parameter(Mandatory)][string]$ReferenceDirectory,
    [Parameter(Mandatory)][string]$EquivalenceReport,
    [string]$Crossgen2='F:/WpfP0/r2r-tools-10.0.14/package/tools/crossgen2.exe',
    [string]$SignTool,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl,
    [switch]$ExecuteSigning
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/tools/Payload.Common.ps1"
$build=Read-WpfJson "$OutputRoot/build-receipt.json"
$comparison=Read-WpfJson $EquivalenceReport
Assert-Wpf ($comparison.ContainsKey('unexpectedDifferences') -and $comparison.unexpectedDifferences -ceq $false) 'Unexpected/missing equivalence decision: stop this direction.'
Assert-Set @($comparison.files.name) @(Get-WpfNames) 'Equivalence report files'
foreach($entry in $comparison.files) {
    $path=Join-Path "$OutputRoot/il" $entry.name
    Assert-Wpf ((Get-FileHash -LiteralPath $path).Hash -ceq $entry.newSha256) "Equivalence report does not match actual IL: $path"
}
foreach($name in @('references','r2r')) {
    Assert-Wpf (-not (Test-Path -LiteralPath "$OutputRoot/$name")) "Finalization requires a new $name directory; do not reuse signed/evidence outputs."
}
# Same S5q crossgen2 contract, but all WPF references come from this new build.
New-Item -ItemType Directory "$OutputRoot/references" | Out-Null
Get-ChildItem -LiteralPath $ReferenceDirectory -Filter '*.dll' -File | Copy-Item -Destination "$OutputRoot/references"
Get-ChildItem -LiteralPath "$OutputRoot/il" -Filter '*.dll' -File | Copy-Item -Destination "$OutputRoot/references" -Force
$references=@(Get-ChildItem -LiteralPath "$OutputRoot/references" -Filter '*.dll' -File | Sort-Object Name | ForEach-Object {
    @{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
[IO.File]::WriteAllText("$OutputRoot/reference-inputs.json",($references | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
& "$QuickerRepository/Experiments/WpfRenderBackend/new-r2r-overlay.ps1" -IlDirectory "$OutputRoot/il" `
    -ReferenceDirectory "$OutputRoot/references" -OutputDirectory "$OutputRoot/r2r" -Architecture $build.architecture -Crossgen2 $Crossgen2
$overlay=Read-WpfJson "$OutputRoot/r2r/overlay-manifest.json"
foreach($file in $overlay.files) {
    $facts=Get-PeFacts "$OutputRoot/r2r/$($file.name)"
    Assert-WpfReleaseFile $facts $file.name
    if($file.managed) { Assert-Wpf $facts.readyToRun "Missing R2R header: $($file.name)" }
}
# Signing order is deliberate: R2R -> identity/headers -> signing -> final hashes.
& "$PSScriptRoot/Sign-WpfD3D11Runtime.ps1" -Directory "$OutputRoot/r2r" -QuickerRepository $QuickerRepository `
    -SignTool $SignTool -CertificateThumbprint $CertificateThumbprint -TimestampUrl $TimestampUrl -Execute:$ExecuteSigning
$overlay.build=$build
$overlay.equivalence=@{path=[IO.Path]::GetFullPath($EquivalenceReport);sha256=(Get-FileHash -LiteralPath $EquivalenceReport).Hash}
$overlay.referenceInputsSha256=(Get-FileHash "$OutputRoot/reference-inputs.json").Hash
$overlay.r2rScriptSha256=(Get-FileHash "$QuickerRepository/Experiments/WpfRenderBackend/new-r2r-overlay.ps1").Hash
$overlay.overlayCommonSha256=(Get-FileHash "$QuickerRepository/Experiments/WpfRenderBackend/overlay-common.ps1").Hash
$overlay.signing=Read-WpfJson "$OutputRoot/r2r/signing.json"
foreach($file in $overlay.files) { $file.sha256=(Get-FileHash "$OutputRoot/r2r/$($file.name)").Hash }
[IO.File]::WriteAllText("$OutputRoot/r2r/overlay-manifest.json",($overlay | ConvertTo-Json -Depth 25),[Text.UTF8Encoding]::new($false))
Write-Output "Finalized $($build.architecture): 28 R2R; DirectWriteForwarder separate; signing=$($overlay.signing.mode)"
