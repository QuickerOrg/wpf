#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Directory,
    [Parameter(Mandatory)][string]$QuickerRepository,
    [string]$SignTool,
    [string]$CertificateThumbprint,
    [string]$TimestampUrl,
    [switch]$Execute
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/tools/Payload.Common.ps1"
$records=@(); $owned=@()
foreach($name in Get-WpfNames) {
    $path=(Get-Item -LiteralPath "$Directory/$name").FullName
    $upstream=$name -in @('PresentationNative_cor3.dll','vcruntime140_cor3.dll','D3DCompiler_47_cor3.dll')
    if(-not $upstream) { $owned+=$path }
}
if($Execute) {
    Assert-Wpf ($SignTool -and $CertificateThumbprint -and $TimestampUrl) 'Explicit tool, certificate thumbprint and timestamp URL required.'
    # Reuse Bridge publishing's mutex, signtool arguments and post-sign checks.
    # No certificate creation/import, no implicit certificate selection.
    $arguments=@('sign','/sha1',$CertificateThumbprint,'/tr',$TimestampUrl,'/td','sha256','/fd','sha256')+$owned
    & "$QuickerRepository/Tools/Build/RunWithMutex.ps1" -MutexName 'Local\Quicker.CodeSigning' `
        -CommandPath $SignTool -WorkingDirectory $Directory -TimeoutSeconds 300 -RetryCount 0 -CommandArguments $arguments
    if($LASTEXITCODE) { throw 'WPF signing failed' }
}
foreach($name in @(Get-WpfNames)+@('D3DCompiler_47_cor3.dll')) {
    $path=Join-Path $Directory $name
    if(-not (Test-Path -LiteralPath $path)) { continue }
    $sig=Get-AuthenticodeSignature -LiteralPath $path
    $upstream=$name -in @('PresentationNative_cor3.dll','vcruntime140_cor3.dll','D3DCompiler_47_cor3.dll')
    $thumb=if($sig.SignerCertificate){$sig.SignerCertificate.Thumbprint}else{$null}
    if($Execute -and -not $upstream) {
        Assert-Wpf ($sig.Status -eq 'Valid' -and $thumb -eq $CertificateThumbprint -and $sig.TimeStamperCertificate) "Signature/timestamp verification failed: $path"
    }
    $records+=@{name=$name;path=[IO.Path]::GetFullPath($path);origin=if($upstream){'upstream'}else{'fork'};
        action=if($upstream){'preserve'}elseif($Execute){'signed'}else{'unsigned/dryrun'};
        authenticodeStatus=$sig.Status.ToString();signerThumbprint=$thumb;
        timestampThumbprint=if($sig.TimeStamperCertificate){$sig.TimeStamperCertificate.Thumbprint}else{$null};
        sha256=(Get-FileHash -LiteralPath $path).Hash}
}
$report=@{mode=if($Execute){'execute'}else{'dryrun'};tool=$SignTool;certificateThumbprint=$CertificateThumbprint;files=$records}
[IO.File]::WriteAllText("$Directory/signing.json",($report | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
$owned | ForEach-Object { Write-Output "$(if($Execute){'SIGNED'}else{'WOULD SIGN'}): $_" }
