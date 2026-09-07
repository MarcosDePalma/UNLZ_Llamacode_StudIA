<#
.SYNOPSIS
    LlamaCode zero-to-running bootstrap for Windows.

.DESCRIPTION
    Installs every dependency (git, Python, CMake, VS Build Tools 2022 v143,
    Qt 6.8.3 msvc2022_64), clones the repo into an isolated folder, builds, and
    deploys the Qt runtime next to the binary.

.EXAMPLE
    irm https://raw.githubusercontent.com/cristianlukas/UNLZ_Llamacode/main/scripts/bootstrap.ps1 | iex

.NOTES
    Override defaults via env vars before running:
      $env:LC_DIR     = "C:\path\to\install"   # default: %USERPROFILE%\LlamaCode
      $env:LC_BRANCH  = "main"
      $env:LC_CONFIG  = "Debug"                 # Debug (default) | Release
      $env:LC_NORUN   = "1"                      # skip launching at the end
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ── Config ──────────────────────────────────────────────────────────────────
$Repo    = 'https://github.com/cristianlukas/UNLZ_Llamacode.git'
$Dir     = if ($env:LC_DIR)    { $env:LC_DIR }    else { Join-Path $env:USERPROFILE 'LlamaCode' }
$Branch  = if ($env:LC_BRANCH) { $env:LC_BRANCH } else { 'main' }
$Config  = if ($env:LC_CONFIG) { $env:LC_CONFIG } else { 'Debug' }
$QtVer   = '6.8.3'
$QtArch  = 'win64_msvc2022_64'
$QtDir   = "C:\Qt\$QtVer\msvc2022_64"
$QtAqtModules = @('qtmultimedia')
$QtRequiredComponents = @('Core', 'Quick', 'Sql', 'Concurrent', 'Network', 'Widgets', 'Multimedia', 'Svg')

