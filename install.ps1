[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('install', 'update', 'configure', 'status', 'uninstall')]
    [string]$Action = 'install',

    [Parameter(Position = 1)]
    [string]$Version = 'latest'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoSlug = if ($env:BTOP_REPO) { $env:BTOP_REPO } else { 'JeffreyHu17/btop--' }
$ServiceName = 'btop-agent'
$AssetPathOverride = $env:BTOP_AGENT_ASSET_PATH
$ChecksumsPathOverride = $env:BTOP_AGENT_CHECKSUMS_PATH
$TaskBaseName = 'btop-agent'

$script:ResolvedVersion = ''
$script:ReleaseMetadata = $null
$script:DownloadedAssetName = ''
$script:IsAdminInstall = $false
$script:InstallRoot = ''
$script:ConfigRoot = ''
$script:BinaryPath = ''
$script:BtopAliasPath = ''
$script:ConfigPath = ''
$script:StatePath = ''
$script:PathScope = 'User'
$script:WindowsArch = ''
$script:TaskName = ''
$script:CurrentUserId = ''
$script:CurrentUserSid = ''

$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("btop-agent-install-" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TempRoot | Out-Null

function Write-Log {
    param([string]$Message)
    Write-Host $Message
}

function Fail {
    param([string]$Message)
    throw $Message
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
}

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        Fail "required command not found: $Name"
    }
}

function Get-NormalizedArch {
    switch ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture) {
        'X64' { return 'x86_64' }
        'X86' { return 'x86' }
        'Arm64' { return 'arm64' }
        default { Fail "unsupported Windows architecture: $([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)" }
    }
}

function Initialize-Paths {
    $script:IsAdminInstall = Test-IsAdministrator
    $script:WindowsArch = Get-NormalizedArch
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $script:CurrentUserId = $identity.Name
    $script:CurrentUserSid = $identity.User.Value

    if ($script:IsAdminInstall) {
        $script:InstallRoot = Join-Path $env:ProgramFiles 'btop-agent'
        $script:ConfigRoot = Join-Path $env:ProgramData 'btop-agent'
        $script:PathScope = 'Machine'
        $script:TaskName = "$TaskBaseName-system"
    }
    else {
        $script:InstallRoot = Join-Path $env:LOCALAPPDATA 'btop-agent'
        $script:ConfigRoot = Join-Path $env:APPDATA 'btop-agent'
        $script:PathScope = 'User'
        $script:TaskName = "$TaskBaseName-user-$($script:CurrentUserSid -replace '-', '_')"
    }

    $script:BinaryPath = Join-Path $script:InstallRoot 'btop-agent.exe'
    $script:BtopAliasPath = Join-Path $script:InstallRoot 'btop.exe'
    $script:ConfigPath = Join-Path $script:ConfigRoot 'distributed-client.json'
    $script:StatePath = Join-Path $script:ConfigRoot 'install-state.json'
}

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Set-PrivateAcl {
    param(
        [string]$Path,
        [switch]$Directory
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $permission = if ($Directory) { '(OI)(CI)F' } else { 'F' }
    & icacls $Path /inheritance:r /grant:r "$($script:CurrentUserId):$permission" "*S-1-5-18:$permission" "*S-1-5-32-544:$permission" | Out-Null
}

function Prompt-Default {
    param([string]$Prompt, [string]$Default)
    $value = Read-Host "$Prompt [$Default]"
    if ([string]::IsNullOrWhiteSpace($value)) { return $Default }
    return $value.Trim()
}

function Prompt-Secret {
    param([string]$Prompt)
    $secure = Read-Host $Prompt -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    }
    finally {
        if ($bstr -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
    }
}

function Prompt-YesNo {
    param([string]$Prompt, [bool]$Default)
    $suffix = if ($Default) { 'Y/n' } else { 'y/N' }
    $value = Read-Host "$Prompt [$suffix]"
    if ([string]::IsNullOrWhiteSpace($value)) { return $Default }
    switch ($value.Trim().ToLowerInvariant()) {
        'y' { return $true }
        'yes' { return $true }
        'n' { return $false }
        'no' { return $false }
        default { return $Default }
    }
}

function Read-JsonField {
    param([string]$Path, [string]$Key)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        $data = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    }
    catch {
        return $null
    }
    $property = $data.PSObject.Properties[$Key]
    if ($null -eq $property) { return $null }
    return [string]$property.Value
}

