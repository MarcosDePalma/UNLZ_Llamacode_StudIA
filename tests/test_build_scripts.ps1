$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$build = [IO.File]::ReadAllText((Join-Path $root 'build.bat'))
$auto  = [IO.File]::ReadAllText((Join-Path $root 'build_auto.bat'))
$tests = [IO.File]::ReadAllText((Join-Path $root 'tests.bat'))
$fails = 0

function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "  PASS $message" }
    else { Write-Host "  FAIL $message" -ForegroundColor Red; $script:fails++ }
}

Write-Host '== incremental build script policy =='
Check (-not $build.Contains('call "%~dp0bump-patch.bat"')) 'build does not mutate the version'
Check (-not $build.Contains('taskkill /F /IM MSBuild.exe')) 'build does not kill foreign MSBuild processes'
Check (-not $build.Contains('for /r "build" %%f in (*.tlog)')) 'build preserves MSBuild tracking logs'
Check ($build.Contains('if not exist CMakeCache.txt')) 'app configures only without a cache'
Check ($tests.Contains('if not exist build_tests\CMakeCache.txt')) 'tests configure only without a cache'
Check ($build.Contains('-DFETCHCONTENT_UPDATES_DISCONNECTED=ON')) 'dependency updates are disconnected after fetch'
Check ($tests.Contains('-DFETCHCONTENT_UPDATES_DISCONNECTED=ON')) 'test dependencies are disconnected after fetch'
Check ($build.Contains('Program Files (x86)\Microsoft Visual Studio\2022')) 'VS 2022 x86 install root is detected'
Check (-not $build.Contains('Generator changed from')) 'existing build trees are not forcibly migrated'
Check ($build.Contains('_deps\qtkeychain-subbuild\CMakeCache.txt')) 'interrupted configure reuses dependency generator'
Check ($build.Contains('if not exist CMakeFiles\VerifyGlobs.cmake set NEED_CONFIG=1')) 'partial configure is repaired'
Check ($build.Contains('Removing incompatible generated QtKeychain build metadata')) 'partial mixed-generator dependency is repaired'

# build_auto.bat es el que corren las IAs/CI: misma politica de generador que
# build.bat. Divergio una vez y el configure moria con "Does not match the
# generator used previously" solo por ese lado.
Write-Host '== build_auto.bat shares the generator policy =='
Check ($auto.Contains('Program Files (x86)\Microsoft Visual Studio\2022')) 'VS 2022 x86 install root is detected'
Check (-not $auto.Contains('Generator changed from')) 'existing build trees are not forcibly migrated'
Check ($auto.Contains('Removing incompatible generated QtKeychain build metadata')) 'mixed-generator dependency is repaired'
Check ($auto.Contains('-DFETCHCONTENT_UPDATES_DISCONNECTED=ON')) 'dependency updates are disconnected after fetch'
Check ($auto.Contains('if not exist CMakeFiles\VerifyGlobs.cmake set NEED_CONFIG=1')) 'partial configure is repaired'

# El chequeo del subbuild va FUERA del bloque NEED_CONFIG: un arbol ya
# configurado tambien puede tener el _deps del generador viejo.
foreach ($pair in @(@('build.bat', $build), @('build_auto.bat', $auto))) {
    $depIdx = $pair[1].IndexOf('_deps\qtkeychain-subbuild\CMakeCache.txt', $pair[1].IndexOf('set NEED_CONFIG=0'))
    $cfgIdx = $pair[1].IndexOf('if "%NEED_CONFIG%"=="1" (')
    Check ($depIdx -gt 0 -and $depIdx -lt $cfgIdx) "$($pair[0]) repairs the dependency generator before deciding to configure"
}

