#include "EffectiveProfileBuilder.h"
#include "MtpDetection.h"
#include "../GGUFScanner.h"
#include <QFileInfo>
#include <QRegularExpression>

namespace {
struct SamplingFlag {
    QString canonical;
    QStringList names;
    QString recommended;
};
}

static bool supportsAnyFlag(const LlamaBinary &bin, const QString &flag)
{
    const QString resolved = bin.resolveFlag(flag);
    return bin.supportedFlags.isEmpty() || bin.supportsFlag(resolved);
}

static bool isTemplateThinkingModel(const CatalogModel &model)
{
    const QString combined = (model.fileName + QLatin1Char(' ') + model.familyHint).toLower();
    return combined.contains(QStringLiteral("qwen3")) ||
           combined.contains(QStringLiteral("qwen-3")) ||
           combined.contains(QStringLiteral("qwq"));
}

static bool isQwenCodingModel(const CatalogModel &model)
{
    const QString combined = (model.fileName + QLatin1Char(' ') + model.familyHint).toLower();
    return combined.contains(QStringLiteral("qwen")) ||
           combined.contains(QStringLiteral("qwq")) ||
           combined.contains(QStringLiteral("coder"));
}

static QList<SamplingFlag> codingSamplingFlags()
{
    return {
        {QStringLiteral("--temp"),
         {QStringLiteral("--temp"), QStringLiteral("--temperature")},
         QStringLiteral("0.6")},
        {QStringLiteral("--top-p"),
         {QStringLiteral("--top-p"), QStringLiteral("--top_p")},
         QStringLiteral("0.95")},
        {QStringLiteral("--top-k"),
         {QStringLiteral("--top-k"), QStringLiteral("--top_k")},
         QStringLiteral("20")},
        {QStringLiteral("--min-p"),
         {QStringLiteral("--min-p"), QStringLiteral("--min_p")},
         QStringLiteral("0.0")},
        {QStringLiteral("--repeat-penalty"),
         {QStringLiteral("--repeat-penalty"), QStringLiteral("--repeat_penalty")},
         QStringLiteral("1.0")},
        {QStringLiteral("--presence-penalty"),
         {QStringLiteral("--presence-penalty"), QStringLiteral("--presence_penalty")},
         QStringLiteral("0.0")},
    };
}

static bool hasAnyFlag(const QStringList &args, const QStringList &names)
{
    for (const QString &name : names) {
        if (args.contains(name))
            return true;
    }
    return false;
}

static QString valueAfterFlag(const QStringList &args, const QStringList &names)
{
    for (int i = 0; i < args.size(); ++i) {
        if (!names.contains(args.at(i))) continue;
        if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-')))
            return args.at(i + 1);
        return QString();
    }
    return QString();
}

static void removeFlagWithValue(QStringList &args, const QStringList &names)
{
    for (int i = 0; i < args.size(); ++i) {
        if (!names.contains(args.at(i))) continue;
        args.removeAt(i);
        if (i < args.size() && !args.at(i).startsWith(QLatin1Char('-')))
            args.removeAt(i);
        --i;
    }
}

static int llamaCppBuildNumber(const LlamaBinary &bin)
{
    const QString haystack = (bin.versionHint + QLatin1Char(' ') + bin.name
                              + QLatin1Char(' ') + bin.path).toLower();
    const QRegularExpression re(QStringLiteral("\\bb(\\d{4,})\\b"));
    const QRegularExpressionMatch m = re.match(haystack);
    if (!m.hasMatch())
        return 0;
    return m.captured(1).toInt();
}

static bool supportsGemma4AssistantDraft(const LlamaBinary &bin)
{
    const int build = llamaCppBuildNumber(bin);
    return build == 0 || build >= 9763;
}

void EffectiveProfileBuilder::applyAdaptiveSpeculation(const ModelProfile &mp,
                                                       const LlamaBinary &bin,
                                                       QStringList &args,
                                                       QStringList &warnings,
                                                       QStringList &errors)
{
    if (!mp.specDraftAdaptive)
        return;

    if (mp.specDraftNMax <= 0) {
        errors.append(QStringLiteral(
            "Adaptive speculative decoding requiere spec-draft-n-max mayor que cero."));
        return;
    }
    if (mp.specDraftNMin < 0 || mp.specDraftNMin > mp.specDraftNMax) {
        errors.append(QStringLiteral(
            "Adaptive speculative decoding requiere 0 <= spec-draft-n-min <= "
            "spec-draft-n-max."));
        return;
    }

    // La capacidad se valida como obligatoria: activar el modo adaptativo en un
    // binario que sólo conoce el MTP fijo produce una configuración engañosa.
    addFlag(bin, "--spec-draft-adaptive", {}, args, warnings, true, &errors);
    if (mp.specDraftNMin > 0)
        addFlag(bin, "--spec-draft-n-min", QString::number(mp.specDraftNMin), args,
                warnings, true, &errors);
}

