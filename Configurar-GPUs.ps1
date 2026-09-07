#requires -Version 5.1

<#
    ASUS = juegos flat, juegos VR y runtimes VR
    PNY  = aplicaciones de escritorio seleccionadas

    Antes de ejecutarlo, configura manualmente dos EXE en:
    Configuracion > Sistema > Pantalla > Graficos:
      - un EXE en la ASUS;
      - otro EXE en la PNY.

    El script copia los identificadores SpecificAdapter que Windows guardo
    para esas referencias. No modifica procesos de Windows ni CUDA/IA.
    Usar -WhatIf para simular sin escribir el registro.
#>

[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = "Medium")]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RegPath = "HKCU:\SOFTWARE\Microsoft\DirectX\UserGpuPreferences"
$RegNativePath = "HKCU\SOFTWARE\Microsoft\DirectX\UserGpuPreferences"
$ProgramFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
$script:Changed = @()
$script:Skipped = @()
$script:Errors = @()

function Section {
    param([Parameter(Mandatory)][string]$Title)
    Write-Host ""
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

function Normalize-Path {
    param([Parameter(Mandatory)][string]$Path)
    $clean = [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"').Trim("'"))
    if ([string]::IsNullOrWhiteSpace($clean)) {
        return $null
    }
    try {
        return [IO.Path]::GetFullPath($clean)
    }
    catch {
        return $null
    }
}

function Add-Exe {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$List,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Path
    )
    $full = Normalize-Path $Path
    if ($null -eq $full -or
        -not (Test-Path -LiteralPath $full -PathType Leaf) -or
        [IO.Path]::GetExtension($full) -ine ".exe") {
        return
    }
    foreach ($item in $List) {
        if ($item -ieq $full) {
            return
        }
    }
    $List.Add($full) | Out-Null
}

function Add-ExeTree {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][System.Collections.Generic.List[string]]$List,
        [Parameter(Mandatory)][string]$Folder,
        [switch]$GameFolder
    )
    if (-not (Test-Path -LiteralPath $Folder -PathType Container)) {
        return
    }
    $excludedFolders = '\\(_CommonRedist|CommonRedist|Redist|Redistributable|DirectX|VCRedist|Support|Tools|EasyAntiCheat|BattlEye)\\'
    $excludedNames = '^(unins|uninstall|crash|report|redist|setup|installer|vc_redist|dxsetup|repair|install)[^\\]*\.exe$'
    $files = Get-ChildItem -LiteralPath $Folder -Filter "*.exe" -File -Recurse -ErrorAction SilentlyContinue
    foreach ($file in $files) {
        if ($GameFolder -and
            ($file.FullName -match $excludedFolders -or $file.Name -match $excludedNames)) {
            continue
        }
        Add-Exe -List $List -Path $file.FullName
    }
}

function Registry-Entry {
    param([Parameter(Mandatory)][string]$Exe)
    $full = Normalize-Path $Exe
    if ($null -eq $full -or -not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "No existe el ejecutable: $Exe"
    }
    if (-not (Test-Path -LiteralPath $RegPath)) {
        return $null
    }
    $props = Get-ItemProperty -LiteralPath $RegPath
    foreach ($prop in $props.PSObject.Properties) {
        if ($prop.Name -ieq $full) {
            return [PSCustomObject]@{ Exe = $full; Value = [string]$prop.Value }
        }
    }
    return $null
}

function Token {
    param(
        [Parameter(Mandatory)][string]$Value,
        [Parameter(Mandatory)][string]$Name
    )
    $match = [regex]::Match($Value, "(?i)(?:^|;){0}=([^;]*);?" -f [regex]::Escape($Name))
    if ($match.Success) {
        return $match.Groups[1].Value
    }
    return $null
}

