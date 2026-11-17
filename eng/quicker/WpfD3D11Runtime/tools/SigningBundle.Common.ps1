#requires -Version 5.1
# Bundle transport checks do not load PE files or require the source repository.
function Write-WpfJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 50), [Text.UTF8Encoding]::new($false))
}
function Get-WpfBundlePath([string]$Root, [string]$Relative) {
    Assert-Wpf ($Relative -cmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -and
        @($Relative.Split('/') | Where-Object { $_ -in @('.','..') }).Count -eq 0) "Unsafe bundle path: $Relative"
    $full=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    $null=Get-WpfRelativePath $Root $full
    return $full
}
function Test-WpfSigningBundle([string]$Root, [string]$Archive, [string]$ExpectedSha256) {
    Assert-Wpf ($ExpectedSha256 -match '^[a-fA-F0-9]{64}$') 'Expected bundle SHA256 is required (64 hex digits)'
    Assert-Wpf ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -eq $ExpectedSha256) 'Bundle archive SHA256 mismatch'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entries=@($zip.Entries | Where-Object FullName -ceq 'bundle-manifest.json')
        Assert-Wpf ($entries.Count -eq 1) 'Expected exactly one bundle manifest'
        $sha=[Security.Cryptography.SHA256]::Create(); $stream=$entries[0].Open()
        try { $hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','') }
        finally { $stream.Dispose(); $sha.Dispose() }
        Assert-Wpf ($hash -eq (Get-FileHash -LiteralPath "$Root/bundle-manifest.json").Hash) 'Extracted manifest differs from authenticated archive'
        $manifest=Read-WpfJson "$Root/bundle-manifest.json"
        Assert-Wpf ($manifest.schema -eq 1 -and $manifest.kind -ceq 'unsigned-signing-bundle') 'Unsupported signing bundle'
        $expected=@('bundle-manifest.json')+@($manifest.files.path)
        Assert-Set @($zip.Entries.FullName) $expected 'Bundle ZIP inventory'
        Assert-Wpf (@($expected | Sort-Object -Unique).Count -eq $expected.Count) 'Duplicate bundle paths'
    } finally { $zip.Dispose() }
    $actual=@(Get-ChildItem -LiteralPath $Root -Recurse -Force -File | ForEach-Object {
        (Get-WpfRelativePath $Root $_.FullName).Replace('\','/')
    })
    Assert-Set $actual $expected 'Extracted bundle inventory'
    foreach($item in Get-ChildItem -LiteralPath $Root -Force -Recurse) {
        Assert-Wpf (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) 'Reparse points are forbidden in bundle'
    }
    foreach($f in $manifest.files) {
        $path=Get-WpfBundlePath $Root $f.path
        Assert-Wpf ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ceq $f.sha256) "Bundle file SHA256 mismatch: $($f.path)"
    }
    foreach($rid in @('win-x64','win-arm64')) {
        $overlay=Read-WpfJson "$Root/inputs/$rid/overlay-manifest.json"
        $locked=Read-WpfManifest $Root $rid
        Assert-Wpf ($locked.packageVersion -ceq $manifest.packageVersion -and $locked.forkCommit -ceq $manifest.forkCommit -and
            $locked.signingMode -ceq 'dryrun') 'Locked P2 package provenance mismatch'
        Assert-Set @($overlay.files.name) @(Get-WpfNames) "$rid overlay"
        Assert-Wpf ($overlay.build.forkCommit -ceq $manifest.forkCommit -and
            $overlay.build.packageVersion -ceq $manifest.packageVersion) 'Bundle/overlay provenance mismatch'
        $names=@(Get-WpfNames); if($rid -eq 'win-x64') { $names+='D3DCompiler_47_cor3.dll' }
        $payload=@($manifest.files | Where-Object { $_.path -like "inputs/$rid/*.dll" })
        Assert-Set @($payload.path | ForEach-Object { [IO.Path]::GetFileName($_) }) $names "$rid payload"
        Assert-Wpf (@($payload | Where-Object role -ceq 'sign-fork').Count -eq 31) 'Expected 31 fork files per RID'
        foreach($file in $payload) {
            $name=[IO.Path]::GetFileName($file.path)
            $role=if(Test-WpfForkFile $name){'sign-fork'}else{'preserve-upstream'}
            Assert-Wpf ($file.role -ceq $role) "Invalid signing classification: $name"
            $accepted=@($locked.files | Where-Object name -ceq $name)[0]
            Assert-Wpf ($accepted.sha256 -ceq $file.sha256) "Payload differs from locked P2 package: $name"
            if($name -ne 'D3DCompiler_47_cor3.dll') {
                $entry=@($overlay.files | Where-Object name -ceq $name)[0]
                Assert-Wpf ($entry.sha256 -ceq $file.sha256) "Overlay/bundle hash mismatch: $name"
            }
        }
    }
    return $manifest
}