static bool isNinfer3090(const LlamaBinary &bin)
{
    const QString tag = (bin.flavor + QLatin1Char(' ') + bin.name + QLatin1Char(' ')
                         + bin.path).toLower();
    const QString exe = QFileInfo(bin.path).fileName();
    return tag.contains(QStringLiteral("ninfer-3090"))
        || tag.contains(QStringLiteral("ninfer-rtx3090"))
        || exe.compare(QStringLiteral("ninfer-serve.exe"), Qt::CaseInsensitive) == 0
        || exe.compare(QStringLiteral("ninfer-serve"), Qt::CaseInsensitive) == 0;
}

EffectiveProfile EffectiveProfileBuilder::build(const Context &ctx)
{
    EffectiveProfile result;
    QStringList args;
    QMap<QString, QString> env = ctx.binary.envDefaults;

    // Cloud/remote OpenAI-compatible profiles are already served by an external
    // process. They must be fingerprintable and selectable without pretending
    // that a local llama-server binary or GGUF is required.
    if (ctx.backend.isCloud()) {
        if (ctx.backend.cloudBaseUrl.trimmed().isEmpty())
            result.blockingErrors.append("Cloud backend has no base URL.");
        if (ctx.backend.cloudModel.trimmed().isEmpty())
            result.blockingErrors.append("Cloud backend has no model.");

        for (auto it = ctx.backend.envOverrides.cbegin();
             it != ctx.backend.envOverrides.cend(); ++it)
            env[it.key()] = it.value();
        for (auto it = ctx.launch.envOverrides.cbegin();
             it != ctx.launch.envOverrides.cend(); ++it)
            env[it.key()] = it.value();

        result.effectiveArgs = ctx.launch.extraArgs;
        result.effectiveEnv = env;
        result.commandLine = QStringLiteral("<external> %1 model=%2")
                             .arg(ctx.backend.cloudBaseUrl.trimmed(),
                                  ctx.backend.cloudModel.trimmed());
        return result;
    }

    // Validate binary
    if (ctx.binary.id.isEmpty()) {
        result.blockingErrors.append("No binary selected.");
        return result;
    }
    if (!QFileInfo::exists(ctx.binary.path)) {
        result.blockingErrors.append(
            QStringLiteral("Binary not found: %1").arg(ctx.binary.path));
        return result;
    }

    applyBackend(ctx.backend, args, env, result.warnings, result.blockingErrors);
    applyModel(ctx.model, ctx.catalogModel, ctx.mmprojModel, ctx.draftModel,
               ctx.binary, args, result.warnings, result.blockingErrors);
    // Speculative decoding activo: el perfil puede elegir explícitamente el KV.
    // La política del catálogo admite q8_0 o menor; no se debe elevar en silencio
    // a f16 porque eso vuelve incomparable la candidata y aumenta el uso de VRAM.
    // La aceptación del draft puede bajar con KV cuantizado, por lo que se avisa
    // y se deja que el benchmark mida el costo real.
    const bool specDecoding =
        !ctx.model.specType.isEmpty()
        || (!ctx.model.draftModelId.isEmpty() && ctx.draftModel.isAvailable);
    applyRuntime(ctx.runtime, ctx.binary, args, result.warnings,
                 result.blockingErrors, specDecoding);

    // Harness env
    for (auto it = ctx.harness.env.begin(); it != ctx.harness.env.end(); ++it)
        env[it.key()] = it.value();

    // Launch overrides (raw, highest priority)
    for (auto it = ctx.launch.envOverrides.begin(); it != ctx.launch.envOverrides.end(); ++it)
        env[it.key()] = it.value();

    // Resolve aliases in extraArgs with pair-aware parsing.
    // NOTA: NO descartar `-np 1` / `--parallel 1`. Algunos forks (p.ej. MTP)
    // tienen n_parallel=auto que abre 4 slots; cada uno reserva su KV-cache del
    // ctx-size completo (262k) → OOM de VRAM y crash 0xC0000409. `-np 1` es
    // necesario para limitar a un slot. Pasar todos los flags tal cual.
    QStringList extraTokens;
    for (const QString &rawArg : ctx.launch.extraArgs) {
        const QStringList tokens = rawArg.trimmed().split(u' ', Qt::SkipEmptyParts);
        for (const QString &t : tokens)
            extraTokens.append(t);
    }
    // El draft puede venir del ModelProfile o declarado a mano en los extraArgs con
    // una ruta absoluta (--spec-draft-model / -md / --model-draft). Si está ahí y el
    // archivo existe, el perfil es válido: exigir además draftModelId lo rechazaba
    // por una vía que llama-server sí acepta.
    bool draftInExtraArgs = false;
    for (int i = 0; i + 1 < extraTokens.size(); ++i) {
        const QString &t = extraTokens.at(i);
        if (t != QLatin1String("--spec-draft-model") && t != QLatin1String("-md")
            && t != QLatin1String("--model-draft"))
            continue;
        if (QFileInfo::exists(extraTokens.at(i + 1))) { draftInExtraArgs = true; break; }
        result.warnings.append(
            QStringLiteral("El draft declarado en los argumentos no está en disco: %1")
                .arg(extraTokens.at(i + 1)));
    }

    for (int i = 0; i < extraTokens.size(); ++i) {
        const QString &cur = extraTokens.at(i);
        if (!cur.startsWith(u'-')) { args.append(cur); continue; }
        if (cur == QLatin1String("--spec-type") && i + 1 < extraTokens.size()
            && extraTokens.at(i + 1).contains(QStringLiteral("draft"), Qt::CaseInsensitive)
            && extraTokens.at(i + 1).compare(QStringLiteral("draft-dspark"),
                                             Qt::CaseInsensitive) != 0
            && ctx.model.draftModelId.isEmpty()
            && !draftInExtraArgs
            && !MtpDetection::isSelfContained(ctx.catalogModel.fileName)) {
            result.blockingErrors.append(QStringLiteral(
                "Este perfil declara %1 %2, pero no tiene draftModel asociado. "
                "Corregí el perfil o instalá un perfil actualizado que declare y descargue el draft.")
                .arg(cur, extraTokens.at(i + 1)));
        }
        // Flags de speculative/MTP/ngram: si el binario NO los soporta (p.ej.
        // fallback a llama.cpp oficial sin MTP), descartarlos junto con su valor
        // para no romper el arranque. Solo si el binario declara supportedFlags.
        const bool isSpec = cur.startsWith(QStringLiteral("--spec-"))
                            || cur.startsWith(QStringLiteral("--ngram-"))
                            || cur.startsWith(QStringLiteral("--draft-"));
        if (isSpec && !ctx.binary.supportedFlags.isEmpty()
            && !ctx.binary.supportsFlag(ctx.binary.resolveFlag(cur))) {
            result.blockingErrors.append(QStringLiteral(
                "Este perfil requiere la capacidad %1, pero el binario actual no la soporta. "
                "Actualizá o elegí un binario compatible antes de iniciar.")
                .arg(cur));
            if (i + 1 < extraTokens.size() && !extraTokens.at(i + 1).startsWith(u'-'))
                ++i;   // saltar el valor asociado
            continue;
        }
        args.append(ctx.binary.resolveFlag(cur));
    }

    // NInfer ya incorpora el chat-template en el artefacto y no acepta --jinja.
    if (!isNinfer3090(ctx.binary) && !args.contains(QStringLiteral("--jinja")))
        args << QStringLiteral("--jinja");

    applyReasoningControl(ctx, args, result.warnings);
    applySamplingPolicy(ctx, args, result.warnings);

    result.effectiveArgs = args;
    result.effectiveEnv = env;
    result.binaryPath = ctx.binary.path;
    result.commandLine = QStringLiteral("\"%1\" %2")
                         .arg(ctx.binary.path, args.join(' '));

    return result;
}

