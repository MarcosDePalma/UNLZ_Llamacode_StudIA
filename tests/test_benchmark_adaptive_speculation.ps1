$ErrorActionPreference = "Stop"

Describe "benchmark_adaptive_speculation summary" {
    It "sums draft timings from ordered run records" {
        $scriptPath = Join-Path $PSScriptRoot "..\tools\benchmark_adaptive_speculation.ps1"
        $scriptText = Get-Content -LiteralPath $scriptPath -Raw
        $start = $scriptText.IndexOf("function Get-PropertyValue")
        $end = $scriptText.IndexOf("function Get-ResponseAnswer")
        if ($start -lt 0 -or $end -le $start) {
            throw "No se encontraron los helpers de resumen del benchmark"
        }
        Invoke-Expression $scriptText.Substring($start, $end - $start)

        $records = @(
            [ordered]@{ draftN = 3; draftAccepted = 2 }
            [ordered]@{ draftN = 4; draftAccepted = 3 }
        )

        $draftTotal = Get-NumericSum @($records | ForEach-Object { Get-PropertyValue $_ "draftN" })
        $draftAcceptedTotal = Get-NumericSum @($records | ForEach-Object { Get-PropertyValue $_ "draftAccepted" })

        $draftTotal | Should Be 7
        $draftAcceptedTotal | Should Be 5
    }

    It "keeps fixed and adaptive configurations in separate summary groups" {
        $scriptPath = Join-Path $PSScriptRoot "..\tools\benchmark_adaptive_speculation.ps1"
        $scriptText = Get-Content -LiteralPath $scriptPath -Raw
        $start = $scriptText.IndexOf("function Get-PropertyValue")
        $end = $scriptText.IndexOf("function Get-ResponseAnswer")
        Invoke-Expression $scriptText.Substring($start, $end - $start)

        $records = @(
            [ordered]@{ configId = "fixed-3"; error = "" }
            [ordered]@{ configId = "adaptive-3-5"; error = "" }
        )

        $groups = @($records | Where-Object { (Get-PropertyValue $_ "error") -eq "" } |
            Group-Object { Get-PropertyValue $_ "configId" })

        $groups.Count | Should Be 2
        @($groups | ForEach-Object { $_.Name } | Sort-Object) |
            Should Be @("adaptive-3-5", "fixed-3")
    }

    It "parses comma-separated n-max values without concatenating them" {
        $scriptPath = Join-Path $PSScriptRoot "..\tools\benchmark_adaptive_speculation.ps1"
        $scriptText = Get-Content -LiteralPath $scriptPath -Raw
        $start = $scriptText.IndexOf("function Get-PropertyValue")
        $end = $scriptText.IndexOf("function Get-ResponseAnswer")
        Invoke-Expression $scriptText.Substring($start, $end - $start)

        $values = Convert-ToIntList @("5,7,8,9") "AdaptiveNMax"

        $values | Should Be @(5, 7, 8, 9)
    }
}
