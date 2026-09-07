<#
  test_session_guard.ps1 - regresion de los hooks de convivencia (tools/session_guard.ps1).

  No corre en ctest (es infra de harness en PS, no C++). Correr a mano:
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_session_guard.ps1
  Exit 0 = todo verde.

  Lo que mas importa aca NO es que bloquee, sino que NO bloquee de mas: un guard
  que se come 'git checkout -- <path>' (la salida segura que el propio guard
  recomienda) o 'git status' se desactiva a los dos dias y no protege nada.
#>
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$guard = Join-Path $root 'tools\session_guard.ps1'
$claimDir = Join-Path $root '.buildlock\claims'
$fails = 0
function Ok($cond,$msg){ if($cond){Write-Host "  PASS $msg"}else{Write-Host "  FAIL $msg" -Foreground Red; $script:fails++} }

# El guard solo actua si el tree tiene trabajo sin commitear: garantizo esa
# condicion con un probe propio (y lo saco al final).
$probe = Join-Path $root 'tests\_guard_probe.tmp'
'probe de test_session_guard' | Set-Content $probe -Encoding ASCII

# Corre el guard con un JSON por stdin y devuelve la salida cruda.
function RunGuard([string]$mode, $payload) {
    $json = ($payload | ConvertTo-Json -Depth 5 -Compress)
    $out = ($json | & powershell -NoProfile -ExecutionPolicy Bypass -File $guard -Mode $mode 2>&1) | Out-String
    return $out.Trim()
}
function GitGuard([string]$cmd) {
    return RunGuard 'git-guard' @{ session_id='s1'; tool_name='Bash'; tool_input=@{ command=$cmd } }
}

Write-Host "== test 0: los scripts de infra son ASCII puro =="
# Windows PowerShell 5.1 lee un .ps1 sin BOM como ANSI. Un caracter UTF-8 dentro
# de un STRING se decodifica mal y puede terminar el string: el em-dash (E2 80 94)
# da 'a EUR "' en CP1252, y ese ultimo byte es una comilla -> ParserError y el
# hook muere entero. En comentarios solo se ve feo; en strings rompe. Los hooks
# corren bajo el host que el harness elija, asi que: ASCII puro, sin excepciones.
foreach ($f in @('tools\session_guard.ps1','worktree.ps1','build_coord.ps1')) {
    $txt = [IO.File]::ReadAllText((Join-Path $root $f))
    $bad = [regex]::Matches($txt, '[^\x00-\x7F]')
    $sample = if ($bad.Count) { " (primero: '" + $bad[0].Value + "')" } else { '' }
    Ok ($bad.Count -eq 0) "$f sin caracteres no-ASCII$sample"
}

Write-Host "== test 1: git-guard BLOQUEA lo que barre el tree entero =="
foreach ($c in @(
    'git checkout -- .',
    'git checkout .',
    'git restore .',
    'git reset --hard origin/main',
    'git clean -fd',
    'git stash',
    'git add -A',
    'git add .'
)) {
    $o = GitGuard $c
    Ok ($o -match '"permissionDecision":"deny"') "bloquea: $c"
}

Write-Host "== test 2: git-guard DEJA PASAR lo seguro (no bloquear de mas) =="
foreach ($c in @(
    'git checkout -- src/core/agent/LlamaAgentBackend.cpp',  # por path = la salida recomendada
    'git restore --staged src/foo.cpp',                      # solo desestagea, no destruye
    'git add src/foo.cpp tests/bar.cpp',                     # por path
    'git status --porcelain',
    'git log --oneline -5',
    'git stash list',
    'git checkout -b session/nueva-rama',                    # crear rama no descarta nada
    'git diff',
    'cmake --build build --config Release'
)) {
    $o = GitGuard $c
    Ok (-not ($o -match 'deny')) "deja pasar: $c"
}

Write-Host "== test 2a: 'git commit -a' es 'git add -A' con otro nombre =="
# El agujero que tenia el guard: bloqueaba add -A y dejaba pasar commit -am, que
# stagea TODO lo tracked igual. Es el incidente real que ya paso una vez (un
# commit que se llevo 3 archivos de otra sesion).
foreach ($c in @('git commit -am "wip"', 'git commit -a -m "wip"', 'git commit --all -m "wip"')) {
    $o = GitGuard $c
    Ok ($o -match 'deny') "bloquea: $c"
}
foreach ($c in @(
    'git commit -m "solo lo staged"',           # la forma correcta
    'git commit --amend --no-edit',             # no barre el working tree
    'git commit -m "arreglo el flag -a de foo"' # el '-a' esta DENTRO del mensaje
)) {
    $o = GitGuard $c
    Ok (-not ($o -match 'deny')) "deja pasar: $c"
}