function Reference {
    param(
        [Parameter(Mandatory)][string]$Role,
        [Parameter(Mandatory)][string]$Exe
    )
    $entry = Registry-Entry $Exe
    if ($null -eq $entry -or [string]::IsNullOrWhiteSpace($entry.Value)) {
        throw "No hay preferencia guardada para $Role ($Exe). Configuralo manualmente y volve a ejecutar."
    }
    $adapter = Token -Value $entry.Value -Name "SpecificAdapter"
    $gpuPreference = Token -Value $entry.Value -Name "GpuPreference"
    if ([string]::IsNullOrWhiteSpace($adapter)) {
        throw "$Role no contiene SpecificAdapter. Windows no esta identificando una GPU fisica concreta."
    }
    if ([string]::IsNullOrWhiteSpace($gpuPreference)) {
        throw "$Role no contiene GpuPreference."
    }
    return [PSCustomObject]@{
        Role = $Role
        Exe = $entry.Exe
        Raw = $entry.Value
        Adapter = $adapter
        GpuPreference = $gpuPreference
    }
}

function Merge-Preference {
    param(
        [AllowEmptyString()][string]$Current,
        [Parameter(Mandatory)][string]$Adapter,
        [Parameter(Mandatory)][string]$GpuPreference
    )
    $other = @()
    if (-not [string]::IsNullOrWhiteSpace($Current)) {
        $other = @($Current -split ';' | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and
            $_ -notmatch '^(?i:SpecificAdapter|GpuPreference)='
        })
    }
    $parts = @("SpecificAdapter=$Adapter", "GpuPreference=$GpuPreference") + $other
    return (($parts | ForEach-Object { "$_;" }) -join "")
}

function Set-Preference {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param(
        [Parameter(Mandatory)][string]$Exe,
        [Parameter(Mandatory)][string]$Adapter,
        [Parameter(Mandatory)][string]$GpuPreference,
        [Parameter(Mandatory)][string]$Role
    )
    $full = Normalize-Path $Exe
    if ($null -eq $full -or -not (Test-Path -LiteralPath $full -PathType Leaf)) {
        $script:Skipped += $Exe
        Write-Host "OMITIDO [$Role] no encontrado: $Exe" -ForegroundColor DarkGray
        return
    }
    try {
        if (-not (Test-Path -LiteralPath $RegPath)) {
            New-Item -Path $RegPath -Force | Out-Null
        }
        $oldEntry = Registry-Entry $full
        $old = if ($null -eq $oldEntry) { "" } else { $oldEntry.Value }
        $new = Merge-Preference -Current $old -Adapter $Adapter -GpuPreference $GpuPreference
        if ($old -eq $new) {
            Write-Host "YA OK [$Role] $full" -ForegroundColor DarkYellow
            return
        }
        if ($PSCmdlet.ShouldProcess($full, "Asignar a $Role")) {
            $arguments = @{
                Path = $RegPath
                Name = $full
                Value = $new
                PropertyType = "String"
                Force = $true
            }
            New-ItemProperty @arguments | Out-Null
            $script:Changed += $full
            Write-Host "OK [$Role] $full" -ForegroundColor Green
        }
        else {
            Write-Host "SIMULADO [$Role] $full" -ForegroundColor Gray
        }
    }
    catch {
        $script:Errors += $full
        Write-Host "ERROR [$Role] $full : $($_.Exception.Message)" -ForegroundColor Red
    }
}

function Set-GlobalHighPerformanceAdapter {
    [CmdletBinding(SupportsShouldProcess = $true)]
    param([Parameter(Mandatory)][string]$Adapter)

    try {
        $current = ""
        if (Test-Path -LiteralPath $RegPath) {
            $props = Get-ItemProperty -LiteralPath $RegPath
            foreach ($prop in $props.PSObject.Properties) {
                if ($prop.Name -ieq "DirectXUserGlobalSettings") {
                    $current = [string]$prop.Value
                    break
                }
            }
        }

        $other = @($current -split ';' | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and
            $_ -notmatch '^(?i:HighPerfAdapter)='
        })
        $parts = @("HighPerfAdapter=$Adapter") + $other
        $new = (($parts | ForEach-Object { "$_;" }) -join "")

        if ($current -eq $new) {
            Write-Host "YA OK [global] HighPerfAdapter=$Adapter" -ForegroundColor DarkYellow
            return
        }

        if ($PSCmdlet.ShouldProcess("DirectXUserGlobalSettings", "Usar PNY como adaptador global de alto rendimiento")) {
            if (-not (Test-Path -LiteralPath $RegPath)) {
                New-Item -Path $RegPath -Force | Out-Null
            }
            $arguments = @{
                Path = $RegPath
                Name = "DirectXUserGlobalSettings"
                Value = $new
                PropertyType = "String"
                Force = $true
            }
            New-ItemProperty @arguments | Out-Null
            $script:Changed += "[global] DirectXUserGlobalSettings"
            Write-Host "OK [global] HighPerfAdapter=$Adapter" -ForegroundColor Green
        }
        else {
            Write-Host "SIMULADO [global] HighPerfAdapter=$Adapter" -ForegroundColor Gray
        }
    }
    catch {
        $script:Errors += "[global] DirectXUserGlobalSettings"
        Write-Host "ERROR [global] HighPerfAdapter: $($_.Exception.Message)" -ForegroundColor Red
    }
}

