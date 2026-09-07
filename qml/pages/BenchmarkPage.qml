import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0
import "../components/BenchmarkScore.js" as BenchmarkScore

Item {
    id: root

    property var selectedIds: {
        try { return JSON.parse(App.readSetting("benchSelectedIds", "[]")) } catch (e) { return [] }
    }
    onSelectedIdsChanged: App.writeSetting("benchSelectedIds", JSON.stringify(selectedIds))
    // El historial abre mostrando primero las corridas más recientes.
    property string sortColumn: "date"
    property int sortDirection: -1
    property var failureRow: ({})
    property string modeFilter: ""
    property string benchmarkFilter: ""
    // Filtros estilo Excel por columna: { column: [valores permitidos] }. Vacío = sin filtro.
    property var columnFilters: ({})
    // Anchos redimensionables por columna del historial.
    property var colW: ({
        profile: 200, target: 58, agentProfile: 96, benchmark: 100, thinking: 58, score: 60, firstAttemptScore: 58,
        finalScore: 58, qpm: 68, repairAttempts: 52, timeToFirstAttempt: 62, totalTime: 62,
        passedAfterRepair: 68, tps: 60, ttft: 60, nonGeneration: 78, firstTool: 78, firstWrite: 78, firstEvaluable: 82, seconds: 70, ram: 60, vram: 60, vramGpu0: 66, vramGpu1: 66, date: 118
    })
    function colWidth(c) { const w = colW[c]; return (w !== undefined && w > 0) ? w : 60 }
    function setColWidth(c, w) {
        const m = {}; for (const k in colW) m[k] = colW[k]
        m[c] = Math.max(40, Math.round(w)); colW = m
    }
    property int leftPanelWidth: 280
    property int leftPanelMinWidth: 240
    property int leftPanelMaxWidth: Math.max(leftPanelMinWidth, Math.min(560, Math.max(leftPanelMinWidth, width - 520)))
    property int _savedLeftPanelWidth: parseInt(App.readSetting("benchLeftPanelWidth", "0")) || 0
    property bool _lpRestored: false   // ya se aplicó el ancho guardado
    property bool _optsRestored: false // idem para opciones (target/modo/thinking)

    // Aplica el ancho guardado recién cuando el layout tiene tamaño real
    // (leftPanelMaxWidth > min). Si no, el clamp de arranque lo achicaría.
    function tryRestoreLeftPanel() {
        if (_lpRestored) return
        if (_savedLeftPanelWidth > 0 && leftPanelMaxWidth > leftPanelMinWidth) {
            leftPanelWidth = clampLeftPanelWidth(_savedLeftPanelWidth)
            _lpRestored = true
        }
    }
    onLeftPanelMaxWidthChanged: {
        if (!_lpRestored) tryRestoreLeftPanel()
        else leftPanelWidth = clampLeftPanelWidth(leftPanelWidth)
    }

    function cycleSort(column) {
        if (sortColumn !== column) {
            sortColumn = column
            sortDirection = 1
        } else if (sortDirection === 1) {
            sortDirection = -1
        } else {
            sortColumn = ""
            sortDirection = 0
        }
    }
    function sortIndicator(column) {
        if (sortColumn !== column || sortDirection === 0) return "↕"
        return sortDirection > 0 ? "▲" : "▼"
    }
    function benchmarkTargetLabel(row) {
        const target = (row.target ?? "").toString().toLowerCase()
        return target === "agent" ? "Agente" : "Chat"
    }
    function benchmarkNameLabel(row) {
        const explicitName = (row.benchmarkName ?? "").toString()
        if (explicitName.length > 0) return explicitName
        const mode = (row.mode ?? "").toString().toLowerCase()
        if (mode === "short") return "Corta"
        if (mode === "full") return "Completa"
        const label = (row.runLabel ?? "").toString()
        return label.length > 0 && label !== "standard" ? label : mode.toUpperCase()
    }
    function thinkingLabel(row) {
        if (row.thinkingEnabled === true || row.thinking === true) return "On"
        if (row.thinkingEnabled === false || row.thinking === false) return "Off"
        return "—"
    }
    function sortValue(row, column) {
        if (column === "profile") return (row.profileName ?? "").toString().toLowerCase()
        if (column === "target") return benchmarkTargetLabel(row).toLowerCase()
        if (column === "agentProfile") return (row.agentProfileName ?? "").toString().toLowerCase()
        if (column === "benchmark") return benchmarkNameLabel(row).toLowerCase()
        if (column === "thinking") return thinkingLabel(row).toLowerCase()
        if (column === "score") return BenchmarkScore.sortKey(row, "qualityScore", "qualityTotal")
        if (column === "firstAttemptScore")
            return BenchmarkScore.sortKey(row, "firstAttemptScore", "firstAttemptTotal")
        if (column === "finalScore") return BenchmarkScore.sortKey(row, "finalScore", "finalTotal")
        if (column === "qpm") return BenchmarkScore.qualityPerMinute(row)
        if (column === "repairAttempts") return row.repairAttempts ?? 0
        if (column === "timeToFirstAttempt") return row.timeToFirstAttempt ?? row.elapsedSec ?? 0
        if (column === "totalTime") return row.totalTime ?? row.elapsedSec ?? 0
        if (column === "passedAfterRepair") return (row.passedAfterRepair ?? false) ? 1 : 0
        if (column === "tps") return row.avgTps ?? 0
        if (column === "ttft") return row.avgTtftMs ?? 0
        if (column === "nonGeneration") return row.nonGenerationSec ?? 0
        if (column === "firstTool") return row.firstToolCallSec ?? -1
        if (column === "firstWrite") return row.firstWriteSec ?? -1
        if (column === "firstEvaluable") return row.firstEvaluableSec ?? -1
        if (column === "seconds") return row.elapsedSec ?? 0
        if (column === "ram") return row.ramMb ?? 0
        if (column === "vram") return row.vramMb ?? 0
        if (column === "vramGpu0") return row.vramGpu0Mb ?? 0
        if (column === "vramGpu1") return row.vramGpu1Mb ?? 0
        if (column === "date") return row.timestamp ?? 0
        return ""
    }
    function sortedBenchmarkResults(rows, column, direction) {
        const copy = []
        for (let i = 0; i < rows.length; i++)
            copy.push({ row: rows[i], originalIndex: i })
        if (direction === 0 || column === "")
            return copy.map(x => x.row)
        copy.sort((a, b) => {
            const av = sortValue(a.row, column)
            const bv = sortValue(b.row, column)
            let cmp = 0
            if (typeof av === "number" && typeof bv === "number")
                cmp = av === bv ? 0 : (av < bv ? -1 : 1)
            else
                cmp = av.toString().localeCompare(bv.toString())
            if (cmp === 0) cmp = a.originalIndex - b.originalIndex
            return direction > 0 ? cmp : -cmp
        })
        return copy.map(x => x.row)
    }
    function uniqueColumnValues(rows, getter) {
        const seen = {}
        const out = []
        for (let i = 0; i < rows.length; i++) {
            const value = getter(rows[i])
            if (value.length === 0 || seen[value]) continue
            seen[value] = true
            out.push(value)
        }
        out.sort((a, b) => a.localeCompare(b))
        return out
    }
    function modeFilterValues() {
        return ["Todos"].concat(uniqueColumnValues(App.benchmarkResults, row => benchmarkTargetLabel(row)))
    }
    function benchmarkFilterValues() {
        return ["Todos"].concat(uniqueColumnValues(App.benchmarkResults, row => benchmarkNameLabel(row)))
    }
    function indexOfValue(values, value) {
        for (let i = 0; i < values.length; i++)
            if (values[i] === value) return i
        return 0
    }
    // Texto mostrado de una celda (para agrupar/filtrar por valor, estilo Excel).
    function columnLabel(row, column) {
        if (column === "profile") return (row.profileName ?? "").toString()
        if (column === "target") return benchmarkTargetLabel(row)
        if (column === "agentProfile") { const n = (row.agentProfileName ?? "").toString(); return n.length ? n : "—" }
        if (column === "benchmark") return benchmarkNameLabel(row)
        if (column === "thinking") return thinkingLabel(row)
        if (column === "score") return scoreLabel(row, "qualityScore", "qualityTotal")
        if (column === "firstAttemptScore") return scoreLabel(row, "firstAttemptScore", "firstAttemptTotal")
        if (column === "finalScore") return scoreLabel(row, "finalScore", "finalTotal")
        if (column === "qpm") return BenchmarkScore.qualityPerMinuteLabel(row)
        if (column === "repairAttempts") return String(row.repairAttempts ?? 0)
        if (column === "timeToFirstAttempt") return secondsLabel(row.timeToFirstAttempt ?? row.elapsedSec)
        if (column === "totalTime") return secondsLabel(row.totalTime ?? row.elapsedSec)
        if (column === "passedAfterRepair") return (row.passedAfterRepair ?? false) ? "Sí" : "No"
        if (column === "tps") { const v = row.avgTps ?? 0; return v > 0 ? v.toFixed(1) : "—" }
        if (column === "ttft") { const v = row.avgTtftMs ?? 0; return v > 0 ? Math.round(v) + " ms" : "—" }
        if (column === "nonGeneration") return secondsLabel(row.nonGenerationSec)
        if (column === "firstTool") return secondsLabel(row.firstToolCallSec)
        if (column === "firstWrite") return secondsLabel(row.firstWriteSec)
        if (column === "firstEvaluable") return secondsLabel(row.firstEvaluableSec)
        if (column === "seconds") return secondsLabel(row.elapsedSec)
        if (column === "ram") { const v = row.ramMb ?? 0; return v > 0 ? Math.round(v) + " MB" : "—" }
        if (column === "vram") { const v = row.vramMb ?? 0; return v > 0 ? Math.round(v) + " MB" : "—" }
        if (column === "vramGpu0") { const v = row.vramGpu0Mb ?? 0; return v > 0 ? Math.round(v) + " MB" : "—" }
        if (column === "vramGpu1") { const v = row.vramGpu1Mb ?? 0; return v > 0 ? Math.round(v) + " MB" : "—" }
        if (column === "date") { const t = row.timestamp ?? 0; if (!t) return "—"; const d = new Date(t < 1e12 ? t * 1000 : t); return isNaN(d) ? String(t) : Qt.formatDate(d, "yyyy-MM-dd") }
        return ""
    }
    function distinctColumnValues(column) {
        return uniqueColumnValues(App.benchmarkResults, row => columnLabel(row, column))
    }
    function columnHasFilter(column) {
        const a = columnFilters[column]
        return a !== undefined && a.length > 0
    }
    function activeFilterCount() {
        let n = 0
        for (const k in columnFilters) if (columnFilters[k] && columnFilters[k].length > 0) n++
        return n
    }
    function setColumnFilter(column, values) {
        const cf = {}
        for (const k in columnFilters) cf[k] = columnFilters[k]
        if (!values || values.length === 0) delete cf[column]
        else cf[column] = values
        columnFilters = cf
    }
    function clearAllFilters() { columnFilters = ({}) }

    function selectedCustomDefinition() {
        const defs = App.customBenchmarks || []
        for (let i = 0; i < defs.length; i++)
            if (String(defs[i].id ?? "") === customId) return defs[i]
        return ({})
    }

    function selectedCustomPromptCount() {
        const prompts = selectedCustomDefinition().prompts
        return prompts && prompts.length !== undefined ? prompts.length : 0
    }

    function customBenchmarkText(def) {
        const name = String(def.name || "(sin nombre)")
        const description = String(def.description || "").trim()
        // Los packs importados ya incluyen cantidad y tareas en name/description.
        // Para benchmarks personales sin descripción, indicar al menos su tamaño.
        if (description.length > 0 || name.indexOf("·") >= 0) return name
        const prompts = def.prompts
        const count = prompts && prompts.length !== undefined ? prompts.length : 0
        return count > 0 ? name + " · " + count + (count === 1 ? " ítem" : " ítems") : name
    }

    function recommendedTimeoutSec() {
        const n = Number(selectedCustomDefinition().recommendedTimeoutSec ?? 0)
        return isFinite(n) && n > 0 ? Math.round(n) : 0
    }

    function filteredBenchmarkResults(rows) {
        const out = []
        const cf = columnFilters
        for (let i = 0; i < rows.length; i++) {
            const row = rows[i]
            let ok = true
            for (const col in cf) {
                const allowed = cf[col]
                if (!allowed || allowed.length === 0) continue
                if (allowed.indexOf(columnLabel(row, col)) < 0) { ok = false; break }
            }
            if (ok) out.push(row)
        }
        return out
    }
    function visibleBenchmarkResults() {
        return sortedBenchmarkResults(filteredBenchmarkResults(App.benchmarkResults), sortColumn, sortDirection)
    }
    function scoreLabel(row, scoreKey, totalKey) {
        return BenchmarkScore.scoreLabel(row, scoreKey, totalKey)
    }
    function scoreColor(row, scoreKey, totalKey) {
        const tone = BenchmarkScore.scoreTone(row, scoreKey, totalKey)
        return tone === "muted" ? Theme.textMuted
             : tone === "ok" ? Theme.successText
             : tone === "warn" ? Theme.warnText
             : Theme.errorText
    }
    function secondsLabel(value) {
        const sec = value ?? 0
        return sec > 0 ? sec.toFixed(1) + " s" : "—"
    }
    function clampLeftPanelWidth(value) {
        return Math.max(leftPanelMinWidth, Math.min(leftPanelMaxWidth, Math.round(value)))
    }

    function toggleProfile(profileId) {
        const idx = selectedIds.indexOf(profileId)
        if (idx >= 0) {
            const copy = selectedIds.slice(); copy.splice(idx, 1); selectedIds = copy
        } else {
            selectedIds = selectedIds.concat([profileId])
        }
    }

    // Snapshot reactivo: las llamadas a invokables no forman por sí solas una
    // dependencia de binding; se refresca al marcar/desmarcar desde Perfiles.
    property var benchmarkLaunches: App.profileManager.launchProfilesForMenu()
    Connections {
        target: App.profileManager
        function onLaunchesChanged() {
            root.benchmarkLaunches = App.profileManager.launchProfilesForMenu()
        }
    }

    function benchmarkMarkedIds() {
        const ids = []
        for (const item of (root.benchmarkLaunches || [])) {
            if (item.benchmark && item.id && ids.indexOf(item.id) < 0)
                ids.push(item.id)
        }
        return ids
    }

    // Custom benchmark selection: "" = standard tasks, else a custom benchmark id
    property string customId: App.readSetting("benchCustomId", "")
    onCustomIdChanged: App.writeSetting("benchCustomId", customId)
    // Escalera explícita para comparar cualquier selección con las tres etapas
    // públicas importadas como custom benchmarks.
    property string stageHe0Id: App.readSetting("benchStageHe0Id", "")
    property string stageHe20Id: App.readSetting("benchStageHe20Id", "")
    property string stageBcbId: App.readSetting("benchStageBcbId", "")
    onStageHe0IdChanged: App.writeSetting("benchStageHe0Id", stageHe0Id)
    onStageHe20IdChanged: App.writeSetting("benchStageHe20Id", stageHe20Id)
    onStageBcbIdChanged: App.writeSetting("benchStageBcbId", stageBcbId)

    function customStage(def) {
        const name = String(def.name || "").toLowerCase()
        const count = def.prompts && def.prompts.length !== undefined ? def.prompts.length : 0
        if (name.indexOf("bigcodebench") >= 0 || name.indexOf("bcb") >= 0) return "bcb"
        if (name.indexOf("he0") >= 0 || (name.indexOf("humaneval") >= 0 && count === 1)) return "he0"
        if (name.indexOf("he20") >= 0 || (name.indexOf("humaneval") >= 0 && count >= 20)) return "he20"
        return ""
    }

    function stageDefinitions(stage) {
        const out = []
        for (const def of (App.customBenchmarks || []))
            if (customStage(def) === stage) out.push(def)
        return out
    }

    function stageModel(stage) {
        const out = []
        for (const def of stageDefinitions(stage))
            out.push({ id: def.id, text: customBenchmarkText(def) })
        if (out.length === 0)
            out.push({ id: "", text: "(importá una suite para esta etapa)" })
        return out
    }

    function stageIndex(id, stage) {
        const defs = stageDefinitions(stage)
        for (let i = 0; i < defs.length; ++i)
            if (String(defs[i].id) === String(id)) return i
        return 0
    }

    function synchronizeStageIds() {
        const pick = function(current, stage) {
            const defs = stageDefinitions(stage)
            for (const def of defs) if (String(def.id) === String(current)) return current
            return defs.length > 0 ? String(defs[0].id) : ""
        }
        stageHe0Id = pick(stageHe0Id, "he0")
        stageHe20Id = pick(stageHe20Id, "he20")
        stageBcbId = pick(stageBcbId, "bcb")
    }

    function stageReady() {
        return stageHe0Id.length > 0 && stageHe20Id.length > 0 && stageBcbId.length > 0
    }

    property var proBenchmarkIds: {
        try { return JSON.parse(App.readSetting("benchProBenchmarkIds", "[]")) } catch (e) { return [] }
    }
    onProBenchmarkIdsChanged: App.writeSetting("benchProBenchmarkIds", JSON.stringify(proBenchmarkIds))
    function toggleProBenchmark(id) {
        const a = proBenchmarkIds.slice(), i = a.indexOf(id)
        if (i >= 0) a.splice(i, 1); else a.push(id)
        proBenchmarkIds = a
    }

    Component {
        id: sortableHeader
        Item {
            id: hdr
            property string title: ""
            property string column: ""
            property int columnWidth: 60
            property bool fill: false
            Layout.preferredWidth: root.colWidth(column)
            Layout.minimumWidth: root.colWidth(column)
            height: 30

            RowLayout {
                anchors.fill: parent
                anchors.rightMargin: 5
                spacing: 2
                layoutDirection: hdr.fill ? Qt.LeftToRight : Qt.RightToLeft

                // Embudo de filtro (estilo Excel). Resaltado si la columna está filtrada.
                Text {
                    text: "▾"
                    color: root.columnHasFilter(hdr.column) ? Theme.accent
                           : (funnelHover.hovered ? Theme.textPrimary : Theme.textMuted)
                    font.pixelSize: 11
                    font.bold: root.columnHasFilter(hdr.column)
                    HoverHandler { id: funnelHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { filterPopup.initChecked(); filterPopup.open() } }
                }
                // Título + indicador de orden (tap = ordenar)
                Text {
                    Layout.fillWidth: hdr.fill
                    text: hdr.title + " " + root.sortIndicator(hdr.column)
                    color: titleHover.hovered || root.sortColumn === hdr.column ? Theme.textPrimary : Theme.textSecondary
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: hdr.fill ? Text.AlignLeft : Text.AlignRight
                    HoverHandler { id: titleHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.cycleSort(hdr.column) }
                }
            }

            // Grip de redimensión (borde derecho). Arrastrar para cambiar ancho.
            Rectangle {
                width: 5
                height: parent.height
                anchors.right: parent.right
                color: (gripHover.hovered || gripDrag.active) ? Theme.accent : "transparent"
                HoverHandler { id: gripHover; cursorShape: Qt.SizeHorCursor }
                DragHandler {
                    id: gripDrag
                    target: null
                    property real startW: 0
                    onActiveChanged: if (active) startW = root.colWidth(hdr.column)
                    onTranslationChanged: root.setColWidth(hdr.column, startW + translation.x)
                }
            }

            // Popup estilo Excel: ordenar + lista de valores con checkboxes.
            Popup {
                id: filterPopup
                y: hdr.height + 2
                x: {
                    const p = hdr.mapToItem(root, 0, 0)
                    return Math.max(8 - p.x, Math.min(0, root.width - 8 - width - p.x))
                }
                width: Math.min(300, Math.max(220, root.width - 16))
                padding: 0
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                property var checked: ({})

                function initChecked() {
                    const vals = root.distinctColumnValues(hdr.column)
                    const cur = root.columnFilters[hdr.column]
                    const c = {}
                    for (var i = 0; i < vals.length; i++)
                        c[vals[i]] = (cur === undefined || cur.length === 0) ? true : (cur.indexOf(vals[i]) >= 0)
                    checked = c
                }
                function setVal(v, on) {
                    const c = {}; for (const k in checked) c[k] = checked[k]; c[v] = on; checked = c
                }
                function setAll(on) {
                    const c = {}; const vals = root.distinctColumnValues(hdr.column)
                    for (var i = 0; i < vals.length; i++) c[vals[i]] = on; checked = c
                }
                function apply() {
                    const vals = root.distinctColumnValues(hdr.column)
                    const sel = []; let all = true
                    for (var i = 0; i < vals.length; i++) { if (checked[vals[i]]) sel.push(vals[i]); else all = false }
                    root.setColumnFilter(hdr.column, all ? [] : sel)
                    close()
                }

                background: Rectangle { color: Theme.inputBg; border.color: Theme.borderColor; radius: 6 }
                contentItem: ColumnLayout {
                    spacing: 6
                    // Ordenar
                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: 8; spacing: 6
                        LcButton {
                            text: "↑ Asc"; secondary: true; Layout.fillWidth: true
                            onClicked: { root.sortColumn = hdr.column; root.sortDirection = 1; filterPopup.close() }
                        }
                        LcButton {
                            text: "↓ Desc"; secondary: true; Layout.fillWidth: true
                            onClicked: { root.sortColumn = hdr.column; root.sortDirection = -1; filterPopup.close() }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    // Seleccionar todo
                    LcCheckBox {
                        id: selAll
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        text: "(Seleccionar todo)"
                        tristate: false
                        checked: {
                            const vals = root.distinctColumnValues(hdr.column); let any=false, all=true
                            for (var i=0;i<vals.length;i++){ if(filterPopup.checked[vals[i]]) any=true; else all=false }
                            return all
                        }
                        onToggled: filterPopup.setAll(checked)
                        contentItem: Text { text: selAll.text; color: Theme.textPrimary; font.pixelSize: 12; leftPadding: selAll.indicator.width + 6; verticalAlignment: Text.AlignVCenter }
                    }
                    // Lista de valores
                    ScrollView {
                        Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 4
                        Layout.preferredHeight: Math.min(220, Math.max(40, root.distinctColumnValues(hdr.column).length * 26))
                        clip: true
                        ColumnLayout {
                            width: parent.width; spacing: 1
                            Repeater {
                                model: root.distinctColumnValues(hdr.column)
                                delegate: LcCheckBox {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    checked: filterPopup.checked[modelData] === true
                                    onToggled: filterPopup.setVal(modelData, checked)
                                    text: modelData
                                }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: 8; spacing: 6
                        LcButton { text: "Limpiar"; secondary: true; Layout.fillWidth: true; onClicked: { root.setColumnFilter(hdr.column, []); filterPopup.close() } }
                        LcButton { text: "Aplicar"; Layout.fillWidth: true; onClicked: filterPopup.apply() }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        // Se precargan después del primer pintado desde AppController. Esta
        // página se crea junto con Main.qml aunque aún no sea visible.
        // Restaurar ancho del panel izquierdo (si el layout ya tiene tamaño).
        tryRestoreLeftPanel()
        Qt.callLater(tryRestoreLeftPanel)

        // Restaurar opciones de la sesión anterior.
        if (String(App.readSetting("benchTarget", "model")) === "agent") agentTarget.checked = true
        else modelTarget.checked = true
        const m = String(App.readSetting("benchMode", "short"))
        if (m === "custom") customMode.checked = true
        else if (m === "full") fullMode.checked = true
        else if (m === "stages") stageMode.checked = true
        else shortMode.checked = true
        App.thinkingEnabled = (App.readSetting("benchThinking", App.thinkingEnabled) === true
                               || String(App.readSetting("benchThinking", "")) === "true")
        _optsRestored = true
        synchronizeStageIds()
    }

    Connections {
        target: App
        function onCustomBenchmarksChanged() { root.synchronizeStageIds() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("nav.benchmark"))
            subtitle: App.benchmarkRunning
                ? App.benchmarkStatus
                : "Compará perfiles por RAM, velocidad y calidad"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Panel izquierdo ────────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: root.leftPanelWidth
                Layout.minimumWidth: root.leftPanelMinWidth
                Layout.maximumWidth: root.leftPanelMaxWidth
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ColumnLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 14

                    Text {
                        text: "OBJETIVO"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                    }
                    ColumnLayout {
                        ButtonGroup { id: targetGroup }
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: modelTarget.checked = true }
                            RadioButton {
                                id: modelTarget
                                text: ""
                                checked: true
                                onCheckedChanged: if (root._optsRestored && checked) App.writeSetting("benchTarget", "model")
                                ButtonGroup.group: targetGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                text: "Modo Chat"
                                color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                                font.pixelSize: 12
                            }
                        }
                        Item { height: 2 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: agentTarget.checked = true }
                            RadioButton {
                                id: agentTarget
                                text: ""
                                onCheckedChanged: if (root._optsRestored && checked) App.writeSetting("benchTarget", "agent")
                                ButtonGroup.group: targetGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                text: "Modo Agente"
                                color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                                font.pixelSize: 12
                            }
                        }
                    }

                    // Nivel del agente (perfil): solo aplica al objetivo Agente.
                    // Fija capacidades + directivas para comparar al mismo nivel.
                    Text {
                        text: "NIVEL DEL AGENTE"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                        visible: agentTarget.checked
                    }
                    LcComboBox {
                        id: agentProfileCombo
                        Layout.fillWidth: true
                        visible: agentTarget.checked
                        model: App.profileManager.agentProfiles
                        textRole: "name"; valueRole: "profileId"
                        currentIndex: {
                            const saved = App.readSetting("benchAgentProfile", App.activeAgentProfileId)
                            return Math.max(0, indexOfValue(saved))
                        }
                        onActivated: App.writeSetting("benchAgentProfile", currentValue)
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        contentItem: Text { text: "🤖 " + agentProfileCombo.displayText; color: Theme.textPrimary; font.pixelSize: 12; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                    }

                    CheckBox {
                        id: benchmarkThinkingCheck
                        text: "Thinking"
                        checked: App.thinkingEnabled
                        onToggled: { App.thinkingEnabled = checked; App.writeSetting("benchThinking", checked) }
                        contentItem: Text {
                            text: benchmarkThinkingCheck.text
                            color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                            font.pixelSize: 12
                            leftPadding: benchmarkThinkingCheck.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Text {
                        text: "MODO DE PRUEBA"
                        color: Theme.textSecondary
                        font.pixelSize: 10; font.bold: true
                    }

                    ColumnLayout {
                        ButtonGroup { id: modeGroup }
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: shortMode.checked = true }
                            RadioButton {
                                id: shortMode
                                text: ""
                                checked: true
                                onCheckedChanged: if (root._optsRestored && checked) App.writeSetting("benchMode", "short")
                                ButtonGroup.group: modeGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Corta (~2 min) 2 speed + 5 quality tasks"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                        Item { height: 2 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: fullMode.checked = true }
                            RadioButton {
                                id: fullMode
                                text: ""
                                onCheckedChanged: if (root._optsRestored && checked) App.writeSetting("benchMode", "full")
                                ButtonGroup.group: modeGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Completa (~5 min) 5 speed + 7 quality tasks"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                        Item { height: 2 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: customMode.checked = true }
                            RadioButton {
                                id: customMode
                                text: ""
                                onCheckedChanged: if (root._optsRestored && checked) App.writeSetting("benchMode", "custom")
                                ButtonGroup.group: modeGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Custom benchmark Tus prompts personalizados"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                        Item { height: 2 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            TapHandler { onTapped: stageMode.checked = true }
                            RadioButton {
                                id: stageMode
                                text: ""
                                onCheckedChanged: if (root._optsRestored && checked) App.writeSetting("benchMode", "stages")
                                ButtonGroup.group: modeGroup
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                Layout.minimumWidth: implicitIndicatorWidth
                                Layout.preferredWidth: implicitIndicatorWidth
                                Layout.maximumWidth: implicitIndicatorWidth
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Escalera HE0 → HE20 → BCB (custom)"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // ── Selector de benchmark personalizado (solo en modo custom) ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: customMode.checked

                        LcComboBox {
                            id: benchCombo
                            Layout.fillWidth: true
                            textRole: "text"
                            valueRole: "id"
                            model: {
                                const arr = []
                                const cs = App.customBenchmarks || []
                                for (let i = 0; i < cs.length; i++)
                                    arr.push({ id: cs[i].id, text: root.customBenchmarkText(cs[i]) })
                                if (arr.length === 0)
                                    arr.push({ id: "", text: "(sin benchmarks — creá uno)" })
                                return arr
                            }
                            onActivated: {
                                root.customId = currentValue ?? ""
                                const cs = App.customBenchmarks || []
                                for (let i = 0; i < cs.length; i++) {
                                    if (cs[i].id !== root.customId) continue
                                    // Instalaciones anteriores pueden conservar una copia
                                    // sembrada de la suite v1 sin este campo de metadata.
                                    const fallback = root.customId === "agent_efficiency_e2e_v1" ? 3 : 0
                                    const recommended = parseInt(cs[i].recommendedPasses ?? fallback)
                                    if (recommended > 0) {
                                        passesSpin.value = Math.min(passesSpin.to,
                                                                    Math.max(passesSpin.from, recommended))
                                        App.writeSetting("benchPasses", passesSpin.value)
                                    }
                                    break
                                }
                            }
                            // Reconcile selection to the saved customId. Do NOT write customId
                            // back from currentValue here: the model loads async, so an early
                            // pass would clobber the saved id with index 0 (first benchmark).
                            function _syncToSaved() {
                                const idx = indexOfValue(root.customId)
                                if (idx >= 0) currentIndex = idx
                            }
                            onModelChanged: _syncToSaved()
                            Component.onCompleted: _syncToSaved()
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            rowSpacing: 6
                            columnSpacing: 6
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Pro-Benchmarks (%1)".arg(root.proBenchmarkIds.length)
                                secondary: true
                                onClicked: proBenchPopup.open()
                            }
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Nuevo"
                                onClicked: { editor.loadDef(null); editor.open() }
                            }
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Importar Eval"
                                onClicked: evalImportDialog.open()
                            }
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Editar"
                                enabled: root.customId !== ""
                                onClicked: {
                                    const cs = App.customBenchmarks || []
                                    for (let i = 0; i < cs.length; i++)
                                        if (cs[i].id === root.customId) { editor.loadDef(cs[i]); editor.open(); break }
                                }
                            }
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Borrar"
                                danger: true
                                enabled: root.customId !== ""
                                onClicked: {
                                    App.deleteCustomBenchmark(root.customId)
                                    root.customId = ""
                                    benchCombo.currentIndex = 0
                                }
                            }
                        }

                        Popup {
                            id: proBenchPopup
                            width: Math.min(420, root.width - 24)
                            height: Math.min(460, root.height - 80)
                            anchors.centerIn: Overlay.overlay
                            modal: true
                            focus: true
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            background: Rectangle { color: Theme.surfaceBg; border.color: Theme.borderColor; radius: 8 }
                            contentItem: ColumnLayout {
                                spacing: 8
                                Text { text: "Pro-Benchmarks"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                                Text { text: "Elegí varias suites. Se ejecutan contra todos los perfiles marcados, una por vez."; color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                RowLayout {
                                    Layout.fillWidth: true
                                    LcButton { text: "Todos"; secondary: true; onClicked: { const a=[]; for (const b of (App.customBenchmarks||[])) a.push(b.id); root.proBenchmarkIds=a } }
                                    LcButton { text: "Ninguno"; secondary: true; onClicked: root.proBenchmarkIds=[] }
                                }
                                ScrollView {
                                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                                    ColumnLayout {
                                        width: parent.width; spacing: 2
                                        Repeater {
                                            model: App.customBenchmarks || []
                                            delegate: LcCheckBox {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                text: root.customBenchmarkText(modelData)
                                                checked: root.proBenchmarkIds.indexOf(modelData.id) >= 0
                                                onToggled: root.toggleProBenchmark(modelData.id)
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    LcButton { text: "Cerrar"; secondary: true; Layout.fillWidth: true; onClicked: proBenchPopup.close() }
                                }
                            }
                        }
                    }

                    // ── Escalera pública de tres etapas ────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: stageMode.checked

                        Text {
                            Layout.fillWidth: true
                            text: "SELECCIONÁ LAS SUITES CUSTOM"
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Se ejecutan en orden sobre cada perfil. Si HE0 falla, ese perfil no avanza a HE20; BCB requiere HE20 válido."
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 1
                            rowSpacing: 6
                            Text { text: "HE0 · 1 ítem"; color: Theme.textSecondary; font.pixelSize: 11 }
                            LcComboBox {
                                id: stageHe0Combo
                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "id"
                                model: root.stageModel("he0")
                                currentIndex: root.stageIndex(root.stageHe0Id, "he0")
                                onActivated: root.stageHe0Id = currentValue || ""
                            }
                            Text { text: "HE20 · 20 ítems"; color: Theme.textSecondary; font.pixelSize: 11 }
                            LcComboBox {
                                id: stageHe20Combo
                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "id"
                                model: root.stageModel("he20")
                                currentIndex: root.stageIndex(root.stageHe20Id, "he20")
                                onActivated: root.stageHe20Id = currentValue || ""
                            }
                            Text { text: "BCB · 8 ítems"; color: Theme.textSecondary; font.pixelSize: 11 }
                            LcComboBox {
                                id: stageBcbCombo
                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "id"
                                model: root.stageModel("bcb")
                                currentIndex: root.stageIndex(root.stageBcbId, "bcb")
                                onActivated: root.stageBcbId = currentValue || ""
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !root.stageReady()
                            text: "Importá HumanEval de 1 y 20 ítems y BigCodeBench-Hard desde el selector Custom para habilitar la escalera."
                            color: Theme.warnText
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                    }

                    Rectangle { height: 1; Layout.fillWidth: true; color: Theme.divider }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: "PERFILES A COMPARAR"
                            color: Theme.textSecondary
                            font.pixelSize: 10; font.bold: true
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                        }
                        LcButton {
                            text: "Desmarcar todos"
                            secondary: true
                            enabled: root.selectedIds.length > 0 && !App.benchmarkRunning
                            onClicked: root.selectedIds = []
                        }
                        LcButton {
                            text: "Seleccionar 🏆 benchmark (" + root.benchmarkMarkedIds().length + ")"
                            secondary: true
                            enabled: !App.benchmarkRunning && root.benchmarkMarkedIds().length > 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Selecciona todos los perfiles marcados como pendientes para HE0 → HE20 → BCB"
                            onClicked: root.selectedIds = root.benchmarkMarkedIds()
                        }
                        LcButton {
                            text: "Exportar CSV"
                            secondary: true
                            enabled: (App.benchmarkResults || []).length > 0
                            onClicked: csvExportDialog.open()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 120
                        color: Theme.inputBg
                        radius: 6
                        border.color: Theme.divider; border.width: 1
                        clip: true

                        ListView {
                            id: profileList
                            anchors { fill: parent; margins: 4 }
                            // Menú ordenado: favoritos (★) arriba; displayName = alias - name.
                            property var launchMenu: App.profileManager.launchProfilesForMenu()
                            Connections {
                                target: App.profileManager
                                function onLaunchesChanged() {
                                    profileList.launchMenu = App.profileManager.launchProfilesForMenu()
                                }
                            }
                            model: launchMenu
                            spacing: 2; clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            flickableDirection: Flickable.VerticalFlick

                            delegate: Item {
                                id: pd
                                required property var modelData
                                width: profileList.width; height: 32
                                property string profileId: modelData.id ?? ""
                                property bool isSelected: root.selectedIds.indexOf(profileId) >= 0

                                Rectangle {
                                    anchors.fill: parent; radius: 4
                                    color: pd.isSelected ? Theme.highlight
                                         : (pdHover.containsMouse ? Theme.hoverBg : "transparent")
                                }
                                HoverHandler { id: pdHover }

                                Row {
                                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                                    spacing: 6
                                    CheckBox {
                                        checked: pd.isSelected
                                        anchors.verticalCenter: parent.verticalCenter
                                        onToggled: root.toggleProfile(pd.profileId)
                                    }
                                    Text {
                                        id: profileNameText
                                        text: pd.modelData.displayName || pd.modelData.name || "(sin nombre)"
                                        color: Theme.textPrimary; font.pixelSize: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: profileList.width - 60; elide: Text.ElideRight
                                        ToolTip.visible: pdHover.hovered
                                        ToolTip.delay: 350
                                        ToolTip.timeout: 5000
                                        ToolTip.text: text
                                    }
                                }
                                TapHandler { onTapped: root.toggleProfile(pd.profileId) }
                            }
                            ScrollBar.vertical: LcScrollBar {}
                        }
                    }

                    // Progress bar (visible durante benchmark)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: App.benchmarkRunning

                        ProgressBar {
                            Layout.fillWidth: true
                            value: App.benchmarkProgress / 100.0
                            background: Rectangle { color: Theme.inputBg; radius: 3 }
                            contentItem: Item {
                                Rectangle {
                                    width: parent.width * parent.parent.value
                                    height: parent.height; radius: 3
                                    color: Theme.accent
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: App.benchmarkStatus
                            color: Theme.textMuted; font.pixelSize: 10
                            wrapMode: Text.Wrap; elide: Text.ElideRight
                        }
                    }

                    // Pasadas por perfil
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        enabled: !App.benchmarkRunning
                        Text {
                            text: "Pasadas por perfil"
                            color: Theme.textSecondary; font.pixelSize: 12
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                        }
                        SpinBox {
                            id: passesSpin
                            from: 1; to: 20; editable: true
                            value: Math.min(20, Math.max(1, parseInt(App.readSetting("benchPasses", "1")) || 1))
                            Layout.preferredWidth: 144
                            Layout.minimumWidth: 144
                            onValueModified: App.writeSetting("benchPasses", value)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: customMode.checked && root.customId === "agent_efficiency_e2e_v1"
                        text: "La suite E2E recomienda 3 pasadas. Compará perfiles con Honey ON/OFF; "
                              + "comparison.json agrupa estabilidad, mediana de calidad y tiempo. "
                              + "Revisá también archivos tocados, tool calls, tokens y reparaciones."
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: customMode.checked && root.selectedCustomPromptCount() === 1
                        text: "Esta suite tiene 1 ítem: sirve como smoke test, no para rankear calidad. "
                              + "Para comparar perfiles usá una suite de al menos 10–20 ítems."
                        color: Theme.warnText
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: customMode.checked && root.selectedCustomPromptCount() > 0
                        text: "T First mide desde el primer prompt; T Total incluye setup y carga. "
                              + "Con 2+ pasadas, la comparación usa la mediana caliente (pasadas 2+); la primera queda separada como fría."
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: customMode.checked && root.recommendedTimeoutSec() > 0
                                 && timeoutSpin.value > 0
                                 && timeoutSpin.value < root.recommendedTimeoutSec()
                        text: "Esta suite recomienda al menos "
                              + root.recommendedTimeoutSec()
                              + " s. El timeout actual puede cortar una reparación y mostrar un resultado parcial."
                        color: Theme.warnText
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    // Timeout duro opcional por corrida (wall-clock, segundos). 0 = sin límite.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        enabled: !App.benchmarkRunning
                        Text {
                            text: "Timeout (opcional)"
                            color: Theme.textSecondary; font.pixelSize: 12
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                            ToolTip.visible: timeoutHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: "Tiempo máximo por corrida (segundos). Si una corrida lo supera, se corta SOLO esa con 'Error de timeout' y sigue con las demás. 0 = sin límite."
                            HoverHandler { id: timeoutHover }
                        }
                        SpinBox {
                            id: timeoutSpin
                            from: 0; to: 7200; stepSize: 30; editable: true
                            value: Math.min(7200, Math.max(0, parseInt(App.readSetting("benchTimeout", "0")) || 0))
                            Layout.preferredWidth: 144
                            Layout.minimumWidth: 144
                            textFromValue: function(v) { return v === 0 ? "—" : v + " s" }
                            valueFromText: function(t) { const n = parseInt(t); return isNaN(n) ? 0 : n }
                            onValueModified: App.writeSetting("benchTimeout", value)
                        }
                    }

                    // Benchmark de concurrencia real: mantiene el mismo modelo
                    // y dispara requests simultáneas contra variantes de slots.
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: concurrencyColumn.implicitHeight + 20
                        color: Theme.inputBg
                        radius: 6
                        border.color: Theme.divider
                        ColumnLayout {
                            id: concurrencyColumn
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                            spacing: 6
                            Text {
                                text: "CONCURRENCIA REAL"
                                color: Theme.textSecondary; font.pixelSize: 10; font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Crea copias del perfil y compara requests simultáneas por slot."
                                color: Theme.textMuted; font.pixelSize: 10; wrapMode: Text.Wrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                enabled: !App.benchmarkRunning
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Slots mínimos"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    SpinBox {
                                        id: concurrencyMin
                                        from: 1; to: 16; value: 1; editable: true
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 86
                                        contentItem: TextInput {
                                            text: concurrencyMin.displayText
                                            color: Theme.textPrimary
                                            font.pixelSize: 13
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 20; rightPadding: 20
                                            readOnly: !concurrencyMin.editable
                                            validator: concurrencyMin.validator
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Slots máximos"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    SpinBox {
                                        id: concurrencyMax
                                        from: 1; to: 16; value: 3; editable: true
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 86
                                        contentItem: TextInput {
                                            text: concurrencyMax.displayText
                                            color: Theme.textPrimary
                                            font.pixelSize: 13
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 20; rightPadding: 20
                                            readOnly: !concurrencyMax.editable
                                            validator: concurrencyMax.validator
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Requests"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    SpinBox {
                                        id: concurrencyRequests
                                        from: 2; to: 32; value: 4; editable: true
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 86
                                        contentItem: TextInput {
                                            text: concurrencyRequests.displayText
                                            color: Theme.textPrimary
                                            font.pixelSize: 13
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 20; rightPadding: 20
                                            readOnly: !concurrencyRequests.editable
                                            validator: concurrencyRequests.validator
                                        }
                                    }
                                }
                            }
                            LcButton {
                                Layout.fillWidth: true
                                text: "Medir concurrencia"
                                secondary: true
                                enabled: !App.benchmarkRunning && root.selectedIds.length === 1
                                ToolTip.visible: hovered
                                ToolTip.text: root.selectedIds.length === 1
                                    ? "Compara 1..N slots con 4 requests simultáneas"
                                    : "Seleccioná exactamente un perfil"
                                onClicked: App.startConcurrencyBenchmark(root.selectedIds[0],
                                    concurrencyMin.value, concurrencyMax.value,
                                    concurrencyRequests.value, 128)
                            }
                        }
                    }

                    // Medición reproducible del servidor: separada del score E2E
                    // porque PP/TG/TTFT y la calidad responden preguntas distintas.
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: serverSpeedColumn.implicitHeight + 20
                        color: Theme.inputBg
                        radius: 6
                        border.color: Theme.divider
                        ColumnLayout {
                            id: serverSpeedColumn
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                            spacing: 6
                            Text {
                                text: "SERVER SPEED v1"
                                color: Theme.accent; font.pixelSize: 10; font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Corpus fijo por categoría: PP/TG, TTFT, ITL, cold/warm, prefill 2K–64K y concurrencia. Guarda condiciones y hash para comparar máquinas."
                                color: Theme.textMuted; font.pixelSize: 10; wrapMode: Text.Wrap
                            }
                            RowLayout {
                                Layout.fillWidth: true; enabled: !App.benchmarkRunning; spacing: 8
                                Text { text: "Pasadas"; color: Theme.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                SpinBox {
                                    id: serverSpeedPasses
                                    from: 1; to: 100; editable: true
                                    value: Math.min(100, Math.max(1, parseInt(App.readSetting("serverSpeedPasses", "5")) || 5))
                                    Layout.preferredWidth: 76
                                    onValueModified: App.writeSetting("serverSpeedPasses", value)
                                }
                                Text { text: "Warmup"; color: Theme.textSecondary; font.pixelSize: 11 }
                                SpinBox {
                                    id: serverSpeedWarmup
                                    from: 0; to: 10; editable: true
                                    value: isNaN(parseInt(App.readSetting("serverSpeedWarmup", "1"))) ? 1
                                           : Math.min(10, Math.max(0, parseInt(App.readSetting("serverSpeedWarmup", "1"))))
                                    Layout.preferredWidth: 76
                                    onValueModified: App.writeSetting("serverSpeedWarmup", value)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; enabled: !App.benchmarkRunning; spacing: 8
                                Text { text: "Prefill máx."; color: Theme.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                SpinBox {
                                    id: serverSpeedPrefill
                                    from: 0; to: 65536; stepSize: 2048; editable: true
                                    value: isNaN(parseInt(App.readSetting("serverSpeedPrefill", "65536"))) ? 65536
                                           : Math.min(65536, Math.max(0, parseInt(App.readSetting("serverSpeedPrefill", "65536"))))
                                    Layout.preferredWidth: 100
                                    textFromValue: function(v) { return v === 0 ? "off" : v + " tok" }
                                    valueFromText: function(t) { const n = parseInt(t); return isNaN(n) ? 0 : n }
                                    onValueModified: App.writeSetting("serverSpeedPrefill", value)
                                }
                                CheckBox {
                                    id: serverSpeedPrefillEnabled
                                    text: "PP"; checked: App.readSetting("serverSpeedIncludePrefill", "true") !== "false"
                                    onToggled: App.writeSetting("serverSpeedIncludePrefill", checked ? "true" : "false")
                                }
                                CheckBox {
                                    id: serverSpeedConcurrencyEnabled
                                    text: "Slots"; checked: App.readSetting("serverSpeedIncludeConcurrency", "true") !== "false"
                                    onToggled: App.writeSetting("serverSpeedIncludeConcurrency", checked ? "true" : "false")
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; enabled: !App.benchmarkRunning; spacing: 8
                                Text { text: "Slots / req."; color: Theme.textSecondary; font.pixelSize: 11; Layout.fillWidth: true }
                                SpinBox {
                                    id: serverSpeedSlots
                                    from: 1; to: 16; editable: true
                                    value: Math.min(16, Math.max(1, parseInt(App.readSetting("serverSpeedSlots", "4")) || 4))
                                    Layout.preferredWidth: 68
                                    onValueModified: App.writeSetting("serverSpeedSlots", value)
                                }
                                SpinBox {
                                    id: serverSpeedRequests
                                    from: 1; to: 32; editable: true
                                    value: Math.min(32, Math.max(1, parseInt(App.readSetting("serverSpeedRequests", "4")) || 4))
                                    Layout.preferredWidth: 68
                                    onValueModified: App.writeSetting("serverSpeedRequests", value)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                LcButton {
                                    Layout.fillWidth: true
                                    text: "Medir servidor"
                                    secondary: true
                                    enabled: !App.benchmarkRunning && root.selectedIds.length > 0
                                    onClicked: App.startServerSpeedBenchmark(root.selectedIds,
                                        serverSpeedPasses.value, serverSpeedWarmup.value,
                                        serverSpeedPrefillEnabled.checked,
                                        serverSpeedConcurrencyEnabled.checked,
                                        serverSpeedPrefill.value, serverSpeedSlots.value,
                                        serverSpeedRequests.value)
                                }
                                LcButton {
                                    Layout.fillWidth: true
                                    text: "A/B + A/A"
                                    secondary: true
                                    enabled: !App.benchmarkRunning && root.selectedIds.length === 2
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Compara los dos perfiles seleccionados con orden AB/BA y control A/A"
                                    onClicked: App.startServerSpeedABBenchmark(root.selectedIds[0], root.selectedIds[1],
                                        Math.max(2, serverSpeedPasses.value * 2), serverSpeedWarmup.value)
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        LcButton {
                            Layout.fillWidth: true
                            text: App.benchmarkRunning ? "Cancelar" : "Iniciar benchmark"
                            danger: App.benchmarkRunning
                            enabled: App.benchmarkRunning
                                     || (root.selectedIds.length > 0
                                         && (stageMode.checked
                                             ? root.stageReady()
                                             : ((!customMode.checked || root.customId !== "")
                                                 || root.proBenchmarkIds.length > 0)))
                            onClicked: {
                                if (App.benchmarkRunning) {
                                    App.cancelBenchmark()
                                } else if (stageMode.checked) {
                                    App.startThreeStageBenchmark(root.selectedIds,
                                                                 root.stageHe0Id,
                                                                 root.stageHe20Id,
                                                                 root.stageBcbId,
                                                                 passesSpin.value,
                                                                 agentTarget.checked ? "agent" : "model",
                                                                 timeoutSpin.value,
                                                                 agentTarget.checked ? agentProfileCombo.currentValue : "")
                                } else if (root.proBenchmarkIds.length > 0) {
                                    App.startProBenchmarks(root.selectedIds, root.proBenchmarkIds, passesSpin.value,
                                                           agentTarget.checked ? "agent" : "model", timeoutSpin.value,
                                                           agentTarget.checked ? agentProfileCombo.currentValue : "")
                                } else if (customMode.checked) {
                                    if (root.customId !== "")
                                        App.startCustomBenchmark(root.selectedIds, root.customId, passesSpin.value,
                                                                 agentTarget.checked ? "agent" : "model", timeoutSpin.value,
                                                                 agentTarget.checked ? agentProfileCombo.currentValue : "")
                                } else {
                                    App.startBenchmark(root.selectedIds, shortMode.checked ? "short" : "full", passesSpin.value,
                                                       agentTarget.checked ? "agent" : "model", timeoutSpin.value,
                                                       agentTarget.checked ? agentProfileCombo.currentValue : "")
                                }
                            }
                        }

                    }
                }
            }

            Item {
                id: leftPanelResizeHandle
                Layout.preferredWidth: 10
                Layout.fillHeight: true

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: resizeMouse.pressed || resizeHover.hovered ? 3 : 1
                    height: parent.height
                    color: resizeMouse.pressed || resizeHover.hovered ? Theme.accent : Theme.divider
                }

                HoverHandler {
                    id: resizeHover
                    cursorShape: Qt.SplitHCursor
                }

                MouseArea {
                    id: resizeMouse
                    anchors.fill: parent
                    cursorShape: Qt.SplitHCursor
                    property real pressRootX: 0
                    property int pressWidth: root.leftPanelWidth

                    onPressed: function(mouse) {
                        pressRootX = mapToItem(root, mouse.x, mouse.y).x
                        pressWidth = root.leftPanelWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const currentRootX = mapToItem(root, mouse.x, mouse.y).x
                        root.leftPanelWidth = root.clampLeftPanelWidth(pressWidth + currentRootX - pressRootX)
                    }
                    onReleased: {
                        root._lpRestored = true   // ya no re-aplicar el guardado viejo
                        root._savedLeftPanelWidth = root.leftPanelWidth
                        App.writeSetting("benchLeftPanelWidth", root.leftPanelWidth)
                    }
                }
            }

            // ── Panel derecho: resultados ──────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.baseBg
                clip: true

                ColumnLayout {
                    anchors { fill: parent; margins: 20 }
                    spacing: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: "▾ por columna para filtrar/ordenar"
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                            Text {
                                text: resultsList.count + " resultados"
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 6
                            columnSpacing: 6
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: root.activeFilterCount() > 0 ? "Limpiar filtros (" + root.activeFilterCount() + ")" : "Limpiar filtros"
                                secondary: true
                                enabled: root.activeFilterCount() > 0
                                onClicked: root.clearAllFilters()
                            }
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Best 25"
                                secondary: true
                                onClicked: best25Popup.open()
                            }
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Best modelos · speed"
                                secondary: true
                                onClicked: bestModelosSpeedPopup.open()
                            }
                            LcButton {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                text: "Best modelos · quality"
                                secondary: true
                                onClicked: bestModelosQualityPopup.open()
                            }
                        }
                    }

                    // Cabecera tabla
                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        color: Theme.surfaceBg; radius: 6

                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                            spacing: 0
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("profile"); onLoaded: { item.title = "Perfil"; item.column = "profile"; item.fill = true } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("target"); onLoaded: { item.title = "Modo"; item.column = "target" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("agentProfile"); onLoaded: { item.title = "Nivel"; item.column = "agentProfile" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("benchmark"); onLoaded: { item.title = "Benchmark"; item.column = "benchmark" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("thinking"); onLoaded: { item.title = "Think"; item.column = "thinking" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("score"); onLoaded: { item.title = "Score"; item.column = "score" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("firstAttemptScore"); onLoaded: { item.title = "First"; item.column = "firstAttemptScore" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("finalScore"); onLoaded: { item.title = "Final"; item.column = "finalScore" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("qpm"); onLoaded: { item.title = "QPM"; item.column = "qpm" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("repairAttempts"); onLoaded: { item.title = "Fixes"; item.column = "repairAttempts" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("timeToFirstAttempt"); onLoaded: { item.title = "T First"; item.column = "timeToFirstAttempt" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("totalTime"); onLoaded: { item.title = "T Total"; item.column = "totalTime" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("passedAfterRepair"); onLoaded: { item.title = "Repaired"; item.column = "passedAfterRepair" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("tps"); onLoaded: { item.title = "TPS"; item.column = "tps" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("ttft"); onLoaded: { item.title = "TTFT"; item.column = "ttft" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("nonGeneration"); onLoaded: { item.title = "T No Gen."; item.column = "nonGeneration" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("firstTool"); onLoaded: { item.title = "1ª Tool"; item.column = "firstTool" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("firstWrite"); onLoaded: { item.title = "1ª Escritura"; item.column = "firstWrite" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("firstEvaluable"); onLoaded: { item.title = "1ª Evaluable"; item.column = "firstEvaluable" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("seconds"); onLoaded: { item.title = "Segundos"; item.column = "seconds" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("ram"); onLoaded: { item.title = "RAM"; item.column = "ram" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("vram"); onLoaded: { item.title = "VRAM"; item.column = "vram" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("vramGpu0"); onLoaded: { item.title = "VRAM G0"; item.column = "vramGpu0" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("vramGpu1"); onLoaded: { item.title = "VRAM G1"; item.column = "vramGpu1" } }
                            Loader { sourceComponent: sortableHeader; Layout.preferredWidth: root.colWidth("date"); onLoaded: { item.title = "Fecha"; item.column = "date" } }
                            Item { Layout.fillWidth: true; height: 30 }
                            Item { Layout.preferredWidth: 34; height: 30 }
                        }
                    }

                    Popup {
                        id: best25Popup
                        width: Math.min(860, root.width - 40)
                        height: Math.min(470, root.height - 100)
                        anchors.centerIn: parent
                        modal: true
                        padding: 12
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        background: Rectangle { color: Theme.surfaceBg; radius: 8; border.color: Theme.divider }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "tabla_best_25 · HumanEval (1 ítem)"
                                    color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                LcButton {
                                    text: "Seleccionar 25"
                                    secondary: true
                                    enabled: (App.benchmarkBest25 || []).length > 0
                                    onClicked: {
                                        const ids = []
                                        for (const row of (App.benchmarkBest25 || []))
                                            if (ids.indexOf(row.profileId) < 0) ids.push(row.profileId)
                                        root.selectedIds = ids
                                        best25Popup.close()
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Fast >60 TPS · Balanced >40 TPS · Quality >5 TPS. Orden: calidad y luego TPS."
                                color: Theme.textMuted; font.pixelSize: 11
                            }
                            Rectangle {
                                Layout.fillWidth: true; height: 28; color: Theme.baseBg; radius: 4
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                    Text { Layout.preferredWidth: 70; text: "Grupo / #"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.fillWidth: true; text: "Perfil"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 80; text: "TPS"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    Text { Layout.preferredWidth: 80; text: "Calidad"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    Text { Layout.preferredWidth: 90; text: "T First"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                }
                            }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 1
                                model: App.benchmarkBest25 || []
                                ScrollBar.vertical: LcScrollBar {}
                                delegate: Rectangle {
                                    width: ListView.view.width; height: 29; color: index % 2 ? Theme.baseBg : "transparent"
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                        Text {
                                            Layout.preferredWidth: 70
                                            text: (modelData.best25Category || "") + " #" + (modelData.best25Rank || "")
                                            color: modelData.best25Category === "Fast" ? Theme.successText
                                                 : modelData.best25Category === "Balanced" ? Theme.accent : Theme.warnText
                                            font.pixelSize: 11; font.bold: true
                                        }
                                        Text { Layout.fillWidth: true; text: modelData.profileName || ""; color: Theme.textPrimary; font.pixelSize: 11; elide: Text.ElideRight }
                                        Text { Layout.preferredWidth: 80; text: Number(modelData.avgTps || 0).toFixed(1); color: Theme.textPrimary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                        Text { Layout.preferredWidth: 80; text: (modelData.qualityScore || 0) + "/" + (modelData.qualityTotal || 0); color: Theme.successText; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                        Text { Layout.preferredWidth: 90; text: root.secondsLabel(modelData.timeToFirstAttempt); color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent; visible: parent.count === 0
                                    text: "Todavía no hay corridas rápidas válidas para construir la tabla."
                                    color: Theme.textMuted; font.pixelSize: 12
                                }
                            }
                        }
                    }

                    Popup {
                        id: bestModelosSpeedPopup
                        width: Math.min(900, root.width - 40)
                        height: Math.min(470, root.height - 100)
                        anchors.centerIn: parent
                        modal: true
                        padding: 12
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        background: Rectangle { color: Theme.surfaceBg; radius: 8; border.color: Theme.divider }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "tabla_best_modelos_speed"
                                    color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                LcButton {
                                    text: "Seleccionar top 3 + ⚡ BEST + 🏆 BENCH"
                                    secondary: true
                                    enabled: (App.benchmarkHumanEval20Candidates || []).length > 0
                                    onClicked: {
                                        const ids = []
                                        for (const row of (App.benchmarkHumanEval20Candidates || []))
                                            if (ids.indexOf(row.profileId) < 0) ids.push(row.profileId)
                                        root.selectedIds = ids
                                        bestModelosSpeedPopup.close()
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "HumanEval/0 evalúa hasta 10 perfiles por GGUF; HumanEval/20 recibe sólo los 3 ganadores de cada GGUF y todos los controles ⚡ BEST / 🏆 BENCH."
                                color: Theme.textMuted; font.pixelSize: 11
                            }
                            Rectangle {
                                Layout.fillWidth: true; height: 28; color: Theme.baseBg; radius: 4
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                    Text { Layout.preferredWidth: 70; text: "# / Grupo"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 250; text: "GGUF"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.fillWidth: true; text: "Perfil"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 75; text: "TPS"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    Text { Layout.preferredWidth: 75; text: "Calidad"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                }
                            }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 1
                                model: App.benchmarkBestModelosSpeed || []
                                ScrollBar.vertical: LcScrollBar {}
                                delegate: Rectangle {
                                    width: ListView.view.width; height: 29; color: index % 2 ? Theme.baseBg : "transparent"
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                        Text {
                                            Layout.preferredWidth: 70
                                            text: "#" + (modelData.bestModelosSpeedRank || "") + " " + (modelData.best25Category || "")
                                            color: Theme.accent; font.pixelSize: 11; font.bold: true
                                        }
                                        Text { Layout.preferredWidth: 250; text: modelData.ggufName || "—"; color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideMiddle }
                                        Text { Layout.fillWidth: true; text: modelData.profileName || ""; color: Theme.textPrimary; font.pixelSize: 11; elide: Text.ElideRight }
                                        Text { Layout.preferredWidth: 75; text: Number(modelData.avgTps || 0).toFixed(1); color: Theme.textPrimary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                        Text { Layout.preferredWidth: 75; text: (modelData.qualityScore || 0) + "/" + (modelData.qualityTotal || 0); color: Theme.successText; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent; visible: parent.count === 0
                                    text: "Todavía no hay candidatos con GGUF resuelto."
                                    color: Theme.textMuted; font.pixelSize: 12
                                }
                            }
                        }
                    }

                    Popup {
                        id: bestModelosQualityPopup
                        width: Math.min(980, root.width - 40)
                        height: Math.min(470, root.height - 100)
                        anchors.centerIn: parent
                        modal: true
                        padding: 12
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        background: Rectangle { color: Theme.surfaceBg; radius: 8; border.color: Theme.divider }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "tabla_best_modelos_quality · HumanEval (20 ítems)"
                                    color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                LcButton {
                                text: "Seleccionar finalistas"
                                    secondary: true
                                    enabled: (App.benchmarkBestModelosQuality || []).length > 0
                                    onClicked: {
                                        const ids = []
                                        for (const row of (App.benchmarkBestModelosQuality || []))
                                            if (ids.indexOf(row.profileId) < 0) ids.push(row.profileId)
                                        root.selectedIds = ids
                                        bestModelosQualityPopup.close()
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "HumanEval/20 compara los 3 mejores por GGUF con todos los controles ⚡ BEST / 🏆 BENCH; infra/timeouts quedan fuera."
                                color: Theme.textMuted; font.pixelSize: 11
                            }
                            Rectangle {
                                Layout.fillWidth: true; height: 28; color: Theme.baseBg; radius: 4
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                    Text { Layout.preferredWidth: 45; text: "#"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 220; text: "GGUF"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.fillWidth: true; text: "Perfil"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 85; text: "Origen"; color: Theme.textSecondary; font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 70; text: "Calidad"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    Text { Layout.preferredWidth: 60; text: "TPS"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    Text { Layout.preferredWidth: 75; text: "T total"; color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                }
                            }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 1
                                model: App.benchmarkBestModelosQuality || []
                                ScrollBar.vertical: LcScrollBar {}
                                delegate: Rectangle {
                                    width: ListView.view.width; height: 29; color: index % 2 ? Theme.baseBg : "transparent"
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                        Text { Layout.preferredWidth: 45; text: modelData.bestModelosQualityRank || (index + 1); color: Theme.accent; font.pixelSize: 11; font.bold: true }
                                        Text { Layout.preferredWidth: 220; text: modelData.ggufName || "—"; color: Theme.textSecondary; font.pixelSize: 11; elide: Text.ElideMiddle }
                                        Text { Layout.fillWidth: true; text: modelData.profileName || ""; color: Theme.textPrimary; font.pixelSize: 11; elide: Text.ElideRight }
                                        Text { Layout.preferredWidth: 85; text: modelData.humanEval20Control ? (modelData.benchmarkControl ? "🏆 BENCH" : "⚡ BEST") : "Top 3"; color: Theme.accent; font.pixelSize: 11 }
                                        Text { Layout.preferredWidth: 70; text: (modelData.qualityScore || 0) + "/" + (modelData.qualityTotal || 0); color: Theme.successText; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                        Text { Layout.preferredWidth: 60; text: Number(modelData.avgTps || 0).toFixed(1); color: Theme.textPrimary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                        Text { Layout.preferredWidth: 75; text: root.secondsLabel(modelData.totalTime || modelData.elapsedSec); color: Theme.textSecondary; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent; visible: parent.count === 0
                                    text: "Todavía no hay resultados válidos de HumanEval (20 ítems)."
                                    color: Theme.textMuted; font.pixelSize: 12
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    // Filas de resultados
                    ListView {
                        id: resultsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.visibleBenchmarkResults()
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.VerticalFlick
                        ScrollBar.vertical: LcScrollBar {}

                        // Placeholder vacío
                        Text {
                            anchors.centerIn: parent
                            visible: resultsList.count === 0 && !App.benchmarkRunning
                            text: "Sin resultados — seleccioná perfiles y ejecutá benchmark"
                            color: Theme.textMuted; font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // Spinner durante benchmark sin resultados aún
                        ColumnLayout {
                            anchors.centerIn: parent
                            visible: resultsList.count === 0 && App.benchmarkRunning
                            spacing: 12

                            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: App.benchmarkRunning }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: App.benchmarkStatus
                                color: Theme.textSecondary; font.pixelSize: 13
                            }
                        }

                        delegate: Rectangle {
                            id: resultRow
                            width: resultsList.width
                            height: expanded ? expandedHeight + 36 : 36
                            color: rowHover.containsMouse ? Theme.hoverBg : "transparent"
                            radius: 4
                            clip: true

                            property bool expanded: false
                            property bool failed: modelData.failed ?? false
                            property int expandedHeight: taskList.implicitHeight + 8

                            Behavior on height { NumberAnimation { duration: 150 } }

                            HoverHandler { id: rowHover }

                            // Click derecho → menú contextual
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: rowMenu.popup()
                            }
                            Menu {
                                id: rowMenu
                                MenuItem {
                                    text: "Ver carpeta contenedora"
                                    enabled: (modelData.runDir ?? "") !== ""
                                    onTriggered: App.openBenchmarkFolder(modelData.runDir ?? "")
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: "Borrar fila"
                                    onTriggered: App.removeBenchmarkResultById(modelData.id ?? "")
                                }
                            }

                            // ── Main row ───────────────────────────────────
                            RowLayout {
                                anchors { left: parent.left; right: parent.right; leftMargin: 12; rightMargin: 12 }
                                height: 36
                                spacing: 0

                                Text {
                                    text: (modelData.comparisonProfileName ?? "") !== ""
                                          ? (modelData.profileName ?? "") + " vs " + modelData.comparisonProfileName
                                          : (modelData.profileName ?? "")
                                    color: Theme.textPrimary; font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: root.colWidth("profile")
                                }
                                Text {
                                    text: root.benchmarkTargetLabel(modelData)
                                    color: Theme.textMuted; font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("target"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.columnLabel(modelData, "agentProfile")
                                    color: Theme.textMuted; font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: root.colWidth("agentProfile"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.benchmarkNameLabel(modelData)
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: root.colWidth("benchmark"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.thinkingLabel(modelData)
                                    color: root.thinkingLabel(modelData) === "On" ? Theme.textSecondary : Theme.textMuted
                                    font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("thinking"); horizontalAlignment: Text.AlignRight
                                }
                                Item {
                                    Layout.preferredWidth: root.colWidth("score")
                                    height: 36
                                    Text {
                                        anchors.fill: parent
                                        visible: !resultRow.failed
                                        text: root.scoreLabel(modelData, "qualityScore", "qualityTotal")
                                        color: root.scoreColor(modelData, "qualityScore", "qualityTotal")
                                        font.pixelSize: 12; font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    LcButton {
                                        anchors { verticalCenter: parent.verticalCenter; right: parent.right }
                                        width: BenchmarkScore.runStatus(modelData) === "timeout" ? 66
                                              : BenchmarkScore.runStatus(modelData) === "infrastructure" ? 54
                                              : 68
                                        height: 24
                                        text: BenchmarkScore.statusLabel(modelData)
                                        danger: true
                                        visible: resultRow.failed
                                        onClicked: {
                                            root.failureRow = modelData
                                            failureDialog.open()
                                        }
                                    }
                                }
                                Text {
                                    text: root.scoreLabel(modelData, "firstAttemptScore", "firstAttemptTotal")
                                    color: root.scoreColor(modelData, "firstAttemptScore", "firstAttemptTotal")
                                    font.pixelSize: 11
                                    font.bold: true
                                    Layout.preferredWidth: root.colWidth("firstAttemptScore"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.scoreLabel(modelData, "finalScore", "finalTotal")
                                    color: root.scoreColor(modelData, "finalScore", "finalTotal")
                                    font.pixelSize: 11
                                    font.bold: true
                                    Layout.preferredWidth: root.colWidth("finalScore"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: BenchmarkScore.qualityPerMinuteLabel(modelData)
                                    color: {
                                        const tone = BenchmarkScore.qualityPerMinuteTone(modelData)
                                        return tone === "ok" ? Theme.successText
                                             : tone === "warn" ? Theme.warnText
                                             : tone === "error" ? Theme.errorText : Theme.textMuted
                                    }
                                    font.pixelSize: 11
                                    font.bold: true
                                    Layout.preferredWidth: root.colWidth("qpm"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: (modelData.repairAttempts ?? 0).toString()
                                    color: (modelData.repairAttempts ?? 0) > 0 ? Theme.warnText : Theme.textMuted
                                    font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("repairAttempts"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.secondsLabel(modelData.timeToFirstAttempt ?? modelData.elapsedSec)
                                    color: Theme.textSecondary; font.pixelSize: 10
                                    Layout.preferredWidth: root.colWidth("timeToFirstAttempt"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.secondsLabel(modelData.totalTime ?? modelData.elapsedSec)
                                    color: Theme.textSecondary; font.pixelSize: 10
                                    Layout.preferredWidth: root.colWidth("totalTime"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: (modelData.passedAfterRepair ?? false) ? "Sí" : "No"
                                    color: (modelData.passedAfterRepair ?? false) ? Theme.successText : Theme.textMuted
                                    font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("passedAfterRepair"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const tps = modelData.avgTps ?? 0
                                        return tps > 0 ? tps.toFixed(1) : "—"
                                    }
                                    color: Theme.accent; font.pixelSize: 12
                                    Layout.preferredWidth: root.colWidth("tps"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const ms = modelData.avgTtftMs ?? 0
                                        return ms > 0 ? ms.toFixed(0) + " ms" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("ttft"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: root.secondsLabel(modelData.nonGenerationSec)
                                    color: Theme.warnText; font.pixelSize: 10
                                    Layout.preferredWidth: root.colWidth("nonGeneration"); horizontalAlignment: Text.AlignRight
                                }
                                Text { text: root.secondsLabel(modelData.firstToolCallSec); color: Theme.textSecondary; font.pixelSize: 10; Layout.preferredWidth: root.colWidth("firstTool"); horizontalAlignment: Text.AlignRight }
                                Text { text: root.secondsLabel(modelData.firstWriteSec); color: Theme.textSecondary; font.pixelSize: 10; Layout.preferredWidth: root.colWidth("firstWrite"); horizontalAlignment: Text.AlignRight }
                                Text { text: root.secondsLabel(modelData.firstEvaluableSec); color: Theme.textSecondary; font.pixelSize: 10; Layout.preferredWidth: root.colWidth("firstEvaluable"); horizontalAlignment: Text.AlignRight }
                                Text {
                                    text: {
                                        const sec = modelData.elapsedSec ?? 0
                                        return sec > 0 ? sec.toFixed(1) + " s" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("seconds"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const mb = modelData.ramMb ?? 0
                                        return mb > 0 ? (mb / 1024).toFixed(1) + " GB" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("ram"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const mb = modelData.vramMb ?? 0
                                        return mb > 0 ? mb.toFixed(0) + " MB" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("vram"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const mb = modelData.vramGpu0Mb ?? 0
                                        return mb > 0 ? mb.toFixed(0) + " MB" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("vramGpu0"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const mb = modelData.vramGpu1Mb ?? 0
                                        return mb > 0 ? mb.toFixed(0) + " MB" : "—"
                                    }
                                    color: Theme.textSecondary; font.pixelSize: 11
                                    Layout.preferredWidth: root.colWidth("vramGpu1"); horizontalAlignment: Text.AlignRight
                                }
                                Text {
                                    text: {
                                        const ts = modelData.timestamp ?? 0
                                        return ts > 0 ? Qt.formatDateTime(new Date(ts), "d MMM HH:mm") : ""
                                    }
                                    color: Theme.textMuted; font.pixelSize: 10
                                    Layout.preferredWidth: root.colWidth("date"); horizontalAlignment: Text.AlignRight
                                }
                                Item { Layout.fillWidth: true; height: 36 }
                                Item {
                                    Layout.preferredWidth: 34
                                    height: 36
                                    Rectangle {
                                        id: delBox
                                        width: 24; height: 24; radius: 4
                                        anchors { verticalCenter: parent.verticalCenter; right: parent.right }
                                        color: delHover.containsMouse ? Theme.errorText : "transparent"
                                        border.color: delHover.containsMouse ? Theme.errorText : Theme.divider
                                        border.width: 1
                                        visible: Boolean(rowHover.containsMouse || delHover.containsMouse)
                                        Text {
                                            anchors.centerIn: parent
                                            text: "✕"; font.pixelSize: 12
                                            color: delHover.containsMouse ? "white" : Theme.textMuted
                                        }
                                        HoverHandler { id: delHover }
                                        TapHandler { onTapped: App.removeBenchmarkResultById(modelData.id ?? "") }
                                    }
                                }
                            }

                            TapHandler { onTapped: resultRow.expanded = !resultRow.expanded }

                            // ── Task breakdown (expanded) ──────────────────
                            Column {
                                id: taskList
                                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 36; leftMargin: 24; rightMargin: 12 }
                                spacing: 3
                                visible: resultRow.expanded

                                Rectangle {
                                    visible: resultRow.failed
                                    width: taskList.width
                                    height: failureSummary.implicitHeight + 14
                                    radius: 6
                                    color: Theme.errorBg
                                    border.color: Theme.errorText
                                    border.width: 1

                                    RowLayout {
                                        anchors { fill: parent; margins: 8 }
                                        spacing: 8
                                        Text {
                                            id: failureSummary
                                            text: BenchmarkScore.statusLabel(modelData) + ": "
                                                  + (modelData.failureMessage ?? "Falló la pasada.")
                                            color: Theme.errorText
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        LcButton {
                                            text: "Ver fallo"
                                            danger: true
                                            Layout.preferredHeight: 26
                                            onClicked: {
                                                root.failureRow = modelData
                                                failureDialog.open()
                                            }
                                        }
                                    }
                                }

                                Column {
                                    visible: (modelData.mode ?? "").toString().indexOf("server-speed") === 0
                                    width: taskList.width
                                    spacing: 2
                                    Text {
                                        text: {
                                            const ab = (modelData.mode ?? "") === "server-speed-ab"
                                            if (ab) {
                                                const s = modelData.pairedSummary || {}
                                                const n = modelData.nullTestSummary || {}
                                                return "A/B TG: " + Number(s.deltaPctMedian || 0).toFixed(1) + "% mediana · "
                                                       + (s.winner || "sin datos") + " · A/A: "
                                                       + (modelData.nullTestPassed === false ? "inestable" : "OK")
                                            }
                                            const s = modelData.serverSpeedSummary || {}
                                            return "TG p50 " + (Number(s.decodeTpsP50 || 0) > 0 ? Number(s.decodeTpsP50).toFixed(1) : "—")
                                                   + " t/s · PP p50 " + (Number(s.promptTpsP50 || 0) > 0 ? Number(s.promptTpsP50).toFixed(1) : "—")
                                                   + " t/s · TTFT p50 " + (Number(s.ttftMsP50 || 0) > 0 ? Math.round(Number(s.ttftMsP50)) : "—") + " ms"
                                        }
                                        color: Theme.accent; font.pixelSize: 11; font.bold: true
                                    }
                                    Text {
                                        visible: (modelData.mode ?? "") !== "server-speed-ab"
                                        text: {
                                            const cold = modelData.coldSummary || {}
                                            const warm = modelData.warmSummary || {}
                                            const pre = modelData.prefillSummary || {}
                                            const slots = (modelData.concurrencySweep || []).length
                                            return "Cold/Warm TG: " + Number(cold.decodeTpsMean || 0).toFixed(1) + "/"
                                                   + Number(warm.decodeTpsMean || 0).toFixed(1) + " t/s · prefill: "
                                                   + (Number(pre.promptTpsMean || 0) > 0 ? Number(pre.promptTpsMean).toFixed(1) : "—")
                                                   + " t/s · puntos slots: " + slots
                                        }
                                        color: Theme.textMuted; font.pixelSize: 10
                                    }
                                }

                                Repeater {
                                    model: modelData.tasks ?? []
                                    delegate: Column {
                                        width: taskList.width
                                        spacing: 1
                                        // true cuando el task viene de un benchmark custom (heurísticas presentes)
                                        property bool isCustom: modelData.codeOnly !== undefined

                                        RowLayout {
                                            width: parent.width
                                            spacing: 8

                                            Text {
                                                text: modelData.type === "speed" ? "⚡" : (modelData.passed ? "✓" : "✗")
                                                color: modelData.type === "speed" ? Theme.accent
                                                     : (modelData.passed ? Theme.successText : Theme.errorText)
                                                font.pixelSize: 11
                                                Layout.preferredWidth: 16
                                            }
                                            Text {
                                                text: modelData.id ?? ""
                                                color: Theme.textSecondary; font.pixelSize: 11
                                                Layout.preferredWidth: 160
                                            }
                                            Text {
                                                text: modelData.type === "speed"
                                                    ? (modelData.tps ?? 0).toFixed(1) + " t/s  TTFT " + (modelData.ttft_ms ?? 0).toFixed(0) + " ms"
                                                    : (modelData.passed ? "correcto" : "incorrecto") + "  " + (modelData.elapsed_ms ?? 0) + " ms"
                                                color: Theme.textMuted; font.pixelSize: 10
                                                Layout.fillWidth: true
                                            }
                                        }

                                        // ── Métricas extra (benchmarks custom) ──────────
                                        Text {
                                            visible: parent.isCustom
                                            leftPadding: 24
                                            color: Theme.textMuted; font.pixelSize: 10
                                            text: {
                                                const tok = modelData.tokens ?? 0
                                                const tot = ((modelData.elapsed_ms ?? 0) / 1000.0).toFixed(2)
                                                return tok + " tok · " + tot + " s total"
                                            }
                                        }
                                        Text {
                                            visible: parent.isCustom
                                            leftPadding: 24
                                            font.pixelSize: 10
                                            color: modelData.codeOnly ? Theme.successText : Theme.warnText
                                            text: (modelData.codeOnly ? "✓ solo código" : "✗ incluye prosa")
                                                + (modelData.hasCode
                                                    ? "   ·   " + (modelData.inventedDeps
                                                        ? "⚠ deps no-stdlib (" + (modelData.depCount ?? 0) + "): " + ((modelData.depList ?? []).join(", "))
                                                        : "✓ sin deps externas")
                                                    : "")
                                        }
                                    }
                                }

                                Column {
                                    width: taskList.width
                                    spacing: 3
                                    visible: (modelData.acceptance ?? []).length > 0

                                    Text {
                                        text: "Criterios de aceptación"
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                    Repeater {
                                        model: modelData.acceptance ?? []
                                        delegate: Column {
                                            width: taskList.width
                                            spacing: 1
                                            RowLayout {
                                                width: parent.width
                                                spacing: 8
                                                Text {
                                                    text: modelData.passed ? "✓" : "✗"
                                                    color: modelData.passed ? Theme.successText : Theme.errorText
                                                    font.pixelSize: 11
                                                    Layout.preferredWidth: 16
                                                }
                                                Text {
                                                    text: (modelData.type ?? "") + " · " + (modelData.name ?? modelData.command ?? "")
                                                    color: Theme.textSecondary
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                    Layout.fillWidth: true
                                                }
                                                Text {
                                                    text: modelData.exitCode !== undefined ? ("exit " + modelData.exitCode) : ""
                                                    color: Theme.textMuted
                                                    font.pixelSize: 10
                                                    Layout.preferredWidth: 54
                                                    horizontalAlignment: Text.AlignRight
                                                }
                                            }
                                            Text {
                                                visible: (modelData.output ?? "").length > 0
                                                width: parent.width
                                                leftPadding: 24
                                                text: modelData.output ?? ""
                                                color: modelData.passed ? Theme.textMuted : Theme.errorText
                                                font.family: "Consolas"
                                                font.pixelSize: 10
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 4
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                Item { height: 4 }
                            }
                        }
                    }
                }
            }
        }
    }

    LcDialog {
        id: failureDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent ? parent.width - 80 : 900, 900)
        height: Math.min(parent ? parent.height - 80 : 680, 680)
        title: "Fallo de benchmark"
        footer: null

        background: Rectangle {
            color: Theme.popupBg
            radius: 8
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: root.failureRow.profileName ?? ""
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: {
                    const stage = root.failureRow.failureStage ?? ""
                    const msg = root.failureRow.failureMessage ?? "Falló la pasada."
                    return stage.length > 0 ? stage + ": " + msg : msg
                }
                color: Theme.errorText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.inputBg
                radius: 6
                border.color: Theme.borderColor
                clip: true

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8
                    TextArea {
                        readOnly: true
                        wrapMode: TextArea.Wrap
                        text: root.failureRow.failureDetail ?? ""
                        color: Theme.textSecondary
                        font.family: "Consolas"
                        font.pixelSize: 11
                        background: null
                        selectByMouse: true
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LcButton {
                    text: "Copiar"
                    secondary: true
                    onClicked: App.copyToClipboard((root.failureRow.failureDetail ?? "").toString())
                }
                LcButton {
                    text: "Cerrar"
                    onClicked: failureDialog.close()
                }
            }
        }
    }

    // ── Editor de benchmark personalizado ──────────────────────────────────────
    FileDialog {
        id: csvExportDialog
        title: "Exportar resultados CSV"
        fileMode: FileDialog.SaveFile
        nameFilters: ["CSV (*.csv)"]
        onAccepted: {
            const path = selectedFile.toString().replace(/^file:\/\//, "")
            App.exportBenchmarkResultsCsv(decodeURIComponent(path))
        }
    }

    FileDialog {
        id: evalImportDialog
        title: "Importar EvalSuite (JSON)"
        nameFilters: ["EvalSuite JSON (*.json)"]
        onAccepted: {
            const path = selectedFile.toString().replace(/^file:\/\/\//, "").replace(/^file:\/\//, "")
            const id = App.importEvalSuite(decodeURIComponent(path))
            if (id && id.length > 0) {
                root.customId = id
            } else {
                importErrorDialog.text = App.lastEvalImportError()
                importErrorDialog.open()
            }
        }
    }

    LcDialog {
        id: importErrorDialog
        property alias text: importErrorLabel.text
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(420, parent.width - 48)
        height: 190
        leftPadding: 20
        rightPadding: 20
        title: "No se pudo importar la EvalSuite"
        standardButtons: Dialog.NoButton
        footer: Rectangle {
            color: Theme.popupHeaderBg
            height: 56
            radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            LcButton {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                text: "Aceptar"
                onClicked: importErrorDialog.close()
            }
        }
        Label { id: importErrorLabel; wrapMode: Text.WordWrap; width: 360 }
    }

    Dialog {
        id: editor
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(parent ? parent.width - 80 : 860, 860)
        height: Math.min(parent ? parent.height - 60 : 720, 760)
        padding: 0
        closePolicy: Popup.CloseOnEscape

        property string editId: ""

        Overlay.modal: Rectangle { color: Theme.overlayColor }
        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }

        ListModel { id: taskModel }

        function loadDef(def) {
            taskModel.clear()
            if (def) {
                editId = def.id || ""
                nameField.text = def.name || ""
                const ps = def.prompts || []
                for (let i = 0; i < ps.length; i++) {
                    const acc = ps[i].acceptance || {}
                    const files = acc.files || []
                    const commands = acc.commands || []
                    const cmdLines = []
                    for (let c = 0; c < commands.length; c++)
                        cmdLines.push(commands[c].command || "")
                    taskModel.append({
                        prompt: ps[i].prompt || "",
                        acceptanceFiles: files.join("\n"),
                        acceptanceCommands: cmdLines.join("\n")
                    })
                }
            } else {
                editId = ""
                nameField.text = ""
            }
            if (taskModel.count === 0)
                taskModel.append({ prompt: "", acceptanceFiles: "", acceptanceCommands: "" })
        }

        function save() {
            const prompts = []
            for (let i = 0; i < taskModel.count; i++) {
                const it = taskModel.get(i)
                if ((it.prompt || "").trim() === "") continue
                const files = []
                const rawFiles = (it.acceptanceFiles || "").split(/\r?\n/)
                for (let f = 0; f < rawFiles.length; f++) {
                    const line = rawFiles[f].trim()
                    if (line.length > 0) files.push(line)
                }
                const commands = []
                const rawCommands = (it.acceptanceCommands || "").split(/\r?\n/)
                for (let c = 0; c < rawCommands.length; c++) {
                    const cmd = rawCommands[c].trim()
                    if (cmd.length > 0)
                        commands.push({ name: "cmd_" + (c + 1), command: cmd, timeoutMs: 30000 })
                }
                const promptDef = { id: "task_" + (i + 1), prompt: it.prompt, isSpeed: true, maxTokens: 8192 }
                if (files.length > 0 || commands.length > 0)
                    promptDef.acceptance = { files: files, commands: commands }
                prompts.push(promptDef)
            }
            if (nameField.text.trim() === "" || prompts.length === 0) return
            const def = { name: nameField.text.trim(), prompts: prompts }
            if (editId !== "") def.id = editId
            const newId = App.saveCustomBenchmark(def)
            root.customId = newId
            benchCombo.currentIndex = Math.max(0, benchCombo.indexOfValue(newId))
            editor.close()
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                height: 52
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Text {
                    anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                    text: "Benchmark personalizado"
                    color: Theme.textPrimary; font.pixelSize: 14; font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 18
                spacing: 10

                LcTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "Nombre del benchmark"
                }

                Text {
                    text: "PROMPTS"
                    color: Theme.textSecondary; font.pixelSize: 10; font.bold: true
                }

                ListView {
                    id: taskEditList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: taskModel
                    spacing: 8
                    ScrollBar.vertical: LcScrollBar {}

                    delegate: Rectangle {
                        width: taskEditList.width - 4
                        height: rowCol.implicitHeight + 16
                        color: Theme.inputBg; radius: 6
                        border.color: promptArea.activeFocus ? Theme.inputBorderFocus : Theme.divider
                        border.width: promptArea.activeFocus ? 2 : 1

                        RowLayout {
                            id: rowCol
                            anchors { fill: parent; margins: 8 }
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                TextArea {
                                    id: promptArea
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.max(100, contentHeight + topPadding + bottomPadding)
                                    wrapMode: TextArea.Wrap
                                    placeholderText: "Prompt..."
                                    color: Theme.textPrimary
                                    placeholderTextColor: Theme.textMuted
                                    font.pixelSize: 13
                                    text: model.prompt
                                    onTextChanged: taskModel.setProperty(index, "prompt", text)
                                    background: null
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    TextArea {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 56
                                        wrapMode: TextArea.Wrap
                                        placeholderText: "Archivos esperados, uno por línea"
                                        color: Theme.textSecondary
                                        placeholderTextColor: Theme.textMuted
                                        font.pixelSize: 11
                                        text: model.acceptanceFiles || ""
                                        onTextChanged: taskModel.setProperty(index, "acceptanceFiles", text)
                                        background: Rectangle { color: "transparent"; border.color: Theme.divider; radius: 4 }
                                    }
                                    TextArea {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 56
                                        wrapMode: TextArea.Wrap
                                        placeholderText: "Comandos de aceptación, uno por línea"
                                        color: Theme.textSecondary
                                        placeholderTextColor: Theme.textMuted
                                        font.pixelSize: 11
                                        text: model.acceptanceCommands || ""
                                        onTextChanged: taskModel.setProperty(index, "acceptanceCommands", text)
                                        background: Rectangle { color: "transparent"; border.color: Theme.divider; radius: 4 }
                                    }
                                }
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                width: 24; height: 24; radius: 4
                                color: delTaskHover.containsMouse ? Theme.errorText : "transparent"
                                opacity: taskModel.count > 1 ? 1 : 0.3
                                Text {
                                    anchors.centerIn: parent; text: "✕"; font.pixelSize: 12
                                    color: delTaskHover.containsMouse ? "white" : Theme.textMuted
                                }
                                HoverHandler { id: delTaskHover }
                                TapHandler { onTapped: if (taskModel.count > 1) taskModel.remove(index) }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LcButton {
                        text: "+ Prompt"
                        secondary: true
                        onClicked: taskModel.append({ prompt: "", acceptanceFiles: "", acceptanceCommands: "" })
                    }
                    Item { Layout.fillWidth: true }
                    LcButton { text: "Cancelar"; secondary: true; onClicked: editor.close() }
                    LcButton { text: "Guardar"; onClicked: editor.save() }
                }
            }
        }
    }
}