Write-Host "== test 2b: MENCIONAR el comando no es EJECUTARLO =="
# Se me colo en la primera version: el guard matcheaba el texto, asi que bloqueaba
# un benchmark que llevaba 'git stash' dentro de un JSON. Nombrar != ejecutar, y
# bloquear por nombrar es lo que hace que alguien termine apagando el guard.
foreach ($c in @(
    'echo "git stash" > notas.txt',                              # lo escribe, no lo corre
    "echo 'no uses git reset --hard aca'",
    'grep -rn "git checkout ." docs/',                           # lo busca
    'python bench.py --payload ''{"cmd":"git stash"}''',         # dato dentro de JSON
    'echo git add -A'                                            # argumento de echo
)) {
    $o = GitGuard $c
    Ok (-not ($o -match 'deny')) "no bloquea (solo lo menciona): $c"
}
# ...pero encadenado SI se ejecuta, y ahi hay que frenarlo.
foreach ($c in @(
    'cd /d C:\repo && git stash',
    'echo hola; git reset --hard HEAD',
    'npm test || git checkout .'
)) {
    $o = GitGuard $c
    Ok ($o -match 'deny') "bloquea (se ejecuta encadenado): $c"
}

Write-Host "== test 2c: la lista de archivos sucios sale entera =="
# El Trim() del blob de 'git status --porcelain' le comia el espacio a la PRIMERA
# linea (formato 'XY <path>', X puede ser espacio) y el parser se llevaba la
# primera letra del path: CMakeLists.txt -> MakeLists.txt.
$reason2 = (GitGuard 'git stash' | ConvertFrom-Json).hookSpecificOutput.permissionDecisionReason
$real = @(& git -C $root status --porcelain | ForEach-Object { if($_ -match '^(..)\s(.+)$'){ $Matches[2].Trim().Trim('"') } })
$first = $real | Select-Object -First 1
Ok ($reason2 -match [regex]::Escape($first)) "el primer path aparece completo ('$first')"

Write-Host "== test 3: git-guard explica como seguir =="
# Asertar sobre el JSON crudo da falsos negativos: ConvertTo-Json escapa '<' como
# < y codifica los saltos como '\n'. El consumidor parsea el JSON, asi que lo
# que importa es el string YA DECODIFICADO.
$reason = (GitGuard 'git checkout -- .' | ConvertFrom-Json).hookSpecificOutput.permissionDecisionReason
Ok ($reason -match 'worktree\.ps1')          "el bloqueo sugiere aislarse en un worktree"
Ok ($reason -match 'git add <path1>')        "el bloqueo ofrece la alternativa por path"
Ok ($reason -match 'git checkout -- <tu/archivo>') "el bloqueo ofrece revertir por path"
# El bug que esto caza: escribir "\n" en un string de PowerShell no produce un
# salto (es backslash-n literal); hay que usar backtick-n.
Ok (-not ($reason -match '\\n'))             "sin backslash-n literal en el texto decodificado"
Ok ($reason -match "`n")                     "los saltos son saltos de verdad"

Write-Host "== test 4: edit-claim avisa solo si OTRA sesion viva toco el archivo =="
if (Test-Path $claimDir) { Remove-Item -Recurse -Force $claimDir -ErrorAction SilentlyContinue }
$target = Join-Path $root 'src\core\agent\LlamaAgentBackend.cpp'
$o1 = RunGuard 'edit-claim' @{ session_id='sess-A'; tool_name='Edit'; tool_input=@{ file_path=$target } }
Ok (-not ($o1 -match 'OJO')) "primera sesion en tocar el archivo: sin aviso"
$o2 = RunGuard 'edit-claim' @{ session_id='sess-A'; tool_name='Edit'; tool_input=@{ file_path=$target } }
Ok (-not ($o2 -match 'OJO')) "la MISMA sesion re-editando: sin aviso (no se avisa a si misma)"
$o3 = RunGuard 'edit-claim' @{ session_id='sess-B'; tool_name='Edit'; tool_input=@{ file_path=$target } }
Ok ($o3 -match 'OJO: otra sesion edito') "otra sesion sobre el mismo archivo: avisa"
Ok ($o3 -match 'additionalContext')       "el aviso entra como additionalContext"
Ok (-not ($o3 -match 'deny'))             "avisa pero NO bloquea (decision del usuario)"
$other = Join-Path $root 'src\core\agent\SubAgentRunner.cpp'
$o4 = RunGuard 'edit-claim' @{ session_id='sess-B'; tool_name='Edit'; tool_input=@{ file_path=$other } }
Ok (-not ($o4 -match 'OJO')) "archivo distinto: sin aviso (el claim es por archivo, no por sesion)"