# Accesos directos: el build mantiene el .lnk del repo, los pins de la barra y
# -desde ahora- el del menu Inicio. El menu Inicio importa por dos razones: es
# de donde arranca la app quien no tiene el pin, y es lo unico que resuelven las
# herramientas que buscan apps por NOMBRE. Si el build no lo mantiene, queda
# apuntando a un build viejo (o directamente no existe) y nadie se entera.
Write-Host '== update-shortcut publishes to the Start Menu =='
$shortcutScript = Join-Path $root 'update-shortcut.ps1'
$sandbox = Join-Path ([IO.Path]::GetTempPath()) ("lc-shortcut-test-" + [guid]::NewGuid().ToString('N'))
$startMenu = Join-Path $sandbox 'StartMenu'
$repoLnk = Join-Path $sandbox 'LlamaCode-Debug.lnk'
New-Item -ItemType Directory -Force $startMenu | Out-Null
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $shortcutScript `
        -Config Debug -ShortcutName 'LlamaCode-Debug' -Icon 'assets\debug_icon.ico' `
        -ShortcutPath $repoLnk -StartMenuDir $startMenu | Out-Null

    $smLnk = Join-Path $startMenu 'LlamaCode-Debug.lnk'
    Check (Test-Path $smLnk) 'the Start Menu shortcut is created'
    if (Test-Path $smLnk) {
        $wsh = New-Object -ComObject WScript.Shell
        $sm = $wsh.CreateShortcut($smLnk)
        $expected = [IO.Path]::GetFullPath((Join-Path $root 'build\Debug\LlamaCode.exe'))
        Check ([string]::Equals([IO.Path]::GetFullPath($sm.TargetPath), $expected,
                                [StringComparison]::OrdinalIgnoreCase)) `
              'it points at the build it was asked for, not at cmd.exe'
        Check ($sm.IconLocation -match 'debug_icon') 'Debug keeps its own icon so both entries differ'
    }

    # Reapuntado: si el acceso quedo viejo, el build lo REPARA en vez de dejarlo
    # roto. Es todo el punto de que lo mantenga el build y no una mano.
    $stale = $wsh.CreateShortcut((Join-Path $startMenu 'LlamaCode-Debug.lnk'))
    $stale.TargetPath = 'C:\Windows\System32\cmd.exe'
    $stale.Save()
    & powershell -NoProfile -ExecutionPolicy Bypass -File $shortcutScript `
        -Config Debug -ShortcutName 'LlamaCode-Debug' -Icon 'assets\debug_icon.ico' `
        -ShortcutPath $repoLnk -StartMenuDir $startMenu | Out-Null
    $fixed = $wsh.CreateShortcut((Join-Path $startMenu 'LlamaCode-Debug.lnk'))
    Check ($fixed.TargetPath -notmatch 'cmd.exe') 'a stale Start Menu shortcut is repaired'

    # -NoStartMenu: los tests y CI no deben tocar el perfil de la maquina.
    $optOut = Join-Path $sandbox 'OptOut'
    New-Item -ItemType Directory -Force $optOut | Out-Null
    & powershell -NoProfile -ExecutionPolicy Bypass -File $shortcutScript `
        -Config Debug -ShortcutName 'LlamaCode-Debug' -Icon 'assets\debug_icon.ico' `
        -ShortcutPath $repoLnk -StartMenuDir $optOut -NoStartMenu | Out-Null
    Check (-not (Test-Path (Join-Path $optOut 'LlamaCode-Debug.lnk'))) `
          '-NoStartMenu skips it entirely'
} finally {
    Remove-Item -Recurse -Force $sandbox -ErrorAction SilentlyContinue
}

# Los dos accesos tienen que quedar publicados: Release y Debug conviven, y el
# usuario elige cual abre. Que el build publique solo uno es el bug que esto evita.
Write-Host '== both configurations are published =='
foreach ($pair in @(@('build.bat', $build), @('build_auto.bat', $auto))) {
    Check ($pair[1].Contains('-ShortcutName "LlamaCode"')) "$($pair[0]) publishes the Release entry"
    Check ($pair[1].Contains('-ShortcutName "LlamaCode-Debug"')) "$($pair[0]) publishes the Debug entry"
}

if ($fails) { throw "$fails build-script regression(s) failed" }
Write-Host 'All build-script regressions passed.'
