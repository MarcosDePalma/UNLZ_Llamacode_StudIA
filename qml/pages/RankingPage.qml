import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0
import "../components/BenchmarkScore.js" as BenchmarkScore

Item {
    id: root

    property string sortColumn: String(App.readSetting("rankingSortColumn", "profile"))
    property int sortDirection: {
        const d = parseInt(App.readSetting("rankingSortDirection", "1"))
        return isNaN(d) ? 1 : d
    }
    property string targetFilter: String(App.readSetting("rankingTargetFilter", "Todos"))
    property bool onlyComplete: App.readSetting("rankingOnlyComplete", false) === true
                                 || String(App.readSetting("rankingOnlyComplete", "")) === "true"
    onSortColumnChanged: { App.writeSetting("rankingSortColumn", sortColumn); scheduleRebuild() }
    onSortDirectionChanged: { App.writeSetting("rankingSortDirection", sortDirection); scheduleRebuild() }
    onTargetFilterChanged: { App.writeSetting("rankingTargetFilter", targetFilter); scheduleRebuild() }
    onOnlyCompleteChanged: { App.writeSetting("rankingOnlyComplete", onlyComplete); scheduleRebuild() }
    // La vista nativa comparte con el dashboard la idea de una tabla
    // configurable: los filtros, el orden y las columnas sobreviven a F5.
    property var columnFilters: {
        try { return JSON.parse(App.readSetting("rankingColumnFilters", "{}")) || ({}) }
        catch (e) { return ({}) }
    }
    property var visibleColumnKeys: {
        const recommended = ["profile", "he0", "he20", "bcb", "he0Time", "he20Time",
                             "bcbTime", "failure", "he0Tps", "he20Tps", "bcbTps",
                             "agent", "thinking", "harness", "specs"]
        try {
            const saved = JSON.parse(App.readSetting("rankingVisibleColumns", "[]"))
            return saved && saved.length ? saved : recommended
        } catch (e) { return recommended }
    }
    property var columnWidths: {
        try { return JSON.parse(App.readSetting("rankingColumnWidths", "{}")) || ({}) }
        catch (e) { return ({}) }
    }
    property var specsCache: ({})
    property var profileConfigCache: ({})
    property var allRankingRows: []
    property var displayedRows: []
    readonly property string emptyFilterToken: "__LLAMACODE_NO_VALUES__"
    onColumnFiltersChanged: { App.writeSetting("rankingColumnFilters", JSON.stringify(columnFilters)); scheduleRebuild() }
    onVisibleColumnKeysChanged: App.writeSetting("rankingVisibleColumns", JSON.stringify(visibleColumnKeys))
    onColumnWidthsChanged: App.writeSetting("rankingColumnWidths", JSON.stringify(columnWidths))

    Timer {
        id: rebuildTimer
        interval: 0
        repeat: false
        onTriggered: root.rebuildRows()
    }

    Connections {
        target: App
        function onBenchmarkResultsChanged() { root.scheduleRebuild() }
    }

    Component.onCompleted: scheduleRebuild()

    readonly property var columns: [
        { key: "profile", title: "Perfil", width: 250 },
        { key: "target", title: "Modo", width: 72 },
        { key: "agent", title: "Nivel", width: 125 },
        { key: "he0", title: "HE0", width: 86 },
        { key: "he20", title: "HE20", width: 92 },
        { key: "bcb", title: "BCB", width: 86 },
        { key: "he0Time", title: "T HE0", width: 88 },
        { key: "he20Time", title: "T HE20", width: 88 },
        { key: "bcbTime", title: "T BCB", width: 88 },
        { key: "failure", title: "FailureKind", width: 150 },
        { key: "he0Tps", title: "TPS HE0", width: 88 },
        { key: "he20Tps", title: "TPS HE20", width: 94 },
        { key: "bcbTps", title: "TPS BCB", width: 88 },
        { key: "he0Ttft", title: "TTFT HE0", width: 94 },
        { key: "he20Ttft", title: "TTFT HE20", width: 100 },
        { key: "bcbTtft", title: "TTFT BCB", width: 94 },
        { key: "ram", title: "RAM", width: 86 },
        { key: "vram", title: "VRAM", width: 86 },
        { key: "thinking", title: "Thinking", width: 105 },
        { key: "harness", title: "Harness", width: 150 },
        { key: "specs", title: "Specs", width: 210 },
        { key: "date", title: "Última fecha", width: 112 }
    ]

    readonly property var recommendedColumnKeys: ["profile", "he0", "he20", "bcb", "he0Time", "he20Time",
        "bcbTime", "failure", "he0Tps", "he20Tps", "bcbTps", "agent", "thinking", "harness", "specs"]

    function columnWidth(column) {
        const saved = Number(columnWidths[column.key])
        return isFinite(saved) && saved >= 48 ? Math.round(saved) : column.width
    }
    function setColumnWidth(key, width) {
        const copy = {}
        for (const k in columnWidths) copy[k] = columnWidths[k]
        copy[key] = Math.max(48, Math.round(width))
        columnWidths = copy
    }

    function scheduleRebuild() {
        if (rebuildTimer) rebuildTimer.restart()
    }
    function shownColumns() {
        const out = []
        for (const key of visibleColumnKeys)
            for (const column of columns)
                if (column.key === key) { out.push(column); break }
        return out.length ? out : columns
    }
    function setVisibleColumns(keys) { visibleColumnKeys = keys.slice() }
    function allColumnKeys() {
        const out = []
        for (const column of columns) out.push(column.key)
        return out
    }
    function toggleVisibleColumn(key, checked) {
        const copy = visibleColumnKeys.slice()
        const i = copy.indexOf(key)
        if (checked && i < 0) copy.push(key)
        if (!checked && i >= 0 && copy.length > 1) copy.splice(i, 1)
        visibleColumnKeys = copy
    }
    function moveVisibleColumn(key, delta) {
        const copy = visibleColumnKeys.slice()
        const i = copy.indexOf(key), j = i + delta
        if (i < 0 || j < 0 || j >= copy.length) return
        const value = copy[i]; copy[i] = copy[j]; copy[j] = value
        visibleColumnKeys = copy
    }

    function sortIndicator(key) {
        if (sortColumn !== key || sortDirection === 0) return "↕"
        return sortDirection > 0 ? "▲" : "▼"
    }

    function cycleSort(key) {
        if (sortColumn !== key) {
            sortColumn = key
            sortDirection = 1
        } else if (sortDirection === 1) {
            sortDirection = -1
        } else {
            sortColumn = ""
            sortDirection = 0
        }
    }

    function stageRow(row, stage) { return row[stage + "Result"] || ({}) }
    function stageScore(row, stage) {
        return BenchmarkScore.sortKey(stageRow(row, stage), "qualityScore", "qualityTotal")
    }
    function stageStatus(row, stage) {
        if (!row[stage + "Has"]) return "—"
        const result = stageRow(row, stage)
        const status = BenchmarkScore.runStatus(result)
        if (status !== "done") return BenchmarkScore.statusLabel(result)
        return String(result.failureKind || "none")
    }
    function stageLines(row, getter) {
        return ["he0", "he20", "bcb"].map(function(stage) {
            return stage.toUpperCase() + ": " + getter(row, stage)
        }).join("\n")
    }

    function profileConfig(profileId) {
        const key = String(profileId || "")
        if (!key) return ({})
        if (profileConfigCache[key] !== undefined) return profileConfigCache[key]
        const lp = App.profileManager.getLaunchProfile(key)
        const config = {
            launch: lp || ({}),
            model: lp && lp.modelProfileId ? App.profileManager.getModelProfile(lp.modelProfileId) : ({}),
            runtime: lp && lp.runtimePresetId ? App.profileManager.getRuntimePreset(lp.runtimePresetId) : ({}),
            backend: lp && lp.backendProfileId ? App.profileManager.getBackend(lp.backendProfileId) : ({})
        }
        const copy = {}
        for (const k in profileConfigCache) copy[k] = profileConfigCache[k]
        copy[key] = config
        profileConfigCache = copy
        return config
    }
    function numberText(value, suffix) {
        const n = Number(value)
        if (!isFinite(n) || n <= 0) return "—"
        let text = n.toFixed(2).replace(/\.?0+$/, "")
        return suffix ? text + suffix : text
    }
    function latestStage(row) {
        for (const stage of ["bcb", "he20", "he0"])
            if (row[stage + "Has"]) return stageRow(row, stage)
        return ({})
    }
    function harnessText(row) {
        return stageLines(row, function(r, stage) {
            if (!r[stage + "Has"]) return "—"
            const result = stageRow(r, stage)
            const id = String(result.harnessEngineId || "legacy")
            const version = result.harnessEngineVersion !== undefined
                ? " v" + result.harnessEngineVersion : ""
            return id + version
        })
    }
    function specsText(row) {
        const cacheKey = String(row.profileId || "")
        if (cacheKey && specsCache[cacheKey] !== undefined) return specsCache[cacheKey]
        const result = latestStage(row)
        const parts = []
        const config = profileConfig(cacheKey)
        const lp = config.launch || ({})
        const mp = config.model || ({})
        const rt = config.runtime || ({})
        const bp = config.backend || ({})
        if (mp && mp.mmprojId) parts.push("Visión: sí")
        else parts.push("Visión: no")
        if (mp && (mp.specType === "draft-mtp" || mp.specType === "draft-dspark" || mp.draftModelId)) {
            const n = Number(mp.specDraftNMax || 0)
            const kind = mp.specType === "draft-dspark" ? "DSpark"
                         : mp.specType === "draft-mtp" ? "MTP" : "draft"
            parts.push("Drafter: " + kind + (n > 0 ? n : ""))
        } else parts.push("Drafter: ninguno")
        if (rt && Number(rt.ctx || 0) > 0) parts.push("Contexto: " + Math.round(Number(rt.ctx) / 1024) + "k")
        if (rt && rt.cacheType) parts.push("KV: " + rt.cacheType)
        if (mp && mp.name) parts.push("Modelo: " + mp.name)
        if (bp && bp.name) parts.push("Runtime: " + bp.name)
        const fingerprint = String(result.profileConfigFingerprint || "")
        const hash = String(result.harnessSpecHash || "")
        if (fingerprint.length) parts.push("Config: " + fingerprint.replace(/^sha256:/, "").slice(0, 12))
        if (hash.length) parts.push("Harness: " + hash.replace(/^sha256:/, "").slice(0, 12))
        if (result.agentVariant) parts.push("Variant: " + result.agentVariant)
        const value = parts.length ? parts.join("\n") : "—"
        if (cacheKey) {
            const copy = {}
            for (const k in specsCache) copy[k] = specsCache[k]
            copy[cacheKey] = value
            specsCache = copy
        }
        return value
    }

    function sortValue(row, key) {
        if (key === "profile") return String(row.profileName || "").toLowerCase()
        if (key === "target") return String(row.target || "").toLowerCase()
        if (key === "agent") return String(row.agentProfileName || "").toLowerCase()
        if (key === "he0" || key === "he20" || key === "bcb") return stageScore(row, key)
        if (key === "failure") return stageLines(row, function(r, stage) { return stageStatus(r, stage) }).toLowerCase()
        if (key === "he0Time") return Number(row.he0ElapsedSec || -1)
        if (key === "he20Time") return Number(row.he20ElapsedSec || -1)
        if (key === "bcbTime") return Number(row.bcbElapsedSec || -1)
        if (key === "he0Tps") return Number(row.he0Tps || -1)
        if (key === "he20Tps") return Number(row.he20Tps || -1)
        if (key === "bcbTps") return Number(row.bcbTps || -1)
        if (key === "he0Ttft") return Number(stageRow(row, "he0").avgTtftMs || -1)
        if (key === "he20Ttft") return Number(stageRow(row, "he20").avgTtftMs || -1)
        if (key === "bcbTtft") return Number(stageRow(row, "bcb").avgTtftMs || -1)
        if (key === "ram") return Math.max(Number(row.he0RamMb || 0), Number(row.he20RamMb || 0), Number(row.bcbRamMb || 0))
        if (key === "vram") return Math.max(Number(row.he0VramMb || 0), Number(row.he20VramMb || 0), Number(row.bcbVramMb || 0))
        if (key === "thinking") return row.thinkingEnabled ? "on" : "off"
        if (key === "harness") return harnessText(row).toLowerCase()
        if (key === "specs") return specsText(row).toLowerCase()
        if (key === "date") return Number(row.latestTimestamp || 0)
        return ""
    }

    function rebuildRows() {
        const source = App.benchmarkRanking || []
        allRankingRows = source.slice()
        const filtered = []
        for (let i = 0; i < source.length; ++i) {
            const row = source[i]
            if (targetFilter !== "Todos" && String(row.target || "").toLowerCase() !== targetFilter)
                continue
            if (onlyComplete && !row.complete) continue
            let matches = true
            for (const column in columnFilters) {
                const allowed = columnFilters[column]
                if (allowed && allowed.length &&
                        (allowed.indexOf(emptyFilterToken) >= 0 || allowed.indexOf(columnLabel(row, column)) < 0)) {
                    matches = false
                    break
                }
            }
            if (!matches) continue
            filtered.push(row)
        }
        if (sortDirection !== 0 && sortColumn !== "") filtered.sort(function(a, b) {
            const av = sortValue(a, sortColumn), bv = sortValue(b, sortColumn)
            let cmp = 0
            if (typeof av === "number" && typeof bv === "number")
                cmp = av === bv ? 0 : (av < bv ? -1 : 1)
            else
                cmp = String(av).localeCompare(String(bv))
            if (cmp === 0) cmp = String(a.profileName || "").localeCompare(String(b.profileName || ""))
            return sortDirection > 0 ? cmp : -cmp
        })
        displayedRows = filtered
    }

    function rows() {
        return displayedRows
    }

    function scoreText(row, stage) {
        if (!row[stage + "Has"]) return "—"
        const r = stageRow(row, stage)
        const status = BenchmarkScore.runStatus(r)
        if (status !== "done") return BenchmarkScore.statusLabel(r)
        return BenchmarkScore.scoreLabel(r, "qualityScore", "qualityTotal")
    }

    function scoreColor(row, stage) {
        if (!row[stage + "Has"]) return Theme.textMuted
        const r = stageRow(row, stage)
        const status = BenchmarkScore.runStatus(r)
        if (status !== "done") return Theme.errorText
        const tone = BenchmarkScore.scoreTone(r, "qualityScore", "qualityTotal")
        return tone === "ok" ? Theme.successText
             : tone === "warn" ? Theme.warnText
             : tone === "muted" ? Theme.textMuted : Theme.errorText
    }

    function seconds(value) { return numberText(value, " s") }
    function tps(value) { return numberText(value) }
    function memory(value) { return Number(value || 0) > 0 ? Math.round(Number(value)) + " MB" : "—" }
    function date(value) {
        const n = Number(value || 0)
        if (!n) return "—"
        const d = new Date(n < 1000000000000 ? n * 1000 : n)
        return isNaN(d) ? "—" : Qt.formatDate(d, "yyyy-MM-dd")
    }

    function cellText(row, key) {
        if (key === "profile") return String(row.profileName || row.profileId || "—")
        if (key === "target") return String(row.target || "model") === "agent" ? "Agente" : "Chat"
        if (key === "agent") return String(row.agentProfileName || "—")
        if (key === "he0" || key === "he20" || key === "bcb") return scoreText(row, key)
        if (key === "failure") return stageLines(row, function(r, stage) { return stageStatus(r, stage) })
        if (key === "he0Time") return seconds(row.he0ElapsedSec)
        if (key === "he20Time") return seconds(row.he20ElapsedSec)
        if (key === "bcbTime") return seconds(row.bcbElapsedSec)
        if (key === "he0Tps") return tps(row.he0Tps)
        if (key === "he20Tps") return tps(row.he20Tps)
        if (key === "bcbTps") return tps(row.bcbTps)
        if (key === "he0Ttft") return numberText(stageRow(row, "he0").avgTtftMs, " ms")
        if (key === "he20Ttft") return numberText(stageRow(row, "he20").avgTtftMs, " ms")
        if (key === "bcbTtft") return numberText(stageRow(row, "bcb").avgTtftMs, " ms")
        if (key === "ram") return memory(Math.max(Number(row.he0RamMb || 0), Number(row.he20RamMb || 0), Number(row.bcbRamMb || 0)))
        if (key === "vram") return memory(Math.max(Number(row.he0VramMb || 0), Number(row.he20VramMb || 0), Number(row.bcbVramMb || 0)))
        if (key === "thinking") return row.thinkingEnabled ? "On" : "Off"
        if (key === "harness") return harnessText(row)
        if (key === "specs") return specsText(row)
        if (key === "date") return date(row.latestTimestamp)
        return "—"
    }

    function cellColor(row, key) {
        if (key === "he0" || key === "he20" || key === "bcb") return scoreColor(row, key)
        if (key === "failure") {
            const value = stageLines(row, function(r, stage) { return stageStatus(r, stage) })
            return value.indexOf("none") >= 0 && value.indexOf("quality") < 0
                ? Theme.successText : Theme.warnText
        }
        if (key === "profile") return Theme.textPrimary
        return Theme.textSecondary
    }

    function columnLabel(row, key) { return cellText(row, key) }
    function distinctColumnValues(key) {
        const seen = {}, values = []
        for (const row of allRankingRows) {
            const value = columnLabel(row, key)
            if (seen[value]) continue
            seen[value] = true
            values.push(value)
        }
        values.sort(function(a, b) { return String(a).localeCompare(String(b)) })
        return values
    }
    function columnHasFilter(key) {
        return columnFilters[key] !== undefined && columnFilters[key].length > 0
    }
    function activeFilterCount() {
        let count = 0
        for (const key in columnFilters)
            if (columnFilters[key] && columnFilters[key].length) count++
        return count
    }
    function setColumnFilter(key, values) {
        const copy = {}
        for (const k in columnFilters) copy[k] = columnFilters[k]
        if (values && values.length) copy[key] = values
        else delete copy[key]
        columnFilters = copy
    }
    function clearAllFilters() { columnFilters = ({}) }

    function openFilterFor(key) {
        filterPanel.openFor(key)
    }

    readonly property int tableWidth: {
        let total = 0
        for (const column of shownColumns()) total += columnWidth(column)
        return total
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("nav.ranking"))
            subtitle: "Compará las últimas corridas HE0, HE20 y BCB por perfil"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 10

            Text {
                text: "RESULTADOS AGRUPADOS"
                color: Theme.textSecondary
                font.pixelSize: 10
                font.bold: true
            }
            LcComboBox {
                id: targetCombo
                Layout.preferredWidth: 130
                model: ["Todos", "model", "agent"]
                currentIndex: targetFilter === "Todos" ? 0 : (targetFilter === "model" ? 1 : 2)
                onActivated: root.targetFilter = currentText
                contentItem: Text {
                    text: targetCombo.displayText === "model" ? "Chat" : targetCombo.displayText === "agent" ? "Agente" : "Todos"
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    leftPadding: 8
                    verticalAlignment: Text.AlignVCenter
                }
            }
            CheckBox {
                id: completeCheck
                text: "Sólo las 3 etapas"
                checked: root.onlyComplete
                onToggled: root.onlyComplete = checked
                contentItem: Text {
                    text: completeCheck.text
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    leftPadding: completeCheck.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }
            LcButton {
                text: root.activeFilterCount() > 0
                      ? "Limpiar filtros (" + root.activeFilterCount() + ")"
                      : "Limpiar filtros"
                secondary: true
                enabled: root.activeFilterCount() > 0
                onClicked: root.clearAllFilters()
            }
            LcButton {
                text: root.activeFilterCount() > 0
                      ? "Filtros (" + root.activeFilterCount() + ")"
                      : "Filtros"
                onClicked: filterPanel.openFor(root.sortColumn || "profile")
            }
            LcButton {
                id: visibleColumnsButton
                text: "Columnas visibles"
                secondary: true
                onClicked: columnsPopup.open()
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.displayedRows.length + " perfil(es) · " + root.shownColumns().length
                      + "/" + root.columns.length + " columnas"
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }

        Popup {
            id: columnsPopup
            x: Math.max(12, root.width - width - 20)
            y: 78
            width: Math.min(410, root.width - 24)
            height: Math.min(520, root.height - 92)
            padding: 10
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            background: Rectangle { color: Theme.surfaceBg; border.color: Theme.borderColor; radius: 8 }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Text {
                    text: "Columnas visibles y orden"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                Text {
                    text: "Desactivá columnas o reordenalas con ↑ / ↓. La selección queda guardada."
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    LcButton {
                        text: "Recomendadas"
                        secondary: true
                        Layout.fillWidth: true
                        onClicked: root.setVisibleColumns(root.recommendedColumnKeys)
                    }
                    LcButton {
                        text: "Todas"
                        secondary: true
                        Layout.fillWidth: true
                        onClicked: root.setVisibleColumns(root.allColumnKeys())
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 2
                        Repeater {
                            model: root.columns
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 3
                                CheckBox {
                                    id: columnCheck
                                    checked: root.visibleColumnKeys.indexOf(modelData.key) >= 0
                                    text: modelData.title
                                    Layout.fillWidth: true
                                    onToggled: root.toggleVisibleColumn(modelData.key, checked)
                                    contentItem: Text {
                                        text: columnCheck.text
                                        color: Theme.textPrimary
                                        font.pixelSize: 12
                                        leftPadding: columnCheck.indicator.width + 6
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                }
                                LcButton {
                                    text: "↑"
                                    secondary: true
                                    enabled: root.visibleColumnKeys.indexOf(modelData.key) > 0
                                    Layout.preferredWidth: 30
                                    onClicked: root.moveVisibleColumn(modelData.key, -1)
                                }
                                LcButton {
                                    text: "↓"
                                    secondary: true
                                    enabled: {
                                        const i = root.visibleColumnKeys.indexOf(modelData.key)
                                        return i >= 0 && i < root.visibleColumnKeys.length - 1
                                    }
                                    Layout.preferredWidth: 30
                                    onClicked: root.moveVisibleColumn(modelData.key, 1)
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    LcButton {
                        text: "Cerrar"
                        secondary: true
                        Layout.fillWidth: true
                        onClicked: columnsPopup.close()
                    }
                }
            }
        }

        Popup {
            id: filterPanel
            x: Math.max(12, root.width - width - 20)
            y: 78
            width: Math.min(360, root.width - 24)
            height: Math.min(520, root.height - 92)
            padding: 10
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            property string columnKey: "profile"
            property var values: []
            property var checkedValues: ({})

            function openFor(key) {
                columnKey = key || "profile"
                const columnIndex = root.columns.findIndex(function(column) {
                    return column.key === columnKey
                })
                if (columnIndex >= 0) filterColumnCombo.currentIndex = columnIndex
                values = root.distinctColumnValues(columnKey)
                const current = root.columnFilters[columnKey]
                const noValues = current && current.length === 1
                                  && current[0] === root.emptyFilterToken
                const copy = {}
                for (const value of values)
                    copy[value] = noValues || !current || !current.length
                                  ? !noValues : current.indexOf(value) >= 0
                checkedValues = copy
                open()
            }

            function setValue(value, checked) {
                const copy = {}
                for (const k in checkedValues) copy[k] = checkedValues[k]
                copy[value] = checked
                checkedValues = copy
            }

            function setAll(checked) {
                const copy = {}
                for (const value of values) copy[value] = checked
                checkedValues = copy
            }

            function applyFilter() {
                const selected = []
                let all = true
                for (const value of values) {
                    if (checkedValues[value]) selected.push(value)
                    else all = false
                }
                if (all) root.setColumnFilter(columnKey, [])
                else if (!selected.length) root.setColumnFilter(columnKey, [root.emptyFilterToken])
                else root.setColumnFilter(columnKey, selected)
                close()
            }

            background: Rectangle {
                color: Theme.inputBg
                border.color: Theme.borderColor
                radius: 8
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Text {
                    text: "Filtrar resultados"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Columna"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcComboBox {
                        id: filterColumnCombo
                        Layout.fillWidth: true
                        model: root.columns.map(function(column) { return column.title })
                        currentIndex: root.columns.findIndex(function(column) {
                            return column.key === filterPanel.columnKey
                        })
                        onActivated: filterPanel.openFor(root.columns[currentIndex].key)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    LcButton {
                        text: "↑ Asc"
                        secondary: true
                        Layout.fillWidth: true
                        onClicked: { root.sortColumn = filterPanel.columnKey; root.sortDirection = 1 }
                    }
                    LcButton {
                        text: "↓ Desc"
                        secondary: true
                        Layout.fillWidth: true
                        onClicked: { root.sortColumn = filterPanel.columnKey; root.sortDirection = -1 }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
                CheckBox {
                    id: filterSelectAll
                    text: "Seleccionar todo"
                    Layout.fillWidth: true
                    checked: filterPanel.values.length > 0 && filterPanel.values.every(function(value) {
                        return filterPanel.checkedValues[value] === true
                    })
                    onToggled: filterPanel.setAll(checked)
                    contentItem: Text {
                        text: filterSelectAll.text
                        color: Theme.textPrimary
                        font.pixelSize: 12
                        leftPadding: filterSelectAll.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 1
                        Repeater {
                            model: filterPanel.values
                            delegate: CheckBox {
                                id: filterValueCheck
                                required property string modelData
                                Layout.fillWidth: true
                                checked: filterPanel.checkedValues[modelData] === true
                                onToggled: filterPanel.setValue(modelData, checked)
                                text: modelData
                                contentItem: Text {
                                    text: modelData
                                    color: Theme.textPrimary
                                    font.pixelSize: 11
                                    leftPadding: filterValueCheck.indicator.width + 6
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    LcButton {
                        text: "Limpiar"
                        secondary: true
                        Layout.fillWidth: true
                        onClicked: { root.setColumnFilter(filterPanel.columnKey, []); filterPanel.close() }
                    }
                    LcButton {
                        text: "Aplicar"
                        Layout.fillWidth: true
                        onClicked: filterPanel.applyFilter()
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        Flickable {
            id: tableFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: root.tableWidth
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: tableContent
                width: root.tableWidth
                property int rowHeight: 64
                height: tableFlick.height

                Rectangle {
                    width: root.tableWidth
                    height: 38
                    color: Theme.surfaceBg
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: root.shownColumns()
                            delegate: Item {
                                required property var modelData
                                id: headerCell
                                width: root.columnWidth(modelData)
                                height: 38

                                Text {
                                    id: headerTitle
                                    anchors { left: parent.left; right: filterIcon.left; top: parent.top; bottom: parent.bottom }
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 3
                                    text: modelData.title + " " + root.sortIndicator(modelData.key)
                                    color: root.sortColumn === modelData.key ? Theme.textPrimary : Theme.textSecondary
                                    font.pixelSize: 11
                                    font.bold: root.sortColumn === modelData.key
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: root.cycleSort(modelData.key) }
                                }
                                Rectangle {
                                    id: filterIcon
                                    width: 24
                                    height: 26
                                    anchors { right: grip.left; verticalCenter: parent.verticalCenter }
                                    radius: 4
                                    color: filterHover.hovered || root.columnHasFilter(modelData.key)
                                           ? Theme.inputBg : "transparent"
                                    border.color: root.columnHasFilter(modelData.key)
                                                  ? Theme.accent : Theme.borderColor
                                    Text {
                                        anchors.centerIn: parent
                                        text: root.columnHasFilter(modelData.key) ? "●" : "▼"
                                        color: root.columnHasFilter(modelData.key) ? Theme.accent : Theme.textMuted
                                        font.pixelSize: 9
                                    }
                                    HoverHandler { id: filterHover; cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: root.openFilterFor(modelData.key) }
                                }
                                Rectangle {
                                    id: grip
                                    width: 5
                                    height: parent.height
                                    anchors.right: parent.right
                                    color: gripHover.hovered || gripDrag.active ? Theme.accent : "transparent"
                                    HoverHandler { id: gripHover; cursorShape: Qt.SizeHorCursor }
                                    DragHandler {
                                        id: gripDrag
                                        target: null
                                        property real startWidth: 0
                                        onActiveChanged: if (active) startWidth = root.columnWidth(headerCell.modelData)
                                        onTranslationChanged: root.setColumnWidth(headerCell.modelData.key, startWidth + translation.x)
                                    }
                                }

                            }
                        }
                    }
                }

                ListView {
                    id: rankingList
                    y: 38
                    width: root.tableWidth
                    height: Math.max(0, tableContent.height - 38)
                    clip: true
                    model: root.displayedRows
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: LcScrollBar {}
                    delegate: Rectangle {
                        id: rankingRowDelegate
                        required property var modelData
                        required property int index
                        property var rankingRow: modelData
                        width: root.tableWidth
                        height: tableContent.rowHeight
                        color: index % 2 ? Theme.baseBg : "transparent"
                        Row {
                            anchors.fill: parent
                            Repeater {
                                model: root.shownColumns()
                                delegate: Item {
                                    required property var modelData
                                    width: root.columnWidth(modelData)
                                    height: tableContent.rowHeight
                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 5
                                        id: cellTextLabel
                                        text: root.cellText(rankingRowDelegate.rankingRow, modelData.key)
                                        color: root.cellColor(rankingRowDelegate.rankingRow, modelData.key)
                                        font.pixelSize: 11
                                        font.bold: modelData.key === "profile"
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 4
                                        elide: Text.ElideRight
                                    }
                                    ToolTip.visible: cellHover.hovered && cellTextLabel.text.length > 0
                                    ToolTip.text: cellTextLabel.text
                                    HoverHandler { id: cellHover }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: root.displayedRows.length === 0
                text: "Todavía no hay resultados HE0, HE20 o BCB para mostrar."
                color: Theme.textMuted
                font.pixelSize: 13
            }
        }
    }
}