Write-Host "== test 4b: el consejo aplica al archivo que tenes enfrente =="
# Dentro del repo: git/worktree son la salida. Fuera (ej. la carpeta de memoria):
# hablar de worktrees es ruido, y un guard que dice pavadas se ignora.
$memFile = 'C:\Users\cristian\.claude\projects\demo\memory\alguna-memoria.md'
RunGuard 'edit-claim' @{ session_id='sess-X'; tool_name='Edit'; tool_input=@{ file_path=$memFile } } | Out-Null
$oOut = (RunGuard 'edit-claim' @{ session_id='sess-Y'; tool_name='Edit'; tool_input=@{ file_path=$memFile } } |
         ConvertFrom-Json).hookSpecificOutput.additionalContext
Ok ($oOut -match 'OJO: otra sesion edito')  "avisa igual fuera del repo (el choque es real)"
Ok (-not ($oOut -match 'worktree'))         "fuera del repo NO sugiere worktree"
Ok (-not ($oOut -match 'git checkout'))     "fuera del repo NO habla de git checkout"
$inOut = (RunGuard 'edit-claim' @{ session_id='sess-Y'; tool_name='Edit'; tool_input=@{ file_path=$target } } |
          ConvertFrom-Json).hookSpecificOutput.additionalContext
Ok ($inOut -match 'worktree')                "dentro del repo SI sugiere worktree"

Write-Host "== test 4c: el claim es por path completo, no por el final del nombre =="
# Truncar el path hacia dizq (los ultimos N chars) hace colisionar archivos con el
# mismo final: a/x/CLAUDE.md vs b/y/CLAUDE.md eran el MISMO claim -> avisos de
# choques inexistentes. Con hash del path completo, no.
if (Test-Path $claimDir) { Remove-Item -Recurse -Force $claimDir -ErrorAction SilentlyContinue }
$p1 = Join-Path $root 'CLAUDE.md'
$p2 = Join-Path $root 'research_sub\CLAUDE.md'   # mismo basename, otro archivo
RunGuard 'edit-claim' @{ session_id='sess-P'; tool_name='Edit'; tool_input=@{ file_path=$p1 } } | Out-Null
$oCol = RunGuard 'edit-claim' @{ session_id='sess-Q'; tool_name='Edit'; tool_input=@{ file_path=$p2 } }
Ok (-not ($oCol -match 'OJO')) "dos CLAUDE.md en dirs distintos no colisionan"
$oSame = RunGuard 'edit-claim' @{ session_id='sess-Q'; tool_name='Edit'; tool_input=@{ file_path=$p1 } }
Ok ($oSame -match 'OJO') "el mismo path si detecta el choque"

Write-Host "== test 4d: apply_patch reclama todos sus archivos =="
if (Test-Path $claimDir) { Remove-Item -Recurse -Force $claimDir -ErrorAction SilentlyContinue }
$patch = "*** Begin Patch`n*** Update File: src/core/agent/LlamaAgentBackend.cpp`n*** Add File: src/core/agent/AgentLifecycle.h`n*** End Patch"
$patchPayload = @{ session_id='sess-PATCH'; tool_name='apply_patch'; tool_input=@{ command=$patch } }
RunGuard 'edit-claim' $patchPayload | Out-Null
$patchClaims = @(Get-ChildItem $claimDir -Filter 'sess-PATCH__*.claim' -File -ErrorAction SilentlyContinue)
Ok ($patchClaims.Count -eq 2) "un patch multiarchivo crea dos claims (actual=$($patchClaims.Count))"
$patchCollision = RunGuard 'edit-claim' @{ session_id='sess-OTHER'; tool_name='apply_patch'; tool_input=@{ command=$patch } }
Ok ($patchCollision -match 'LlamaAgentBackend\.cpp') "patch informa colision en el primer archivo"