function Prompt-ValidatedInteger {
    param(
        [string]$Prompt,
        [int]$Default,
        [int]$Min,
        [int]$Max,
        [string]$FieldName
    )

    while ($true) {
        $raw = Prompt-Default -Prompt $Prompt -Default ([string]$Default)
        $value = 0
        if ([int]::TryParse($raw, [ref]$value) -and $value -ge $Min -and $value -le $Max) {
            return $value
        }
        Write-Warning "Invalid $FieldName. Expected an integer between $Min and $Max."
    }
}

function Resolve-Endpoint {
    param([string]$RawEndpoint)

    $trimmed = $RawEndpoint.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        Fail 'control plane host is required'
    }

    if ($trimmed -match '^[a-zA-Z][a-zA-Z0-9+.-]*://') {
        $uri = [uri]$trimmed
        if ($uri.Scheme -eq 'https') {
            Fail 'https endpoints are not supported by the current agent transport yet'
        }
        return [pscustomobject]@{
            Host = $uri.Host
            Port = if ($uri.IsDefaultPort) { $null } else { $uri.Port }
        }
    }

    if ($trimmed.StartsWith('[')) {
        if ($trimmed -match '^\[(.+)\](?::(\d+))?$') {
            return [pscustomobject]@{
                Host = $matches[1]
                Port = if ($matches[2]) { [int]$matches[2] } else { $null }
            }
        }
        Fail 'invalid bracketed IPv6 endpoint'
    }

    $colonCount = ($trimmed.ToCharArray() | Where-Object { $_ -eq ':' }).Count
    if ($colonCount -gt 1) {
        return [pscustomobject]@{ Host = $trimmed; Port = $null }
    }

    if ($trimmed.Contains(':')) {
        $parts = $trimmed.Split(':', 2)
        $port = 0
        if (-not [int]::TryParse($parts[1], [ref]$port) -or $port -lt 1 -or $port -gt 65535) {
            Fail 'control plane port must be an integer between 1 and 65535'
        }
        return [pscustomobject]@{ Host = $parts[0]; Port = $port }
    }

    return [pscustomobject]@{ Host = $trimmed; Port = $null }
}

function Format-HttpHost {
    param([string]$Host)
    if ($Host.Contains(':') -and -not ($Host.StartsWith('[') -and $Host.EndsWith(']'))) {
        return "[$Host]"
    }
    return $Host
}

