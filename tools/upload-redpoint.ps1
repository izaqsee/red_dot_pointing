# Windows PowerShell 5.1 and PowerShell 7; no external modules required.
[CmdletBinding()]
param(
    [string] $WorkspacePath = (Split-Path -Parent $PSScriptRoot),
    [switch] $DetectOnly
)

function Get-RedPointCandidates {
    param([AllowEmptyCollection()][object[]] $Devices)
    @($Devices | Where-Object {
        $_.DeviceID -match '^COM[0-9]+$' -and
        $_.PNPDeviceID -match '^USB\\VID_2E8A(?:&|\\)'
    } | Sort-Object DeviceID -Unique)
}

function Test-RedPointResponse {
    param([string] $Line)
    if (-not $Line.StartsWith('@CONFIG ')) { return $false }
    if ($Line.Substring(8) -notmatch '^\s*\{') { return $false }
    try {
        $response = $Line.Substring(8) | ConvertFrom-Json -ErrorAction Stop
        return ($null -ne $response -and
            $response.ok -is [bool] -and $response.ok -eq $true -and
            $response.command -is [string] -and $response.command -ceq 'GET')
    } catch { return $false }
}

function Test-RedPointPort {
    param(
        [Parameter(Mandatory)][string] $Port,
        [ValidateRange(1, 1500)][int] $TimeoutMs = 1200,
        [scriptblock] $CreateSerial = { param($Name) New-Object System.IO.Ports.SerialPort $Name, 115200 }
    )
    $serial = $null
    $matched = $false
    $reason = 'GET response timed out'
    try {
        $serial = & $CreateSerial $Port
        $serial.BaudRate = 115200
        $serial.DataBits = 8
        $serial.Parity = [System.IO.Ports.Parity]::None
        $serial.StopBits = [System.IO.Ports.StopBits]::One
        $serial.Handshake = [System.IO.Ports.Handshake]::None
        $serial.DtrEnable = $true
        $serial.RtsEnable = $false
        $serial.ReadTimeout = $TimeoutMs
        $serial.WriteTimeout = [Math]::Min(200, $TimeoutMs)
        $serial.Open()
        $serial.DiscardInBuffer()
        # Terminate a partial line from an earlier session; never send mutations.
        $serial.Write("`nGET`n")
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $line = New-Object System.Text.StringBuilder
        $overflow = $false
        :receive while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
            # ReadExisting is nonblocking. Preserve incomplete lines across reads.
            $chunk = $serial.ReadExisting()
            foreach ($character in $chunk.ToCharArray()) {
                if ($watch.ElapsedMilliseconds -ge $TimeoutMs) { break receive }
                if ($character -eq "`r" -or $character -eq "`n") {
                    if (-not $overflow -and (Test-RedPointResponse $line.ToString())) {
                        $matched = $true
                        $reason = 'GET identity confirmed'
                        break receive
                    }
                    [void] $line.Clear()
                    $overflow = $false
                } elseif (-not $overflow) {
                    if ($line.Length -ge 2048) {
                        [void] $line.Clear()
                        $overflow = $true
                    } else { [void] $line.Append($character) }
                }
            }
            if (-not $chunk) { Start-Sleep -Milliseconds 10 }
        }
    } catch {
        $reason = "Probe failed: $($_.Exception.Message)"
    } finally {
        if ($null -ne $serial) {
            try { $serial.Close() } catch {
                $matched = $false
                $reason = "Port close failed: $($_.Exception.Message)"
            }
            try { $serial.Dispose() } catch {
                $matched = $false
                $reason = "Port disposal failed: $($_.Exception.Message)"
            }
        }
    }
    [pscustomobject]@{ Port = $Port; Matched = $matched; Reason = $reason }
}

function Invoke-RedPointUpload {
    param(
        [string] $WorkspacePath,
        [switch] $DetectOnly,
        [scriptblock] $EnumeratePorts = { Get-CimInstance -ClassName Win32_SerialPort -ErrorAction Stop },
        [scriptblock] $ProbePort = { param($Name) Test-RedPointPort -Port $Name },
        [scriptblock] $RunCli = {
            param([string[]] $Arguments)
            $cli = @(Get-Command arduino-cli -CommandType Application -ErrorAction Stop)[0]
            # Stream native output to the terminal, leaving only the exit code on
            # this function's success pipeline (also on Windows PowerShell 5.1).
            $ErrorActionPreference = 'Continue'
            & $cli.Source @Arguments | Out-Host
            if ($null -eq $LASTEXITCODE) { return 1 }
            return $LASTEXITCODE
        }
    )
    try {
        $sketch = Join-Path $WorkspacePath 'firmware/redpoint'
        if (-not (Test-Path -LiteralPath (Join-Path $sketch 'redpoint.ino') -PathType Leaf)) {
            throw "Sketch not found: $sketch"
        }
        $candidates = @(Get-RedPointCandidates -Devices @(& $EnumeratePorts))
        $names = @($candidates | ForEach-Object { $_.DeviceID })
        Write-Host ('RP2040 candidate ports: ' + $(if ($names.Count) { $names -join ', ' } else { '(none)' }))
        $found = @()
        foreach ($candidate in $candidates) {
            try {
                $result = & $ProbePort $candidate.DeviceID
                Write-Host ("{0}: {1}" -f $candidate.DeviceID, $result.Reason)
                if ($result.Matched -eq $true) { $found += $candidate.DeviceID }
            } catch {
                Write-Host ("{0}: Probe failed: {1}" -f $candidate.DeviceID, $_.Exception.Message)
            }
        }
        if ($found.Count -eq 0) {
            Write-Host 'RedPoint not found. Connect the device and try again.'
            Write-Host 'Close Configurator / Serial Monitor and wait for firmware startup before retrying.'
            return 1
        }
        if ($found.Count -gt 1) {
            Write-Host ("Multiple RedPoint devices found: {0}" -f ($found -join ', '))
            Write-Host 'Automatic upload is ambiguous. Disconnect the other RedPoint devices and retry.'
            return 1
        }
        Write-Host "RedPoint detected on $($found[0])"
        if ($DetectOnly) { return 0 }
        $cliArguments = @('compile', '--fqbn', 'rp2040:rp2040:vccgnd_yd_rp2040',
            '--upload', '--port', $found[0], $sketch)
        return (& $RunCli $cliArguments)
    } catch {
        Write-Host "RedPoint upload aborted: $($_.Exception.Message)"
        return 1
    }
}

# Dot-sourcing exposes functions for hardware-free tests without running upload.
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    exit (Invoke-RedPointUpload -WorkspacePath $WorkspacePath -DetectOnly:$DetectOnly)
}