Write-Host "== test 5: edit-claim ignora claims stale (sesion muerta no traba a nadie) =="
# Autocontenido a proposito: antes dependia de claims que dejaba el test 4, y
# cuando el 4c empezo a limpiar el registry el test paso a verificar NADA (no
# habia claim stale que ignorar, asi que "no avisa" salia solo). Un test que pasa
# por vacio es peor que no tenerlo. De ahi el control positivo primero.
if (Test-Path $claimDir) { Remove-Item -Recurse -Force $claimDir -ErrorAction SilentlyContinue }
RunGuard 'edit-claim' @{ session_id='sess-OLD'; tool_name='Edit'; tool_input=@{ file_path=$target } } | Out-Null
$ctrl = RunGuard 'edit-claim' @{ session_id='sess-C'; tool_name='Edit'; tool_input=@{ file_path=$target } }
Ok ($ctrl -match 'OJO') "control positivo: con el claim FRESCO si avisa (el setup sirve)"

# Ahora envejezco el claim de sess-OLD y saco el de sess-C: lo unico que cambia
# es la edad, asi que si ahora no avisa, es por la edad y no por vacio.
Get-ChildItem $claimDir -Filter 'sess-OLD__*' -File | ForEach-Object {
    $_.LastWriteTime = (Get-Date).AddMinutes(-120)   # mas viejo que $staleMin (90)
}
Get-ChildItem $claimDir -Filter 'sess-C__*' -File | Remove-Item -Force
$o5 = RunGuard 'edit-claim' @{ session_id='sess-D'; tool_name='Edit'; tool_input=@{ file_path=$target } }
Ok (-not ($o5 -match 'OJO')) "claim de hace 2h no genera aviso"

Write-Host "== test 5b: -Mode status muestra quien toca que, y los solapamientos =="
# status es para humanos: no lee stdin y no emite JSON.
function RunStatus() {
    $out = (& powershell -NoProfile -ExecutionPolicy Bypass -File $guard -Mode status 2>&1) | Out-String
    return [pscustomobject]@{ rc = $LASTEXITCODE; out = $out.Trim() }
}
if (Test-Path $claimDir) { Remove-Item -Recurse -Force $claimDir -ErrorAction SilentlyContinue }
$s0 = RunStatus
Ok ($s0.out -match 'ninguna')  "sin claims: dice que no hay sesiones activas"
Ok ($s0.rc -eq 0)              "sin claims: exit 0; actual=$($s0.rc)"

# Dos sesiones distintas, archivos distintos -> actividad pero sin solapamiento.
RunGuard 'edit-claim' @{ session_id='sess-M'; tool_name='Edit'; tool_input=@{ file_path=$target } } | Out-Null
RunGuard 'edit-claim' @{ session_id='sess-N'; tool_name='Edit'; tool_input=@{ file_path=$other } }  | Out-Null
$s1 = RunStatus
Ok ($s1.out -match 'sess-M' -and $s1.out -match 'sess-N') "lista las dos sesiones"
Ok ($s1.out -match 'LlamaAgentBackend\.cpp')              "muestra el archivo de cada una"
Ok ($s1.out -notmatch 'C:\\Users')                        "paths relativos al repo, no absolutos"
Ok ($s1.rc -eq 0)                                         "sin solapamiento: exit 0; actual=$($s1.rc)"

# Ahora si: las dos sobre el MISMO archivo.
RunGuard 'edit-claim' @{ session_id='sess-N'; tool_name='Edit'; tool_input=@{ file_path=$target } } | Out-Null
$s2 = RunStatus
Ok ($s2.rc -eq 1) "con solapamiento: exit 1 (accionable para un script); actual=$($s2.rc)"
$ovl = ($s2.out -split '== Solapamientos')[1]
Ok ($ovl -match 'LlamaAgentBackend\.cpp') "nombra el archivo solapado"
Ok ($ovl -match 'sess-M' -and $ovl -match 'sess-N') "nombra a las dos sesiones que lo tienen"

Write-Host "== test 6: session-start avisa si el tree ya tiene trabajo en curso =="
$o6 = RunGuard 'session-start' @{ session_id='sess-D' }
Ok ($o6 -match 'sin commitear')   "lista que hay trabajo sin commitear"
Ok ($o6 -match 'worktree\.ps1')   "sugiere el worktree"
Ok ($o6 -match 'SessionStart')    "emite el hookEventName correcto"

# Limpieza
Remove-Item $probe -Force -ErrorAction SilentlyContinue
if (Test-Path $claimDir) { Remove-Item -Recurse -Force $claimDir -ErrorAction SilentlyContinue }

Write-Host ""
if($fails -eq 0){ Write-Host "=== ALL SESSION-GUARD TESTS PASSED ===" -Foreground Green; exit 0 }
else { Write-Host "=== $fails SESSION-GUARD TEST(S) FAILED ===" -Foreground Red; exit 1 }
