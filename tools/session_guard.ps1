<#
  session_guard.ps1 - hooks de convivencia entre sesiones que comparten el tree.

  El problema: build_coord.ps1 serializa builds y worktree.ps1 aisla trees, pero
  las dos son opt-in - hay que ACORDARSE de usarlas. Los choques reales (dos
  sesiones editando el mismo archivo, un `git checkout --` que se come trabajo
  ajeno, "modified on disk") pasan antes y sin que nadie se acuerde de nada. Los
  hooks los ejecuta el harness solo, asi que son el unico lugar donde esto se
  aplica sin disciplina humana.

  Tres modos (los engancha .claude/settings.json):

    -Mode session-start   SessionStart. Si el tree ya tiene trabajo sin commitear,
                          lo lista y sugiere aislarse en un worktree. Informativo.

    -Mode edit-claim      PreToolUse(Edit|Write). Registra que esta sesion toca el
                          archivo y avisa si otra sesion VIVA lo toco antes.
                          Avisa, NO bloquea: un claim stale no debe trabar trabajo
                          legitimo, y el costo de un choque de edicion es un merge,
                          no una perdida.

    -Mode git-guard       PreToolUse(Bash|PowerShell). BLOQUEA git destructivo
                          (checkout --/restore/stash/reset --hard/clean/add -A)
                          cuando el tree tiene trabajo sin commitear. Este si
                          bloquea: es el unico caso irreversible - si se lleva
                          puesto el trabajo de otra sesion no hay undo. Un falso
                          positivo cuesta un rodeo (hacerlo por path); un falso
                          negativo cuesta horas ajenas.

  Contrato de hooks: el input llega por stdin como JSON; la salida es JSON por
  stdout. Exit 0 siempre (un hook que revienta no debe romper la sesion).

  Y un modo para humanos (NO va en settings.json, no lee stdin, imprime texto):

    -Mode status          Quien esta tocando que, ahora. Sale del mismo registry
                          de claims que llena edit-claim. Lo importante no es la
                          lista de sesiones sino los SOLAPAMIENTOS: un archivo
                          reclamado por dos sesiones vivas es un merge esperando.
                          Exit 0 = sin solapamientos; 1 = hay solapamientos.
                              powershell -File tools\session_guard.ps1 -Mode status
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('session-start','edit-claim','git-guard','status')] [string]$Mode
)

# Un hook nunca debe tumbar la sesion: ante cualquier sorpresa, salir en silencio.
$ErrorActionPreference = 'Continue'

$root      = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$claimDir  = Join-Path $root '.buildlock\claims'
$staleMin  = 90     # un claim mas viejo que esto ya no representa a nadie

function Git-Run() {
    $ErrorActionPreference = 'Continue'
    $out = & git -C $root @args 2>&1
    return [pscustomobject]@{ rc = $LASTEXITCODE; out = ($out | Out-String).Trim() }
}

# Como Git-Run pero devuelve las lineas crudas, SIN Trim() global. Para
# --porcelain el Trim() es destructivo: el formato es 'XY <path>' y X puede ser un
# espacio (' M CMakeLists.txt'), asi que trimear el blob entero le come el espacio
# a la PRIMERA linea y despues el parser se lleva puesta la primera letra del path
# ('CMakeLists.txt' -> 'MakeLists.txt').
function Git-Lines() {
    $ErrorActionPreference = 'Continue'
    $out = & git -C $root @args 2>&1
    if ($LASTEXITCODE -ne 0) { return @() }
    return @($out | ForEach-Object { [string]$_ })
}

# Archivos con cambios sin commitear (tracked + untracked), como lista de paths.
function Get-DirtyPaths() {
    return @(Git-Lines status --porcelain | ForEach-Object {
        $line = $_.TrimEnd()
        if ($line -match '^(..)\s(.+)$') {
            $p = $Matches[2].Trim().Trim('"')
            # Renombres vienen como 'old -> new': nos interesa el destino.
            if ($p -match '\s->\s(.+)$') { $p = $Matches[1].Trim().Trim('"') }
            $p
        }
    } | Where-Object { $_ })
}