function Write-ClientConfig {
    param(
        [string]$TargetPath,
        [string]$ServerAddress,
        [int]$ServerPort,
        [string]$AuthToken,
        [int]$CollectionIntervalMs,
        [bool]$EnableGpu
    )

    $config = [ordered]@{
        mode = 'distributed'
        run_mode = 'interactive'
        server_address = $ServerAddress
        server_port = $ServerPort
        auth_token = $AuthToken
        collection_interval_ms = $CollectionIntervalMs
        enable_gpu = $EnableGpu
        reconnect_delay_ms = 5000
        max_reconnect_attempts = 10
        log_file = ''
        pid_file = ''
    }

    $json = $config | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($TargetPath, $json + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
}

function Write-ConfigInteractive {
    param([string]$OutputPath)

    $currentHost = Read-JsonField -Path $script:ConfigPath -Key 'server_address'
    if ([string]::IsNullOrWhiteSpace($currentHost)) { $currentHost = '127.0.0.1' }

    $currentPort = Read-JsonField -Path $script:ConfigPath -Key 'server_port'
    if ([string]::IsNullOrWhiteSpace($currentPort)) { $currentPort = '9000' }

    $currentInterval = Read-JsonField -Path $script:ConfigPath -Key 'collection_interval_ms'
    if ([string]::IsNullOrWhiteSpace($currentInterval)) { $currentInterval = '1000' }

    $currentGpu = Read-JsonField -Path $script:ConfigPath -Key 'enable_gpu'
    if ([string]::IsNullOrWhiteSpace($currentGpu)) { $currentGpu = 'true' }

    $endpointInput = Prompt-Default -Prompt 'Control plane URL or hostname' -Default $currentHost
    $endpoint = Resolve-Endpoint -RawEndpoint $endpointInput
    $serverHost = $endpoint.Host
    $serverPort = if ($null -ne $endpoint.Port -and $endpoint.Port -ne '') { [int]$endpoint.Port } else { [int]$currentPort }
    $serverPort = Prompt-ValidatedInteger -Prompt 'Control plane port' -Default $serverPort -Min 1 -Max 65535 -FieldName 'control plane port'

    $token = Prompt-Secret -Prompt 'API key'
    if ([string]::IsNullOrWhiteSpace($token)) {
        Fail 'API key is required'
    }

    $interval = Prompt-ValidatedInteger -Prompt 'Collection interval in milliseconds' -Default ([int]$currentInterval) -Min 250 -Max 86400000 -FieldName 'collection interval'
    $enableGpuRequested = Prompt-YesNo -Prompt 'Enable GPU collection?' -Default $false
    $enableGpu = $false
    if ($enableGpuRequested) {
        Write-Warning 'GPU collection is not available in the current fused Windows build and will remain disabled.'
    }

    Ensure-Directory -Path ([System.IO.Path]::GetDirectoryName($OutputPath))
    Write-ClientConfig -TargetPath $OutputPath -ServerAddress $serverHost -ServerPort $serverPort -AuthToken $token -CollectionIntervalMs $interval -EnableGpu $enableGpu
}

function Get-AssetBaseName {
    return "btop-agent_${script:ResolvedVersion}_windows_${script:WindowsArch}"
}

function Fetch-ReleaseMetadata {
    if ($AssetPathOverride) {
        $script:ResolvedVersion = $Version
        return
    }

    $endpoint = if ($Version -eq 'latest') {
        "https://api.github.com/repos/$RepoSlug/releases/latest"
    }
    else {
        "https://api.github.com/repos/$RepoSlug/releases/tags/$Version"
    }

    $script:ReleaseMetadata = Invoke-RestMethod -Uri $endpoint -Headers @{ Accept = 'application/vnd.github+json' }
    $script:ResolvedVersion = [string]$script:ReleaseMetadata.tag_name
}

function Resolve-ReleaseAsset {
    param([string[]]$Names)
    if ($null -eq $script:ReleaseMetadata) {
        Fail 'release metadata is not available'
    }

    foreach ($asset in $script:ReleaseMetadata.assets) {
        if ($Names -contains [string]$asset.name) {
            return $asset
        }
    }

    return $null
}

function Download-AgentAsset {
    $archivePath = Join-Path $TempRoot 'agent.zip'
    if ($AssetPathOverride) {
        if (-not (Test-Path -LiteralPath $AssetPathOverride)) {
            Fail "local asset does not exist: $AssetPathOverride"
        }
        if ([System.IO.Path]::GetExtension($AssetPathOverride).ToLowerInvariant() -ne '.zip') {
            Fail 'local Windows asset must be a .zip archive'
        }
        Copy-Item -LiteralPath $AssetPathOverride -Destination $archivePath -Force
        $script:DownloadedAssetName = [System.IO.Path]::GetFileName($AssetPathOverride)
        return $archivePath
    }

    $primaryName = (Get-AssetBaseName) + '.zip'
    $fallbackName = "btop-agent-$script:ResolvedVersion-windows-$script:WindowsArch.zip"
    $asset = Resolve-ReleaseAsset -Names @($primaryName, $fallbackName)
    if ($null -eq $asset) {
        Fail "release asset not found for windows/$script:WindowsArch"
    }

    $script:DownloadedAssetName = [string]$asset.name
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archivePath
    return $archivePath
}

function Download-ChecksumsManifest {
    $checksumsPath = Join-Path $TempRoot 'SHA256SUMS.txt'
    if ($ChecksumsPathOverride) {
        Copy-Item -LiteralPath $ChecksumsPathOverride -Destination $checksumsPath -Force
        return $checksumsPath
    }

    if ($AssetPathOverride) {
        $resolvedAssetPath = (Resolve-Path -LiteralPath $AssetPathOverride).ProviderPath
        $siblingPath = Join-Path ([System.IO.Path]::GetDirectoryName($resolvedAssetPath)) 'SHA256SUMS.txt'
        if (-not (Test-Path -LiteralPath $siblingPath)) {
            Fail 'local installs require BTOP_AGENT_CHECKSUMS_PATH or a sibling SHA256SUMS.txt'
        }
        Copy-Item -LiteralPath $siblingPath -Destination $checksumsPath -Force
        return $checksumsPath
    }

    $asset = Resolve-ReleaseAsset -Names @('SHA256SUMS.txt')
    if ($null -eq $asset) {
        Fail 'release checksum manifest not found'
    }
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $checksumsPath
    return $checksumsPath
}

function Verify-ArchiveChecksum {
    param([string]$ArchivePath)

    $checksumsPath = Download-ChecksumsManifest
    $expectedLine = Get-Content -LiteralPath $checksumsPath | Where-Object {
        $parts = $_ -split '\s+'
        $parts.Count -ge 2 -and $parts[-1].TrimStart('./') -eq $script:DownloadedAssetName
    } | Select-Object -First 1

    if (-not $expectedLine) {
        Fail "checksum for $($script:DownloadedAssetName) not found in manifest"
    }

    $expectedHash = ($expectedLine -split '\s+')[0].ToLowerInvariant()
    $actualHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expectedHash -ne $actualHash) {
        Fail "checksum mismatch for $($script:DownloadedAssetName)"
    }
}