function Backup {
    if (-not (Test-Path -LiteralPath $RegPath)) {
        Write-Host "No existe aun la clave de preferencias; no hay backup para exportar." -ForegroundColor DarkYellow
        return $null
    }
    $desktop = [Environment]::GetFolderPath("Desktop")
    if ([string]::IsNullOrWhiteSpace($desktop)) {
        $desktop = (Get-Location).Path
    }
    $path = Join-Path $desktop ("UserGpuPreferences_Backup_{0}.reg" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
    & reg.exe export $RegNativePath $path /y | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "No se pudo crear el backup: $path"
    }
    Write-Host "Backup creado: $path" -ForegroundColor Green
    return $path
}

function Show-Gpus {
    Section "GPU DETECTADAS"
    $gpus = @(Get-CimInstance Win32_VideoController | Where-Object {
        $_.Name -match '(?i)(RTX\s*3090|3090)'
    })
    if ($gpus.Count -eq 0) {
        Write-Host "No se detectaron RTX 3090 con Win32_VideoController." -ForegroundColor Yellow
        return
    }
    foreach ($gpu in $gpus) {
        [PSCustomObject]@{
            Nombre = $gpu.Name
            VRAM_GB = if ($null -ne $gpu.AdapterRAM) { [math]::Round([double]$gpu.AdapterRAM / 1GB, 1) } else { "?" }
            PNPDeviceID = $gpu.PNPDeviceID
            Driver = $gpu.DriverVersion
            Estado = $gpu.Status
        } | Format-List
    }
    Write-Host "La ASUS/PNY se identifica por SpecificAdapter, no por GPU 0/GPU 1." -ForegroundColor Yellow
}

function Auto-Desktop {
    $items = New-Object 'System.Collections.Generic.List[string]'
    $paths = @(
        (Join-Path $env:ProgramFiles "Google\Chrome\Application\chrome.exe"),
        (Join-Path $ProgramFilesX86 "Google\Chrome\Application\chrome.exe"),
        (Join-Path $env:ProgramFiles "Mozilla Firefox\firefox.exe"),
        (Join-Path $ProgramFilesX86 "Mozilla Firefox\firefox.exe"),
        (Join-Path $env:LOCALAPPDATA "Microsoft\Edge\Application\msedge.exe"),
        (Join-Path $ProgramFilesX86 "Microsoft\Edge\Application\msedge.exe"),
        (Join-Path $env:APPDATA "Spotify\Spotify.exe"),
        (Join-Path $env:LOCALAPPDATA "Spotify\Spotify.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Notion\Notion.exe"),
        (Join-Path $env:LOCALAPPDATA "Notion\Notion.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Microsoft VS Code\Code.exe"),
        (Join-Path $env:ProgramFiles "Microsoft VS Code\Code.exe"),
        (Join-Path $env:ProgramFiles "VideoLAN\VLC\vlc.exe"),
        (Join-Path $ProgramFilesX86 "VideoLAN\VLC\vlc.exe"),
        (Join-Path $env:ProgramFiles "Epic Games\Launcher\Portal\Binaries\Win64\EpicGamesLauncher.exe"),
        (Join-Path $ProgramFilesX86 "Epic Games\Launcher\Portal\Binaries\Win64\EpicGamesLauncher.exe"),
        (Join-Path $ProgramFilesX86 "Steam\steam.exe"),
        (Join-Path $env:ProgramFiles "Steam\steam.exe"),
        (Join-Path $env:LOCALAPPDATA "WhatsApp\WhatsApp.exe")
    )
    foreach ($path in $paths) {
        Add-Exe -List $items -Path $path
    }
    $discordRoot = Join-Path $env:LOCALAPPDATA "Discord"
    if (Test-Path -LiteralPath $discordRoot -PathType Container) {
        Get-ChildItem -LiteralPath $discordRoot -Filter "Discord.exe" -File -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object { Add-Exe -List $items -Path $_.FullName }
    }
    return ,$items
}

