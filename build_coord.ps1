<#
  build_coord.ps1  -  Encolamiento inteligente de builds para sesiones paralelas.

  Varias IAs/CI pueden lanzar build_auto.bat / tests.bat a la vez. Este
  coordinador serializa por "lane" (build vs tests, dirs separados) usando un
  lock atomico (mkdir). Resuelve la carrera con tres desenlaces:

    OWNER  (exit 0)  -> tomaste el lock; compila vos y despues llama -Action release.
    REUSE  (exit 10) -> ya habia un build EN CURSO con el mismo fingerprint de
                        fuente y termino OK; adoptas su resultado, no recompilas.
    error  (exit 1)  -> timeout / fallo.
    Ademas: si el build compartido termino en FALLO, se devuelve OWNER para que
    reintentes desde cero (no queres adoptar un rojo silenciosamente).

  OJO - el lock serializa QUIEN compila, no QUE fuente hay en disco. Si dos
  sesiones comparten el mismo working tree, la otra puede editar src/ MIENTRAS
  vos compilas: tu binario no corresponde al fingerprint con el que tomaste el
  lock. Dos defensas (la de verdad es no compartir el tree: ver worktree.ps1):

    - acquire hace un chequeo de mutacion (dos fingerprints separados por
      -MutationCheckMs): si la fuente cambia bajo tus pies, avisa ANTES de
      gastar minutos de MSBuild y comerte un error ajeno. -MutationCheckMs 0
      lo apaga.
    - release revalida el fingerprint: si la fuente cambio durante el build,
      publica DIRTY en vez de OK (exit 12). Nadie adopta por REUSE un artefacto
      que no corresponde a la fuente que dice, y el owner se entera de que su
      binario/gate no es confiable.

  El "fingerprint" es un hash del contenido de src/ qml/ tests/ CMakeLists.txt con
  los triples semver (X.Y.Z) neutralizados, para que el auto-bump de version NO
  cuente como cambio de fuente (si contara, cada sharer creeria ver un arbol
  distinto y recompilaria al pedo).

  Locks muertos se roban de inmediato usando la identidad del proceso real que
  ejecuta el .bat (PID + hora de creacion, para resistir reutilizacion de PID).
  No usa procesos guardianes ni sleeps como senal de vida.

  Uso:
    powershell ... build_coord.ps1 -Lane build  -Action acquire [-TimeoutSec 1800]
    powershell ... build_coord.ps1 -Lane build  -Action release -Result OK|FAIL
    powershell ... build_coord.ps1 -Lane tests -Action fingerprint   (debug)
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('build','tests')] [string]$Lane,
    [Parameter(Mandatory=$true)][ValidateSet('acquire','release','fingerprint','status')] [string]$Action,
    [ValidateSet('OK','FAIL')] [string]$Result = 'OK',
    [int]$OwnerPid = 0,
    [int]$TimeoutSec = 1800,
    [int]$StaleSec   = 3600,
    [int]$MutationCheckMs = 1200
)

$ErrorActionPreference = 'Stop'
$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$lockRoot = Join-Path $root '.buildlock'
$lockDir  = Join-Path $lockRoot ("{0}.lock" -f $Lane)      # mkdir atomico = el lock
$ownerJson= Join-Path $lockDir 'owner.json'
$resultDir= Join-Path $lockRoot 'results'

function Log([string]$m) { Write-Host ("[coord:{0}] {1}" -f $Lane, $m) }

# --- fingerprint de la fuente (semver neutralizado) --------------------------
# Extensiones que NO pueden afectar el build/ctest de C++ aunque vivan en
# src/ qml/ tests/. Sin esto, editar tests\test_build_coord.ps1 (infra PowerShell,
# que ni CMake mira) invalida el gate de C++ que esta corriendo: DIRTY falso, y un
# DIRTY falso ensena a ignorar los DIRTY de verdad. Blacklist y no whitelist a
# proposito: excluir de mas seria un DIRTY que NO salta (verde mentiroso), que es
# el error caro; incluir de mas solo cuesta un re-run.
# Los tests Python generan bytecode en tests/__pycache__/ durante la corrida;
# no es fuente ni debe convertir un gate verde en DIRTY.
$script:FpIgnoreExt = @('.ps1', '.md', '.log', '.tmp', '.claim', '.bat', '.pyc', '.pyo')

