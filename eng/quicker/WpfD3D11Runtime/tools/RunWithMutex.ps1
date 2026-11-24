param(
    [Parameter(Mandatory = $true)]
    [string]$MutexName,

    [Parameter(Mandatory = $true)]
    [string]$CommandPath,

    [string]$WorkingDirectory = (Get-Location).Path,

    [int]$TimeoutSeconds = 180,

    [int]$RetryCount = 3,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CommandArguments = @()
)

$mutex = $null
$hasLock = $false
$exitCode = 1

try {
    # 命名 Mutex 用于串行化多个 VS/MSBuild 进程中的同一类构建动作。
    $mutex = [System.Threading.Mutex]::new($false, $MutexName)

    try {
        $hasLock = $mutex.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds))
    }
    catch [System.Threading.AbandonedMutexException] {
        # 上一个持锁进程异常退出时，当前进程已经获得锁。
        $hasLock = $true
    }

    if (-not $hasLock) {
        Write-Error "等待构建互斥锁超时：$MutexName"
        exit 124
    }

    Push-Location $WorkingDirectory
    try {
        for ($attempt = 0; $attempt -le $RetryCount; $attempt++) {
            # 允许调用方传入 signtool 等外部命令参数，同时保持原有无参数调用兼容。
            & $CommandPath @CommandArguments
            $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }

            if ($exitCode -eq 0) {
                break
            }

            if ($attempt -lt $RetryCount) {
                Start-Sleep -Milliseconds 800
            }
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    if ($hasLock -and $null -ne $mutex) {
        $mutex.ReleaseMutex() | Out-Null
    }

    if ($null -ne $mutex) {
        $mutex.Dispose()
    }
}

exit $exitCode
