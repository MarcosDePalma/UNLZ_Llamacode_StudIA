$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$script = Join-Path $root 'tools\release.ps1'
$fails = 0

function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "  PASS $message" }
    else { Write-Host "  FAIL $message" -ForegroundColor Red; $script:fails++ }
}

Write-Host '== release.ps1 =='

# 0) ASCII puro: Windows PowerShell 5.1 lee un .ps1 sin BOM como ANSI y un
#    caracter UTF-8 dentro de un string puede cerrarlo y romper el parseo.
$bytes = [IO.File]::ReadAllBytes($script)
$nonAscii = @($bytes | Where-Object { $_ -gt 127 })
Check ($nonAscii.Count -eq 0) "el script es ASCII puro (bytes >127: $($nonAscii.Count))"

# 1) Dry run por defecto: no taggea ni publica.
$tagsBefore = @(git -C $root tag --list)
$out = & powershell -NoProfile -ExecutionPolicy Bypass -File $script 2>&1 | Out-String
$tagsAfter = @(git -C $root tag --list)
Check ($LASTEXITCODE -eq 0) 'dry run sale 0'
Check ($out -match '\[DRY RUN\]') 'dry run se anuncia como tal'
Check ($tagsBefore.Count -eq $tagsAfter.Count) 'dry run no crea tags'

# 2) Toma la version de CMakeLists.txt.
$cmake = (Get-Content (Join-Path $root 'CMakeLists.txt') -TotalCount 5) -join "`n"
$version = [regex]::Match($cmake, 'project\(LlamaCode VERSION ([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
Check ($out -match [regex]::Escape("release v$version")) "usa la version del proyecto (v$version)"

# 3) Rechaza versiones mal formadas antes de tocar git.
& powershell -NoProfile -ExecutionPolicy Bypass -File $script -Version '0.1' 2>&1 | Out-Null
Check ($LASTEXITCODE -eq 1) 'rechaza una version que no es x.y.z'

# 4) El tag tiene que parsear como version en el app: AppController saca la 'v'
#    inicial y pide QVersionNumber::fromString no nulo.
Check ($out -match 'release v[0-9]+\.[0-9]+\.[0-9]+') 'el tag es vX.Y.Z (lo que el detector sabe parsear)'

# 5) Publicar es explicito: sin -Publish no hay llamada a gh.
$body = [IO.File]::ReadAllText($script)
Check ($body -match 'if \(-not \$Publish\)') 'publicar requiere -Publish'
Check ($body -match 'gh release create') 'publica con gh release create'

if ($fails) { throw "$fails release-script regression(s) failed" }
Write-Host 'All release-script regressions passed.'
