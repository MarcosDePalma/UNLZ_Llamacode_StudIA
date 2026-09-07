.pragma library

// Etiqueta y tono de las celdas de score de la tabla de benchmarks.
//
// Vive acá y no inline en BenchmarkPage.qml porque hay un caso que se leía como
// un fallo sin serlo: cuando la suite no define criterios de aceptación (la
// "Corta" mide sólo velocidad), el total llega en 0 y la celda mostraba "—" en
// rojo. Una corrida perfecta parecía rota. Ahora dice "sin eval." en gris.
//
// El tono se devuelve como string ("muted"/"ok"/"warn"/"error") para que esto no
// dependa de Theme y se pueda testear con el runtime `qml` en offscreen.

function scoreValue(row, scoreKey) {
    return row[scoreKey] !== undefined && row[scoreKey] !== null
        ? row[scoreKey]
        : (row.qualityScore !== undefined && row.qualityScore !== null ? row.qualityScore : 0)
}

function scoreTotal(row, totalKey) {
    return row[totalKey] !== undefined && row[totalKey] !== null
        ? row[totalKey]
        : (row.qualityTotal !== undefined && row.qualityTotal !== null ? row.qualityTotal : 0)
}

// "3/5" cuando hay criterios; "sin eval." cuando la suite no puntúa; "—" cuando
// la corrida falló (ahí el badge de Fallo/Timeout ya dice lo que pasó).
function scoreLabel(row, scoreKey, totalKey) {
    const total = scoreTotal(row, totalKey)
    if (total > 0) return scoreValue(row, scoreKey) + "/" + total
    return row.failed ? "—" : "sin eval."
}

function scoreTone(row, scoreKey, totalKey) {
    const total = scoreTotal(row, totalKey)
    if (total <= 0) return "muted"        // sin criterios: NO es un cero
    const ratio = scoreValue(row, scoreKey) / total
    return ratio >= 0.8 ? "ok" : ratio >= 0.5 ? "warn" : "error"
}

// Estado de la corrida en una palabra, para que la fila no dependa de leer el
// score: una corrida sin criterios igual completó.
function runStatus(row) {
    if (row.timedOut) return "timeout"
    if (row.failureKind === "infrastructure" || row.failureStage === "server-crash"
            || row.failureStage === "request" || row.failureStage === "agent") return "infrastructure"
    if (row.failureKind === "quality" || row.failureStage === "acceptance") return "quality"
    if (row.failed) return "failed"
    return "done"
}

function statusLabel(row) {
    const status = runStatus(row)
    if (status === "timeout") return "Timeout"
    if (status === "infrastructure") return "Infra"
    if (status === "quality") return "Calidad"
    if (status === "failed") return "Fallo"
    return "OK"
}

// Orden: las filas sin score van al fondo en vez de mezclarse con los ceros.
function sortKey(row, scoreKey, totalKey) {
    const total = scoreTotal(row, totalKey)
    return total > 0 ? scoreValue(row, scoreKey) / total : -1
}

// Score "calidad por minuto": combina calidad relativa, velocidad y reparaciones
// en un solo número comparable entre perfiles.
//
// Fórmula: (calidadRatio * 100) / minutosPrimerIntento * penalizacionReparaciones
//   - calidadRatio = qualityScore / qualityTotal (0..1)
//   - minutosPrimerIntento = timeToFirstAttempt / 60 (mínimo 0.1 para evitar división por cero)
//   - penalizacionReparaciones = 1.0 / (1 + repairs * 0.25)
//
// Mayor = mejor. Permite ordenar perfiles por eficiencia real, no solo tok/s.
function qualityPerMinute(row) {
    const total = scoreTotal(row, "qualityTotal")
    if (row.failed || total <= 0)
        return 0

    const qualityRatio = scoreValue(row, "qualityScore") / total
    const elapsedSec = row.timeToFirstAttempt ?? row.elapsedSec ?? 0
    if (!isFinite(elapsedSec) || elapsedSec <= 0)
        return 0
    const minutes = elapsedSec / 60
    const repairs = row.repairAttempts ?? row.repairs ?? 0
    const repairPenalty = 1.0 / (1 + repairs * 0.25)

    return (qualityRatio * 100) / minutes * repairPenalty
}

// Etiqueta legible del score calidad/minuto.
function qualityPerMinuteLabel(row) {
    const qpm = qualityPerMinute(row)
    if (qpm <= 0) return "—"
    return qpm.toFixed(1) + " qpm"
}

// Tono visual para calidad/minuto.
function qualityPerMinuteTone(row) {
    const qpm = qualityPerMinute(row)
    if (qpm <= 0) return "muted"
    if (qpm >= 50) return "ok"
    if (qpm >= 20) return "warn"
    return "error"
}