void EffectiveProfileBuilder::applyReasoningControl(const Context &ctx,
                                                    QStringList &args,
                                                    QStringList &warnings)
{
    // Extra/base args may already contain a manual reasoning mode. The UI toggle
    // is the source of truth for managed profiles, so remove contradictory forms
    // before appending the best supported controls.
    removeFlagWithValue(args, {
        QStringLiteral("--reasoning"), QStringLiteral("-rea"),
        QStringLiteral("--reasoning-budget"),
        QStringLiteral("--chat-template-kwargs")
    });

    const QString enabled = ctx.reasoningEnabled ? QStringLiteral("on") : QStringLiteral("off");
    const QString budget = ctx.reasoningEnabled ? QStringLiteral("-1") : QStringLiteral("0");
    bool applied = false;

    // New llama.cpp builds: this is the hard switch that changes the template
    // state itself (`chat template, thinking = 0/1`). Prefer it when available.
    if (supportsAnyFlag(ctx.binary, QStringLiteral("--reasoning"))) {
        addFlag(ctx.binary, QStringLiteral("--reasoning"), enabled, args, warnings);
        applied = true;
    }

    // Builds with reasoning budget but no hard switch: best sampler-level
    // fallback. It may still be softer than --reasoning off for some templates.
    if (!applied && supportsAnyFlag(ctx.binary, QStringLiteral("--reasoning-budget"))) {
        addFlag(ctx.binary, QStringLiteral("--reasoning-budget"), budget, args, warnings);
        applied = true;
    }

    // Qwen/QwQ-style templates in older servers expose enable_thinking through
    // chat-template kwargs. Only use it as a fallback; current llama.cpp warns
    // that this is deprecated in favor of --reasoning.
    if (!applied && isTemplateThinkingModel(ctx.catalogModel) &&
        supportsAnyFlag(ctx.binary, QStringLiteral("--chat-template-kwargs"))) {
        const QString json = ctx.reasoningEnabled
            ? QStringLiteral("{\"enable_thinking\":true}")
            : QStringLiteral("{\"enable_thinking\":false}");
        addFlag(ctx.binary, QStringLiteral("--chat-template-kwargs"), json, args, warnings);
        applied = true;
    }

    if (!applied) {
        warnings.append(QStringLiteral(
            "No hard thinking control supported by this binary/model combination; "
            "falling back to per-request hints only."));
    }
}

