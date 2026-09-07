import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property string selectedLaunchId: ""
    // Perfil de SISTEMA seleccionado: inmutable (solo lectura). Se puede duplicar
    // para editar, pero no guardar/borrar/renombrar el original.
    readonly property bool selectedIsSystem:
        selectedLaunchId.length > 0 && App.profileManager.isSystemLaunch(selectedLaunchId)
    property string backendId: ""
    // El backend no tiene binario fijado (binaryId ""). El combo no puede mostrar
    // "vacío" —cae a index 0—, así que lo recordamos aparte para no guardar el
    // primer binario de la lista como si el usuario lo hubiera elegido.
    property bool backendBinaryUnset: false
    // Mismo problema con los modelos: si el id guardado no está en el catálogo, el
    // combo cae al primer gguf de la lista. Guardamos el id original acá para
    // preservarlo en vez de pisar el perfil con un modelo que nadie eligió.
    property string modelMainUnresolved: ""
    property string modelMmprojUnresolved: ""
    property string modelDraftUnresolved: ""

    // Posiciona un combo de modelo en `id`. Devuelve "" si pudo representarlo, o el
    // propio `id` si no está en la lista (combo en index 0, valor a preservar).
    function bindModelCombo(combo, id) {
        const idx = combo.indexOfValue(id)
        combo.currentIndex = Math.max(0, idx)
        return (id.length > 0 && idx < 0) ? id : ""
    }

    // Id a persistir: el del combo, salvo que el guardado no se haya podido
    // representar — ahí se conserva el original en vez de pisarlo.
    function effectiveModelId() {
        return modelMainUnresolved.length > 0 ? modelMainUnresolved : (modelMain.currentValue ?? "")
    }
    function effectiveMmprojId() {
        return modelMmprojUnresolved.length > 0 ? modelMmprojUnresolved : (modelMmproj.currentValue ?? "")
    }
    function effectiveDraftId() {
        return modelDraftUnresolved.length > 0 ? modelDraftUnresolved : (modelDraft.currentValue ?? "")
    }
    property string modelProfileId: ""
    property string runtimeId: ""
    property string backendNameCurrent: ""
    property string modelNameCurrent: ""
    property string runtimeNameCurrent: ""
    property bool mmprojEnabled: false
    property bool draftEnabled: false
    property bool mtpEnabled: false
    property bool launchBest: false       // BEST (rayo), insignia curada del catálogo
    property bool launchFavorite: false   // favorito del perfil de lanzamiento
    property bool launchDeprecated: false // visible sólo en esta página administrativa
    property bool smokeTestRunning: false
    // Política: siempre LlamaAgent. Sin selector de harness; sin "none"/"opencode".
    property string harnessAdapter: "llamaagent"
    property string harnessProfileId: ""
    // Perfil de agente por defecto de este launch (capacidades + directivas).
    property string agentProfileId: ""
    property string plannerProfileId: ""
    property string hybridMode: "off"

    // ── Salud de perfiles (health-check) ──────────────────────────
    // Lista de issues {severity,launchId,entity,code,message,fix} de todos los
    // launches. Se refresca al entrar y ante cambios de la lista de launches.
    property var healthIssues: []
    property int healthErrors: 0
    property int healthWarnings: 0
    function refreshHealth() {
        healthIssues = App.profileHealth()
        const s = App.profileHealthSummary()
        healthErrors = s.errors ?? 0
        healthWarnings = s.warnings ?? 0
    }
    function launchName(id) {
        const lp = App.profileManager.getLaunchProfile(id)
        return (lp && lp.name && lp.name.length > 0) ? lp.name : id
    }

    // ── Maestro (supervisor): cadena de fallbacks ─────────────────
    property string masterEscalation: "manual"  // manual | auto | both
    property int    masterAutoAfterFails: 3
    property var    masterCliStatusCache: ({})   // name -> status map
    function refreshMasterCliStatus(force) {
        const names = App.masterCliList()
        let m = {}
        for (let i = 0; i < names.length; ++i)
            m[names[i]] = App.masterCliStatus(names[i], force === true)
        masterCliStatusCache = m
    }
    // Modelo de la cadena (ordenado primero→último). Cada item:
    // {type,label,profileId,httpUrl,httpModel,httpKeyRef,httpKeyValue,cliName,applyEdits,timeoutSec}
    // httpKeyValue es transitorio (sólo si el usuario escribe una key nueva).
    function masterChainLoad(fallbacks) {
        masterChainModel.clear()
        const arr = fallbacks ?? []
        for (let i = 0; i < arr.length; ++i) {
            const f = arr[i]
            masterChainModel.append({
                type: f.type ?? "http", label: f.label ?? "",
                profileId: f.profileId ?? "",
                httpUrl: f.httpUrl ?? "", httpModel: f.httpModel ?? "",
                httpKeyRef: f.httpKeyRef ?? "", httpKeyValue: "",
                cliName: (f.cliName && f.cliName.length > 0) ? f.cliName : "claude",
                applyEdits: (f.applyEdits !== false),
                timeoutSec: f.timeoutSec ?? 300
            })
        }
    }
    // Serializa el modelo a array para updateLaunchProfile. Las keys nuevas
    // (httpKeyValue) se guardan en SecretStore bajo un ref estable y se persiste
    // sólo el ref.
    function masterChainToArray() {
        const out = []
        for (let i = 0; i < masterChainModel.count; ++i) {
            const m = masterChainModel.get(i)
            let keyRef = m.httpKeyRef ?? ""
            if (m.type === "http" && m.httpKeyValue && m.httpKeyValue.length > 0) {
                keyRef = "master/" + selectedLaunchId + "/" + i
                App.setSecret(keyRef, m.httpKeyValue)
            }
            out.push({
                type: m.type, label: m.label ?? "",
                profileId: m.profileId ?? "",
                httpUrl: (m.httpUrl ?? "").trim(),
                httpModel: (m.httpModel ?? "").trim(),
                httpKeyRef: keyRef,
                cliName: m.cliName ?? "",
                applyEdits: m.applyEdits, timeoutSec: m.timeoutSec
            })
        }
        return out
    }
    ListModel { id: masterChainModel }

    function splitArgs(raw) {
        const out = []
        const lines = raw.split("\n")
        for (let i = 0; i < lines.length; ++i) {
            const tokens = lines[i].trim().split(/\s+/).filter(s => s.length > 0)
            for (let j = 0; j < tokens.length; ++j) out.push(tokens[j])
        }
        return out
    }

    function argsFlat() { return splitArgs(extraArgsArea.text) }

    // ¿El token es un flag (-x / --xxx) y NO un valor numérico negativo (-1, -0.5)?
    function isArgFlag(tok) {
        return /^-/.test(tok) && !/^-?\d*\.?\d+$/.test(tok)
    }

    // Agrupa flag+valor en una línea: "-np 1", "--reasoning off". Los flags sin
    // valor (seguidos de otro flag) quedan solos en su línea.
    function formatArgsForDisplay(tokens) {
        const lines = []
        for (let i = 0; i < tokens.length; ++i) {
            const t = tokens[i]
            if (isArgFlag(t) && i + 1 < tokens.length && !isArgFlag(tokens[i + 1])) {
                lines.push(t + " " + tokens[i + 1])
                ++i
            } else {
                lines.push(t)
            }
        }
        return lines.join("\n")
    }

    function findArgValue(flag) {
        const a = argsFlat()
        for (let i = 0; i < a.length - 1; ++i) {
            if (a[i] === flag) return a[i + 1]
        }
        return ""
    }

    function hasFlag(flag) {
        const a = argsFlat()
        for (let i = 0; i < a.length; ++i) if (a[i] === flag) return true
        return false
    }

    function extractManualArgs(rawArgs) {
        const pairFlags = {
            "--alias": true, "--n-predict": true, "--cache-type-v": true, "--temp": true,
            "--top-p": true, "--top-k": true, "--min-p": true,
            "--repeat-penalty": true, "--presence-penalty": true,
            "--cache-ram": true, "--cache-reuse": true
        }
        const boolFlags = { "--no-context-shift": true, "--context-shift": true, "--metrics": true, "--no-warmup": true }
        const out = []
        for (let i = 0; i < rawArgs.length; ++i) {
            const cur = rawArgs[i]
            if (pairFlags[cur]) { i += 1; continue }
            if (boolFlags[cur]) continue
            out.push(cur)
        }
        return out
    }

    function selectProfile(id) {
        if (!id || id.length === 0) return false
        const i = launchCombo.indexOfValue(id)
        if (i < 0) return false
        launchCombo.currentIndex = i
        selectedLaunchId = id
        loadLaunch()
        return true
    }

    function createDefaultHarnessProfile(prefix) {
        return App.profileManager.addHarness(prefix + " · LlamaAgent", "llamaagent")
    }

    function newProfile() {
        const bId = App.profileManager.addBackend("Backend", "", "127.0.0.1", 8080)
        const mId = App.profileManager.addModelProfile("Model", "", "", "")
        const rId = App.profileManager.addRuntimePreset("Runtime", 4096, 512, -1, false, true)
        const hId = createDefaultHarnessProfile("Nuevo perfil")
        const lId = App.profileManager.addLaunchProfile("Nuevo perfil", bId, mId, rId)
        App.profileManager.updateLaunchProfile({
            "id": lId, "name": "Nuevo perfil",
            "backendProfileId": bId, "modelProfileId": mId, "runtimePresetId": rId,
            "harnessProfileId": hId
        })
        selectProfile(lId)
    }

    function tokenizeArgs(raw) {
        // Join continuation lines (\<newline>), then tokenize respecting quotes
        const cleaned = raw.replace(/\\\s*\n/g, ' ').replace(/\n/g, ' ')
        const tokens = []; let cur = ''; let inQ = false; let qch = ''
        for (let i = 0; i < cleaned.length; i++) {
            const ch = cleaned[i]
            if ((ch === '"' || ch === "'") && !inQ) { inQ = true; qch = ch }
            else if (ch === qch && inQ)             { inQ = false }
            else if (ch === ' ' && !inQ)            { if (cur.length) { tokens.push(cur); cur = '' } }
            else                                    { cur += ch }
        }
        if (cur.length) tokens.push(cur)
        return tokens
    }

    function importFromArgs(profileName, rawArgs) {
        const tokens = tokenizeArgs(rawArgs)
        // Skip binary / script name (first token not starting with -)
        let i = (tokens.length > 0 && !tokens[0].startsWith('-')) ? 1 : 0

        let host = '127.0.0.1', port = 8080, modelPath = ''
        let ctx = 4096, batch = 512, ubatch = 512, threads = -1, gpuLayers = -1
        let flashAttn = false, useMmap = true, useMlock = false, contBatch = true
        let parallel = 1, cacheType = 'q8_0'
        const extra = []

        while (i < tokens.length) {
            const t = tokens[i]
            const next = tokens[i + 1] ?? ''
            // Pair flags
            if ((t === '--host') && next)                         { host = next;               i += 2 }
            else if ((t === '--port') && next)                    { port = parseInt(next)||8080; i += 2 }
            else if ((t === '--model' || t === '-m') && next)     { modelPath = next;           i += 2 }
            else if ((t === '--ctx-size' || t === '-c' || t === '--ctx') && next)
                                                                  { ctx = parseInt(next)||4096; i += 2 }
            else if (t === '--batch-size' && next)                { batch = parseInt(next)||512; i += 2 }
            else if (t === '--ubatch-size' && next)               { ubatch = parseInt(next)||512; i += 2 }
            else if ((t === '--threads' || t === '-t') && next)   { threads = parseInt(next)||-1; i += 2 }
            else if ((t === '--n-gpu-layers' || t === '-ngl' || t === '--n_gpu_layers') && next)
                                                                  { gpuLayers = parseInt(next); i += 2 }
            else if ((t === '--parallel' || t === '-np') && next) { parallel = parseInt(next)||1; i += 2 }
            else if (t === '--cache-type-k' && next)              { cacheType = next;            i += 2 }
            // Bool flags
            else if (t === '--flash-attn' || t === '-fa')         { flashAttn = true;  i++ }
            else if (t === '--no-mmap')                           { useMmap   = false; i++ }
            else if (t === '--mlock')                             { useMlock  = true;  i++ }
            else if (t === '--cont-batching' || t === '-cb')      { contBatch = true;  i++ }
            // Unknown → extra
            else                                                  { extra.push(t); i++ }
        }

        const bId = App.profileManager.addBackend(profileName + " · Backend", "", host, port)
        const mId = App.profileManager.addModelProfile(profileName + " · Model", "", "", "")
        const rId = App.profileManager.addRuntimePreset(profileName + " · Runtime", ctx, batch, gpuLayers, flashAttn, contBatch)
        App.profileManager.updateRuntimePreset({
            "id": rId, "name": profileName + " · Runtime",
            "ctx": ctx, "batch": batch, "ubatch": ubatch,
            "threads": threads, "gpuLayers": gpuLayers,
            "flashAttention": flashAttn, "mmap": useMmap, "mlock": useMlock,
            "contBatching": contBatch, "cacheType": cacheType, "parallelSlots": parallel
        })
        const lId = App.profileManager.addLaunchProfile(profileName, bId, mId, rId)
        const hId = createDefaultHarnessProfile(profileName)
        // Put unparsed args + model path as extraArgs if needed
        const extras = extra.slice()
        if (modelPath.length > 0) extras.unshift(modelPath, '--model')
        App.profileManager.updateLaunchProfile({
            "id": lId, "name": profileName,
            "backendProfileId": bId, "modelProfileId": mId, "runtimePresetId": rId,
            "harnessProfileId": hId,
            "extraArgs": extras, "envOverrides": {}
        })
        selectProfile(lId)
    }

    function duplicateProfile() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return
        // Clon profundo (incl. perfiles de sistema → copia editable de usuario).
        // Vía AppController, no ProfileManager: la copia de un perfil de sistema
        // necesita que se le fije el binario que el original resolvía por política.
        const lId = App.duplicateLaunchProfile(selectedLaunchId)
        if (lId && lId.length > 0) selectProfile(lId)
    }

    function loadLaunch() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return
        const lp = App.profileManager.getLaunchProfile(selectedLaunchId)
        if (!lp || !lp.id) return

        backendId = lp.backendProfileId ?? ""
        modelProfileId = lp.modelProfileId ?? ""
        runtimeId = lp.runtimePresetId ?? ""
        harnessProfileId = lp.harnessProfileId ?? ""
        agentProfileId = lp.agentProfileId ?? ""
        agentProfileCombo.currentIndex = Math.max(0, agentProfileCombo.indexOfValue(agentProfileId))
        // Siempre LlamaAgent: perfiles viejos con "none"/"opencode" se normalizan.
        harnessAdapter = "llamaagent"
        const rawExtra = (lp.extraArgs ?? [])
        manualExtraArgsArea.text = formatArgsForDisplay(extractManualArgs(rawExtra))

        // Alias (display) opcional + favorito del perfil.
        root.launchBest = (lp.best === true)
        root.launchFavorite = (lp.favorite === true)
        root.launchDeprecated = (lp.deprecated === true)
        profileAliasField.text = lp.alias ?? ""
        powerLimitField.text = ((lp.powerLimitW ?? 0) > 0) ? (lp.powerLimitW).toString() : ""
        browserAutoCombo.currentIndex = Math.max(0, browserAutoCombo.indexOfValue(lp.browserAutomation ?? "inherit"))

        const bp = App.profileManager.getBackend(backendId)
        backendNameCurrent = bp.name ?? ""
        backendHost.text = bp.host ?? "127.0.0.1"
        backendPort.text = (bp.port ?? 8080).toString()
        const bpBinaryId = bp.binaryId ?? ""
        backendBinaryUnset = (bpBinaryId.length === 0)
        backendBinary.currentIndex = Math.max(0, backendBinary.indexOfValue(bpBinaryId))
        backendKind.currentIndex = Math.max(0, backendKind.indexOfValue(bp.kind ?? "local"))
        cloudBaseUrl.text = bp.cloudBaseUrl ?? ""
        cloudKeyRef.text = bp.cloudKeyRef ?? ""
        cloudKeyValue.text = ""   // el secreto nunca se carga desde el perfil
        cloudModel.text = bp.cloudModel ?? ""
        cloudCtx.text = ((bp.cloudCtx ?? 0) > 0) ? (bp.cloudCtx).toString() : ""

        const mp = App.profileManager.getModelProfile(modelProfileId)
        modelNameCurrent = mp.name ?? ""
        // Un id que el combo no puede representar (no está en el catálogo, o la fila
        // quedó no disponible) NO debe caer al índice 0: eso muestra el primer gguf
        // de la lista como si fuera el del perfil, y al guardar lo escribe de verdad.
        // Recordamos el id original y lo preservamos al guardar.
        modelMainUnresolved  = bindModelCombo(modelMain,   mp.modelId ?? "")
        mmprojEnabled = (mp.mmprojId ?? "").length > 0
        modelMmprojUnresolved = bindModelCombo(modelMmproj, mp.mmprojId ?? "")
        draftEnabled = (mp.draftModelId ?? "").length > 0
        modelDraftUnresolved  = bindModelCombo(modelDraft,  mp.draftModelId ?? "")
        mtpEnabled = (mp.specType ?? "") === "draft-mtp"
        specNMaxField.text = ((mp.specDraftNMax ?? 0) || 0).toString()
        specKvType.currentIndex = Math.max(0, specKvType.find(mp.specDraftTypeK ?? ""))

        const rt = App.profileManager.getRuntimePreset(runtimeId)
        runtimeNameCurrent = rt.name ?? ""
        ctxField.text = (rt.ctx ?? 4096).toString()
        batchField.text = (rt.batch ?? 512).toString()
        ubatchField.text = (rt.ubatch ?? 512).toString()
        threadsField.text = (rt.threads ?? -1).toString()
        gpuLayersField.text = (rt.gpuLayers ?? -1).toString()
        parallelSlotsField.text = (rt.parallelSlots ?? 1).toString()
            cacheTypeField.text = rt.cacheType ?? "q8_0"
        tensorOverridesField.text = (rt.tensorOverrides ?? []).join(", ")
        flashAttnCheck.checked = rt.flashAttention ?? false
        mmapCheck.checked = rt.mmap ?? true
        mlockCheck.checked = rt.mlock ?? false
        contBatchCheck.checked = rt.contBatching ?? true

        aliasField.text = ""; nPredictField.text = ""; cacheTypeVField.text = ""
        tempField.text = ""; topPField.text = ""; topKField.text = ""
        repeatPenaltyField.text = ""; presencePenaltyField.text = ""
        contextShiftCheck.checked = true
        metricsCheck.checked = false; noWarmupCheck.checked = false
        cacheRamField.text = ""; cacheReuseField.text = ""
        for (let i = 0; i < rawExtra.length; ++i) {
            const cur = rawExtra[i]
            const nxt = (i + 1 < rawExtra.length) ? rawExtra[i + 1] : ""
            if (cur === "--alias") { aliasField.text = nxt; i += 1; continue }
            if (cur === "--n-predict") { nPredictField.text = nxt; i += 1; continue }
            if (cur === "--cache-type-v") { cacheTypeVField.text = nxt; i += 1; continue }
            if (cur === "--temp") { tempField.text = nxt; i += 1; continue }
            if (cur === "--top-p") { topPField.text = nxt; i += 1; continue }
            if (cur === "--top-k") { topKField.text = nxt; i += 1; continue }
            if (cur === "--repeat-penalty") { repeatPenaltyField.text = nxt; i += 1; continue }
            if (cur === "--presence-penalty") { presencePenaltyField.text = nxt; i += 1; continue }
            if (cur === "--cache-ram") { cacheRamField.text = nxt; i += 1; continue }
            if (cur === "--cache-reuse") { cacheReuseField.text = nxt; i += 1; continue }
            if (cur === "--no-context-shift") { contextShiftCheck.checked = false; continue }
            if (cur === "--context-shift")    { contextShiftCheck.checked = true;  continue }
            if (cur === "--metrics") { metricsCheck.checked = true; continue }
            if (cur === "--no-warmup") { noWarmupCheck.checked = true; continue }
        }

        // Maestro (supervisor): cadena de fallbacks
        const mc = lp.master ?? {}
        masterEscalation = mc.escalation ?? "manual"
        masterAutoAfterFails = mc.autoAfterFails ?? 3
        masterChainLoad(mc.fallbacks ?? [])
        plannerProfileId = lp.plannerProfileId ?? ""
        hybridMode = lp.hybridMode ?? "off"
        refreshMasterCliStatus(false)

        // Refresca vista previa del comando para el perfil cargado
        App.computeEffectiveProfile(selectedLaunchId)
    }

    // Arma los args extra (manuales + campos) tal como van al final del comando.
    function collectExtraArgs(binId) {
        function rf(flag) { return binId.length > 0 ? App.resolveFlag(binId, flag) : flag }
        const out = []
        if (aliasField.text.trim().length > 0) out.push(rf("--alias"), aliasField.text.trim())
        if (nPredictField.text.trim().length > 0) out.push(rf("--n-predict"), nPredictField.text.trim())
        if (cacheTypeVField.text.trim().length > 0) out.push(rf("--cache-type-v"), cacheTypeVField.text.trim())
        if (tempField.text.trim().length > 0) out.push(rf("--temp"), tempField.text.trim())
        if (topPField.text.trim().length > 0) out.push(rf("--top-p"), topPField.text.trim())
        if (topKField.text.trim().length > 0) out.push(rf("--top-k"), topKField.text.trim())
        if (repeatPenaltyField.text.trim().length > 0) out.push(rf("--repeat-penalty"), repeatPenaltyField.text.trim())
        if (presencePenaltyField.text.trim().length > 0) out.push(rf("--presence-penalty"), presencePenaltyField.text.trim())
        // Never use rf() for these — avoids alias mapping --no-context-shift → --context-shift
        out.push(contextShiftCheck.checked ? "--context-shift" : "--no-context-shift")
        if (metricsCheck.checked)  out.push("--metrics")
        if (noWarmupCheck.checked) out.push("--no-warmup")
        if (cacheRamField.text.trim().length > 0) out.push(rf("--cache-ram"), cacheRamField.text.trim())
        if (cacheReuseField.text.trim().length > 0) out.push(rf("--cache-reuse"), cacheReuseField.text.trim())
        const manual = splitArgs(manualExtraArgsArea.text)
        for (let i = 0; i < manual.length; ++i)
            out.push(manual[i].startsWith('-') ? rf(manual[i]) : manual[i])
        return out
    }

    // Recalcula la vista previa del comando con los valores actuales del editor (sin guardar).
    function recomputePreview() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return
        const binId = backendBinaryUnset ? "" : (backendBinary.currentValue ?? "")
        App.computeEffectiveProfilePreview(selectedLaunchId, {
            "host": backendHost.text,
            "port": parseInt(backendPort.text) || 8080,
            "binaryId": binId,
            "modelId": effectiveModelId(),
            "mmprojId": mmprojEnabled ? effectiveMmprojId() : "",
            "draftModelId": draftEnabled ? effectiveDraftId() : "",
            "specType": mtpEnabled ? "draft-mtp" : "",
            "specDraftNMax": mtpEnabled ? (parseInt(specNMaxField.text) || 0) : 0,
            "specDraftNgl": (draftEnabled && mtpEnabled) ? "all" : "",
            "specDraftTypeK": (draftEnabled && mtpEnabled) ? (specKvType.currentText ?? "") : "",
            "specDraftTypeV": (draftEnabled && mtpEnabled) ? (specKvType.currentText ?? "") : "",
            "ctx": parseInt(ctxField.text) || 4096,
            "batch": parseInt(batchField.text) || 512,
            "ubatch": parseInt(ubatchField.text) || 512,
            "threads": parseInt(threadsField.text) || -1,
            "gpuLayers": parseInt(gpuLayersField.text) || -1,
            "flashAttention": flashAttnCheck.checked,
            "mmap": mmapCheck.checked,
            "mlock": mlockCheck.checked,
            "contBatching": contBatchCheck.checked,
            "cacheType": cacheTypeField.text,
            "parallelSlots": parseInt(parallelSlotsField.text) || 1,
            "tensorOverrides": tensorOverridesField.text.split(/[,\n]/).map(s => s.trim()).filter(s => s.length > 0),
            "extraArgs": collectExtraArgs(binId)
        })
    }

    function saveAll() {
        if (!selectedLaunchId || selectedLaunchId.length === 0) return

        // envOverrides ya no se edita en UI; se preserva el valor existente del perfil.
        let envOverrides = {}
        let launchName = ""
        try {
            const lpCur = App.profileManager.getLaunchProfile(selectedLaunchId)
            envOverrides = lpCur.envOverrides ?? {}
            launchName = lpCur.name ?? ""
        } catch (e) { envOverrides = {} }

        // Backend: update if exists, create if not
        const binaryId = backendBinaryUnset ? "" : (backendBinary.currentValue ?? "")
        let effectiveBid = backendId
        if (!App.profileManager.updateBackend(effectiveBid, backendNameCurrent, binaryId, backendHost.text, parseInt(backendPort.text), [])) {
            effectiveBid = App.profileManager.addBackend(
                backendNameCurrent.length > 0 ? backendNameCurrent : "Backend",
                binaryId, backendHost.text, parseInt(backendPort.text))
            if (!effectiveBid || effectiveBid.length === 0) { App.serverError("No se pudo crear Backend."); return }
            backendId = effectiveBid
        }
        App.profileManager.setBackendCloud(effectiveBid, backendKind.currentValue ?? "local",
            cloudBaseUrl.text, cloudKeyRef.text, cloudModel.text, parseInt(cloudCtx.text) || 0)
        // El secreto va al store fuera del repo (nunca al JSON del perfil).
        if ((backendKind.currentValue ?? "local") === "cloud" && cloudKeyValue.text.length > 0)
            App.setSecret(cloudKeyRef.text, cloudKeyValue.text)

        // Model: update if exists, create if not
        const mainModelId  = effectiveModelId()
        const mmprojId     = mmprojEnabled  ? effectiveMmprojId() : ""
        const draftModelId = draftEnabled   ? effectiveDraftId()  : ""
        let effectiveMid = modelProfileId
        if (!App.profileManager.updateModelProfile(effectiveMid, modelNameCurrent, mainModelId, mmprojId, draftModelId)) {
            effectiveMid = App.profileManager.addModelProfile(
                modelNameCurrent.length > 0 ? modelNameCurrent : "Model",
                mainModelId, mmprojId, draftModelId)
            if (!effectiveMid || effectiveMid.length === 0) { App.serverError("No se pudo crear Model Profile."); return }
            modelProfileId = effectiveMid
        }
        // MTP admite draft separado o cabezal autocontenido en el GGUF principal.
        const specOn = mtpEnabled
        App.profileManager.setModelSpec(
            effectiveMid,
            specOn ? "draft-mtp" : "",
            specOn ? (parseInt(specNMaxField.text) || 0) : 0,
            specOn ? "all" : "",
            specOn ? (specKvType.currentText ?? "") : "",
            specOn ? (specKvType.currentText ?? "") : "")

        // Runtime: update if exists, create if not
        let effectiveRid = runtimeId
        const rtData = {
            "id": effectiveRid, "name": runtimeNameCurrent,
            "ctx": parseInt(ctxField.text), "batch": parseInt(batchField.text),
            "ubatch": parseInt(ubatchField.text), "threads": parseInt(threadsField.text),
            "gpuLayers": parseInt(gpuLayersField.text), "flashAttention": flashAttnCheck.checked,
            "mmap": mmapCheck.checked, "mlock": mlockCheck.checked,
            "contBatching": contBatchCheck.checked, "cacheType": cacheTypeField.text,
            "parallelSlots": parseInt(parallelSlotsField.text),
            "tensorOverrides": tensorOverridesField.text.split(/[,\n]/).map(s => s.trim()).filter(s => s.length > 0)
        }
        if (!App.profileManager.updateRuntimePreset(rtData)) {
            effectiveRid = App.profileManager.addRuntimePreset(
                runtimeNameCurrent.length > 0 ? runtimeNameCurrent : "Runtime",
                parseInt(ctxField.text), parseInt(batchField.text),
                parseInt(gpuLayersField.text), flashAttnCheck.checked, contBatchCheck.checked)
            if (!effectiveRid || effectiveRid.length === 0) { App.serverError("No se pudo crear Runtime."); return }
            runtimeId = effectiveRid
            App.profileManager.updateRuntimePreset(Object.assign({}, rtData, {"id": effectiveRid}))
        }

        const bp = App.profileManager.getBackend(backendId)
        const binId = bp.binaryId ?? ""
        const rebuiltArgs = collectExtraArgs(binId)

        // Persist harness selection
        let resolvedHarnessId = ""
        if (harnessAdapter !== "none") {
            if (harnessProfileId.length > 0) {
                App.profileManager.updateHarness({"id": harnessProfileId, "adapter": harnessAdapter, "name": harnessAdapter})
                resolvedHarnessId = harnessProfileId
            } else {
                resolvedHarnessId = App.profileManager.addHarness(harnessAdapter, harnessAdapter)
                harnessProfileId = resolvedHarnessId
            }
        }

        console.log("[saveAll] selectedLaunchId=", selectedLaunchId,
                    "bid=", effectiveBid, "mid=", effectiveMid, "rid=", effectiveRid,
                    "args=", rebuiltArgs.length)

        const lpOk = App.profileManager.updateLaunchProfile({
            "id": selectedLaunchId, "name": launchName,
            "alias": profileAliasField.text.trim(), "favorite": root.launchFavorite,
            "deprecated": root.launchDeprecated,
            "powerLimitW": parseInt(powerLimitField.text) || 0,
            "browserAutomation": browserAutoCombo.currentValue ?? "inherit",
            "backendProfileId": effectiveBid, "modelProfileId": effectiveMid,
            "runtimePresetId": effectiveRid, "extraArgs": rebuiltArgs, "envOverrides": envOverrides,
            "harnessProfileId": resolvedHarnessId,
            "agentProfileId": agentProfileCombo.currentValue ?? "",
            "plannerProfileId": hybridMode === "off" ? "" : plannerProfileId,
            "hybridMode": hybridMode,
            "master": {
                "fallbacks": masterChainToArray(),
                "escalation": masterEscalation,
                "autoAfterFails": masterAutoAfterFails
            }
        })

        console.log("[saveAll] updateLaunchProfile result:", lpOk)
        App.profileManager.saveProfiles()   // fuerza write a disco

        if (!lpOk) App.serverError("No se pudo guardar Launch Profile.")

        // Recalcula vista previa con los args recién guardados
        App.computeEffectiveProfile(selectedLaunchId)
    }

    Component.onCompleted: {
        const lastId = App.readSetting("lastLaunchId", "")
        if (lastId === "" || !selectProfile(lastId)) {
            if (launchCombo.count > 0) {
                launchCombo.currentIndex = 0
                selectedLaunchId = launchCombo.currentValue ?? ""
                loadLaunch()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("profiles.title"))
            subtitle: (App.langV, App.l("profiles.subtitle"))
        }

        ScrollView {
            id: editorScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftPadding: 16
            rightPadding: 16
            topPadding: 16
            bottomPadding: 16
            contentWidth: Math.max(900, editorScroll.availableWidth)

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: LcScrollBar {
                parent: editorScroll
                anchors.right: editorScroll.right
                anchors.top: editorScroll.top
                anchors.bottom: editorScroll.bottom
                anchors.topMargin: editorScroll.topPadding
                anchors.bottomMargin: editorScroll.bottomPadding
                policy: ScrollBar.AsNeeded
            }

            Item {
                width: Math.max(900, editorScroll.availableWidth)
                implicitHeight: editorCol.implicitHeight

                ColumnLayout {
                id: editorCol
                anchors { left: parent.left; right: parent.right; top: parent.top }
                spacing: 12

                Component.onCompleted: root.refreshHealth()
                Connections {
                    target: App.profileManager
                    function onLaunchesChanged() { root.refreshHealth() }
                    function onProfilesReloaded() { root.refreshHealth() }
                }

                // ── Banner de salud de perfiles ──
                Rectangle {
                    id: healthBanner
                    Layout.fillWidth: true
                    radius: 8
                    readonly property bool healthy: root.healthIssues.length === 0
                    color: healthy ? Theme.surfaceBg
                         : (root.healthErrors > 0 ? Qt.rgba(Theme.errorText.r, Theme.errorText.g, Theme.errorText.b, 0.08)
                                                  : Qt.rgba(Theme.warnText.r, Theme.warnText.g, Theme.warnText.b, 0.08))
                    border.color: healthy ? Theme.borderColor
                         : (root.healthErrors > 0 ? Theme.errorText : Theme.warnText)
                    implicitHeight: healthCol.implicitHeight + 20

                    ColumnLayout {
                        id: healthCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text {
                                text: healthBanner.healthy ? "✓" : (root.healthErrors > 0 ? "✕" : "!")
                                color: healthBanner.healthy ? Theme.successText
                                     : (root.healthErrors > 0 ? Theme.errorText : Theme.warnText)
                                font.pixelSize: 16; font.bold: true
                            }
                            Text {
                                text: healthBanner.healthy
                                    ? "Todos los perfiles están sanos"
                                    : (root.healthErrors + " error(es), " + root.healthWarnings + " aviso(s)")
                                color: Theme.textPrimary; font.pixelSize: 14; font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            LcButton {
                                text: "Revisar salud"
                                onClicked: root.refreshHealth()
                            }
                        }

                        Repeater {
                            model: root.healthIssues
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    width: 8; height: 8; radius: 4
                                    y: 5
                                    color: modelData.severity === "error" ? Theme.errorText : Theme.warnText
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.launchName(modelData.launchId) + " — " + modelData.message
                                        color: Theme.textPrimary; font.pixelSize: 13
                                        wrapMode: Text.WordWrap
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "→ " + modelData.fix
                                        color: Theme.textSecondary; font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: topCol.implicitHeight + 20

                    ColumnLayout {
                        id: topCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        RowLayout {
                        id: topRow
                        Layout.fillWidth: true
                        spacing: 10

                        Text { text: (App.langV, App.l("launch.profile")); color: Theme.textSecondary; font.pixelSize: 13 }
                        LcComboBox {
                            id: launchCombo
                            Layout.fillWidth: true
                            // Menú ordenado: favoritos (★) arriba; displayName = alias - name.
                            property var launchMenu: App.profileManager.launchProfilesForProfilesPage()
                            Connections {
                                target: App.profileManager
                                function onLaunchesChanged() {
                                    const sel = launchCombo.currentValue
                                    launchCombo.launchMenu = App.profileManager.launchProfilesForProfilesPage()
                                    const i = launchCombo.indexOfValue(sel)
                                    if (i >= 0) launchCombo.currentIndex = i
                                }
                            }
                            model: launchMenu
                            textRole: "displayName"
                            valueRole: "id"
                            onCurrentValueChanged: { selectedLaunchId = currentValue ?? ""; loadLaunch() }
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text {
                                text: launchCombo.displayText.length > 0 ? launchCombo.displayText : (App.langV, App.l("common.selectPlaceholder"))
                                color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter
                            }
                        }
                        // Favorito (★): se guarda al instante y manda el perfil arriba del dropdown.
                        LcButton {
                            text: root.launchFavorite ? "★" : "☆"
                            secondary: true
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem
                            ToolTip.visible: hovered
                            ToolTip.text: root.launchFavorite ? "Quitar de favoritos" : "Marcar favorito"
                            onClicked: {
                                root.launchFavorite = !root.launchFavorite
                                App.profileManager.setLaunchFavorite(selectedLaunchId, root.launchFavorite)
                            }
                        }
                        // BEST (⚡): distintivo curado; los perfiles BEST son de solo lectura.
                        LcButton {
                            iconSource: "qrc:/qt/qml/LlamaCode/assets/best_bolt.svg"
                            text: ""
                            visible: root.launchBest
                            enabled: false
                            secondary: true
                            ToolTip.visible: hovered
                            ToolTip.text: "BEST: perfil recomendado"
                        }
                        // Deprecated (⚠): queda visible sólo en Perfiles y se
                        // excluye de Lanzar, Agente, Benchmark y demás selectores.
                        LcButton {
                            text: root.launchDeprecated ? "⚠" : "✓"
                            secondary: true
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem
                            ToolTip.visible: hovered
                            ToolTip.text: root.launchDeprecated
                                ? "Reactivar perfil operativo"
                                : "Marcar como deprecated (ocultar de selectores)"
                            onClicked: {
                                root.launchDeprecated = !root.launchDeprecated
                                App.profileManager.updateLaunchProfile({
                                    "id": selectedLaunchId,
                                    "deprecated": root.launchDeprecated
                                })
                            }
                        }
                        // Alias opcional (prioridad sobre el nombre en los dropdowns).
                        LcTextField {
                            id: profileAliasField
                            Layout.preferredWidth: 160
                            placeholderText: "Alias (opcional)"
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem
                            onEditingFinished: if (selectedLaunchId.length > 0 && !selectedIsSystem)
                                App.profileManager.setLaunchAlias(selectedLaunchId, text.trim())
                        }
                        }

                        Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        LcButton {
                            text: {
                                const _lang = App.langV
                                return smokeTestRunning ? App.l("profiles.smokeTesting") : App.l("profiles.smokeTest")
                            }
                            secondary: true
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem && !smokeTestRunning && !App.serverRunning
                            onClicked: { saveAll(); smokeTestRunning = true; App.smokeTestServer(selectedLaunchId) }
                        }
                        LcButton {
                            text: App.autoTuneRunning ? "Tuning…" : "Auto-tune"
                            secondary: true
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem && !App.serverRunning && !App.autoTuneRunning
                            ToolTip.visible: hovered
                            ToolTip.text: "Optimiza ngl/batch/flash-attn/cache-type maximizando tok/s sin degradar calidad. Usa PPL si encuentra llama-perplexity. Crea un perfil nuevo \"Opti - …\". Para elegir PP vs TG y comparar contra el perfil actual, usá la sección Tuner"
                            onClicked: { saveAll(); App.startAutoTune(selectedLaunchId, 24, 0.6, 256, "auto") }
                        }
                        LcButton {
                            text: "Tune CPU"
                            secondary: true
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem && !App.serverRunning && !App.autoTuneRunning
                            ToolTip.visible: hovered
                            ToolTip.text: "Fuerza -ngl 0 y optimiza threads/batch/ubatch/cache para inferencia CPU-only"
                            onClicked: { saveAll(); App.startAutoTune(selectedLaunchId, 24, 0.6, 256, "cpu") }
                        }
                        LcButton {
                            text: "Cancelar tune"
                            secondary: true
                            visible: App.autoTuneRunning
                            onClicked: App.cancelAutoTune()
                        }
                        LcButton { text: (App.langV, App.l("profiles.new")); secondary: true; onClicked: newProfile() }
                        LcButton { text: "Importar"; secondary: true; onClicked: importDialog.open() }
                        LcButton {
                            text: (App.langV, App.l("profiles.duplicate")); secondary: true
                            enabled: selectedLaunchId.length > 0
                            onClicked: duplicateProfile()
                        }
                        LcButton {
                            text: (App.langV, App.l("profiles.rename")); secondary: true
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem
                            onClicked: {
                                const lp = App.profileManager.getLaunchProfile(selectedLaunchId)
                                renameField.text = lp.name ?? ""
                                renameDialog.open()
                            }
                        }
                        LcButton { text: (App.langV, App.l("profiles.cancel")); secondary: true; onClicked: loadLaunch() }
                        LcButton { text: (App.langV, App.l("profiles.save")); enabled: !selectedIsSystem; onClicked: saveAll() }
                        LcButton {
                            text: (App.langV, App.l("profiles.delete"))
                            danger: true
                            enabled: selectedLaunchId.length > 0 && !selectedIsSystem
                            onClicked: deleteDialog.open()
                        }
                        }
                    }
                }

                // Aviso: perfil de SISTEMA (solo lectura). Duplicar para editar.
                Text {
                    visible: selectedIsSystem
                    Layout.fillWidth: true
                    text: "🔒 Perfil de sistema (solo lectura). Usá \"" + App.l("profiles.duplicate") + "\" para crear una copia editable."
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                // Estado del auto-tune (progreso + mejor config aplicada).
                Text {
                    visible: App.autoTuneRunning || App.autoTuneStatus.length > 0
                    Layout.fillWidth: true
                    text: (App.autoTuneRunning ? "⏳ " : "") + App.autoTuneStatus
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                LcDialog {
                    id: deleteDialog
                    title: (App.langV, App.l("profiles.deleteTitle"))
                    width: 440
                    height: 190
                    contentItem: Text {
                        width: 404
                        text: (App.langV, App.lf("common.deleteConfirm", launchCombo.displayText))
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    onAccepted: {
                        const idToDelete = selectedLaunchId
                        App.profileManager.removeLaunchProfile(idToDelete)
                        selectedLaunchId = ""
                        if (launchCombo.count > 0) {
                            launchCombo.currentIndex = 0
                            selectedLaunchId = launchCombo.currentValue ?? ""
                            loadLaunch()
                        }
                    }
                }

                LcDialog {
                    id: smokeTestResultDialog
                    property bool passed: false
                    property string output: ""
                    title: {
                        const _lang = App.langV
                        return passed ? App.l("profiles.smokeTestPassed") : App.l("profiles.smokeTestFailed")
                    }
                    width: 520
                    height: 320
                    contentItem: Item {
                        width: 484
                        height: 220
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.inputBg
                            radius: 6
                            border.color: smokeTestResultDialog.passed ? Theme.successText : Theme.errorText
                            clip: true
                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 8
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                TextArea {
                                    readOnly: true
                                    text: smokeTestResultDialog.output
                                    color: smokeTestResultDialog.passed ? Theme.successText : Theme.errorText
                                    font { family: Theme.codeFont; pixelSize: 11 }
                                    wrapMode: TextArea.WrapAnywhere
                                    background: null
                                }
                            }
                        }
                    }
                }

                Connections {
                    target: App
                    function onSmokeTestFinished(passed, output) {
                        smokeTestRunning = false
                        smokeTestResultDialog.passed = passed
                        smokeTestResultDialog.output = output
                        smokeTestResultDialog.open()
                    }
                }

                LcDialog {
                    id: renameDialog
                    title: (App.langV, App.l("profiles.renameTitle"))
                    width: 440
                    height: 200
                    contentItem: Item {
                        width: 404
                        height: 40
                        LcTextField {
                            id: renameField
                            anchors.fill: parent
                            Keys.onReturnPressed: renameDialog.accept()
                            Keys.onEnterPressed: renameDialog.accept()
                        }
                    }
                    onAccepted: {
                        const newName = renameField.text.trim()
                        if (newName.length === 0) return
                        const lp = App.profileManager.getLaunchProfile(selectedLaunchId)
                        App.profileManager.updateLaunchProfile({
                            "id": selectedLaunchId, "name": newName,
                            "backendProfileId": lp.backendProfileId ?? "",
                            "modelProfileId": lp.modelProfileId ?? "",
                            "runtimePresetId": lp.runtimePresetId ?? "",
                            "extraArgs": lp.extraArgs ?? [],
                            "envOverrides": lp.envOverrides ?? {}
                        })
                    }
                }

                // ── Import dialog ─────────────────────────────────────────────
                Dialog {
                    id: importDialog
                    title: "Importar perfil desde argumentos"
                    modal: true
                    parent: Overlay.overlay
                    x: Math.round((parent.width - width) / 2)
                    y: Math.round((parent.height - height) / 2)
                    width: 560
                    height: 420
                    closePolicy: Popup.CloseOnEscape

                    background: Rectangle {
                        color: Theme.popupBg; radius: 12
                        border.color: Theme.popupBorderColor; border.width: 1
                    }
                    Overlay.modal: Rectangle { color: Theme.overlayColor }

                    header: Rectangle {
                        color: Theme.popupHeaderBg; height: 56; radius: 12
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
                        Text {
                            anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                            text: "Importar perfil desde argumentos"
                            font { pixelSize: 14; bold: true }
                            color: Theme.textPrimary
                        }
                    }

                    footer: Rectangle {
                        color: Theme.popupHeaderBg; height: 56; radius: 12
                        Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                        Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
                        Row {
                            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                            spacing: 10
                            LcButton {
                                text: "Cancelar"; secondary: true
                                onClicked: { importDialog.close(); importNameField.text = ""; importArgsArea.text = "" }
                            }
                            LcButton {
                                text: "Importar"
                                enabled: importNameField.text.trim().length > 0 && importArgsArea.text.trim().length > 0
                                onClicked: {
                                    importFromArgs(importNameField.text.trim(), importArgsArea.text)
                                    importDialog.close()
                                    importNameField.text = ""
                                    importArgsArea.text = ""
                                }
                            }
                        }
                    }

                    contentItem: ColumnLayout {
                        spacing: 12
                        width: 520

                        Text { text: "Nombre del perfil:"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField {
                            id: importNameField
                            Layout.fillWidth: true
                            placeholderText: "Mi servidor local"
                            Keys.onTabPressed: importArgsArea.forceActiveFocus()
                        }

                        Text {
                            text: "Pegar argumentos (desde terminal, script, o README):"
                            color: Theme.textSecondary; font.pixelSize: 12
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: Theme.inputBg; radius: 8
                            border.color: importArgsArea.activeFocus ? Theme.inputBorderFocus : Theme.borderColor
                            border.width: 1
                            clip: true

                            ScrollView {
                                anchors.fill: parent; anchors.margins: 2
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                TextArea {
                                    id: importArgsArea
                                    placeholderText: "./llama-server --model /ruta/al/modelo.gguf --ctx-size 8192 --port 8080 --n-gpu-layers 99 --flash-attn"
                                    color: Theme.textPrimary
                                    placeholderTextColor: Theme.textMuted
                                    font { family: Theme.codeFont; pixelSize: 12 }
                                    wrapMode: TextArea.WrapAnywhere
                                    background: null
                                    padding: 10
                                    selectByMouse: true
                                }
                            }
                        }

                        Text {
                            text: "Se parsearán: --host --port --model --ctx-size --batch-size --ubatch-size --threads --n-gpu-layers --flash-attn --no-mmap --mlock --parallel --cache-type-k"
                            color: Theme.textMuted; font.pixelSize: 10
                            wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }

                    onOpened: importNameField.forceActiveFocus()
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: backendGrid.implicitHeight + 20
                    GridLayout {
                        id: backendGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 2; rowSpacing: 8; columnSpacing: 10

                        property bool cloud: backendKind.currentValue === "cloud"

                        Text { text: "Tipo"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcComboBox {
                            id: backendKind
                            Layout.fillWidth: true
                            textRole: "text"; valueRole: "value"
                            model: [ { text: "Local (llama-server)", value: "local" },
                                     { text: "Cloud (OpenAI-compat: OpenAI / OpenRouter / Groq…)", value: "cloud" } ]
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: backendKind.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }

                        Text { text: "Binary"; color: Theme.textSecondary; font.pixelSize: 12; visible: !backendGrid.cloud }
                        LcComboBox {
                            id: backendBinary
                            Layout.fillWidth: true
                            visible: !backendGrid.cloud
                            model: App.binaryRegistry
                            textRole: "displayLabel"; valueRole: "binId"
                            // Elegir explícitamente = deja de estar "sin fijar".
                            onActivated: root.backendBinaryUnset = false
                            displayText: root.backendBinaryUnset
                                ? "(automático — sin fijar)" : currentText
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: backendBinary.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                        Text { text: "Host"; color: Theme.textSecondary; font.pixelSize: 12; visible: !backendGrid.cloud }
                        LcTextField { id: backendHost; Layout.fillWidth: true; visible: !backendGrid.cloud }
                        Text { text: "Port"; color: Theme.textSecondary; font.pixelSize: 12; visible: !backendGrid.cloud }
                        LcTextField { id: backendPort; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; visible: !backendGrid.cloud }

                        Text { text: "Base URL"; color: Theme.textSecondary; font.pixelSize: 12; visible: backendGrid.cloud }
                        LcTextField { id: cloudBaseUrl; Layout.fillWidth: true; visible: backendGrid.cloud; placeholderText: "https://api.openai.com" }
                        Text { text: "Var de key"; color: Theme.textSecondary; font.pixelSize: 12; visible: backendGrid.cloud }
                        LcTextField { id: cloudKeyRef; Layout.fillWidth: true; visible: backendGrid.cloud; placeholderText: "OPENAI_API_KEY" }
                        Text { text: "API key"; color: Theme.textSecondary; font.pixelSize: 12; visible: backendGrid.cloud }
                        ColumnLayout {
                            Layout.fillWidth: true; visible: backendGrid.cloud; spacing: 2
                            LcTextField {
                                id: cloudKeyValue; Layout.fillWidth: true; echoMode: TextInput.Password
                                placeholderText: cloudKeyStored.visible ? "•••• ya guardada (dejá vacío)" : "sk-… (se guarda fuera del repo)"
                            }
                            Text {
                                id: cloudKeyStored
                                text: "✓ key resuelta (env var o store) — no se commitea"
                                color: Theme.textMuted; font.pixelSize: 10
                                visible: cloudKeyRef.text.length > 0 && App.hasSecret(cloudKeyRef.text)
                            }
                        }
                        Text { text: "Modelo"; color: Theme.textSecondary; font.pixelSize: 12; visible: backendGrid.cloud }
                        LcTextField { id: cloudModel; Layout.fillWidth: true; visible: backendGrid.cloud; placeholderText: "gpt-4o · anthropic/claude-…" }
                        Text { text: "Ctx"; color: Theme.textSecondary; font.pixelSize: 12; visible: backendGrid.cloud }
                        LcTextField { id: cloudCtx; Layout.fillWidth: true; visible: backendGrid.cloud; inputMethodHints: Qt.ImhDigitsOnly; placeholderText: "32768" }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: modelGrid.implicitHeight + 20
                    GridLayout {
                        id: modelGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 3; rowSpacing: 8; columnSpacing: 10

                        Item { implicitWidth: 20 }
                        Text { text: "Main model"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcComboBox {
                            id: modelMain
                            Layout.fillWidth: true
                            model: App.modelCatalog; textRole: "fileName"; valueRole: "modelId"
                            onActivated: root.modelMainUnresolved = ""
                            displayText: root.modelMainUnresolved.length > 0
                                ? "(no está en el catálogo — elegí uno)" : currentText
                            background: Rectangle { color: Theme.inputBg; radius: 6
                                border.color: root.modelMainUnresolved.length > 0 ? Theme.errorBorder : Theme.borderColor }
                            contentItem: Text { text: modelMain.displayText
                                color: root.modelMainUnresolved.length > 0 ? Theme.errorText : Theme.textPrimary
                                font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }

                        CheckBox { id: mmprojCheck; checked: mmprojEnabled; onCheckedChanged: mmprojEnabled = checked; padding: 0 }
                        Text { text: "mmproj"; color: mmprojEnabled ? Theme.textSecondary : Theme.textMuted; font.pixelSize: 12 }
                        LcComboBox {
                            id: modelMmproj
                            Layout.fillWidth: true
                            enabled: mmprojEnabled; opacity: mmprojEnabled ? 1.0 : 0.4
                            model: App.modelCatalog; textRole: "fileName"; valueRole: "modelId"
                            onActivated: root.modelMmprojUnresolved = ""
                            displayText: root.modelMmprojUnresolved.length > 0
                                ? "(no está en el catálogo — elegí uno)" : currentText
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: modelMmproj.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }

                        CheckBox { id: draftCheck; checked: draftEnabled; onCheckedChanged: draftEnabled = checked; padding: 0 }
                        Text { text: "Draft model"; color: draftEnabled ? Theme.textSecondary : Theme.textMuted; font.pixelSize: 12 }
                        LcComboBox {
                            id: modelDraft
                            Layout.fillWidth: true
                            enabled: draftEnabled; opacity: draftEnabled ? 1.0 : 0.4
                            model: App.modelCatalog; textRole: "fileName"; valueRole: "modelId"
                            onActivated: root.modelDraftUnresolved = ""
                            displayText: root.modelDraftUnresolved.length > 0
                                ? "(no está en el catálogo — elegí uno)" : currentText
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: modelDraft.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }

                        // ── Speculative decoding / MTP (draft separado o embebido) ──
                        CheckBox { id: mtpCheck; checked: mtpEnabled; onCheckedChanged: mtpEnabled = checked; padding: 0 }
                        Text { text: draftEnabled ? "MTP (draft-mtp)" : "MTP autocontenido"; color: mtpEnabled ? Theme.textSecondary : Theme.textMuted; font.pixelSize: 12 }
                        Item { Layout.fillWidth: true; implicitHeight: 1 }

                        Item { implicitWidth: 20 }
                        Text { text: "spec n-max"; color: mtpEnabled ? Theme.textSecondary : Theme.textMuted; font.pixelSize: 12 }
                        LcTextField {
                            id: specNMaxField
                            Layout.fillWidth: true
                            enabled: mtpEnabled; opacity: enabled ? 1.0 : 0.4
                            inputMethodHints: Qt.ImhDigitsOnly
                            placeholderText: "0 = default"
                        }

                        Item { implicitWidth: 20 }
                        Text { text: "draft KV"; color: (draftEnabled && mtpEnabled) ? Theme.textSecondary : Theme.textMuted; font.pixelSize: 12 }
                        LcComboBox {
                            id: specKvType
                            Layout.fillWidth: true
                            enabled: draftEnabled && mtpEnabled; opacity: enabled ? 1.0 : 0.4
                            model: ["", "q8_0"]
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: specKvType.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: runtimeGrid.implicitHeight + 20
                    GridLayout {
                        id: runtimeGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 4; rowSpacing: 8; columnSpacing: 10

                        Text { text: "ctx"; color: Theme.textSecondary; font.pixelSize: 12 }
                        RowLayout {
                            Layout.fillWidth: true
                            LcTextField { id: ctxField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                            LcButton {
                                text: "Sugerir"
                                secondary: true
                                enabled: modelMain.currentValue !== undefined && modelMain.currentValue !== ""
                                onClicked: {
                                    const m = App.modelCatalog.get(modelMain.currentValue)
                                    const trained = Number(m.trainedContext || 0)
                                    if (trained > 0)
                                        ctxField.text = String(Math.min(trained, 32768))
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: "Usa hasta 32k sin superar el contexto entrenado del GGUF. No se aplica automáticamente."
                            }
                        }
                        Text { text: "batch"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: batchField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "ubatch"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: ubatchField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "threads"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: threadsField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "gpuLayers"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: gpuLayersField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "parallelSlots"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: parallelSlotsField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cacheType"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheTypeField; Layout.fillWidth: true }
                        Text { text: "tensorOverrides"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField {
                            id: tensorOverridesField; Layout.fillWidth: true
                            placeholderText: "ffn_.*=Q4_K, attn_.*=Q8_0"
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    CheckBox { id: flashAttnCheck; text: "flash-attn"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: mmapCheck; text: "mmap"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: mlockCheck; text: "mlock"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: contBatchCheck; text: "cont-batching"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                }

                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: advancedGrid.implicitHeight + 20
                    GridLayout {
                        id: advancedGrid
                        anchors.fill: parent; anchors.margins: 10
                        columns: 4; rowSpacing: 8; columnSpacing: 10

                        Text { text: "alias"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: aliasField; Layout.fillWidth: true }
                        Text { text: "n-predict"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: nPredictField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cache-type-v"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheTypeVField; Layout.fillWidth: true }
                        Text { text: "temp"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: tempField; Layout.fillWidth: true }
                        Text { text: "top-p"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: topPField; Layout.fillWidth: true }
                        Text { text: "top-k"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: topKField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "repeat-penalty"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: repeatPenaltyField; Layout.fillWidth: true }
                        Text { text: "presence-penalty"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: presencePenaltyField; Layout.fillWidth: true }
                        Text { text: "cache-ram"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheRamField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "cache-reuse"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField { id: cacheReuseField; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly }
                        Text { text: "power-limit (W)"; color: Theme.textSecondary; font.pixelSize: 12 }
                        LcTextField {
                            id: powerLimitField
                            Layout.fillWidth: true
                            inputMethodHints: Qt.ImhDigitsOnly
                            placeholderText: "0 = global"
                        }
                        Text { text: "browser MCP"; color: Theme.textSecondary; font.pixelSize: 12 }
                        ComboBox {
                            id: browserAutoCombo
                            Layout.fillWidth: true
                            // value = "inherit" | "on" | "off"
                            textRole: "label"
                            valueRole: "value"
                            model: [
                                { label: "Heredar (global)", value: "inherit" },
                                { label: "Forzar ON",        value: "on" },
                                { label: "Forzar OFF",       value: "off" }
                            ]
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    CheckBox { id: contextShiftCheck;   text: "context-shift";    checked: true; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: metricsCheck; text: "metrics"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                    CheckBox { id: noWarmupCheck; text: "no-warmup"; contentItem: Text { text: parent.text; color: Theme.textSecondary; leftPadding: parent.indicator.width + 6 } }
                }

                // ── Agente ───────────────────────────────────────────────────
                // Política: todo perfil usa el agente nativo LlamaAgent. No hay
                // selector de harness (se quitaron "Ninguno" y "Opencode"). Si el
                // usuario no quiere agente, usa el modo Chat. LlamaAgent es backend
                // interno (sin binario), así que siempre está disponible.
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: harnessCol.implicitHeight + 20

                    ColumnLayout {
                        id: harnessCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text { text: "🛠"; font.pixelSize: 18 }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Text {
                                    text: "Agente: LlamaAgent"
                                    color: Theme.textPrimary
                                    font { pixelSize: 13; bold: true }
                                }
                                Text {
                                    text: "Agente nativo, siempre activo. Para usar sin agente, abrí el modo Chat."
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            Row {
                                spacing: 6
                                Rectangle { width: 7; height: 7; radius: 4; color: Theme.successText; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    text: (App.langV, App.l("harness.installed"))
                                    color: Theme.successText
                                    font.pixelSize: 11
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        // Perfil de agente por defecto (capacidades + directivas).
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text {
                                text: "Perfil de agente"
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 110
                            }
                            LcComboBox {
                                id: agentProfileCombo
                                Layout.fillWidth: true
                                model: App.profileManager.agentProfiles
                                textRole: "name"; valueRole: "profileId"
                                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                contentItem: Text { text: agentProfileCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                            }
                            Text {
                                text: "Se editan en Ajustes › Perfiles de agente"
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                // ─────────────────────────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: 220
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10
                        Text { text: (App.langV, App.l("profiles.extraArgs")); color: Theme.textSecondary; font.pixelSize: 12 }
                        ScrollView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                            TextArea {
                                id: manualExtraArgsArea
                                color: Theme.textPrimary
                                wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            }
                        }
                    }
                }

                // ── Modo híbrido ─────────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg; border.color: Theme.borderColor; radius: 8
                    implicitHeight: hybridCol.implicitHeight + 20
                    ColumnLayout {
                        id: hybridCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8
                        Text { text: "Modo híbrido — Planificador + ejecutor"; color: Theme.textSecondary; font { pixelSize: 12; bold: true } }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: "En cada request, el perfil elegido arma un plan detallado y este perfil lo ejecuta. Secuencial permite compartir GPU y puerto."
                            color: Theme.textMuted; font.pixelSize: 11
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            LcComboBox {
                                id: hybridModeCombo; Layout.preferredWidth: 180
                                model: [{text:"Desactivado", value:"off"}, {text:"Secuencial", value:"sequential"}, {text:"Concurrente", value:"concurrent"}]
                                textRole: "text"; valueRole: "value"
                                currentIndex: Math.max(0, indexOfValue(root.hybridMode))
                                onActivated: root.hybridMode = currentValue
                            }
                            LcComboBox {
                                id: plannerProfileCombo; Layout.fillWidth: true
                                enabled: root.hybridMode !== "off"
                                model: App.profileManager.launchProfilesForMenu()
                                textRole: "displayName"; valueRole: "id"
                                currentIndex: Math.max(0, indexOfValue(root.plannerProfileId))
                                onActivated: root.plannerProfileId = currentValue
                            }
                        }
                    }
                }

                // ── Maestro (supervisor) ─────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: masterCol.implicitHeight + 20
                    ColumnLayout {
                        id: masterCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        Text {
                            text: "Maestro (supervisor) — Fallbacks"
                            color: Theme.textSecondary; font { pixelSize: 12; bold: true }
                        }
                        Text {
                            text: "Cadena ordenada de modelos/CLIs más capaces. Al atascarse el LLM local, "
                                + "se escala al primero; si también falla, al siguiente, y así sucesivamente."
                            color: Theme.textMuted; font.pixelSize: 11
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }

                        // ── Lista de fallbacks (ordenada) ─────────────────────
                        Repeater {
                            model: masterChainModel
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                color: Theme.inputBg
                                border.color: Theme.borderColor
                                radius: 6
                                implicitHeight: fbCol.implicitHeight + 16
                                ColumnLayout {
                                    id: fbCol
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                    spacing: 6

                                    // Encabezado: nivel + tipo + reordenar/quitar
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 6
                                        Text { text: "#" + (index + 1); color: Theme.textSecondary
                                               Layout.preferredWidth: 28
                                               font.pixelSize: 12; font.bold: true }
                                        LcComboBox {
                                            Layout.fillWidth: true; implicitHeight: 30
                                            model: [
                                                { value: "profile", label: "Otro perfil LlamaCode" },
                                                { value: "http",    label: "Cloud API (OpenAI-compat)" },
                                                { value: "cli",     label: "CLI suscripción (Claude/Codex)" }
                                            ]
                                            textRole: "label"; valueRole: "value"
                                            currentIndex: Math.max(0, indexOfValue(type))
                                            onActivated: masterChainModel.setProperty(index, "type", currentValue)
                                        }
                                        LcButton { text: "▲"; secondary: true; implicitHeight: 28; implicitWidth: 30
                                            enabled: index > 0
                                            onClicked: masterChainModel.move(index, index - 1, 1) }
                                        LcButton { text: "▼"; secondary: true; implicitHeight: 28; implicitWidth: 30
                                            enabled: index < masterChainModel.count - 1
                                            onClicked: masterChainModel.move(index, index + 1, 1) }
                                        LcButton { text: "✕"; secondary: true; implicitHeight: 28; implicitWidth: 30
                                            onClicked: masterChainModel.remove(index) }
                                    }

                                    LcTextField {
                                        Layout.fillWidth: true; implicitHeight: 30
                                        placeholderText: "Etiqueta (opcional, ej. GPT-4o / Claude sub)"
                                        text: label ?? ""
                                        onTextChanged: masterChainModel.setProperty(index, "label", text)
                                    }

                                    // ── type == profile ──────────────────────
                                    LcComboBox {
                                        visible: type === "profile"
                                        Layout.fillWidth: true; implicitHeight: 30
                                        model: App.profileManager.launchProfilesForMenu()
                                        textRole: "displayName"; valueRole: "id"
                                        currentIndex: Math.max(0, indexOfValue(masterChainModel.get(index).profileId))
                                        onActivated: masterChainModel.setProperty(index, "profileId", currentValue)
                                    }

                                    // ── type == http (cloud) ─────────────────
                                    ColumnLayout {
                                        visible: type === "http"
                                        Layout.fillWidth: true; spacing: 4
                                        LcTextField { Layout.fillWidth: true; implicitHeight: 30
                                            placeholderText: "Endpoint (ej. https://api.openai.com)"
                                            text: httpUrl ?? ""
                                            onTextChanged: masterChainModel.setProperty(index, "httpUrl", text) }
                                        LcTextField { Layout.fillWidth: true; implicitHeight: 30
                                            placeholderText: "Modelo (ej. gpt-4o)"
                                            text: httpModel ?? ""
                                            onTextChanged: masterChainModel.setProperty(index, "httpModel", text) }
                                        LcTextField { Layout.fillWidth: true; implicitHeight: 30
                                            echoMode: TextInput.Password
                                            placeholderText: (httpKeyRef && httpKeyRef.length > 0)
                                                ? "API key guardada — escribí para reemplazar" : "API key"
                                            text: httpKeyValue ?? ""
                                            onTextChanged: masterChainModel.setProperty(index, "httpKeyValue", text) }
                                    }

                                    // ── type == cli ──────────────────────────
                                    ColumnLayout {
                                        visible: type === "cli"
                                        Layout.fillWidth: true; spacing: 4
                                        RowLayout {
                                            Layout.fillWidth: true; spacing: 6
                                            LcComboBox {
                                                Layout.fillWidth: true; implicitHeight: 30
                                                model: App.masterCliList()
                                                currentIndex: Math.max(0, indexOfValue(masterChainModel.get(index).cliName))
                                                onActivated: masterChainModel.setProperty(index, "cliName", currentValue)
                                            }
                                            LcButton { text: "Detectar"; secondary: true; implicitHeight: 28
                                                onClicked: refreshMasterCliStatus(true) }
                                        }
                                        Text {
                                            Layout.fillWidth: true; font.pixelSize: 11; wrapMode: Text.WrapAnywhere
                                            property var st: masterCliStatusCache[cliName] ?? ({})
                                            text: (st.installed === true)
                                                ? ((st.label ?? cliName) + " — " + (st.version ?? "instalado"))
                                                : ((st.label ?? cliName) + " no detectado · " + App.masterCliInstallCommand(cliName))
                                            color: (st.installed === true) ? Theme.successText : Theme.textMuted
                                        }
                                        CheckBox {
                                            text: "Edita archivos directo (sino, sólo plan)"
                                            checked: applyEdits !== false
                                            onToggled: masterChainModel.setProperty(index, "applyEdits", checked)
                                            contentItem: Text {
                                                text: parent.text; color: Theme.textPrimary; font.pixelSize: 12
                                                leftPadding: parent.indicator.width + 6
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 6
                                        Text { text: "Timeout (s)"; color: Theme.textSecondary; font.pixelSize: 11 }
                                        SpinBox {
                                            from: 10; to: 3600; stepSize: 10; implicitHeight: 28
                                            value: timeoutSec ?? 300
                                            onValueModified: masterChainModel.setProperty(index, "timeoutSec", value)
                                        }
                                    }
                                }
                            }
                        }

                        LcButton {
                            text: "+ Agregar fallback"; secondary: true; implicitHeight: 30
                            onClicked: masterChainModel.append({
                                type: "http", label: "", profileId: "",
                                httpUrl: "", httpModel: "", httpKeyRef: "", httpKeyValue: "",
                                cliName: "claude", applyEdits: true, timeoutSec: 300 })
                        }

                        // ── Política de escalado (global a la cadena) ─────────
                        RowLayout {
                            visible: masterChainModel.count > 0
                            Layout.fillWidth: true; spacing: 8
                            Text { text: "Escalado"; color: Theme.textSecondary; font.pixelSize: 12
                                   Layout.preferredWidth: 90 }
                            LcComboBox {
                                id: masterEscalationCombo
                                Layout.fillWidth: true
                                implicitHeight: 32
                                model: [
                                    { value: "manual", label: "Manual (botón en el agente)" },
                                    { value: "auto",   label: "Automático (al atascarse)" },
                                    { value: "both",   label: "Ambos" }
                                ]
                                textRole: "label"
                                valueRole: "value"
                                currentIndex: Math.max(0, indexOfValue(masterEscalation))
                                onActivated: masterEscalation = currentValue
                            }
                        }
                        RowLayout {
                            visible: masterChainModel.count > 0 && masterEscalation !== "manual"
                            Layout.fillWidth: true; spacing: 8
                            Text { text: "Auto tras N fallos"; color: Theme.textSecondary; font.pixelSize: 12
                                   Layout.preferredWidth: 130 }
                            SpinBox {
                                from: 1; to: 20; value: masterAutoAfterFails
                                onValueModified: masterAutoAfterFails = value
                                implicitHeight: 32
                            }
                        }
                    }
                }

                // ── Vista previa del comando ─────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    radius: 8
                    implicitHeight: previewCol.implicitHeight + 20
                    ColumnLayout {
                        id: previewCol
                        anchors.fill: parent; anchors.margins: 10
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: (App.langV, App.l("launch.cmdPreview"))
                                color: Theme.textSecondary; font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            LcButton {
                                text: "Recalcular comando"
                                secondary: true
                                onClicked: recomputePreview()
                            }
                        }
                        CommandPreview {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 140
                            visible: App.effectiveProfile.launchId === selectedLaunchId
                            commandLine: App.effectiveProfile.commandLine ?? ""
                            warnings: App.effectiveProfile.warnings ?? []
                            errors: App.effectiveProfile.blockingErrors ?? []
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.preferredHeight: 16 }
                }
            }
        }
    }
}