function Prepare-ArchiveContents {
    param([string]$ArchivePath)

    $stageRoot = Join-Path $TempRoot 'stage'
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    Ensure-Directory -Path $stageRoot
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $stageRoot -Force

    $packageDir = Join-Path $stageRoot 'btop-agent'
    $binary = Join-Path $packageDir 'btop-agent.exe'
    $sampleConfig = Join-Path $packageDir 'distributed-client.example.json'

    if (-not (Test-Path -LiteralPath $binary)) {
        Fail 'archive is missing btop-agent.exe'
    }
    if (-not (Test-Path -LiteralPath $sampleConfig)) {
        Fail 'archive is missing distributed-client.example.json'
    }

    & $binary --help | Out-Null
    $aliasPath = Join-Path $packageDir 'btop.exe'
    Copy-Item -LiteralPath $binary -Destination $aliasPath -Force
    & $aliasPath --help | Out-Null

    return $packageDir
}

function Add-PathEntry {
    param([string]$Entry, [ValidateSet('User', 'Machine')] [string]$Scope)

    $target = [Environment]::GetEnvironmentVariable('Path', $Scope)
    $parts = @()
    if (-not [string]::IsNullOrWhiteSpace($target)) {
        $parts = $target.Split(';', [StringSplitOptions]::RemoveEmptyEntries)
    }
    $parts = @($parts | Where-Object { $_ -ne $Entry })
    $newValue = if ($parts.Count -eq 0) { $Entry } else { (@($Entry) + $parts) -join ';' }
    [Environment]::SetEnvironmentVariable('Path', $newValue, $Scope)
    $envParts = @(($env:Path -split ';') | Where-Object { $_ -and $_ -ne $Entry })
    $env:Path = if ($envParts.Count -eq 0) { $Entry } else { (@($Entry) + $envParts) -join ';' }
}

