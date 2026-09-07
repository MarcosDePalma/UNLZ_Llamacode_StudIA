param(
    [string]$Api = 'http://127.0.0.1:8765',
    [int]$TimeoutSec = 1800,
    [int]$InfraRetries = 2,
    [string]$AgentProfileId = '',
    [string]$LogPath = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $local = [Environment]::GetFolderPath('LocalApplicationData')
    $LogPath = Join-Path $local 'LlamaCode\LlamaCode\benchmark-campaign-post-correction.log'
}

$logDir = Split-Path -Parent $LogPath
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Write-CampaignLog([string]$Message) {
    $line = "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] $Message"
    Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8
    Write-Output $line
}

function Invoke-Control([string]$Method, [object[]]$ParamList) {
    $body = @{ method = $Method; args = @($ParamList) } | ConvertTo-Json -Depth 10 -Compress
    Invoke-RestMethod "$Api/invoke" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 60
}

function Get-ControlProperty([string]$Name) {
    (Invoke-RestMethod "$Api/prop?name=$Name" -TimeoutSec 60).value
}

function Get-ControlPropertySafe([string]$Name) {
    try { return Get-ControlProperty $Name } catch { return $null }
}

function Get-ActiveBenchmarkProfiles {
    # launchMenu() is authoritative: it includes benchmarkVariants expanded by
    # ProfileManager, which are not separate rows in the source JSON files.
    $menu = @((Invoke-Control 'launchMenu' @()).result)
    $selected = @($menu | Where-Object {
        $_.benchmark -eq $true -and $_.ready -eq $true -and
        -not [string]::IsNullOrWhiteSpace([string]$_.id)
    })
    return @($selected)
}

function Get-CoverageRow([string]$ProfileId) {
    $coverage = @((Get-ControlProperty 'benchmarkCoverage'))
    return $coverage | Where-Object { [string]$_.profileId -eq $ProfileId } | Select-Object -First 1
}

function Get-StageState($Row, [string]$Stage) {
    if ($null -eq $Row -or $null -eq $Row.stageStates) { return 'pending' }
    $key = $Stage.ToLowerInvariant()
    $value = $Row.stageStates.PSObject.Properties[$key]
    if ($null -eq $value) { $value = $Row.stageStates.PSObject.Properties[$Stage] }
    if ($null -eq $value) { return 'pending' }
    return [string]$value.Value
}

function Wait-BenchmarkFinished([string]$ProfileName, [string]$Stage) {
    $maxWait = [Math]::Max(600, $TimeoutSec + 300)
    $started = Get-Date
    do {
        Start-Sleep -Seconds 15
        $running = Get-ControlPropertySafe 'benchmarkRunning'
        $status = Get-ControlPropertySafe 'benchmarkStatus'
        $progress = Get-ControlPropertySafe 'benchmarkProgress'
        if ($null -ne $status -and $status -ne '') {
            Write-CampaignLog "$ProfileName · $Stage · progreso=$progress · $status"
        }
        if ($running -ne $true) { return $true }
        if (((Get-Date) - $started).TotalSeconds -gt $maxWait) {
            Write-CampaignLog "$ProfileName · $Stage · límite externo alcanzado; solicitando cancelación segura"
            try { Invoke-Control 'cancelBenchmark' @() | Out-Null } catch { }
            return $false
        }
    } while ($true)
}

try {
    Invoke-RestMethod "$Api/health" -TimeoutSec 15 | Out-Null
    $profiles = @(Get-ActiveBenchmarkProfiles)
    if ($profiles.Count -eq 0) { throw 'No se encontraron perfiles benchmark=true listos.' }

    Write-CampaignLog "Campaña post-corrección iniciada: $($profiles.Count) perfiles; orden HE0 → HE20 → BCB; target=agent; retriesInfra=$InfraRetries"
    $profileIndex = 0

    foreach ($profile in $profiles) {
        $profileIndex++
        $id = [string]$profile.id
        $name = [string]$profile.name
        $row = Get-CoverageRow $id
        if ($null -eq $row) {
            Write-CampaignLog "[$profileIndex/$($profiles.Count)] $name · no aparece en coverage; se omite"
            continue
        }

        Write-CampaignLog "[$profileIndex/$($profiles.Count)] $name · estado=$($row.coverageState) próximo=$($row.nextStage)"
        while ($null -ne $row -and -not [string]::IsNullOrWhiteSpace([string]$row.nextStage)) {
            $stage = [string]$row.nextStage
            $stageDone = $false

            for ($attempt = 0; $attempt -le $InfraRetries; $attempt++) {
                $stateBefore = Get-StageState $row $stage
                Write-CampaignLog "$name · $stage · inicio intento $($attempt + 1)/$($InfraRetries + 1) · estado previo=$stateBefore"
                $response = Invoke-Control 'startNextPendingBenchmark' @($id, 1, 'agent', $TimeoutSec, $AgentProfileId)
                $start = $response.result
                if ($null -eq $start -or $start.started -ne $true) {
                    $reason = if ($null -eq $start) { 'sin respuesta' } else { [string]$start.reason }
                    Write-CampaignLog "$name · $stage · no iniciado: $reason"
                    break
                }

                Wait-BenchmarkFinished $name $stage | Out-Null
                Start-Sleep -Seconds 5
                $row = Get-CoverageRow $id
                $state = Get-StageState $row $stage
                Write-CampaignLog "$name · $stage · resultado=$state · próximo=$($row.nextStage)"
                if ($state -eq 'valid') {
                    $stageDone = $true
                    break
                }
                if ($state -ne 'infra-timeout') { break }
                if ($attempt -lt $InfraRetries) {
                    Write-CampaignLog "$name · $stage · reintento por infraestructura/timeout"
                    Start-Sleep -Seconds 10
                }
            }

            if (-not $stageDone) {
                Write-CampaignLog "$name · $stage · detenido: no quedó validado; no se avanza a la etapa siguiente"
                break
            }
            $row = Get-CoverageRow $id
        }

        $final = Get-CoverageRow $id
        Write-CampaignLog "[$profileIndex/$($profiles.Count)] $name · cierre=$($final.coverageState) próximo=$($final.nextStage) estados=$($final.stageStates | ConvertTo-Json -Compress)"
        Start-Sleep -Seconds 5
    }

    Write-CampaignLog 'Campaña post-corrección finalizada.'
} catch {
    Write-CampaignLog "Campaña detenida por error de orchestration: $($_.Exception.Message)"
    throw
}