void EffectiveProfileBuilder::applySamplingPolicy(const Context &ctx,
                                                  QStringList &args,
                                                  QStringList &warnings)
{
    if (!isQwenCodingModel(ctx.catalogModel))
        return;

    const QList<SamplingFlag> flags = codingSamplingFlags();
    bool hasManualSampling = false;
    for (const SamplingFlag &flag : flags) {
        if (hasAnyFlag(args, flag.names)) {
            hasManualSampling = true;
            break;
        }
    }

    if (!hasManualSampling) {
        for (const SamplingFlag &flag : flags)
            addFlag(ctx.binary, flag.canonical, flag.recommended, args, warnings);
        warnings.append(QStringLiteral(
            "Qwen/coding profile: applied conservative sampling preset "
            "(temp 0.6, top-p 0.95, top-k 20, min-p 0.0, repeat/presence penalties neutral)."));
        return;
    }

    for (const SamplingFlag &flag : flags) {
        const QString configured = valueAfterFlag(args, flag.names);
        if (configured.isEmpty())
            continue;

        bool okConfigured = false;
        bool okRecommended = false;
        const double configuredValue = configured.toDouble(&okConfigured);
        const double recommendedValue = flag.recommended.toDouble(&okRecommended);
        const bool matches = okConfigured && okRecommended
            ? qAbs(configuredValue - recommendedValue) < 0.0001
            : configured == flag.recommended;
        if (!matches) {
            warnings.append(QStringLiteral(
                "Qwen/coding sampling: %1=%2 differs from recommended %3.")
                .arg(flag.canonical, configured, flag.recommended));
        }
    }
}

void EffectiveProfileBuilder::applyBackend(const BackendProfile &bp,
                                           QStringList &args,
                                           QMap<QString, QString> &env,
                                           QStringList &warnings,
                                           QStringList &errors)
{
    Q_UNUSED(errors)
    args << "--host" << bp.host;
    args << "--port" << QString::number(bp.port);
    args.append(bp.baseArgs);
    for (auto it = bp.envOverrides.begin(); it != bp.envOverrides.end(); ++it)
        env[it.key()] = it.value();
    if (bp.port < 1024)
        warnings.append(QStringLiteral("Port %1 requires admin privileges.").arg(bp.port));
}