function Remove-PathEntry {
    param([string]$Entry, [ValidateSet('User', 'Machine')] [string]$Scope)

    $target = [Environment]::GetEnvironmentVariable('Path', $Scope)
    if ([string]::IsNullOrWhiteSpace($target)) { return }
    $parts = $target.Split(';', [StringSplitOptions]::RemoveEmptyEntries) | Where-Object { $_ -ne $Entry }
    [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), $Scope)
}

function Write-StateFile {
    Ensure-Directory -Path $script:ConfigRoot
    $state = [ordered]@{
        version = $script:ResolvedVersion
        install_root = $script:InstallRoot
        binary_path = $script:BinaryPath
        btop_alias_path = $script:BtopAliasPath
        config_path = $script:ConfigPath
        path_scope = $script:PathScope
        task_name = $script:TaskName
    }
    ($state | ConvertTo-Json -Depth 8) + [Environment]::NewLine | Set-Content -LiteralPath $script:StatePath -Encoding utf8
    Set-PrivateAcl -Path $script:StatePath
}

function New-BtopAlias {
    if (Test-Path -LiteralPath $script:BtopAliasPath) {
        Remove-Item -LiteralPath $script:BtopAliasPath -Force
    }
    try {
        New-Item -ItemType HardLink -Path $script:BtopAliasPath -Target $script:BinaryPath -Force | Out-Null
    }
    catch {
        Copy-Item -LiteralPath $script:BinaryPath -Destination $script:BtopAliasPath -Force
    }
}

function Cleanup-PartialInstall {
    Stop-AgentRuntime
    Unregister-AgentTask
    if (Test-Path -LiteralPath $script:BtopAliasPath) { Remove-Item -LiteralPath $script:BtopAliasPath -Force }
    if (Test-Path -LiteralPath $script:BinaryPath) { Remove-Item -LiteralPath $script:BinaryPath -Force }
    if (Test-Path -LiteralPath (Join-Path $script:InstallRoot 'distributed-client.example.json')) { Remove-Item -LiteralPath (Join-Path $script:InstallRoot 'distributed-client.example.json') -Force }
    if (Test-Path -LiteralPath $script:ConfigPath) { Remove-Item -LiteralPath $script:ConfigPath -Force }
    if (Test-Path -LiteralPath $script:StatePath) { Remove-Item -LiteralPath $script:StatePath -Force }
    Remove-PathEntry -Entry $script:InstallRoot -Scope $script:PathScope
}

function Install-ArchiveContents {
    param([string]$StageDir)

    Ensure-Directory -Path $script:InstallRoot
    Ensure-Directory -Path $script:ConfigRoot
    Copy-Item -LiteralPath (Join-Path $StageDir 'btop-agent.exe') -Destination $script:BinaryPath -Force
    Copy-Item -LiteralPath (Join-Path $StageDir 'distributed-client.example.json') -Destination (Join-Path $script:InstallRoot 'distributed-client.example.json') -Force
    New-BtopAlias
    Add-PathEntry -Entry $script:InstallRoot -Scope $script:PathScope
}

function Invoke-PreflightConnectivity {
    param([string]$ConfigPath)

    $config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
    $baseUrl = "http://$(Format-HttpHost -Host ([string]$config.server_address)):$($config.server_port)"
    Invoke-WebRequest -Uri "$baseUrl/api/ping" | Out-Null
    Invoke-WebRequest -Uri "$baseUrl/api/auth/status" -Headers @{ Authorization = "Bearer $($config.auth_token)" } | Out-Null
}

function Test-AgentOnce {
    param([string]$Binary, [string]$ConfigPath)
    & $Binary --config $ConfigPath --once
    if ($LASTEXITCODE -ne 0) {
        Fail 'agent validation failed'
    }
}