function Get-Fingerprint {
    $targets = @('src','qml','tests') | ForEach-Object { Join-Path $root $_ }
    $files = @()
    foreach ($t in $targets) {
        if (Test-Path $t) {
            $files += Get-ChildItem -Path $t -Recurse -File -ErrorAction SilentlyContinue |
                      Where-Object { $FpIgnoreExt -notcontains $_.Extension.ToLower() }
        }
    }
    $cmake = Join-Path $root 'CMakeLists.txt'
    if (Test-Path $cmake) { $files += Get-Item $cmake }

    # No depender de Sort-Object: PowerShell 5.1 y 7 pueden ordenar igual texto
    # de paths con reglas de cultura distintas. El owner suele ser pwsh y los
    # sharers de los .bat pueden ser powershell.exe; si el orden cambia, una
    # misma fuente parece otra y nunca llega a REUSE. SortedDictionary Ordinal
    # mantiene el fingerprint estable entre ambas versiones.
    $sorted = New-Object 'System.Collections.Generic.SortedDictionary[string,string]' ([System.StringComparer]::Ordinal)
    foreach ($f in $files) {
        try {
            $txt = [IO.File]::ReadAllText($f.FullName)
        } catch { continue }
        # Neutraliza triples semver (X.Y.Z) para que el bump de version no cuente.
        $txt = [regex]::Replace($txt, '\d+\.\d+\.\d+', 'V')
        $rel = $f.FullName.Substring($root.Length)
        $sorted[$rel] = $txt
    }
    $sb = [System.Text.StringBuilder]::new()
    foreach ($item in $sorted.GetEnumerator()) {
        [void]$sb.Append($item.Key).Append("|").Append($item.Value).Append("`n")
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes($sb.ToString())
    $sha   = [Security.Cryptography.SHA256]::Create()
    $hash  = $sha.ComputeHash($bytes)
    return ([BitConverter]::ToString($hash) -replace '-','').Substring(0,32)
}

function Test-Alive([int]$procId) {
    if (-not $procId) { return $false }
    try { return [bool](Get-Process -Id $procId -ErrorAction Stop) } catch { return $false }
}

function Get-ProcessIdentity([int]$procId) {
    if (-not $procId) { return $null }
    try {
        $p = Get-Process -Id $procId -ErrorAction Stop
        return [pscustomobject]@{
            pid = $p.Id
            startedUtc = $p.StartTime.ToUniversalTime().ToString('o')
        }
    } catch { return $null }
}

function Test-OwnerAlive($owner) {
    if (-not $owner -or -not $owner.ownerPid -or -not $owner.ownerStartedUtc) {
        return $false # formato legacy/guardian: stale por diseno
    }
    $live = Get-ProcessIdentity ([int]$owner.ownerPid)
    return $live -and $live.startedUtc -eq [string]$owner.ownerStartedUtc
}

function Read-Owner {
    if (-not (Test-Path $ownerJson)) { return $null }
    try { return Get-Content -Raw $ownerJson | ConvertFrom-Json } catch { return $null }
}

function Remove-Lock {
    try { Remove-Item -Recurse -Force $lockDir -ErrorAction Stop } catch {}
}

function Result-Path([string]$fp) { Join-Path $resultDir ("{0}.{1}.txt" -f $Lane, $fp) }

# Publicar el resultado como reemplazo atómico. Set-Content directo deja una
# ventana en la que el archivo ya existe pero todavía está vacío/parcial; un
# sharer puede leerlo, clasificarlo como FALLO y robar el turno en vez de hacer
# REUSE (o descartar correctamente un DIRTY). El temporal vive en el mismo
# directorio para que Move reemplace dentro del mismo volumen.
function Publish-Result([string]$path, [string]$content) {
    $tmp = "$path.$PID.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        [System.IO.File]::WriteAllText($tmp, $content, [System.Text.Encoding]::ASCII)
        # File.Move(source, destination, overwrite) sólo existe en .NET Core /
        # PowerShell 7. Los .bat lanzan Windows PowerShell 5.1, donde ese
        # overload falla y el catch de release dejaría al sharer sin resultado.
        # Replace es atómico cuando ya existe el destino; Move también lo es
        # para la primera publicación dentro del mismo directorio.
        if ([System.IO.File]::Exists($path)) {
            [System.IO.File]::Replace($tmp, $path, $null, $true)
        } else {
            [System.IO.File]::Move($tmp, $path)
        }
    } finally {
        if (Test-Path -LiteralPath $tmp) {
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        }
    }
}