void EffectiveProfileBuilder::applyModel(const ModelProfile &mp,
                                         const CatalogModel &model,
                                         const CatalogModel &mmproj,
                                         const CatalogModel &draft,
                                         const LlamaBinary &bin,
                                         QStringList &args,
                                         QStringList &warnings,
                                         QStringList &errors)
{
    if (mp.modelId.isEmpty() || model.id.isEmpty()) {
        errors.append("No model selected.");
        return;
    }
    if (!model.isAvailable) {
        errors.append(QStringLiteral("Model unavailable: %1").arg(model.fileName));
        return;
    }
    if (isNinfer3090(bin)) {
        if (!model.absolutePath.endsWith(QStringLiteral(".ninfer"), Qt::CaseInsensitive)) {
            errors.append(QStringLiteral(
                "NInfer-3090 sólo acepta artefactos .ninfer; el perfil apunta a '%1'.")
                .arg(model.fileName));
            return;
        }
        args << model.absolutePath;
        if (!mp.mmprojId.isEmpty() || !mp.draftModelId.isEmpty())
            warnings.append(QStringLiteral(
                "NInfer-3090 usa visión/MTP embebidos en el artefacto; se ignoran "
                "mmproj y draft externos del perfil."));
        if (mp.specDraftAdaptive)
            errors.append(QStringLiteral(
                "NInfer-3090 no admite adaptive speculative decoding administrado por LlamaCode."));
        return;
    }

    args << "--model" << model.absolutePath;

    // Gemma QAT q4_0 crudo (Google-style): degradado en llama.cpp. Avisar que el
    // dynamic quant de unsloth (UD-Q4_K_XL) rinde mejor a igual tamaño.
    if (GGUFScanner::isDegradedQatQuant(model.fileName, model.familyHint,
                                        model.quantReal)) {
        warnings.append(QStringLiteral(
            "Gemma QAT q4_0 'crudo' (%1): llama.cpp lo degrada (scales fp16 vs "
            "bf16). Preferí el dynamic quant de unsloth (UD-Q4_K_XL).")
            .arg(model.fileName));
    }

    if (!mp.mmprojId.isEmpty()) {
        if (!mmproj.isAvailable || mmproj.absolutePath.isEmpty())
            warnings.append("mmproj opcional no disponible; visión desactivada.");
        else
            addFlag(bin, "--mmproj", mmproj.absolutePath, args, warnings);
    }

    const bool selfContainedMtp = mp.specType == QLatin1String("draft-mtp")
        && mp.draftModelId.isEmpty()
        && MtpDetection::isSelfContained(model.fileName);
    if (selfContainedMtp) {
        addFlag(bin, "--spec-type", "draft-mtp", args, warnings);
        if (mp.specDraftNMax > 0)
            addFlag(bin, "--spec-draft-n-max", QString::number(mp.specDraftNMax), args, warnings);
        if (mp.specDraftConfMin > 0.0)
            addFlag(bin, "--spec-draft-conf-min",
                    QString::number(mp.specDraftConfMin, 'f', 3), args, warnings);
        applyAdaptiveSpeculation(mp, bin, args, warnings, errors);
    } else if (!mp.draftModelId.isEmpty()) {
        if (!draft.isAvailable || draft.absolutePath.isEmpty()) {
            errors.append(QStringLiteral(
                "Draft model unavailable: este perfil declara speculative/MTP con draft, "
                "pero el draft no está instalado. Instalá las dependencias del perfil antes de iniciar."));
        } else if (mp.specType == QLatin1String("draft-mtp")
                   && !supportsGemma4AssistantDraft(bin)) {
            errors.append(QStringLiteral(
                "Este perfil requiere llama-server b9763 o superior para cargar "
                "draft MTP gemma4-assistant. El binario actual es '%1'. "
                "Actualizá el binario compatible del perfil antes de iniciar.")
                .arg(bin.versionHint.isEmpty() ? bin.path : bin.versionHint));
        } else {
            // beellama (MTP/DFlash con draft separado) usa --spec-draft-model;
            // el spec-decoding plano de llama.cpp usa --draft-model.
            const QString draftFlag = !mp.specType.isEmpty()
                ? QStringLiteral("--spec-draft-model") : QStringLiteral("--draft-model");
            addFlag(bin, draftFlag, draft.absolutePath, args, warnings);
            // Flags de speculative decoding. MTP externo no necesita --spec-type
            // en este flujo; DSpark sí lo declara explícitamente en llama.cpp.
            if (mp.specType == QLatin1String("draft-dspark"))
                addFlag(bin, "--spec-type", "draft-dspark", args, warnings);
            if (mp.specDraftNMax > 0)
                addFlag(bin, "--spec-draft-n-max",
                        QString::number(mp.specDraftNMax), args, warnings);
            if (mp.specDraftConfMin > 0.0)
                addFlag(bin, "--spec-draft-conf-min",
                        QString::number(mp.specDraftConfMin, 'f', 3), args, warnings);
            if (!mp.specDraftNgl.isEmpty())
                addFlag(bin, "--spec-draft-ngl", mp.specDraftNgl, args, warnings);
            if (!mp.specDraftTypeK.isEmpty())
                addFlag(bin, "--spec-draft-type-k", mp.specDraftTypeK, args, warnings);
            if (!mp.specDraftTypeV.isEmpty())
                addFlag(bin, "--spec-draft-type-v", mp.specDraftTypeV, args, warnings);
            applyAdaptiveSpeculation(mp, bin, args, warnings, errors);
        }
    } else if (mp.specDraftAdaptive) {
        errors.append(QStringLiteral(
            "Adaptive speculative decoding requiere un modelo MTP/DFlash autocontenido "
            "o un draft model asociado."));
    }
}

