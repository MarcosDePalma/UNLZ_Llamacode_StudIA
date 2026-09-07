<#
  worktree.ps1 - un working tree por sesion/mejora en curso.

  El PROBLEMA que resuelve: build_coord.ps1 serializa QUIEN compila, pero si dos
  sesiones (IAs/CI/vos) comparten el mismo working tree, la otra edita src/
  MIENTRAS vos compilas. Sintomas tipicos:
    - "error de compilacion que no es mio" (codigo ajeno a medio escribir),
    - hunks de otra tarea mezclados en CMakeLists.txt al momento de stagear,
    - gate verde que en realidad no corrio sobre tu fuente.
  Ningun lock de build arregla eso: la edicion pasa FUERA del lock. La unica cura
  es no compartir el tree.

  Cada worktree tiene su propio build/, build_tests/ y .buildlock/ (todos estan
  gitignoreados root-anchored), asi que las lanes quedan aisladas solas: dos
  worktrees compilan en paralelo sin pisarse. Comparten el object store de git,
  asi que crear uno es barato (segundos, no un clone).

  Uso:
    powershell -File worktree.ps1 -Action new    -Name fuzzy-match
    powershell -File worktree.ps1 -Action list
    powershell -File worktree.ps1 -Action remove -Name fuzzy-match [-Force]

  Costo a saber: el worktree nuevo arranca con build/ vacio -> el primer build es
  full (minutos). Vale la pena si la tarea dura mas que eso; para un one-liner en
  un tree quieto, no.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('new','list','remove')] [string]$Action,
    [string]$Name,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Log([string]$m) { Write-Host "[worktree] $m" }
function Die([string]$m) { Write-Host "[worktree] ERROR: $m" -Foreground Red; exit 1 }

# git escribe progreso a stderr AUNQUE le vaya bien ("Preparing worktree..."), y
# con $ErrorActionPreference='Stop' PowerShell convierte ese stderr en un error
# terminante (el 2>&1 no alcanza: el ErrorRecord redirigido sigue disparando el
# Stop). Bajo el EAP solo aca: para un exe nativo el exit code es la unica senal
# de fallo que vale.
function Git-Run() {
    $ErrorActionPreference = 'Continue'
    $out = & git @args 2>&1
    return [pscustomobject]@{ rc = $LASTEXITCODE; out = ($out | Out-String).Trim() }
}

# El repo principal, aunque nos llamen desde un worktree ya creado.
$probe = Git-Run -C $root rev-parse --is-inside-work-tree
if ($probe.rc -ne 0 -or $probe.out -ne 'true') { Die "no estoy dentro de un repo git ($root)" }

# --- list --------------------------------------------------------------------
if ($Action -eq 'list') {
    $r = Git-Run -C $root worktree list
    Write-Output $r.out   # stdout, no Write-Host: asi es capturable/pipeable
    exit $r.rc
}

if (-not $Name) { Die "-Name es obligatorio para -Action $Action" }
if ($Name -notmatch '^[a-z0-9][a-z0-9._-]*$') {
    Die "-Name invalido ('$Name'): usa kebab-case ([a-z0-9._-], no arranca con separador)"
}

$parent   = Split-Path -Parent $root
$wtPath   = Join-Path $parent ("LlamaCode-{0}" -f $Name)
$branch   = "session/$Name"

# --- remove ------------------------------------------------------------------
if ($Action -eq 'remove') {
    if (-not (Test-Path $wtPath)) { Die "no existe el worktree $wtPath" }

    # Nunca descartar trabajo sin avisar: si hay cambios sin commitear, mostralos
    # y frena. -Force solo despues de que el humano los vio.
    $st = Git-Run -C $wtPath status --porcelain
    if ($st.out -and -not $Force) {
        Log "el worktree tiene cambios sin commitear:"
        $st.out -split "`n" | ForEach-Object { Write-Host "    $_" }
        Die "abortado. Commitea/stashea primero, o repeti con -Force para descartarlos."
    }

    $r = if ($Force) { Git-Run -C $root worktree remove --force $wtPath }
         else        { Git-Run -C $root worktree remove $wtPath }
    if ($r.rc -ne 0) { Log $r.out; Die "git worktree remove fallo (rc=$($r.rc))" }
    Log "worktree removido: $wtPath"
    Log "la rama '$branch' sigue existiendo (borrala con: git branch -d $branch)"
    exit 0
}

# --- new ---------------------------------------------------------------------
if (Test-Path $wtPath) { Die "ya existe $wtPath (usa -Action remove, o elegi otro -Name)" }

$branchExists = (Git-Run -C $root show-ref --verify --quiet "refs/heads/$branch").rc -eq 0
if ($branchExists) {
    Log "la rama '$branch' ya existe -> la reuso (checkout en el worktree nuevo)"
    $r = Git-Run -C $root worktree add $wtPath $branch
} else {
    Log "creando rama '$branch' desde HEAD"
    $r = Git-Run -C $root worktree add $wtPath -b $branch
}
if ($r.rc -ne 0) { Log $r.out; Die "git worktree add fallo (rc=$($r.rc))" }

Log ""
Log "listo -> $wtPath   (rama: $branch)"
Log "build/, build_tests/ y .buildlock/ son propios: aislado de las otras sesiones."
Log ""
Log "Proximos pasos:"
Log "  cd `"$wtPath`""
Log "  tests.bat Release          # primer build es full (minutos): tree limpio"
Log "  git push -u origin $branch"
Log ""
Log "Cuando termines (mergeado o descartado):"
Log "  powershell -File `"$root\worktree.ps1`" -Action remove -Name $Name"
exit 0