function Read-StdinJson() {
    $raw = [Console]::In.ReadToEnd()
    if (-not $raw) { return $null }
    try { return $raw | ConvertFrom-Json } catch { return $null }
}

# Los hooks se comunican por JSON en stdout. additionalContext = texto que entra
# al contexto del modelo; permissionDecision 'deny' aborta la tool call.
function Emit-Context([string]$text) {
    @{ hookSpecificOutput = @{ hookEventName = 'PreToolUse'; additionalContext = $text } } |
        ConvertTo-Json -Depth 5 -Compress | Write-Output
}
function Emit-Deny([string]$reason) {
    @{ hookSpecificOutput = @{
           hookEventName            = 'PreToolUse'
           permissionDecision       = 'deny'
           permissionDecisionReason = $reason
       } } | ConvertTo-Json -Depth 5 -Compress | Write-Output
}

# =============================================================================
if ($Mode -eq 'status') {
    # El claim guarda el path completo como contenido y el mtime hace de heartbeat.
    $claims = @()
    if (Test-Path $claimDir) {
        $claims = @(Get-ChildItem $claimDir -Filter '*.claim' -File -ErrorAction SilentlyContinue |
            Where-Object { ((Get-Date) - $_.LastWriteTime).TotalMinutes -lt $staleMin } |
            ForEach-Object {
                $sid = ($_.Name -split '__')[0]
                [pscustomobject]@{
                    sid  = $sid
                    file = (Get-Content -Raw $_.FullName -ErrorAction SilentlyContinue).Trim()
                    mins = [int]((Get-Date) - $_.LastWriteTime).TotalMinutes
                }
            } | Where-Object { $_.file })
    }

    Write-Output "== Sesiones activas (claims de menos de $staleMin min) =="
    if (-not $claims) {
        Write-Output "  (ninguna: nadie edito nada recientemente en este tree)"
    }
    foreach ($g in ($claims | Group-Object sid | Sort-Object { ($_.Group | Measure-Object mins -Minimum).Minimum })) {
        $last = ($g.Group | Measure-Object mins -Minimum).Minimum
        $tag  = if ($g.Name -eq $env:CLAUDE_SESSION_ID) { ' (yo)' } else { '' }
        Write-Output ("  {0}{1} - {2} archivo(s), ultimo toque hace {3} min" -f $g.Name, $tag, $g.Group.Count, $last)
        foreach ($c in ($g.Group | Sort-Object mins)) {
            $rel = $c.file
            if ($rel.ToLower().StartsWith($root.ToLower())) { $rel = $rel.Substring($root.Length).TrimStart('\','/') }
            Write-Output ("      {0}  (hace {1} min)" -f $rel, $c.mins)
        }
    }

    # Lo unico verdaderamente accionable: el mismo archivo en dos manos vivas.
    $overlaps = @($claims | Group-Object file | Where-Object { ($_.Group | Select-Object -ExpandProperty sid -Unique).Count -gt 1 })
    Write-Output ""
    Write-Output "== Solapamientos (mismo archivo, dos sesiones vivas) =="
    if (-not $overlaps) {
        Write-Output "  (ninguno)"
    }
    foreach ($o in $overlaps) {
        $rel = $o.Name
        if ($rel.ToLower().StartsWith($root.ToLower())) { $rel = $rel.Substring($root.Length).TrimStart('\','/') }
        $who = ($o.Group | Select-Object -ExpandProperty sid -Unique) -join ', '
        Write-Output ("  {0}" -f $rel)
        Write-Output ("      lo tienen: {0}" -f $who)
    }

    # Contexto: lanes de build y estado del tree.
    Write-Output ""
    Write-Output "== Lanes =="
    foreach ($lane in @('build','tests')) {
        $coord = Join-Path $root 'build_coord.ps1'
        $out = (& powershell -NoProfile -ExecutionPolicy Bypass -File $coord -Lane $lane -Action status 2>&1 | Out-String).Trim()
        Write-Output ("  {0}" -f $out)
    }
    $dirty = Get-DirtyPaths
    Write-Output ""
    Write-Output ("== Tree: {0} archivo(s) sin commitear ==" -f $dirty.Count)
    if ($overlaps) {
        Write-Output ""
        Write-Output "Hay solapamientos: coordinen, o aislate con"
        Write-Output "  powershell -File worktree.ps1 -Action new -Name <tarea>"
    }
    exit $(if ($overlaps) { 1 } else { 0 })
}

# =============================================================================
if ($Mode -eq 'session-start') {
    $dirty = Get-DirtyPaths
    if (-not $dirty) { exit 0 }

    $shown = $dirty | Select-Object -First 12
    $more  = $dirty.Count - $shown.Count
    $msg  = "[session-guard] El working tree ya tiene $($dirty.Count) archivo(s) sin commitear"
    $msg += " - puede ser trabajo de OTRA sesion en curso:`n"
    $msg += ($shown | ForEach-Object { "  $_" }) -join "`n"
    if ($more -gt 0) { $msg += "`n  ... y $more mas" }
    $msg += "`n`nSi vas a trabajar en algo distinto, aislate en tu propio worktree:"
    $msg += "`n  powershell -File worktree.ps1 -Action new -Name <tarea>"
    $msg += "`nSi compartis el tree igual: commitea SOLO tus paths (nunca 'git add -A'),"
    $msg += "`ny no uses 'git checkout --' / 'git stash' / 'git reset --hard' sobre el tree."
    @{ hookSpecificOutput = @{ hookEventName = 'SessionStart'; additionalContext = $msg } } |
        ConvertTo-Json -Depth 5 -Compress | Write-Output
    exit 0
}

# =============================================================================
function Get-EditPaths($inputObject) {
    $toolInput = $inputObject.tool_input
    $toolName = [string]$inputObject.tool_name
    $paths = @()
    $seen = @{}

    foreach ($key in @('file_path','path','filePath','filename','target','destination','files','paths')) {
        $value = $toolInput.$key
        if ($null -eq $value) { continue }
        $values = if ($value -is [System.Array]) { $value } else { @($value) }
        foreach ($item in $values) {
            $path = [string]$item
            if (-not $path) { continue }
            $normal = ($path -replace '/', '\')
            $id = $normal.ToLowerInvariant()
            if (-not $seen.ContainsKey($id)) { $seen[$id] = $true; $paths += $normal }
        }
    }

    # Codex can carry apply_patch in tool_input.command instead of file_path.
    # Read the patch headers so one hook invocation claims every touched file.
    $command = [string]$toolInput.command
    if ($toolName -match 'apply.?patch' -or $command -match '\*\*\* Begin Patch') {
        $headers = [regex]::Matches($command, '(?m)^\s*\*\*\*\s+(?:Update|Add|Delete) File:\s*(.+?)\s*$')
        foreach ($match in $headers) {
            $path = [string]$match.Groups[1].Value.Trim()
            if (-not $path) { continue }
            $normal = ($path -replace '/', '\')
            $id = $normal.ToLowerInvariant()
            if (-not $seen.ContainsKey($id)) { $seen[$id] = $true; $paths += $normal }
        }
    }
    return @($paths)
}

# =============================================================================
if ($Mode -eq 'edit-claim') {
    $in = Read-StdinJson
    if (-not $in) { exit 0 }
    $sid  = [string]$in.session_id
    $files = @(Get-EditPaths $in)
    if (-not $sid -or $files.Count -eq 0) { exit 0 }

    foreach ($file in $files) {

    New-Item -ItemType Directory -Force -Path $claimDir | Out-Null

    # Los claims vencidos no los mira nadie pero el dir crece para siempre: podo
    # de paso. Barato (decenas de archivos) y evita tener que limpiar a mano.
    Get-ChildItem $claimDir -Filter '*.claim' -File -ErrorAction SilentlyContinue |
        Where-Object { ((Get-Date) - $_.LastWriteTime).TotalMinutes -ge ($staleMin * 2) } |
        Remove-Item -Force -ErrorAction SilentlyContinue

    # Un claim por (sesion, archivo). El nombre codifica ambos; el mtime es el
    # heartbeat (no hay PID confiable de la sesion, y no hace falta: lo unico que
    # importa es si alguien la toco recientemente).
    # Hash del path completo, no los ultimos 80 chars: truncar hace colisionar
    # archivos con el mismo final (a/x/CLAUDE.md vs b/y/CLAUDE.md), o sea avisos
    # de choques que no existen.
    $md5  = [Security.Cryptography.MD5]::Create()
    $key  = ([BitConverter]::ToString($md5.ComputeHash([Text.Encoding]::UTF8.GetBytes($file.ToLower()))) -replace '-','').Substring(0,16)
    $mine = Join-Path $claimDir ("{0}__{1}.claim" -f $sid, $key)

    # ?Alguna otra sesion viva reclamo este mismo archivo?
    $others = @(Get-ChildItem $claimDir -Filter ("*__{0}.claim" -f $key) -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -notlike ("{0}__*" -f $sid) -and
                               ((Get-Date) - $_.LastWriteTime).TotalMinutes -lt $staleMin })

    Set-Content -Path $mine -Value $file -Encoding UTF8 -ErrorAction SilentlyContinue

    if ($others.Count -gt 0) {
        $ago = [int]((Get-Date) - ($others | Sort-Object LastWriteTime -Descending)[0].LastWriteTime).TotalMinutes
        $inRepo = $file.ToLower().StartsWith($root.ToLower())
        $rel = if ($inRepo) { $file.Substring($root.Length).TrimStart('\','/') } else { $file }

        # El consejo tiene que aplicar al archivo que tenes enfrente. Un choque en
        # la carpeta de memoria (o en cualquier cosa fuera del repo) es real y vale
        # avisarlo, pero hablarle de 'git checkout' o de worktrees es ruido - y un
        # guard que dice pavadas se ignora, o sea deja de proteger.
        $tail = if ($inRepo) {
            "Antes de escribir: releelo (puede haber cambiado en disco desde tu ultimo Read).`n" +
            "Al commitear, stagea solo TUS hunks; no revertas con 'git checkout -- $rel'`n" +
            "(te llevas lo suyo). Para trabajar tranquilo:`n" +
            "  powershell -File worktree.ps1 -Action new -Name <tarea>"
        } else {
            "Esta fuera del repo: no hay historial que te cubra si lo pisas. Releelo`n" +
            "antes de escribir y suma lo tuyo en vez de reescribir el archivo entero."
        }
        Emit-Context ("[session-guard] OJO: otra sesion edito '$rel' hace ~${ago} min y sigue activa.`n" +
                      "Vas a pisar o mezclar su trabajo. $tail")
    }
    }
    exit 0
}

# =============================================================================
if ($Mode -eq 'git-guard') {
    $in = Read-StdinJson
    if (-not $in) { exit 0 }
    $cmd = [string]$in.tool_input.command
    if (-not $cmd) { exit 0 }

    # Solo comandos git que barren el working tree ENTERO. El criterio es "alcance
    # global", no "git peligroso": la forma por path (git checkout -- src/foo.cpp)
    # es justamente la salida que recomendamos, asi que NO puede caer aca.
    # '\.(?![\w/-])' = un '.' como argumento suelto (git checkout . / -- .), que es
    # el que se lleva puesto todo. --staged solo desestagea: no destruye nada.
    $dot = '(?<![\w./-])\.(?![\w/-])'
    $patterns = @(
        @{ rx = "\bgit\s+(checkout|restore)\b(?![^|;&]*--staged(?![^|;&]*--worktree))[^|;&]*$dot"
           what = 'git checkout/restore . (descarta el working tree entero)' },
        @{ rx = '\bgit\s+reset\s+[^|;&]*--hard';              what = 'git reset --hard (descarta TODO el working tree)' },
        @{ rx = '\bgit\s+clean\b[^|;&]*\s-[a-zA-Z]*[fdx]';    what = 'git clean -fd/-fx (borra archivos sin trackear)' },
        @{ rx = '\bgit\s+stash\b(?![^|;&]*\b(list|show)\b)';  what = 'git stash (se lleva los cambios de TODAS las sesiones)' },
        @{ rx = "\bgit\s+add\s+[^|;&]*(-A\b|--all\b|$dot)";   what = 'git add -A/. (stagea trabajo de otras sesiones)' },
        # 'git commit -a' es 'git add -A' con otro nombre: stagea TODO lo tracked,
        # incluido lo que esta editando otra sesion. Bloquear add -A y dejar pasar
        # commit -am es cerrar la puerta y dejar la ventana abierta: el incidente
        # real (un commit que se llevo 3 archivos ajenos) fue exactamente esto.
        # 'git commit -m' (solo lo staged) pasa: es la forma correcta.
        @{ rx = '\bgit\s+commit\b(?![^|;&]*--amend)[^|;&]*(\s-[a-zA-Z]*a[a-zA-Z]*\b|--all\b)'
           what = 'git commit -a/-am (stagea y commitea trabajo de otras sesiones)' }
    )
    # Matchear el TEXTO del comando bloquea cosas que solo NOMBRAN el comando sin
    # ejecutarlo: un echo, un grep, documentacion, un JSON con 'git stash' adentro.
    # Eso es exactamente lo que hace que un guard se termine desactivando. Asi que
    # antes de bloquear, exigimos que el 'git' este de verdad en posicion de
    # comando y fuera de comillas.

    # Blanquea lo que este entre comillas, conservando las posiciones. Asi ningun
    # patron puede pegar contra el CONTENIDO de un string: ni el 'git stash' de un
    # echo, ni el '-a' del mensaje en git commit -m "arreglo el flag -a".
    function Get-Masked([string]$s) {
        $sb = [Text.StringBuilder]::new($s)
        $inS = $false; $inD = $false
        for ($k = 0; $k -lt $s.Length; $k++) {
            $c = $s[$k]
            if     ($c -eq "'" -and -not $inD) { $inS = -not $inS }
            elseif ($c -eq '"' -and -not $inS) { $inD = -not $inD }
            elseif ($inS -or $inD)             { [void]$sb.Replace($c, ' ', $k, 1) }
        }
        return $sb.ToString()
    }
    # ?Esta 'git' donde arranca un comando, y no como argumento de otro?
    # ('echo git stash' no ejecuta nada; 'foo && git stash' si.)
    function Test-CommandPos([string]$s, [int]$i) {
        $k = $i - 1
        while ($k -ge 0 -and [char]::IsWhiteSpace($s[$k])) { $k-- }
        if ($k -lt 0) { return $true }
        return ([string]$s[$k] -in @(';', '|', '&', '(', '{', '`', "`n"))
    }

    $masked = Get-Masked $cmd
    $hit = $null
    foreach ($p in $patterns) {
        foreach ($m in [regex]::Matches($masked, $p.rx)) {
            if (Test-CommandPos $masked $m.Index) { $hit = $p; break }
        }
        if ($hit) { break }
    }
    if (-not $hit) { exit 0 }

    # Sin trabajo ajeno en riesgo, no hay nada que proteger: dejalo pasar.
    $dirty = Get-DirtyPaths
    if ($dirty.Count -eq 0) { exit 0 }

    $shown = ($dirty | Select-Object -First 10 | ForEach-Object { "  $_" }) -join "`n"
    $more  = $dirty.Count - [Math]::Min(10, $dirty.Count)
    if ($more -gt 0) { $shown += "`n  ... y $more mas" }

    Emit-Deny ("BLOQUEADO: $($hit.what), con $($dirty.Count) archivo(s) sin commitear en el tree.`n`n" +
               "$shown`n`n" +
               "Este tree lo comparten varias sesiones: parte de eso puede ser trabajo ajeno EN CURSO,`n" +
               "y este comando no tiene undo. Alternativas:`n" +
               "  - Revertir SOLO lo tuyo, por path: git checkout -- <tu/archivo> (uno por uno, revisado)`n" +
               "  - Deshacer tus ediciones con la tool Edit (quirurgico, sin tocar lo ajeno)`n" +
               "  - Commitear solo tus paths: git add <path1> <path2> && git commit`n" +
               "  - Aislarte primero: powershell -File worktree.ps1 -Action new -Name <tarea>`n`n" +
               "Si de verdad queres el comando global, pediselo al usuario explicitamente.")
    exit 0
}
