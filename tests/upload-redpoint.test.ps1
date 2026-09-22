# Hardware-free regression tests; no Pester, real COM access or real Arduino CLI.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'tools/upload-redpoint.ps1')
$script:checks = 0
function Assert($Condition, [string] $Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
    $script:checks++
}
function Device([string] $Port, [string] $Id = 'USB\VID_2E8A&PID_000A&MI_00\fixture') {
    [pscustomobject]@{ DeviceID = $Port; PNPDeviceID = $Id }
}
function MockSerial([string[]] $Chunks, [bool] $FailOpen = $false) {
    $serial = [pscustomobject]@{
        BaudRate = 0; DataBits = 0; Parity = 0; StopBits = 0; Handshake = 0
        DtrEnable = $false; RtsEnable = $true; ReadTimeout = 0; WriteTimeout = 0
        Chunks = [Collections.Generic.Queue[string]]::new(); Writes = ''
        Opened = $false; Closed = $false; Disposed = $false; Discarded = $false
        FailOpen = $FailOpen; FailRead = $false; FailClose = $false
    }
    foreach ($chunk in $Chunks) { $serial.Chunks.Enqueue($chunk) }
    $serial | Add-Member ScriptMethod Open {
        if ($this.FailOpen) { throw 'Access denied: port busy' }; $this.Opened = $true
    }
    $serial | Add-Member ScriptMethod DiscardInBuffer { $this.Discarded = $true }
    $serial | Add-Member ScriptMethod Write { param($Text) $this.Writes += $Text }
    $serial | Add-Member ScriptMethod ReadExisting {
        if ($this.FailRead) { throw 'Device unplugged' }
        if ($this.Chunks.Count) { return $this.Chunks.Dequeue() }; return ''
    }
    $serial | Add-Member ScriptMethod Close {
        $this.Closed = $true; if ($this.FailClose) { throw 'Close failed' }
    }
    $serial | Add-Member ScriptMethod Dispose { $this.Disposed = $true }
    return $serial
}

$reply = '@CONFIG {"ok":true,"command":"GET","config":{}}'
Assert (Test-RedPointResponse $reply) 'GET JSON accepted'
foreach ($invalid in @('@DEBUG anything', '@CONFIG {', '@CONFIG null', '@CONFIG {}',
    '@CONFIG {"ok":"true","command":"GET"}', '@CONFIG {"ok":1,"command":"GET"}',
    '@CONFIG {"ok":false,"command":"GET"}', '@CONFIG {"ok":true,"command":"get"}',
    '@CONFIG {"ok":true,"command":"PING"}', '@CONFIG [{"ok":true,"command":"GET"}]')) {
    Assert (-not (Test-RedPointResponse $invalid)) "Reject $invalid"
}
$candidates = @(Get-RedPointCandidates @(
    (Device COM9), (Device COM14 'usb\vid_2e8a&pid_FFFF\test'), (Device COM9),
    (Device COM3 'USB\VID_1234&PID_000A\test'), (Device COM4 'USB\VID_2E8AB&PID_000A\test'),
    (Device LPT1), (Device COM5 'BTHENUM\VID_2E8A\test')))
Assert ($candidates.Count -eq 2) 'VID filtering, PID independence and duplicate removal'