function Register-AgentTask {
    if ($script:IsAdminInstall) {
        $action = New-ScheduledTaskAction -Execute $script:BinaryPath -Argument ('--config "' + $script:ConfigPath + '"')
        $trigger = New-ScheduledTaskTrigger -AtStartup
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
        Register-ScheduledTask -TaskName $script:TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    }
    else {
        $action = New-ScheduledTaskAction -Execute $script:BinaryPath -Argument ('--config "' + $script:ConfigPath + '"')
        $trigger = New-ScheduledTaskTrigger -AtLogOn -User $script:CurrentUserId
        $principal = New-ScheduledTaskPrincipal -UserId $script:CurrentUserId -LogonType InteractiveToken -RunLevel Limited
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
        Register-ScheduledTask -TaskName $script:TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    }
}

function Unregister-AgentTask {
    if (Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $script:TaskName -Confirm:$false
    }
}

function Stop-AgentProcess {
    try {
        $processes = Get-CimInstance Win32_Process -Filter "Name='btop-agent.exe'" -ErrorAction Stop
    }
    catch {
        return
    }

    foreach ($process in $processes) {
        if ($process.ExecutablePath -eq $script:BinaryPath) {
            Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
        }
    }
}

function Wait-AgentProcessExit {
    param([int]$TimeoutSeconds = 30)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $running = @()
        try {
            $running = Get-CimInstance Win32_Process -Filter "Name='btop-agent.exe'" -ErrorAction Stop | Where-Object {
                $_.ExecutablePath -eq $script:BinaryPath
            }
        }
        catch {
            $running = @()
        }

        if ($running.Count -eq 0) {
            return
        }

        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    Fail "timed out waiting for $($script:BinaryPath) to stop"
}

function Stop-AgentRuntime {
    if (Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue | Out-Null
    }
    Stop-AgentProcess
    Wait-AgentProcessExit
}

function Start-AgentRuntime {
    if (Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue) {
        Start-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
    }
}

function Get-TaskStateSafe {
    $task = Get-ScheduledTask -TaskName $script:TaskName -ErrorAction SilentlyContinue
    if ($null -eq $task) { return 'missing' }
    $info = Get-ScheduledTaskInfo -TaskName $script:TaskName -ErrorAction SilentlyContinue
    if ($null -eq $info) { return 'registered' }
    return $info.State.ToString()
}

function Install-Action {
    Initialize-Paths
    if ((Test-Path -LiteralPath $script:BinaryPath) -or (Test-Path -LiteralPath $script:BtopAliasPath)) {
        Fail "agent already appears to be installed at $script:BinaryPath; use update or uninstall first"
    }
    Fetch-ReleaseMetadata
    $archivePath = Download-AgentAsset
    Verify-ArchiveChecksum -ArchivePath $archivePath
    $stageDir = Prepare-ArchiveContents -ArchivePath $archivePath

    $candidateConfig = Join-Path $TempRoot 'distributed-client.candidate.json'
    $existingConfigBackup = Join-Path $TempRoot 'distributed-client.existing.json'
    if (Test-Path -LiteralPath $script:ConfigPath) {
        Copy-Item -LiteralPath $script:ConfigPath -Destination $existingConfigBackup -Force
    }
    Write-ConfigInteractive -OutputPath $candidateConfig
    Invoke-PreflightConnectivity -ConfigPath $candidateConfig
    Test-AgentOnce -Binary (Join-Path $stageDir 'btop-agent.exe') -ConfigPath $candidateConfig

    try {
        Install-ArchiveContents -StageDir $stageDir
        Copy-Item -LiteralPath $candidateConfig -Destination $script:ConfigPath -Force
        Set-PrivateAcl -Path $script:ConfigRoot -Directory
        Set-PrivateAcl -Path $script:ConfigPath
        Register-AgentTask
        Start-AgentRuntime
        Write-StateFile
    }
    catch {
        Cleanup-PartialInstall
        if (Test-Path -LiteralPath $existingConfigBackup) {
            Ensure-Directory -Path $script:ConfigRoot
            Copy-Item -LiteralPath $existingConfigBackup -Destination $script:ConfigPath -Force
            Set-PrivateAcl -Path $script:ConfigRoot -Directory
            Set-PrivateAcl -Path $script:ConfigPath
        }
        throw
    }

    Write-Log "Installed $ServiceName $script:ResolvedVersion"
    Write-Log "Binary: $script:BinaryPath"
    Write-Log "TUI alias: $script:BtopAliasPath"
    Write-Log "Config: $script:ConfigPath"
}