function Info($m)  { Write-Host "[*] $m"  -ForegroundColor Cyan }
function Ok($m)    { Write-Host "[OK] $m" -ForegroundColor Green }
function Die($m)   { Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

function Have($cmd) { [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }

function Refresh-Path {
    $machine = [Environment]::GetEnvironmentVariable('Path','Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path','User')
    $env:Path = "$machine;$user"
}

function Get-MissingQtComponents {
    $missing = @()
    foreach ($component in $QtRequiredComponents) {
        $config = Join-Path $QtDir "lib\cmake\Qt6$component\Qt6$component`Config.cmake"
        if (-not (Test-Path $config)) {
            $missing += $component
        }
    }
    return $missing
}

function New-LlamaCodeShortcut {
    param(
        [Parameter(Mandatory = $true)][string]$ShortcutPath,
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [string]$IconPath
    )

    $shortcutDir = Split-Path -Parent $ShortcutPath
    if ($shortcutDir -and -not (Test-Path $shortcutDir)) {
        New-Item -ItemType Directory -Force -Path $shortcutDir | Out-Null
    }

    $wsh = New-Object -ComObject WScript.Shell
    $shortcut = $wsh.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.Arguments = ''
    $shortcut.WorkingDirectory = $WorkingDirectory
    $shortcut.Description = 'UNLZ LlamaCode'
    if ($IconPath -and (Test-Path $IconPath)) {
        $shortcut.IconLocation = "$IconPath,0"
    } else {
        $shortcut.IconLocation = "$TargetPath,0"
    }
    $shortcut.Save()
}

function Stop-LlamaCodeProcesses {
    $processNames = @('LlamaCode', 'llama-server', 'opencode', 'aider')
    foreach ($name in $processNames) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object {
            Info "Stopping running process $($_.ProcessName) (PID $($_.Id))..."
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 800
}

Write-Host ""
Write-Host "=== LlamaCode bootstrap (Windows) ===" -ForegroundColor Magenta
Write-Host "Target: $Dir  branch=$Branch  config=$Config"
Write-Host ""

# ── winget (required, cannot self-install) ──────────────────────────────────
if (-not (Have winget)) {
    Die "winget not found. Install 'App Installer' from the Microsoft Store, then re-run."
}

# ── git ─────────────────────────────────────────────────────────────────────
if (-not (Have git)) {
    Info "Installing Git..."
    winget install --id Git.Git --exact --source winget --accept-source-agreements --accept-package-agreements
    Refresh-Path
}
if (-not (Have git)) { Die "Git not on PATH after install. Open a new terminal and re-run." }
Ok "git"

# ── Python ──────────────────────────────────────────────────────────────────
if (-not (Have python)) {
    Info "Installing Python..."
    winget install --id Python.Python.3.12 --exact --source winget --accept-source-agreements --accept-package-agreements
    Refresh-Path
}
if (-not (Have python)) { Die "Python not on PATH after install. Open a new terminal and re-run." }
Ok "python"

# ── CMake ───────────────────────────────────────────────────────────────────
$CMake = "$env:ProgramFiles\CMake\bin\cmake.exe"
if (-not (Test-Path $CMake)) {
    if (Have cmake) { $CMake = (Get-Command cmake).Source }
    else {
        Info "Installing CMake..."
        winget install --id Kitware.CMake --exact --source winget --accept-source-agreements --accept-package-agreements
        Refresh-Path
        if (-not (Test-Path $CMake)) { if (Have cmake) { $CMake = (Get-Command cmake).Source } }
    }
}
if (-not (Test-Path $CMake)) { Die "CMake not found after install." }
Ok "cmake: $CMake"

# ── Visual Studio Build Tools 2022 (v143) ───────────────────────────────────
$HasVs = (Test-Path "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe") -or `
         (Test-Path "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe")
if (-not $HasVs) {
    Info "Installing Visual Studio Build Tools 2022 (v143) -- this is large, be patient..."
    winget install --id Microsoft.VisualStudio.2022.BuildTools --exact `
        --override "--quiet --wait --norestart --nocache --installPath C:\BuildTools2022 --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.v143.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621" `
        --accept-source-agreements --accept-package-agreements
}
$Generator = if (Test-Path "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe") { "Visual Studio 17 2022" }
             elseif (Test-Path "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe") { "Visual Studio 17 2022" }
             else { Die "VS Build Tools 2022 not found after install." }
Ok "VS Build Tools 2022 ($Generator)"

# ── Qt 6.8.3 (msvc2022_64) via aqtinstall ───────────────────────────────────
$QtBaseConfig = "$QtDir\lib\cmake\Qt6\Qt6Config.cmake"
if (-not (Test-Path $QtBaseConfig)) {
    Info "Installing Qt $QtVer ($QtArch) via aqtinstall..."
    python -m pip install --user --upgrade aqtinstall
    python -m aqt install-qt windows desktop $QtVer $QtArch -O C:\Qt --modules $QtAqtModules
} else {
    $MissingQtComponents = @(Get-MissingQtComponents)
    if ($MissingQtComponents.Count -gt 0) {
        Info "Qt exists but is missing required components: $($MissingQtComponents -join ', '). Installing add-on modules..."
        python -m pip install --user --upgrade aqtinstall
        python -m aqt install-qt windows desktop $QtVer $QtArch -O C:\Qt --modules $QtAqtModules
    }
}
if (-not (Test-Path $QtBaseConfig)) { Die "Qt6 not found at $QtDir after install." }
$MissingQtComponents = @(Get-MissingQtComponents)
if ($MissingQtComponents.Count -gt 0) {
    Die "Qt6 at $QtDir is incomplete. Missing components: $($MissingQtComponents -join ', ')."
}
Ok "Qt6: $QtDir"

# ── Clone / update ──────────────────────────────────────────────────────────
if (Test-Path (Join-Path $Dir '.git')) {
    # 'Actualizar ahora' pasa LC_DIR con la instalacion que esta corriendo, que
    # puede ser un checkout de trabajo: el reset --hard de abajo se llevaria
    # puesto todo lo no commiteado. Abortar salvo LC_FORCE=1.
    $Dirty = @(git -C $Dir status --porcelain | Where-Object { $_ })
    if ($Dirty.Count -gt 0 -and -not $env:LC_FORCE) {
        Write-Host ""
        Write-Host "[ERROR] $Dir tiene $($Dirty.Count) archivo(s) sin commitear." -ForegroundColor Red
        $Dirty | Select-Object -First 10 | ForEach-Object { Write-Host "        $_" }
        Write-Host "        Commitealos (o corre con LC_FORCE=1 para descartarlos)." -ForegroundColor Red
        exit 1
    }
    Info "Repo exists -- pulling latest..."
    git -C $Dir fetch --depth 1 origin $Branch
    git -C $Dir checkout $Branch
    git -C $Dir reset --hard "origin/$Branch"
} else {
    Info "Cloning into $Dir ..."
    git clone --depth 1 --branch $Branch $Repo $Dir
}
Ok "source ready"

# ── Build ───────────────────────────────────────────────────────────────────
$BuildDir = Join-Path $Dir 'build'
Info "Configuring..."
& $CMake -S $Dir -B $BuildDir -G $Generator -A x64 -DCMAKE_PREFIX_PATH="$QtDir"
if ($LASTEXITCODE -ne 0) { Die "CMake configure failed." }

# Recien aca se cierra la app: el link necesita el exe libre, pero si algo de
# arriba falla el usuario se queda con su version vieja andando.
Stop-LlamaCodeProcesses
Info "Building ($Config)..."
& $CMake --build $BuildDir --config $Config -- /maxcpucount
if ($LASTEXITCODE -ne 0) { Die "Build failed." }

# ── Deploy Qt runtime ───────────────────────────────────────────────────────
$ExeDir  = Join-Path $BuildDir $Config
$ExePath = Join-Path $ExeDir 'LlamaCode.exe'
if (-not (Test-Path $ExePath)) { Die "Built exe missing at $ExePath" }

$DeployFlag = if ($Config -ieq 'Debug') { '--debug' } else { '--release' }
Info "Deploying Qt runtime..."
& "$QtDir\bin\windeployqt.exe" $DeployFlag --qmldir (Join-Path $Dir 'qml') --no-translations --compiler-runtime $ExePath
# Qt.labs.settings is not always picked up by windeployqt.
$LabsSrc = "$QtDir\qml\Qt\labs\settings"
if (Test-Path $LabsSrc) {
    Copy-Item -Recurse -Force $LabsSrc (Join-Path $ExeDir 'qml\Qt\labs\settings')
}

# Make the app discoverable from Windows Start search. The project-root shortcut
# is convenient for manual inspection; the Start Menu shortcut is what Windows
# indexes as an installed app for the current user.
$ShortcutName = if ($Config -ieq 'Debug') { 'LlamaCode-Debug' } else { 'LlamaCode' }
$IconRel = if ($Config -ieq 'Debug') { 'assets\debug_icon.ico' } else { 'assets\app_icon.ico' }
$IconPath = Join-Path $Dir $IconRel
$ProjectShortcut = Join-Path $Dir "$ShortcutName.lnk"
$StartMenuDir = [Environment]::GetFolderPath('Programs')
if ([string]::IsNullOrWhiteSpace($StartMenuDir)) {
    $StartMenuDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
}
$StartMenuShortcut = Join-Path $StartMenuDir "$ShortcutName.lnk"
Info "Creating shortcuts..."
New-LlamaCodeShortcut -ShortcutPath $ProjectShortcut -TargetPath $ExePath -WorkingDirectory $ExeDir -IconPath $IconPath
New-LlamaCodeShortcut -ShortcutPath $StartMenuShortcut -TargetPath $ExePath -WorkingDirectory $ExeDir -IconPath $IconPath

Write-Host ""
Ok "Done. Binary: $ExePath"
Ok "Start Menu shortcut: $StartMenuShortcut"
Write-Host ""

if (-not $env:LC_NORUN) {
    Info "Launching LlamaCode..."
    Start-Process -FilePath $ExePath -WorkingDirectory $ExeDir
}