function Auto-Vr {
    $items = New-Object 'System.Collections.Generic.List[string]'
    $paths = @(
        (Join-Path $ProgramFilesX86 "Steam\steamapps\common\SteamVR\bin\win64\vrserver.exe"),
        (Join-Path $ProgramFilesX86 "Steam\steamapps\common\SteamVR\bin\win64\vrcompositor.exe"),
        (Join-Path $ProgramFilesX86 "Steam\steamapps\common\SteamVR\bin\win64\vrmonitor.exe"),
        (Join-Path $ProgramFilesX86 "Steam\steamapps\common\SteamVR\bin\win64\vrdashboard.exe"),
        (Join-Path $env:ProgramFiles "Virtual Desktop Streamer\VirtualDesktop.Streamer.exe"),
        (Join-Path $ProgramFilesX86 "Virtual Desktop Streamer\VirtualDesktop.Streamer.exe"),
        (Join-Path $env:ProgramFiles "Oculus\Support\oculus-runtime\OVRServer_x64.exe"),
        (Join-Path $ProgramFilesX86 "Oculus\Support\oculus-runtime\OVRServer_x64.exe")
    )
    foreach ($path in $paths) {
        Add-Exe -List $items -Path $path
    }
    return ,$items
}

function Read-ExeList {
    param([Parameter(Mandatory)][string]$Prompt)
    $items = New-Object 'System.Collections.Generic.List[string]'
    Write-Host $Prompt
    Write-Host "Ruta completa; ENTER vacio para terminar."
    while ($true) {
        $path = Read-Host "EXE"
        if ([string]::IsNullOrWhiteSpace($path)) {
            break
        }
        $full = Normalize-Path $path
        if ($null -eq $full -or -not (Test-Path -LiteralPath $full -PathType Leaf)) {
            Write-Host "No existe: $path" -ForegroundColor Red
            continue
        }
        Add-Exe -List $items -Path $full
    }
    return ,$items
}

function Read-FolderList {
    param([Parameter(Mandatory)][string]$Prompt)
    $items = New-Object 'System.Collections.Generic.List[string]'
    Write-Host $Prompt
    Write-Host "Ruta de carpeta; ENTER vacio para terminar."
    while ($true) {
        $path = Read-Host "Carpeta"
        if ([string]::IsNullOrWhiteSpace($path)) {
            break
        }
        $clean = [Environment]::ExpandEnvironmentVariables($path.Trim().Trim('"').Trim("'"))
        if (-not (Test-Path -LiteralPath $clean -PathType Container)) {
            Write-Host "No existe: $path" -ForegroundColor Red
            continue
        }
        $items.Add((Resolve-Path -LiteralPath $clean).Path) | Out-Null
    }
    return ,$items
}

function Show-FinalPreferences {
    Section "PREFERENCIAS FINALES"
    if (-not (Test-Path -LiteralPath $RegPath)) {
        Write-Host "No existe la clave."
        return
    }
    $rows = @()
    $props = Get-ItemProperty -LiteralPath $RegPath
    foreach ($prop in $props.PSObject.Properties) {
        if ($prop.Name -match '^PS(Path|ParentPath|ChildName|Drive|Provider)$') {
            continue
        }
        $value = [string]$prop.Value
        $adapter = Token -Value $value -Name "SpecificAdapter"
        $role = "OTRA"
        if ($adapter -ieq $script:Asus.Adapter) { $role = "ASUS" }
        elseif ($adapter -ieq $script:Pny.Adapter) { $role = "PNY" }
        $rows += [PSCustomObject]@{ Rol = $role; Ejecutable = $prop.Name; Preferencia = $value }
    }
    $rows | Sort-Object Rol, Ejecutable | Format-Table -AutoSize
}

Section "CONFIGURADOR ASUS / PNY"
Write-Host "ASUS -> juegos flat, juegos VR y runtimes VR"
Write-Host "PNY  -> aplicaciones de escritorio seleccionadas"
Write-Host "No se modifican procesos de Windows ni configuracion CUDA/IA."
Show-Gpus