foreach ($fixture in @(
    @{ Chunks = @("@DEBUG noise`r`n@CONFIG {broken}`n", '@CON', 'FIG {"ok":true,', '"command":"GET"}', "`r", "`n") },
    @{ Chunks = @("$reply`n") }, @{ Chunks = @("$reply`r") },
    @{ Chunks = @(('x' * 2050), "`n$reply`r`n") }
)) {
    $mock = MockSerial $fixture.Chunks
    $result = Test-RedPointPort COM9 -TimeoutMs 500 -CreateSerial { param($Name) $mock }
    Assert $result.Matched 'Chunked GET, DEBUG, malformed JSON, CR/LF and overflow recovery'
    Assert ($mock.Writes -ceq "`nGET`n") 'Only GET is sent'
    Assert ($mock.BaudRate -eq 115200 -and $mock.Discarded) '115200 and stale input discarded'
    Assert ($mock.Closed -and $mock.Disposed) 'Success closes and disposes'
}
foreach ($failure in @('timeout', 'open', 'read', 'close')) {
    $mock = MockSerial @() ($failure -eq 'open')
    $mock.FailRead = $failure -eq 'read'
    $mock.FailClose = $failure -eq 'close'
    if ($mock.FailClose) { $mock.Chunks.Enqueue("$reply`n") }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $result = Test-RedPointPort COM14 -TimeoutMs 30 -CreateSerial { param($Name) $mock }
    Assert (-not $result.Matched) "$failure does not identify device"
    Assert ($mock.Closed -and $mock.Disposed) "$failure cleans up"
    Assert ($timer.ElapsedMilliseconds -lt 1000) "$failure is bounded"
}

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('redpoint upload tests ' + [guid]::NewGuid())
[void] (New-Item -ItemType Directory -Path (Join-Path $temporary 'firmware/redpoint') -Force)
Set-Content -LiteralPath (Join-Path $temporary 'firmware/redpoint/redpoint.ino') -Value '// fixture'
$oldPath = $env:PATH
try {
    foreach ($case in @(
        @{ Ports = @((Device COM9)); Match = @('COM9'); Expected = 23 },
        @{ Ports = @((Device COM14)); Match = @('COM14'); Expected = 23 },
        @{ Ports = @((Device COM14)); Match = @(); Expected = 1 },
        @{ Ports = @((Device COM1 'USB\VID_1234&PID_000A\test')); Match = @(); Expected = 1 },
        @{ Ports = @(); Match = @(); Expected = 1 },
        @{ Ports = @((Device COM9), (Device COM14)); Match = @('COM14'); Expected = 23 },
        @{ Ports = @((Device COM9), (Device COM14)); Match = @('COM9', 'COM14'); Expected = 1 }
    )) {
        $script:cliCalls = 0
        $script:probed = @()
        $result = Invoke-RedPointUpload -WorkspacePath $temporary -EnumeratePorts { $case.Ports } -ProbePort {
            param($Name)
            $script:probed += $Name
            [pscustomobject]@{ Matched = $case.Match -contains $Name; Reason = 'fixture' }
        } -RunCli {
            param($Arguments)
            $script:cliCalls++
            Assert (($Arguments -join '|') -ceq (@('compile', '--fqbn', 'rp2040:rp2040:vccgnd_yd_rp2040',
                '--upload', '--port', $case.Match[0], (Join-Path $temporary 'firmware/redpoint')) -join '|')) 'CLI arguments and workspace with spaces'
            return 23
        }
        Assert ($result -eq $case.Expected) 'Selection result and injected exit code'
        Assert ($script:cliCalls -eq [int]($case.Expected -eq 23)) 'Upload only for exactly one identity'
        Assert ($script:probed.Count -eq @(Get-RedPointCandidates $case.Ports).Count) 'Every candidate probed'
    }
    $result = Invoke-RedPointUpload -WorkspacePath $temporary -DetectOnly -EnumeratePorts {
        (Device COM9); (Device COM14)
    } -ProbePort {
        param($Name)
        if ($Name -eq 'COM14') { throw 'Port open failed' }
        [pscustomobject]@{ Matched = $true; Reason = 'fixture' }
    } -RunCli { throw 'DetectOnly must not invoke CLI' }
    Assert ($result -eq 0) 'Probe exception continues to next candidate; DetectOnly never uploads'
    $result = Invoke-RedPointUpload -WorkspacePath $temporary -EnumeratePorts { throw 'CIM failure' } -RunCli { throw 'Must not upload' }
    Assert ($result -eq 1) 'Discovery failure aborts'

    # Exercise actual native invocation and process exit propagation using only
    # a temporary fake CLI, never the installed arduino-cli or any serial device.
    $fakeCli = Join-Path $temporary 'arduino-cli.cmd'
    $wrapper = Join-Path $temporary 'invoke fixture.ps1'
    @'
param($Helper, $Workspace)
. $Helper
exit (Invoke-RedPointUpload -WorkspacePath $Workspace -EnumeratePorts {
    [pscustomobject]@{ DeviceID = 'COM14'; PNPDeviceID = 'USB\VID_2E8A&PID_000A\fixture' }
} -ProbePort { [pscustomobject]@{ Matched = $true; Reason = 'mock GET' } })
'@ | Set-Content -LiteralPath $wrapper
    $env:PATH = "$temporary;$oldPath"
    $expectedSketch = Join-Path $temporary 'firmware/redpoint'
    foreach ($exitCode in @(0, 23)) {
        @"
@echo off
if not "%~1"=="compile" exit /b 97
if not "%~6"=="COM14" exit /b 97
if not "%~7"=="$expectedSketch" exit /b 97
echo fixture-cli
exit /b $exitCode
"@ | Set-Content -LiteralPath $fakeCli -Encoding ASCII
        $testShell = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh.exe' } else { 'powershell.exe' }
        & (Join-Path $PSHOME $testShell) -NoProfile -ExecutionPolicy Bypass -File $wrapper -Helper (Join-Path $root 'tools/upload-redpoint.ps1') -Workspace $temporary | Out-Host
        Assert ($LASTEXITCODE -eq $exitCode) "Native CLI exit $exitCode reaches task process"
    }
    $tasks = Get-Content -Raw (Join-Path $root '.vscode/tasks.json') | ConvertFrom-Json
    $build = $tasks.tasks | Where-Object label -eq 'RedPoint: Build'
    $upload = $tasks.tasks | Where-Object label -eq 'RedPoint: Build & Upload'
    Assert ($build.command -eq 'arduino-cli' -and $build.args -notcontains '--upload') 'Build stays compile-only'
    Assert ($upload.type -eq 'process' -and $upload.args -contains '${workspaceFolder}/tools/upload-redpoint.ps1') 'Upload label and separate path arguments preserved'
    Write-Host "PASS: $script:checks upload-helper assertions (no hardware or upload)"
} finally {
    $env:PATH = $oldPath
    # Remove only this test's unique temporary workspace, in the same shell.
    $resolved = [IO.Path]::GetFullPath($temporary)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved) -like 'redpoint upload tests *') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
