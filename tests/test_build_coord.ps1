<#
  test_build_coord.ps1 - regresion del encolamiento inteligente (build_coord.ps1).

  No corre en ctest (es infra de build en PS/batch, no C++). Correr a mano:
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_build_coord.ps1
  Exit 0 = todo verde.

  Cubre: fingerprint estable, OWNER exclusivo, robo inmediato al morir el dueno,
  rechazo de release ajeno, SHARE/REUSE, lanes independientes, deteccion de
  fuente mutando (acquire) y publicacion DIRTY (release). Ademas worktree.ps1.
#>
$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$coord  = Join-Path $root 'build_coord.ps1'
$lock   = Join-Path $root '.buildlock'
$fails  = 0
function Ok($cond,$msg){ if($cond){Write-Host "  PASS $msg"}else{Write-Host "  FAIL $msg" -Foreground Red; $script:fails++} }
function Clean(){ if(Test-Path $lock){ Remove-Item -Recurse -Force $lock -ErrorAction SilentlyContinue } }
Clean

# Corre acquire en background y devuelve el objeto Process (con out redirigido).
# MutationCheckMs 0 por default: el chequeo de mutacion agrega latencia y solo lo
# quiere el test que lo ejercita.
function StartAcquire($lane,$out,$timeout=60,$mutMs=0){
    $of = Join-Path $lock $out
    $rf = "$of.rc"
    New-Item -ItemType Directory -Force -Path $lock | Out-Null
    $cmd = "& '$coord' -Lane $lane -Action acquire -OwnerPid `$PID -TimeoutSec $timeout -MutationCheckMs $mutMs; Set-Content -Path '$rf' -Value `$LASTEXITCODE"
    return Start-Process powershell -PassThru -WindowStyle Hidden -RedirectStandardOutput $of `
        -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-Command',$cmd
}
# Espera al proceso y lee el exit code que dejo en su archivo .rc.
function ReadRC($proc,$out){
    $proc.WaitForExit(30000) | Out-Null
    $rf = (Join-Path $lock $out) + '.rc'
    for($i=0;$i -lt 20 -and -not (Test-Path $rf);$i++){ Start-Sleep -Milliseconds 100 }
    if(Test-Path $rf){ return [int]((Get-Content -Raw $rf).Trim()) } else { return $null }
}
function Acquire($lane,$mutMs=0){ & $coord -Lane $lane -Action acquire -OwnerPid $PID -MutationCheckMs $mutMs | Out-Null }
# git de limpieza: que falle es esperable (rama/worktree inexistente). Con
# $ErrorActionPreference='Stop' el stderr de un exe nativo aborta el script, asi
# que lo tragamos explicitamente.
function GitQuiet(){ $ErrorActionPreference='Continue'; & git @args 2>&1 | Out-Null; $global:LASTEXITCODE = 0 }
function Release($lane,$res='OK'){ & $coord -Lane $lane -Action release -Result $res -OwnerPid $PID | Out-Null }

Write-Host "== test 1: fingerprint estable ante bump semver =="
$fp = (& $coord -Lane build -Action fingerprint).Trim()
Ok ($fp.Length -eq 32) "fingerprint 32 chars ($fp)"
# Simula bump: escribo un archivo temporal en tests/ con un triple semver, recomputo.
$tmp = Join-Path $root 'tests\_fp_probe.cpp'
'version 1.2.3'  | Set-Content $tmp -Encoding ASCII
$fpA = (& $coord -Lane build -Action fingerprint).Trim()
'version 9.9.9'  | Set-Content $tmp -Encoding ASCII
$fpB = (& $coord -Lane build -Action fingerprint).Trim()
Remove-Item $tmp -Force
Ok ($fpA -eq $fpB) "solo cambia un triple semver -> mismo fingerprint (bump no invalida)"

Write-Host "== test 1b: el fingerprint ignora lo que no puede afectar al build =="
# tests/ tiene infra PowerShell (este mismo archivo) que CMake ni mira. Si contara
# en el fingerprint, editarla invalida un gate de C++ en curso -> DIRTY falso. Y un
# DIRTY falso ensena a ignorar los DIRTY de verdad.
$fpBase = (& $coord -Lane build -Action fingerprint).Trim()
$legacyPs = Get-Command powershell.exe -ErrorAction SilentlyContinue
if ($legacyPs) {
    $fpLegacy = (& powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
        "& '$coord' -Lane build -Action fingerprint").Trim()
    Ok ($fpLegacy -eq $fpBase) "fingerprint igual en Windows PowerShell 5.1 y pwsh 7"
}
$psProbe = Join-Path $root 'tests\_ignored_probe.ps1'
'Write-Host "infra, no fuente"' | Set-Content $psProbe -Encoding ASCII
$fpPs = (& $coord -Lane build -Action fingerprint).Trim()
Remove-Item $psProbe -Force
Ok ($fpPs -eq $fpBase) "tocar un .ps1 en tests/ NO cambia el fingerprint"
$mdProbe = Join-Path $root 'tests\_ignored_probe.md'
'# notas' | Set-Content $mdProbe -Encoding ASCII
$fpMd = (& $coord -Lane build -Action fingerprint).Trim()
Remove-Item $mdProbe -Force
Ok ($fpMd -eq $fpBase) "tocar un .md en tests/ NO cambia el fingerprint"
$pycProbe = Join-Path $root 'tests\_ignored_probe.pyc'
[IO.File]::WriteAllBytes($pycProbe, [byte[]](0, 1, 2, 3))
$fpPyc = (& $coord -Lane build -Action fingerprint).Trim()
Remove-Item $pycProbe -Force
Ok ($fpPyc -eq $fpBase) "tocar un .pyc en tests/ NO cambia el fingerprint"
# ...pero la fuente de verdad si tiene que contar (si no, DIRTY nunca salta).
$cppProbe = Join-Path $root 'tests\_counted_probe.cpp'
'int probe() { return 1; }' | Set-Content $cppProbe -Encoding ASCII
$fpCpp = (& $coord -Lane build -Action fingerprint).Trim()
Remove-Item $cppProbe -Force
Ok ($fpCpp -ne $fpBase) "tocar un .cpp SI cambia el fingerprint"

Write-Host "== test 2: OWNER exclusivo + REUSE (share) =="
Clean
Acquire 'build'
Ok (Test-Path (Join-Path $lock 'build.lock')) "lock creado por A"
$b = StartAcquire 'build' '_b.txt' 60                # B: mismo fp -> debe esperar
Start-Sleep -Seconds 3
Ok (-not $b.HasExited) "B espera (no roba el lock de A vivo)"
Release 'build' 'OK'                                 # A libera OK
$brc  = ReadRC $b '_b.txt'
$bout = Get-Content -Raw (Join-Path $lock '_b.txt')
Ok ($brc -eq 10) "B exit 10 (REUSE); actual=$brc"
Ok ($bout -match 'REUSE') "B tomo el path REUSE"

Write-Host "== test 3: robo de lock muerto =="
Clean
New-Item -ItemType Directory -Force -Path (Join-Path $lock 'build.lock') | Out-Null
# owner.json con PID inexistente y viejo
$dead = [ordered]@{ ownerPid=999999; ownerStartedUtc='2000-01-01T00:00:00Z'; host='x'; fingerprint=$fp; phase='building'; startedUtc=(Get-Date).ToUniversalTime().ToString('o') }
($dead | ConvertTo-Json -Compress) | Set-Content (Join-Path $lock 'build.lock\owner.json') -Encoding ASCII
$c = StartAcquire 'build' '_c.txt' 30                # debe robar y volverse OWNER
$crc = ReadRC $c '_c.txt'
$cout = Get-Content -Raw (Join-Path $lock '_c.txt')
Ok ($crc -eq 0) "acquire roba lock muerto y toma OWNER; actual=$crc"
Ok ($cout -match 'robando lock stale') "loguea el robo"
Release 'build' 'OK'

Write-Host "== test 4: lanes independientes (build vs tests) =="
Clean
Acquire 'build'
$t = StartAcquire 'tests' '_t.txt' 30                # otra lane -> NO debe bloquearse
$trc = ReadRC $t '_t.txt'
Ok ($trc -eq 0) "lane tests toma OWNER en paralelo a lane build; actual=$trc"
Release 'build' 'OK'
Release 'tests' 'OK'

Write-Host "== test 5: release ajeno rechazado =="
Clean
Acquire 'build'
& $coord -Lane build -Action release -Result OK -OwnerPid 999999 | Out-Null
Ok ($LASTEXITCODE -eq 1) "otro PID no puede liberar el lock"
Ok (Test-Path (Join-Path $lock 'build.lock')) "lock sigue presente tras release ajeno"
Release 'build' 'OK'

Write-Host "== test 6: release DIRTY si la fuente cambia durante el build =="
Clean
$probe = Join-Path $root 'tests\_dirty_probe.cpp'
Remove-Item $probe -Force -ErrorAction SilentlyContinue
Acquire 'build'                                      # A toma el lock con fp1
$fp1 = (& $coord -Lane build -Action fingerprint).Trim()
# Otra sesion edita el tree mientras A "compila" (cambio no-semver -> fp distinto).
'contenido nuevo de otra sesion' | Set-Content $probe -Encoding ASCII
& $coord -Lane build -Action release -Result OK -OwnerPid $PID | Out-Null
Ok ($LASTEXITCODE -eq 12) "release avisa DIRTY (exit 12) cuando la fuente cambio; actual=$LASTEXITCODE"
$rp = Join-Path $lock ("results\build.{0}.txt" -f $fp1)
$pub = if(Test-Path $rp){ (Get-Content -Raw $rp).Trim() } else { '<sin archivo>' }
Ok ($pub -like 'DIRTY|*') "publica DIRTY, no OK -> nadie lo adopta por REUSE (actual='$pub')"
Remove-Item $probe -Force -ErrorAction SilentlyContinue

Write-Host "== test 7: un sharer NO adopta un resultado DIRTY =="
Clean
Remove-Item $probe -Force -ErrorAction SilentlyContinue
Acquire 'build'                                      # A: OWNER con fp1
$d = StartAcquire 'build' '_d.txt' 60                # B: mismo fp -> espera a A
Start-Sleep -Seconds 3
Ok (-not $d.HasExited) "B espera el resultado de A"
'otra sesion escribiendo' | Set-Content $probe -Encoding ASCII   # el tree se mueve
& $coord -Lane build -Action release -Result OK -OwnerPid $PID | Out-Null  # -> DIRTY
$drc  = ReadRC $d '_d.txt'
$dout = Get-Content -Raw (Join-Path $lock '_d.txt')
Ok ($drc -eq 0) "B NO reusa (no exit 10): toma OWNER y compila lo suyo; actual=$drc"
Ok ($dout -match 'DIRTY') "B loguea por que descarto el build compartido"
Release 'build' 'OK'
Remove-Item $probe -Force -ErrorAction SilentlyContinue

Write-Host "== test 8: acquire detecta la fuente mutando bajo sus pies =="
Clean
Remove-Item $probe -Force -ErrorAction SilentlyContinue
# acquire con ventana de mutacion de 4s; escribo el tree dentro de esa ventana.
$e = StartAcquire 'build' '_e.txt' 30 4000
Start-Sleep -Milliseconds 1500
'otra sesion escribiendo durante el acquire' | Set-Content $probe -Encoding ASCII
$erc  = ReadRC $e '_e.txt'
$eout = Get-Content -Raw (Join-Path $lock '_e.txt')
Ok ($erc -eq 0) "sigue siendo OWNER (avisa, no aborta el build); actual=$erc"
Ok ($eout -match 'ADVERTENCIA: la fuente esta cambiando') "avisa ANTES de compilar"
Remove-Item $probe -Force -ErrorAction SilentlyContinue

Write-Host "== test 9: worktree.ps1 (aislamiento por sesion) =="
$wt   = Join-Path $root 'worktree.ps1'
$name = 'zz-coord-selftest'
$wtPath = Join-Path (Split-Path -Parent $root) "LlamaCode-$name"
# Limpieza defensiva por si una corrida anterior murio a la mitad.
if (Test-Path $wtPath) { GitQuiet -C $root worktree remove --force $wtPath }
GitQuiet -C $root branch -D "session/$name"

& $wt -Action new -Name 'Nombre Invalido' 2>&1 | Out-Null
Ok ($LASTEXITCODE -eq 1) "rechaza -Name invalido (no kebab-case)"

$newOut = (& $wt -Action new -Name $name 2>&1) -join "`n"
Ok ($LASTEXITCODE -eq 0) "crea el worktree; actual=$LASTEXITCODE"
Ok (Test-Path $wtPath) "el directorio del worktree existe"
$listOut = (& $wt -Action list 2>&1) -join "`n"
Ok ($listOut -match [regex]::Escape("LlamaCode-$name")) "aparece en -Action list"

# El lock del worktree nuevo es propio: no colisiona con el del tree principal.
# OJO: el worktree se crea desde HEAD, asi que su build_coord.ps1 es el COMMITEADO
# (puede no tener flags que todavia estan sin commitear). No le pases parametros
# nuevos aca: lo que se prueba es el aislamiento del lock, que no depende de eso.
Acquire 'build'
$wtLock = Join-Path $wtPath '.buildlock\build.lock'
& (Join-Path $wtPath 'build_coord.ps1') -Lane build -Action acquire -OwnerPid $PID | Out-Null
Ok ($LASTEXITCODE -eq 0) "el worktree toma OWNER en paralelo al tree principal; actual=$LASTEXITCODE"
Ok (Test-Path $wtLock) "usa su propio .buildlock (aislado, no el del tree principal)"
& (Join-Path $wtPath 'build_coord.ps1') -Lane build -Action release -Result OK -OwnerPid $PID | Out-Null
Release 'build' 'OK'

# remove sin -Force debe frenar si hay trabajo sin commitear.
'trabajo sin commitear' | Set-Content (Join-Path $wtPath 'tests\_wt_probe.tmp') -Encoding ASCII
& $wt -Action remove -Name $name 2>&1 | Out-Null
Ok ($LASTEXITCODE -eq 1) "remove sin -Force frena si hay cambios sin commitear"
Ok (Test-Path $wtPath) "y no borra nada"
& $wt -Action remove -Name $name -Force 2>&1 | Out-Null
Ok ($LASTEXITCODE -eq 0) "remove -Force limpia; actual=$LASTEXITCODE"
Ok (-not (Test-Path $wtPath)) "el directorio del worktree ya no existe"
GitQuiet -C $root branch -D "session/$name"

Clean
Write-Host ""
if($fails -eq 0){ Write-Host "=== ALL COORD TESTS PASSED ===" -Foreground Green; exit 0 }
else { Write-Host "=== $fails COORD TEST(S) FAILED ===" -Foreground Red; exit 1 }
