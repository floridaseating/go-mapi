param(
    [Parameter(Mandatory = $true)] [string]$X64Dll,
    [Parameter(Mandatory = $true)] [string]$X64Probe,
    [Parameter(Mandatory = $true)] [string]$X86Dll,
    [Parameter(Mandatory = $true)] [string]$X86Probe
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$clientsPath = 'SOFTWARE\Clients\Mail'
$mapiAppsPath = 'SOFTWARE\Microsoft\Windows Messaging Subsystem\MSMapiApps'
$registryViews = @(
    [Microsoft.Win32.RegistryView]::Registry64,
    [Microsoft.Win32.RegistryView]::Registry32
)

function Open-Hklm {
    param([Microsoft.Win32.RegistryView]$View)
    return [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine,
        $View
    )
}

function Get-ValueSnapshot {
    param(
        [Microsoft.Win32.RegistryView]$View,
        [string]$Path,
        [string]$Name
    )

    $base = Open-Hklm -View $View
    $key = $null
    try {
        $key = $base.OpenSubKey($Path)
        $keyExisted = $null -ne $key
        $valueExisted = $false
        $value = $null
        $kind = $null
        if ($keyExisted) {
            $valueExisted = $key.GetValueNames() -contains $Name
            if ($valueExisted) {
                $value = $key.GetValue(
                    $Name,
                    $null,
                    [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames
                )
                $kind = $key.GetValueKind($Name)
            }
        }
        return [pscustomobject]@{
            View = $View
            Path = $Path
            Name = $Name
            KeyExisted = $keyExisted
            ValueExisted = $valueExisted
            Value = $value
            Kind = $kind
        }
    } finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
}

function Get-KeySnapshot {
    param(
        [Microsoft.Win32.RegistryView]$View,
        [string]$Path
    )

    return [pscustomobject]@{
        View = $View
        Path = $Path
        Existed = Test-RegistryKeyExists -View $View -Path $Path
    }
}

function Assert-StringSnapshotSupported {
    param($Snapshot)

    if (-not $Snapshot.ValueExisted) { return }
    if ($Snapshot.Kind -ne [Microsoft.Win32.RegistryValueKind]::String -and
        $Snapshot.Kind -ne [Microsoft.Win32.RegistryValueKind]::ExpandString) {
        throw "Refusing to replace non-string registry value: $($Snapshot.Path)::$($Snapshot.Name) [$($Snapshot.View)] kind=$($Snapshot.Kind)"
    }
}

function Remove-KeyIfEmpty {
    param(
        [Microsoft.Win32.RegistryView]$View,
        [string]$Path
    )

    $separator = $Path.LastIndexOf('\')
    if ($separator -lt 1) { return }
    $parentPath = $Path.Substring(0, $separator)
    $leafName = $Path.Substring($separator + 1)
    $base = Open-Hklm -View $View
    $parent = $null
    $key = $null
    try {
        $parent = $base.OpenSubKey($parentPath, $true)
        if ($null -eq $parent) { return }
        $key = $parent.OpenSubKey($leafName)
        if ($null -eq $key) { return }
        $isEmpty = $key.GetSubKeyNames().Count -eq 0 -and
            $key.GetValueNames().Count -eq 0
        $key.Dispose()
        $key = $null
        if ($isEmpty) {
            $parent.DeleteSubKey($leafName, $false)
        }
    } finally {
        if ($null -ne $key) { $key.Dispose() }
        if ($null -ne $parent) { $parent.Dispose() }
        $base.Dispose()
    }
}

function Set-StringValue {
    param(
        [Microsoft.Win32.RegistryView]$View,
        [string]$Path,
        [string]$Name,
        [string]$Value
    )

    $base = Open-Hklm -View $View
    $key = $null
    try {
        $key = $base.CreateSubKey($Path)
        $key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String)
    } finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
}

function Restore-ValueSnapshot {
    param($Snapshot)

    $base = Open-Hklm -View $Snapshot.View
    $key = $null
    try {
        if ($Snapshot.ValueExisted) {
            $key = $base.CreateSubKey($Snapshot.Path)
            $key.SetValue($Snapshot.Name, $Snapshot.Value, $Snapshot.Kind)
        } else {
            $key = $base.OpenSubKey($Snapshot.Path, $true)
            if ($null -ne $key) {
                $key.DeleteValue($Snapshot.Name, $false)
            }
        }
    } finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
    if (-not $Snapshot.KeyExisted) {
        Remove-KeyIfEmpty -View $Snapshot.View -Path $Snapshot.Path
    }
}

function Assert-SnapshotRestored {
    param(
        $Expected,
        [switch]$IgnoreKeyExistence
    )

    $actual = Get-ValueSnapshot `
        -View $Expected.View `
        -Path $Expected.Path `
        -Name $Expected.Name
    if ((-not $IgnoreKeyExistence -and
            $actual.KeyExisted -ne $Expected.KeyExisted) -or
        $actual.ValueExisted -ne $Expected.ValueExisted) {
        throw "Registry existence was not restored: $($Expected.Path)::$($Expected.Name) [$($Expected.View)]"
    }
    if ($Expected.ValueExisted) {
        if ($actual.Kind -ne $Expected.Kind -or
            -not [StringComparer]::Ordinal.Equals(
                [string]$actual.Value,
                [string]$Expected.Value
            )) {
            throw "Registry value was not restored exactly: $($Expected.Path)::$($Expected.Name) [$($Expected.View)]"
        }
    }
}

function Assert-KeySnapshotRestored {
    param($Expected)

    $actualExists = Test-RegistryKeyExists `
        -View $Expected.View `
        -Path $Expected.Path
    if ($actualExists -ne $Expected.Existed) {
        throw "Registry key existence was not restored: $($Expected.Path) [$($Expected.View)]"
    }
}

function New-TestMailClient {
    param(
        $Ownership,
        [string]$DllPath
    )

    $clientName = $Ownership.Name
    $base = Open-Hklm -View ([Microsoft.Win32.RegistryView]::Registry64)
    $mailKey = $null
    $existing = $null
    $clientKey = $null
    try {
        $mailKey = $base.CreateSubKey($clientsPath)
        $existing = $mailKey.OpenSubKey($clientName)
        if ($null -ne $existing) {
            $Ownership.Preexisting = $true
            throw "Refusing to replace existing mail client key: $clientName"
        }
        $clientKey = $mailKey.CreateSubKey($clientName)
        $Ownership.Created = $true
        $clientKey.SetValue(
            'GoMapiCiOwner',
            $Ownership.Token,
            [Microsoft.Win32.RegistryValueKind]::String
        )
        $clientKey.SetValue('', $clientName, [Microsoft.Win32.RegistryValueKind]::String)
        $clientKey.SetValue('DLLPath', $DllPath, [Microsoft.Win32.RegistryValueKind]::String)
    } catch {
        $initialError = $_
        if ($Ownership.Created) {
            if ($null -ne $clientKey) {
                $clientKey.Dispose()
                $clientKey = $null
            }
            try {
                Remove-TestMailClient -Ownership $Ownership
            } catch {
                throw "Mail client initialization failed ($($initialError.Exception.Message)); immediate cleanup also failed ($($_.Exception.Message))"
            }
        }
        throw $initialError
    } finally {
        if ($null -ne $clientKey) { $clientKey.Dispose() }
        if ($null -ne $existing) { $existing.Dispose() }
        if ($null -ne $mailKey) { $mailKey.Dispose() }
        $base.Dispose()
    }
}

function Remove-TestMailClient {
    param($Ownership)

    if (-not $Ownership.Created) { return }

    $base = Open-Hklm -View ([Microsoft.Win32.RegistryView]::Registry64)
    $key = $null
    try {
        $path = "$clientsPath\$($Ownership.Name)"
        $key = $base.OpenSubKey($path)
        if ($null -eq $key) {
            $Ownership.Created = $false
            return
        }
        $valueNames = $key.GetValueNames()
        $ownerMatches = $valueNames -contains 'GoMapiCiOwner' -and
            [StringComparer]::Ordinal.Equals(
                [string]$key.GetValue('GoMapiCiOwner'),
                [string]$Ownership.Token
            )
        $isEmpty = $valueNames.Count -eq 0 -and
            $key.GetSubKeyNames().Count -eq 0
        $key.Dispose()
        $key = $null
        if (-not $ownerMatches -and -not $isEmpty) {
            throw "Refusing to remove mail client without matching ownership marker: $($Ownership.Name)"
        }
        $base.DeleteSubKeyTree($path, $false)
        $Ownership.Created = $false
    } finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
}

function Test-RegistryKeyExists {
    param(
        [Microsoft.Win32.RegistryView]$View,
        [string]$Path
    )

    $base = Open-Hklm -View $View
    $key = $null
    try {
        $key = $base.OpenSubKey($Path)
        return $null -ne $key
    } finally {
        if ($null -ne $key) { $key.Dispose() }
        $base.Dispose()
    }
}

function Invoke-Probe {
    param(
        [string]$ProbePath,
        [string]$ProviderPath
    )

    Write-Host "Running native MAPI32 probe: $ProbePath"
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ProbePath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.ArgumentList.Add($ProviderPath)

    $process = [Diagnostics.Process]::Start($startInfo)
    if ($null -eq $process) {
        throw "Failed to start native MAPI32 probe: $ProbePath"
    }
    $processExited = $false
    try {
        $processExited = $process.WaitForExit(60000)
        if (-not $processExited) {
            $terminationError = $null
            try {
                $process.Kill($true)
            } catch {
                try {
                    $process.Kill()
                } catch {
                    $terminationError = $_.Exception.Message
                }
            }
            try {
                $processExited = $process.WaitForExit(5000)
            } catch {
                $terminationError = $_.Exception.Message
            }
            if (-not $processExited) {
                $detail = if ($null -ne $terminationError) {
                    "; termination error: $terminationError"
                } else {
                    '; process did not exit within the 5-second termination window'
                }
                throw "Native MAPI32 probe timed out after 60 seconds and could not be confirmed terminated${detail}: $ProbePath"
            }
            throw "Native MAPI32 probe timed out after 60 seconds: $ProbePath"
        }
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        if (-not [string]::IsNullOrWhiteSpace($stdout)) {
            Write-Host ($stdout.TrimEnd())
        }
        if (-not [string]::IsNullOrWhiteSpace($stderr)) {
            Write-Host ($stderr.TrimEnd())
        }
        $exitCode = $process.ExitCode
        if ($exitCode -ne 0) {
            throw "Native MAPI32 probe failed with exit code ${exitCode}: $ProbePath"
        }
    } finally {
        if (-not $processExited) {
            try {
                $process.Kill($true)
            } catch {
                try { $process.Kill() } catch { }
            }
            try { [void]$process.WaitForExit(5000) } catch { }
        }
        $process.Dispose()
    }
}

$X64Dll = (Resolve-Path -LiteralPath $X64Dll).Path
$X64Probe = (Resolve-Path -LiteralPath $X64Probe).Path
$X86Dll = (Resolve-Path -LiteralPath $X86Dll).Path
$X86Probe = (Resolve-Path -LiteralPath $X86Probe).Path

$x64Client = "go-mapi-ci-x64-$([Guid]::NewGuid().ToString('N'))"
$x86Client = "go-mapi-ci-x86-$([Guid]::NewGuid().ToString('N'))"
$x64ProbeName = [IO.Path]::GetFileName($X64Probe)
$x86ProbeName = [IO.Path]::GetFileName($X86Probe)
if ($x64ProbeName -eq $x86ProbeName) {
    throw 'x64 and x86 probes must have distinct executable names'
}

$keySnapshots = [Collections.Generic.List[object]]::new()
foreach ($keySpec in @(
    @{ View = [Microsoft.Win32.RegistryView]::Registry64; Path = 'SOFTWARE\Clients' },
    @{ View = [Microsoft.Win32.RegistryView]::Registry64; Path = $clientsPath }
)) {
    $keySnapshots.Add((Get-KeySnapshot -View $keySpec.View -Path $keySpec.Path))
}
foreach ($view in $registryViews) {
    foreach ($path in @(
        'SOFTWARE\Microsoft',
        'SOFTWARE\Microsoft\Windows Messaging Subsystem',
        $mapiAppsPath
    )) {
        $keySnapshots.Add((Get-KeySnapshot -View $view -Path $path))
    }
}

$mailDefault = Get-ValueSnapshot `
    -View ([Microsoft.Win32.RegistryView]::Registry64) `
    -Path $clientsPath `
    -Name ''
Assert-StringSnapshotSupported -Snapshot $mailDefault
$mappingSnapshots = [Collections.Generic.List[object]]::new()
$createdClients = [Collections.Generic.List[object]]::new()
$createdClients.Add([pscustomobject]@{
    Name = $x64Client
    Token = [Guid]::NewGuid().ToString('N')
    Created = $false
    Preexisting = $false
})
$createdClients.Add([pscustomobject]@{
    Name = $x86Client
    Token = [Guid]::NewGuid().ToString('N')
    Created = $false
    Preexisting = $false
})
$primaryError = $null

try {
    New-TestMailClient -Ownership $createdClients[0] -DllPath $X64Dll
    New-TestMailClient -Ownership $createdClients[1] -DllPath $X86Dll

    foreach ($view in $registryViews) {
        foreach ($mapping in @(
            @{ Name = $x64ProbeName; Client = $x64Client },
            @{ Name = $x86ProbeName; Client = $x86Client }
        )) {
            $snapshot = Get-ValueSnapshot `
                -View $view `
                -Path $mapiAppsPath `
                -Name $mapping.Name
            Assert-StringSnapshotSupported -Snapshot $snapshot
            $mappingSnapshots.Add($snapshot)
            Set-StringValue `
                -View $view `
                -Path $mapiAppsPath `
                -Name $mapping.Name `
                -Value $mapping.Client
        }
    }

    Assert-SnapshotRestored `
        -Expected $mailDefault `
        -IgnoreKeyExistence
    Invoke-Probe -ProbePath $X64Probe -ProviderPath $X64Dll
    Invoke-Probe -ProbePath $X86Probe -ProviderPath $X86Dll
} catch {
    $primaryError = $_
} finally {
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    for ($index = $mappingSnapshots.Count - 1; $index -ge 0; $index--) {
        try {
            Restore-ValueSnapshot -Snapshot $mappingSnapshots[$index]
        } catch {
            $cleanupErrors.Add($_.Exception.Message)
        }
    }
    for ($index = $createdClients.Count - 1; $index -ge 0; $index--) {
        try {
            Remove-TestMailClient -Ownership $createdClients[$index]
        } catch {
            $cleanupErrors.Add($_.Exception.Message)
        }
    }
    for ($index = $keySnapshots.Count - 1; $index -ge 0; $index--) {
        if (-not $keySnapshots[$index].Existed) {
            try {
                Remove-KeyIfEmpty `
                    -View $keySnapshots[$index].View `
                    -Path $keySnapshots[$index].Path
            } catch {
                $cleanupErrors.Add($_.Exception.Message)
            }
        }
    }

    try {
        Assert-SnapshotRestored -Expected $mailDefault
        foreach ($snapshot in $mappingSnapshots) {
            Assert-SnapshotRestored -Expected $snapshot
        }
        foreach ($snapshot in $keySnapshots) {
            Assert-KeySnapshotRestored -Expected $snapshot
        }
        foreach ($ownership in $createdClients) {
            if (-not $ownership.Preexisting) {
                foreach ($view in $registryViews) {
                    if (Test-RegistryKeyExists -View $view -Path "$clientsPath\$($ownership.Name)") {
                        throw "Temporary mail client key remains: $($ownership.Name) [$view]"
                    }
                }
            }
        }
    } catch {
        $cleanupErrors.Add($_.Exception.Message)
    }

    if ($cleanupErrors.Count -gt 0) {
        $message = "Native MAPI32 cleanup failed: " + ($cleanupErrors -join '; ')
        if ($null -ne $primaryError) {
            $message += "; original failure: " + $primaryError.Exception.Message
        }
        throw $message
    }
}

if ($null -ne $primaryError) {
    throw $primaryError
}

Write-Host 'Native x64/x86 MAPI32 dispatch and buffer-ownership tests passed.'
