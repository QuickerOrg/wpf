#requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Request,[Parameter(Mandatory)][string]$Receipt)
$ErrorActionPreference='Stop'
. ([scriptblock]::Create([IO.File]::ReadAllText("$PSScriptRoot/Payload.Common.ps1")))
$requestData=Read-WpfJson $Request
$result=@{passed=$false;files=@();gates=@()}
try {
    foreach($path in $requestData.paths) {
        $facts=Get-PeFacts $path
        $sig=Get-AuthenticodeSignature -LiteralPath $path
        $facts.signatureStatus=$sig.Status.ToString()
        $facts.signerThumbprint=if($sig.SignerCertificate){$sig.SignerCertificate.Thumbprint}else{$null}
        $facts.timestampThumbprint=if($sig.TimeStamperCertificate){$sig.TimeStamperCertificate.Thumbprint}else{$null}
        $result.files+=@{path=$path;facts=$facts}
    }
    if($requestData.root) {
        foreach($rid in @('win-x64','win-arm64')) {
            # Keep the consumer's original PE/asset/R2R/identity/RequireSigned gate.
            $manifest=Test-WpfInput $requestData.root $rid ([bool]$requestData.requireSigned)
            foreach($file in $manifest.files) {
                $path=Join-Path $requestData.root $file.pathInPackage
                $facts=Get-PeFacts $path
                Assert-Wpf ($facts.readyToRun -eq $file.readyToRun) "R2R record mismatch: $path"
                foreach($key in @('informationalVersion','ilOnly')) {
                    Assert-Wpf ($facts[$key] -ceq $file[$key]) "PE record mismatch ($key): $path"
                }
                Assert-Wpf ($file.signing.sha256 -ceq $facts.sha256) "Signing hash mismatch: $path"
                $signature=Get-AuthenticodeSignature -LiteralPath $path
                $rebuilt=$file.production.action -eq 'rebuilt'
                if($requestData.requireSigned -or -not $rebuilt) {
                    Assert-Wpf ($signature.Status -eq 'Valid' -and $signature.TimeStamperCertificate) "Invalid signature/timestamp: $path"
                }
                if($requestData.requireSigned -and $rebuilt -and (Test-WpfForkFile $file.name)) {
                    Assert-Wpf ($requestData.signerThumbprint -and $requestData.timestampThumbprint) 'Explicit publisher and timestamp policy required'
                    Assert-Wpf ($signature.SignerCertificate.Thumbprint -eq $requestData.signerThumbprint -and
                        $signature.TimeStamperCertificate.Thumbprint -eq $requestData.timestampThumbprint) "Publisher/timestamp policy mismatch: $path"
                }
            }
            $result.gates+=@{rid=$rid;inputPassed=$true;requireSigned=[bool]$requestData.requireSigned}
        }
    }
    $result.passed=$true
} catch { $result.error=$_.Exception.Message; throw }
finally { [IO.File]::WriteAllText($Receipt,($result | ConvertTo-Json -Depth 50),[Text.UTF8Encoding]::new($false)) }