try {
    $backupPath = Backup
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

Section "REFERENCIAS MANUALES"
Write-Host "Configura primero un EXE en cada GPU desde Configuracion > Sistema > Pantalla > Graficos."
$asusReferencePath = Read-Host "Ruta del EXE configurado en la ASUS"
$pnyReferencePath = Read-Host "Ruta del EXE configurado en la PNY"

try {
    $script:Asus = Reference -Role "ASUS" -Exe $asusReferencePath
    $script:Pny = Reference -Role "PNY" -Exe $pnyReferencePath
}
catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host "ASUS SpecificAdapter: $($script:Asus.Adapter)" -ForegroundColor Yellow
Write-Host "PNY  SpecificAdapter: $($script:Pny.Adapter)" -ForegroundColor Yellow
if ($script:Asus.Adapter -ieq $script:Pny.Adapter) {
    Write-Host "DETENIDO: ambas referencias tienen el mismo SpecificAdapter." -ForegroundColor Red
    Write-Host "Windows no esta distinguiendo fisicamente tus dos RTX 3090." -ForegroundColor Red
    exit 1
}

$desktop = Auto-Desktop
$extraDesktop = Read-ExeList -Prompt "EXE adicionales para la PNY (Discord, WhatsApp, reproductores, etc.):"
foreach ($exe in $extraDesktop) { Add-Exe -List $desktop -Path $exe }

$vr = Auto-Vr
$extraVr = Read-ExeList -Prompt "EXE adicionales para la ASUS (SteamVR, OpenXR, runtimes VR, etc.):"
foreach ($exe in $extraVr) { Add-Exe -List $vr -Path $exe }

Section "APLICACIONES DETECTADAS"
Write-Host "PNY: $($desktop.Count)"
$desktop | ForEach-Object { Write-Host "  PNY  $_" }
Write-Host "ASUS/VR: $($vr.Count)"
$vr | ForEach-Object { Write-Host "  ASUS $_" }

$gameFolders = Read-FolderList -Prompt "Carpetas de juegos flat para la ASUS (Steam common, Games, etc.):"
$games = New-Object 'System.Collections.Generic.List[string]'
foreach ($folder in $gameFolders) {
    Add-ExeTree -List $games -Folder $folder -GameFolder
}

Section "JUEGOS DETECTADOS"
Write-Host "Candidatos ASUS: $($games.Count)"
$games | ForEach-Object { Write-Host "  ASUS $_" }

$answer = Read-Host "Aplicar esta configuracion? [S/n]"
if ($answer -match '^(n|no)$') {
    Write-Host "Cancelado. No se escribieron preferencias." -ForegroundColor Yellow
    exit 0
}

Section "ASIGNANDO PNY"
Section "DEFAULT GLOBAL DE ALTO RENDIMIENTO"
Set-GlobalHighPerformanceAdapter -Adapter $script:Pny.Adapter

foreach ($exe in $desktop) {
    Set-Preference -Exe $exe -Adapter $script:Pny.Adapter -GpuPreference $script:Pny.GpuPreference -Role "PNY"
}

Section "ASIGNANDO ASUS / VR"
foreach ($exe in $vr) {
    Set-Preference -Exe $exe -Adapter $script:Asus.Adapter -GpuPreference $script:Asus.GpuPreference -Role "ASUS/VR"
}

Section "ASIGNANDO ASUS / JUEGOS"
foreach ($exe in $games) {
    Set-Preference -Exe $exe -Adapter $script:Asus.Adapter -GpuPreference $script:Asus.GpuPreference -Role "ASUS/JUEGO"
}

Section "RESULTADO"
Write-Host "Cambios escritos: $($script:Changed.Count)" -ForegroundColor Green
Write-Host "Omitidos: $($script:Skipped.Count)" -ForegroundColor DarkYellow
$color = if ($script:Errors.Count -gt 0) { "Red" } else { "Green" }
Write-Host "Errores: $($script:Errors.Count)" -ForegroundColor $color
Write-Host "Cerrá y volvé a abrir las aplicaciones afectadas."
Write-Host "Backup: $backupPath"
Show-FinalPreferences