function Update-Action {
    Initialize-Paths
    if (-not (Test-Path -LiteralPath $script:BinaryPath)) {
        Fail "agent is not installed at $script:BinaryPath"
    }
    Fetch-ReleaseMetadata
    $archivePath = Download-AgentAsset
    Verify-ArchiveChecksum -ArchivePath $archivePath
    $stageDir = Prepare-ArchiveContents -ArchivePath $archivePath
    Invoke-PreflightConnectivity -ConfigPath $script:ConfigPath
    Test-AgentOnce -Binary (Join-Path $stageDir 'btop-agent.exe') -ConfigPath $script:ConfigPath

    $backupRoot = Join-Path $TempRoot 'backup'
    Ensure-Directory -Path $backupRoot
    Copy-Item -LiteralPath $script:BinaryPath -Destination (Join-Path $backupRoot 'btop-agent.exe') -Force
    if (Test-Path -LiteralPath $script:BtopAliasPath) {
        Copy-Item -LiteralPath $script:BtopAliasPath -Destination (Join-Path $backupRoot 'btop.exe') -Force
    }
    if (Test-Path -LiteralPath (Join-Path $script:InstallRoot 'distributed-client.example.json')) {
        Copy-Item -LiteralPath (Join-Path $script:InstallRoot 'distributed-client.example.json') -Destination (Join-Path $backupRoot 'distributed-client.example.json') -Force
    }
    if (Test-Path -LiteralPath $script:StatePath) {
        Copy-Item -LiteralPath $script:StatePath -Destination (Join-Path $backupRoot 'install-state.json') -Force
    }

    try {
        Stop-AgentRuntime
        Unregister-AgentTask
        Install-ArchiveContents -StageDir $stageDir
        Register-AgentTask
        Start-AgentRuntime
        Write-StateFile
    }
    catch {
        Write-Warning 'Update failed. Attempting rollback.'
        Stop-AgentRuntime
        Copy-Item -LiteralPath (Join-Path $backupRoot 'btop-agent.exe') -Destination $script:BinaryPath -Force
        if (Test-Path -LiteralPath (Join-Path $backupRoot 'btop.exe')) {
            Copy-Item -LiteralPath (Join-Path $backupRoot 'btop.exe') -Destination $script:BtopAliasPath -Force
        }
        if (Test-Path -LiteralPath (Join-Path $backupRoot 'distributed-client.example.json')) {
            Copy-Item -LiteralPath (Join-Path $backupRoot 'distributed-client.example.json') -Destination (Join-Path $script:InstallRoot 'distributed-client.example.json') -Force
        }
        if (Test-Path -LiteralPath (Join-Path $backupRoot 'install-state.json')) {
            Copy-Item -LiteralPath (Join-Path $backupRoot 'install-state.json') -Destination $script:StatePath -Force
            Set-PrivateAcl -Path $script:StatePath
        }
        elseif (Test-Path -LiteralPath $script:StatePath) {
            Remove-Item -LiteralPath $script:StatePath -Force
        }
        Register-AgentTask
        Start-AgentRuntime
        throw
    }

    Write-Log "Updated $ServiceName to $script:ResolvedVersion"
}