void EffectiveProfileBuilder::applyRuntime(const RuntimePreset &rt,
                                           const LlamaBinary &bin,
                                           QStringList &args,
                                           QStringList &warnings,
                                           QStringList &errors,
                                           bool specDecoding)
{
    Q_UNUSED(errors)
    if (isNinfer3090(bin)) {
        args << "--max-context" << QString::number(rt.ctx);
        args << "--prefill-chunk" << QString::number(qMax(128, qMin(rt.ubatch, 1024)));
        args << "--kv-dtype"
             << ((rt.cacheType == QStringLiteral("f16") || rt.cacheType == QStringLiteral("bf16"))
                     ? QStringLiteral("bf16") : QStringLiteral("int8"));
        args << "--text-only";
        return;
    }
    args << "--ctx-size" << QString::number(rt.ctx);
    args << "--batch-size" << QString::number(rt.batch);
    args << "--ubatch-size" << QString::number(rt.ubatch);

    if (rt.threads > 0)
        args << "--threads" << QString::number(rt.threads);

    if (rt.gpuLayers < 0)
        args << "--n-gpu-layers" << "999";   // -1 = offload all layers
    else
        args << "--n-gpu-layers" << QString::number(rt.gpuLayers);

    if (rt.flashAttention)
        addFlag(bin, "--flash-attn", "on", args, warnings);

    if (!rt.mmap)
        addFlag(bin, "--no-mmap", {}, args, warnings);
    if (rt.mlock)
        addFlag(bin, "--mlock", {}, args, warnings);
    if (rt.contBatching)
        addFlag(bin, "--cont-batching", {}, args, warnings);

    if (rt.parallelSlots > 1)
        args << "--parallel" << QString::number(rt.parallelSlots);

    if (specDecoding && rt.parallelSlots > 1) {
        warnings.append(QStringLiteral(
            "Speculative decoding con --parallel > 1: la aceptación y la salida "
            "pueden divergir según el backend; validá con parallel=1."));
    }

    if (!rt.cacheType.isEmpty() && rt.cacheType != "f16") {
        if (specDecoding) {
            warnings.append(QStringLiteral(
                "Speculative decoding active: se respeta KV cache quant '%1'; "
                "puede reducir la aceptación del draft.").arg(rt.cacheType));
        }
        addFlag(bin, "--cache-type-k", rt.cacheType, args, warnings);
    }

    // llama.cpp b10228+ acepta varias reglas separadas por coma en un unico
    // --override-tensor. Repetir el flag conserva solo la ultima regla y puede
    // cambiar silenciosamente la colocacion/cuantiacion de tensores.
    QStringList validTensorOverrides;
    for (const QString &spec : rt.tensorOverrides) {
        const QString s = spec.trimmed();
        if (s.isEmpty())
            continue;
        if (!s.contains('=')) {
            warnings.append(QStringLiteral(
                "Ignoring malformed tensor override '%1' (expected '<regex>=<type>').")
                .arg(s));
            continue;
        }
        validTensorOverrides.append(s);
    }
    if (!validTensorOverrides.isEmpty())
        addFlag(bin, "--override-tensor", validTensorOverrides.join(','), args, warnings);
}

void EffectiveProfileBuilder::addFlag(const LlamaBinary &bin, const QString &flag,
                                      const QString &value, QStringList &args,
                                      QStringList &warnings, bool required,
                                      QStringList *errors)
{
    const QString resolved = bin.resolveFlag(flag);
    const bool supported = bin.supportedFlags.isEmpty() || bin.supportsFlag(resolved);
    if (!supported) {
        const QString msg = QStringLiteral("Flag %1 not supported by this binary.").arg(flag);
        if (required && errors) errors->append(msg);
        else warnings.append(msg);
        return;
    }
    args.append(resolved);
    if (!value.isEmpty()) args.append(value);
}