# =============================================================================
if ($Action -eq 'fingerprint') {
    Get-Fingerprint
    exit 0
}

if ($Action -eq 'status') {
    $owner = Read-Owner
    if (-not $owner) { Log 'FREE'; exit 0 }
    $alive = Test-OwnerAlive $owner
    Log ("{0} owner={1} fingerprint={2} started={3}" -f `
         $(if ($alive) {'BUSY'} else {'STALE'}), $owner.ownerPid,
         $owner.fingerprint, $owner.startedUtc)
    exit $(if ($alive) {10} else {11})
}

New-Item -ItemType Directory -Force -Path $lockRoot  | Out-Null
New-Item -ItemType Directory -Force -Path $resultDir | Out-Null

# --- release -----------------------------------------------------------------
if ($Action -eq 'release') {
    $me = Read-Owner
    if (-not $me) { Log 'release ignorado: lock inexistente'; exit 0 }
    if (-not $OwnerPid -or [int]$me.ownerPid -ne $OwnerPid) {
        Log ("release rechazado: caller={0}, owner={1}" -f $OwnerPid, $me.ownerPid)
        exit 1
    }
    # Revalida la fuente: si cambio durante el build, lo que compilaste NO es el
    # fingerprint con el que tomaste el lock. Publicar OK bajo esa etiqueta haria
    # que otra sesion adopte por REUSE un artefacto de otra fuente.
    $dirty = $false
    if ($me.fingerprint) {
        $fpNow = Get-Fingerprint
        $dirty = ($fpNow -ne [string]$me.fingerprint)
        if ($dirty) {
            # No acusar a "otra sesion": el DIRTY puede ser tuyo (editar mientras
            # compilas es lo mas comun de todo). Decir quien fue sin saberlo hace
            # que el aviso se lea como ruido y se ignore.
            Log ("DIRTY: la fuente cambio durante el build ({0} -> {1})." -f $me.fingerprint, $fpNow)
            Log  '       Alguien edito el tree mientras corria: otra sesion, o vos mismo.'
            Log  '       El artefacto/gate NO corresponde a esta fuente.'
        }
        # Publica el resultado para que los sharers en espera lo adopten. Solo un
        # OK limpio es adoptable; DIRTY/FAIL los manda a compilar lo suyo.
        $published = $(if ($dirty) { 'DIRTY' } else { $Result })
        $rp = Result-Path $me.fingerprint
        try {
            $line = "{0}|{1}|{2}" -f $published, (Get-Date).ToUniversalTime().ToString('o'), $PID
            Publish-Result $rp $line
        } catch {}
    }
    Remove-Lock
    Log ("released ({0})" -f $(if ($dirty) { 'DIRTY' } else { $Result }))
    exit $(if ($dirty) { 12 } else { 0 })
}

# --- acquire -----------------------------------------------------------------
$myFp   = Get-Fingerprint
$myOwner = Get-ProcessIdentity $OwnerPid
if (-not $myOwner) { Log ("OwnerPid invalido o muerto: {0}" -f $OwnerPid); exit 1 }
$deadline = (Get-Date).AddSeconds($TimeoutSec)
Log ("fingerprint {0}" -f $myFp)

$queueSince = $null   # desde cuando espero en QUEUE (para el log throttleado)
$queueLastLog = $null

while ($true) {
    if ((Get-Date) -gt $deadline) { Log 'timeout esperando el lock'; exit 1 }

    # Intento tomar el lock (mkdir atomico).
    $got = $false
    try { New-Item -ItemType Directory -Path $lockDir -ErrorAction Stop | Out-Null; $got = $true }
    catch { $got = $false }

    if ($got) {
        # Chequeo de mutacion: si otra sesion esta escribiendo src/ AHORA, mejor
        # enterarse aca que despues de 4 minutos de MSBuild con un error ajeno.
        # Re-fingerprinteo y sigo con el valor asentado (el de release compara
        # contra este), pero lo grito: el tree esta compartido y en movimiento.
        if ($MutationCheckMs -gt 0) {
            Start-Sleep -Milliseconds $MutationCheckMs
            $fp2 = Get-Fingerprint
            if ($fp2 -ne $myFp) {
                Log ('ADVERTENCIA: la fuente esta cambiando ahora mismo ({0} -> {1}).' -f $myFp, $fp2)
                Log  '             Otra sesion escribe el working tree; podes comerte'
                Log  '             un error de compilacion ajeno. Ver worktree.ps1.'
                $myFp = $fp2
            }
        }
        # Borro cualquier resultado viejo de esta fuente ANTES de publicar owner.json,
        # asi un sharer que entre despues no adopta un resultado stale de una corrida
        # anterior en vez de esperar la mia.
        Remove-Item -Force (Result-Path $myFp) -ErrorAction SilentlyContinue
        $owner = [ordered]@{
            ownerPid    = $myOwner.pid
            ownerStartedUtc = $myOwner.startedUtc
            host        = $env:COMPUTERNAME
            fingerprint = $myFp
            phase       = 'building'
            startedUtc  = (Get-Date).ToUniversalTime().ToString('o')
        }
        ($owner | ConvertTo-Json -Compress) | Set-Content -Path $ownerJson -Encoding ASCII
        Log ('OWNER (lock tomado, pid={0}) -> compilando' -f $myOwner.pid)
        exit 0
    }

    # Lock ocupado: inspecciono al owner.
    $owner = Read-Owner
    if (-not $owner) {
        # Lock a medio escribir; espero un toque y reintento.
        Start-Sleep -Milliseconds 400
        continue
    }

    # Lock muerto o vencido -> lo robo.
    $started = $null
    try { $started = [datetime]::Parse($owner.startedUtc).ToUniversalTime() } catch {}
    $expired = $started -and ((Get-Date).ToUniversalTime() - $started).TotalSeconds -gt $StaleSec
    $ownerDead = -not (Test-OwnerAlive $owner)
    if ($ownerDead -or $expired) {
        Log ("robando lock stale (owner={0}, ownerDead={1}, expired={2})" `
             -f $owner.ownerPid, $ownerDead, $expired)
        Remove-Lock
        Start-Sleep -Milliseconds 200
        continue
    }

    if ($owner.fingerprint -eq $myFp) {
        # -- SHARE: hay un build en curso con la MISMA fuente. Espero su resultado.
        Log ("build en curso con misma fuente (pid={0}); esperando para reusar..." -f $owner.ownerPid)
        $rp = Result-Path $myFp
        while ($true) {
            if ((Get-Date) -gt $deadline) { Log 'timeout esperando build compartido'; exit 1 }
            if (Test-Path $rp) {
                $line = (Get-Content -Raw $rp).Trim()
                if     ($line -like 'OK|*')    { Log 'REUSE: build compartido OK'; exit 10 }
                elseif ($line -like 'DIRTY|*') { Log 'build compartido quedo DIRTY (la fuente cambio bajo el) -> compilo lo mio'; Remove-Item -Force $rp -ErrorAction SilentlyContinue; break }
                else                           { Log 'build compartido FALLO -> reintento propio'; Remove-Item -Force $rp -ErrorAction SilentlyContinue; break }
            }
            # El owner libero sin dejar resultado (crash): reintento acquire.
            if (-not (Test-Path $lockDir)) { Log 'owner libero sin resultado -> reintento'; break }
            $cur = Read-Owner
            if ($cur -and $cur.fingerprint -ne $myFp) { Log 'owner cambio de fuente -> reintento'; break }
            Start-Sleep -Seconds 2
        }
        continue
    }
    else {
        # -- QUEUE: build en curso con OTRA fuente. Espero mi turno.
        # Loguear cada vuelta (2s) inunda la consola con la misma linea; aviso al
        # entrar y despues cada 30s, con cuanto llevo esperando.
        if (-not $queueSince) {
            $queueSince = Get-Date
            $queueLastLog = Get-Date
            Log ("build en curso con OTRA fuente (pid={0}); en cola..." -f $owner.ownerPid)
        }
        elseif (((Get-Date) - $queueLastLog).TotalSeconds -ge 30) {
            $queueLastLog = Get-Date
            Log ("en cola hace {0:n0}s detras del pid {1}..." -f `
                 ((Get-Date) - $queueSince).TotalSeconds, $owner.ownerPid)
        }
        Start-Sleep -Seconds 2
        continue
    }
}