function Configure-Action {
    Initialize-Paths
    if (-not (Test-Path -LiteralPath $script:BinaryPath)) {
        Fail "agent is not installed at $script:BinaryPath"
    }

    $candidateConfig = Join-Path $TempRoot 'distributed-client.candidate.json'
    $backupConfig = Join-Path $TempRoot 'distributed-client.previous.json'
    if (Test-Path -LiteralPath $script:ConfigPath) {
        Copy-Item -LiteralPath $script:ConfigPath -Destination $backupConfig -Force
    }
    Write-ConfigInteractive -OutputPath $candidateConfig
    Invoke-PreflightConnectivity -ConfigPath $candidateConfig
    Test-AgentOnce -Binary $script:BinaryPath -ConfigPath $candidateConfig
    try {
        Stop-AgentRuntime
        Copy-Item -LiteralPath $candidateConfig -Destination $script:ConfigPath -Force
        Set-PrivateAcl -Path $script:ConfigRoot -Directory
        Set-PrivateAcl -Path $script:ConfigPath
        Register-AgentTask
        Start-AgentRuntime
        Write-StateFile
    }
    catch {
        if (Test-Path -LiteralPath $backupConfig) {
            Copy-Item -LiteralPath $backupConfig -Destination $script:ConfigPath -Force
            Set-PrivateAcl -Path $script:ConfigRoot -Directory
            Set-PrivateAcl -Path $script:ConfigPath
            Register-AgentTask
            Start-AgentRuntime
        }
        throw
    }
    Write-Log "Updated configuration at $script:ConfigPath"
}

function Status-Action {
    Initialize-Paths
    Write-Log "Binary: $script:BinaryPath"
    Write-Log "TUI alias: $script:BtopAliasPath"
    Write-Log "Config: $script:ConfigPath"
    Write-Log "State: $script:StatePath"
    Write-Log "Installed: $(if (Test-Path -LiteralPath $script:BinaryPath) { 'yes' } else { 'no' })"
    Write-Log "TUI alias present: $(if (Test-Path -LiteralPath $script:BtopAliasPath) { 'yes' } else { 'no' })"
    Write-Log "Config present: $(if (Test-Path -LiteralPath $script:ConfigPath) { 'yes' } else { 'no' })"
    Write-Log "Task: $(Get-TaskStateSafe)"
    if (Test-Path -LiteralPath $script:StatePath) {
        try {
            $state = Get-Content -LiteralPath $script:StatePath -Raw | ConvertFrom-Json
            Write-Log "Version: $($state.version)"
        }
        catch {
            Write-Warning "State file is unreadable: $script:StatePath"
        }
    }
}

function Uninstall-Action {
    Initialize-Paths
    Stop-AgentRuntime
    Unregister-AgentTask
    if (Test-Path -LiteralPath $script:BtopAliasPath) { Remove-Item -LiteralPath $script:BtopAliasPath -Force }
    if (Test-Path -LiteralPath $script:BinaryPath) { Remove-Item -LiteralPath $script:BinaryPath -Force }
    if (Test-Path -LiteralPath (Join-Path $script:InstallRoot 'distributed-client.example.json')) { Remove-Item -LiteralPath (Join-Path $script:InstallRoot 'distributed-client.example.json') -Force }
    if (Test-Path -LiteralPath $script:StatePath) { Remove-Item -LiteralPath $script:StatePath -Force }
    Remove-PathEntry -Entry $script:InstallRoot -Scope $script:PathScope
    if (Prompt-YesNo -Prompt 'Remove config file too?' -Default $false) {
        if (Test-Path -LiteralPath $script:ConfigPath) { Remove-Item -LiteralPath $script:ConfigPath -Force }
    }
    Write-Log "Uninstalled $ServiceName"
}

try {
    Require-Command -Name 'Expand-Archive'
    switch ($Action) {
        'install' { Install-Action }
        'update' { Update-Action }
        'configure' { Configure-Action }
        'status' { Status-Action }
        'uninstall' { Uninstall-Action }
    }
}
finally {
    if (Test-Path -LiteralPath $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
}
