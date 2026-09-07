# Descarga los benchmarks publicos que LlamaCode sabe importar.
#
# Se bajan de HuggingFace por HTTP plano (sin token: son datasets publicos) al
# directorio de packs, y despues se importan desde la app con
# importBenchmarkPack(path, limit) o por la Control API.
#
# ASCII puro a proposito: ver la regla de los scripts de infra en CLAUDE.md.
#
#   powershell -File tools\fetch_benchmarks.ps1
#   powershell -File tools\fetch_benchmarks.ps1 -Only humaneval
#   powershell -File tools\fetch_benchmarks.ps1 -OutDir D:\benchmarks

param(
    [string]$OutDir = "",
    [string]$Only = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $env:LOCALAPPDATA "LlamaCode\LlamaCode\benchmarks"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# HumanEval va primero a proposito: es el unico de los tres que se corrige
# EJECUTANDO codigo, asi que falla por capacidad real (edge cases, off-by-one) y
# no por formato. GSM8K y MMLU son numerico y opcion multiple, que es justo donde
# los modelos empatan y vuelven a saturar.
$packs = @(
    @{
        name = "humaneval"
        file = "humaneval.jsonl"
        url  = "https://huggingface.co/datasets/openai/openai_humaneval/resolve/main/openai_humaneval/test-00000-of-00001.parquet"
        alt  = "https://raw.githubusercontent.com/openai/human-eval/master/data/HumanEval.jsonl.gz"
        note = "164 items, MIT. El mas discriminante."
    },
    @{
        name = "gsm8k"
        file = "gsm8k_test.jsonl"
        url  = "https://raw.githubusercontent.com/openai/grade-school-math/master/grade_school_math/data/test.jsonl"
        alt  = ""
        note = "1319 items, MIT. Control barato."
    }
)

function Save-Url($url, $dest) {
    Write-Host "  bajando $url"
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing -TimeoutSec 300
}

foreach ($p in $packs) {
    if ($Only -and $Only -ne $p.name) { continue }
    $dest = Join-Path $OutDir $p.file
    Write-Host ""
    Write-Host "== $($p.name) - $($p.note)"
    if ((Test-Path $dest) -and -not $Force) {
        Write-Host "  ya existe: $dest (usar -Force para rebajar)"
        continue
    }
    $ok = $false
    foreach ($u in @($p.url, $p.alt)) {
        if ([string]::IsNullOrWhiteSpace($u)) { continue }
        try {
            if ($u.EndsWith(".gz")) {
                $tmp = "$dest.gz"
                Save-Url $u $tmp
                # Descomprimir sin depender de tar/gzip externos.
                $inFile = [System.IO.File]::OpenRead($tmp)
                $outFile = [System.IO.File]::Create($dest)
                $gz = New-Object System.IO.Compression.GZipStream($inFile, [System.IO.Compression.CompressionMode]::Decompress)
                $gz.CopyTo($outFile)
                $gz.Dispose(); $outFile.Dispose(); $inFile.Dispose()
                Remove-Item $tmp -Force
            } elseif ($u.EndsWith(".parquet")) {
                # El parquet no lo sabemos leer sin dependencias: solo sirve el alt.
                continue
            } else {
                Save-Url $u $dest
            }
            $ok = $true
            break
        } catch {
            Write-Host "  fallo: $($_.Exception.Message)"
        }
    }
    if ($ok) {
        $lines = (Get-Content $dest | Measure-Object -Line).Lines
        Write-Host "  OK -> $dest ($lines lineas)"
    } else {
        Write-Host "  NO se pudo bajar $($p.name)"
    }
}

Write-Host ""
Write-Host "Listo. Importar desde la app:"
Write-Host "  curl -XPOST localhost:8765/invoke -d '{\"method\":\"importBenchmarkPack\",\"args\":[\"<path>\",20]}'"
Write-Host "El limit corta la cantidad de items: HumanEval son 164 y GSM8K 1319."
