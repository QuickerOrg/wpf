#requires -Version 5.1
# Schema 2 transport: exact inventory and hashes; sign only the declared rebuilt files.
function Test-WpfSigningBundle([string]$Root,[string]$Archive,[string]$ExpectedSha256) {
    Assert-Wpf ($ExpectedSha256 -match '^[a-fA-F0-9]{64}$') 'Explicit bundle SHA256 required'
    Assert-Wpf ((Get-FileHash -LiteralPath $Archive).Hash -eq $ExpectedSha256) 'Bundle SHA256 mismatch'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entry=@($zip.Entries | Where-Object FullName -ceq 'bundle-manifest.json')
        Assert-Wpf ($entry.Count -eq 1) 'Exactly one bundle manifest required'
        $reader=[IO.StreamReader]::new($entry[0].Open())
        try { $original=$reader.ReadToEnd() } finally { $reader.Dispose() }
        Assert-Wpf ($original -ceq [IO.File]::ReadAllText("$Root/bundle-manifest.json")) 'Extracted bundle manifest changed'
        $m=Read-WpfJson "$Root/bundle-manifest.json"
        Assert-Wpf ($m.schema -eq 2 -and $m.kind -ceq 'unsigned-signing-bundle') 'Expected schema 2 signing bundle'
        $expected=@('bundle-manifest.json')+@($m.files.Keys)
        Assert-Set @($zip.Entries.FullName) $expected 'Bundle ZIP inventory'
        Assert-Wpf (@($expected | Sort-Object -Unique).Count -eq $expected.Count) 'Duplicate bundle paths'
        $actual=@(Get-ChildItem -LiteralPath $Root -Force -Recurse -File | ForEach-Object {
            (Get-WpfRelativePath $Root $_.FullName).Replace('\','/')
        })
        Assert-Set $actual $expected 'Extracted bundle inventory'
        foreach($item in Get-ChildItem -LiteralPath $Root -Force -Recurse) {
            Assert-Wpf (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) 'Bundle reparse point forbidden'
        }
        foreach($relative in $m.files.Keys) {
            Assert-Wpf ($relative -cmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -and
                @($relative.Split('/') | Where-Object { $_ -in @('.','..') }).Count -eq 0) "Unsafe bundle path: $relative"
            $path=Join-Path $Root $relative
            $null=Get-WpfRelativePath $Root $path
            Assert-Wpf ((Get-FileHash -LiteralPath $path).Hash -ceq $m.files[$relative]) "Bundle hash mismatch: $relative"
        }
        $runtime=@($m.files.Keys | Where-Object { $_ -like 'runtimes/*' })
        Assert-Set (@($m.signFiles)+@($m.preserveFiles)) $runtime 'Signing roles'
        Assert-Wpf (@($m.signFiles).Count -gt 0) 'Empty signing selection'
        foreach($p in $m.signFiles) {
            Assert-Wpf (Test-WpfForkFile ([IO.Path]::GetFileName($p))) 'Signing upstream originals forbidden'
        }
        $declared=@()
        foreach($rid in @('win-x64','win-arm64')) {
            $inputManifest=Test-WpfInput $Root $rid $false
            $declared+=@($inputManifest.files | Where-Object { $_.production.action -eq 'rebuilt' } | ForEach-Object pathInPackage)
        }
        Assert-Set @($m.signFiles) $declared 'Bundle signing roles vs locked manifests'
        return $m
    } finally { $zip.Dispose() }
}
