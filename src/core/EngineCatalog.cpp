#include "EngineCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>

static QString currentPlatform()
{
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("other");
#endif
}

static QString currentArch()
{
    const QString cpu = QSysInfo::currentCpuArchitecture().toLower();
    if (cpu.contains(QStringLiteral("arm64")) || cpu.contains(QStringLiteral("aarch64")))
        return QStringLiteral("arm64");
    return QStringLiteral("x64");
}

static EngineVariant variant(const QString &id, const QString &label, const QString &backend,
                             QStringList platforms, QStringList vendors, bool prebuilt,
                             bool source, const QString &stability, const QString &speed)
{
    EngineVariant v;
    v.id = id;
    v.label = label;
    v.backend = backend;
    v.platforms = platforms;
    v.gpuVendors = vendors;
    v.hasPrebuilt = prebuilt;
    v.buildFromSource = source;
    v.stability = stability;
    v.speed = speed;
    return v;
}

QList<EngineCatalogEntry> EngineCatalog::entries()
{
    return {
        {QStringLiteral("llama.cpp"),
         QStringLiteral("llama.cpp"),
         QStringLiteral("llama-server"),
         QStringLiteral("Motor GGUF oficial. Instala el backend nativo disponible para el hardware."),
         QStringLiteral("ggml-org/llama.cpp"),
         QStringLiteral("https://github.com/ggml-org/llama.cpp"),
         QStringLiteral("official"),
         QStringLiteral("stable"),
         QStringLiteral("Recomendado como base: CUDA en NVIDIA/Windows, Vulkan o CPU como fallback."),
         {
             variant(QStringLiteral("llama.cpp-cuda"), QStringLiteral("CUDA (NVIDIA)"),
                     QStringLiteral("cuda"), {QStringLiteral("windows")}, {QStringLiteral("nvidia")},
                     true, false, QStringLiteral("stable"), QStringLiteral("fast")),
             variant(QStringLiteral("llama.cpp-vulkan"), QStringLiteral("Vulkan"),
                     QStringLiteral("vulkan"), {QStringLiteral("windows"), QStringLiteral("linux")},
                     {}, true, false, QStringLiteral("stable"), QStringLiteral("baseline")),
             variant(QStringLiteral("llama.cpp-cpu"), QStringLiteral("CPU"),
                     QStringLiteral("cpu"), {QStringLiteral("windows"), QStringLiteral("linux"), QStringLiteral("macos")},
                     {}, true, false, QStringLiteral("stable"), QStringLiteral("baseline")),
             variant(QStringLiteral("llama.cpp-metal"), QStringLiteral("Metal"),
                     QStringLiteral("metal"), {QStringLiteral("macos")}, {QStringLiteral("apple")},
                     true, false, QStringLiteral("stable"), QStringLiteral("fast")),
         }},
        {QStringLiteral("beellama"),
         QStringLiteral("beellama MTP"),
         QStringLiteral("llama-server"),
         QStringLiteral("Fork con soporte MTP/DFlash para perfiles Qwen/NextN en NVIDIA."),
         QStringLiteral("Anbeeld/beellama.cpp"),
         QStringLiteral("https://github.com/Anbeeld/beellama.cpp"),
         QStringLiteral("beellama"),
         QStringLiteral("experimental"),
         QStringLiteral("CUDA-only. Si no hay NVIDIA, LlamaCode cae al motor oficial."),
         {variant(QStringLiteral("beellama-cuda"), QStringLiteral("CUDA (NVIDIA)"),
                  QStringLiteral("cuda"), {QStringLiteral("windows")}, {QStringLiteral("nvidia")},
                  true, false, QStringLiteral("experimental"), QStringLiteral("fast"))}},
        {QStringLiteral("llama.cpp-adaptive"),
         QStringLiteral("llama.cpp adaptive MTP"),
         QStringLiteral("llama-server"),
         QStringLiteral("Fork con adaptive speculative decoding para ajustar MTP según el contenido."),
         QStringLiteral("LaurentZuijdwijk/llama.cpp"),
         QStringLiteral("https://github.com/LaurentZuijdwijk/llama.cpp"),
         QStringLiteral("mtp-fork"),
         QStringLiteral("experimental"),
         QStringLiteral("Build desde source; requiere toolchain CUDA/MSVC y se registra con detección de capacidades."),
         {variant(QStringLiteral("llama.cpp-adaptive-cuda-source"), QStringLiteral("Build CUDA (NVIDIA)"),
                  QStringLiteral("cuda"), {QStringLiteral("windows")}, {QStringLiteral("nvidia")},
                  false, true, QStringLiteral("experimental"), QStringLiteral("fast"))}},
        {QStringLiteral("llama.cpp-kv-streaming"),
         QStringLiteral("llama.cpp adaptive KV streaming"),
         QStringLiteral("llama-server"),
         QStringLiteral("Fork experimental que mantiene el KV autoritativo en RAM pinned y transmite paginas a una piscina CUDA acotada."),
         QStringLiteral("sachin-detrax/llama.cpp-adaptive-kv-streaming"),
         QStringLiteral("https://github.com/sachin-detrax/llama.cpp-adaptive-kv-streaming"),
         QStringLiteral("kv-streaming"),
         QStringLiteral("experimental"),
         QStringLiteral("Sólo dense Qwen3, una GPU y un slot; está orientado a contexto largo y es sensible al ancho de banda PCIe."),
         {variant(QStringLiteral("llama.cpp-kv-streaming-cuda-source"), QStringLiteral("Build CUDA (NVIDIA)"),
                  QStringLiteral("cuda"), {QStringLiteral("windows")}, {QStringLiteral("nvidia")},
                  false, true, QStringLiteral("experimental"), QStringLiteral("long-context"))},
         QStringLiteral("feature/adaptive-kv-stream"),
         {QStringLiteral("-DLLAMA_BUILD_TESTS=OFF"),
          QStringLiteral("-DLLAMA_BUILD_EXAMPLES=OFF"),
          QStringLiteral("-DLLAMA_BUILD_APP=OFF"),
          QStringLiteral("-DLLAMA_BUILD_SERVER=ON"),
          QStringLiteral("-DLLAMA_CURL=OFF"),
          QStringLiteral("-DGGML_CUDA_FA_ALL_QUANTS=ON")},
         QStringLiteral("llama-server")},
        {QStringLiteral("llama.cpp-deepseek-lid-cuda"),
         QStringLiteral("llama.cpp DeepSeek LID CUDA"),
         QStringLiteral("llama-server"),
         QStringLiteral("Fork experimental con Lightning Indexer CUDA para DeepSeek V4 Flash y contextos largos."),
         QStringLiteral("spencer-zaid/llama.cpp"),
         QStringLiteral("https://github.com/spencer-zaid/llama.cpp"),
         QStringLiteral("deepseek-lid-cuda"),
         QStringLiteral("experimental"),
         QStringLiteral("Sólo para DeepSeek V4 Flash. Requiere build CUDA desde source; esta rama usa KV f16 y no reemplaza al llama.cpp oficial."),
         {variant(QStringLiteral("llama.cpp-deepseek-lid-cuda-source"), QStringLiteral("Build CUDA (NVIDIA)"),
                  QStringLiteral("cuda"), {QStringLiteral("windows")}, {QStringLiteral("nvidia")},
                  false, true, QStringLiteral("experimental"), QStringLiteral("long-context"))},
         QStringLiteral("deepseek-lid-cuda"),
         {QStringLiteral("-DLLAMA_BUILD_TESTS=OFF"),
          QStringLiteral("-DLLAMA_BUILD_EXAMPLES=OFF"),
          QStringLiteral("-DLLAMA_BUILD_APP=OFF"),
          QStringLiteral("-DLLAMA_BUILD_SERVER=ON"),
          QStringLiteral("-DLLAMA_BUILD_UI=OFF"),
          // El fork siempre embebe assets para llama-server aunque no compile
          // la UI. Permitir el paquete prebuilt evita npm/Node; el recipe agrega
          // un fallback para el loading.html que falta en algunos paquetes.
          QStringLiteral("-DLLAMA_USE_PREBUILT_UI=ON")},
         QStringLiteral("llama-server")},
        {QStringLiteral("qwen38-next"),
         QStringLiteral("llama.cpp Qwen3.8-Flash-Next"),
         QStringLiteral("llama-server"),
         QStringLiteral("PR upstream con soporte para la arquitectura Qwen4 (MoE + Ngram/PLE)."),
         QStringLiteral("ggml-org/llama.cpp"),
         QStringLiteral("https://github.com/ggml-org/llama.cpp/pull/27742"),
         QStringLiteral("qwen38-next"),
         QStringLiteral("experimental"),
         QStringLiteral("Necesario para Qwen3.8-Flash-Next: mainline todavia no mergeo la arquitectura. Build CUDA desde el pull request."),
         {variant(QStringLiteral("qwen38-next-cuda-source"), QStringLiteral("Build CUDA (NVIDIA)"),
                  QStringLiteral("cuda"), {QStringLiteral("windows")}, {QStringLiteral("nvidia")},
                  false, true, QStringLiteral("experimental"), QStringLiteral("long-context"))},
         QStringLiteral("pull/27742/head"),
         {QStringLiteral("-DBUILD_SHARED_LIBS=OFF"),
          QStringLiteral("-DLLAMA_CURL=OFF")},
         QStringLiteral("llama-server")},
        {QStringLiteral("nanbeige42"),
         QStringLiteral("Nanbeige llama.cpp"),
         QStringLiteral("llama-server"),
         QStringLiteral("Fork oficial con soporte para la arquitectura Looped Transformer de Nanbeige4.2."),
         QStringLiteral("Nanbeige/llama.cpp"),
         QStringLiteral("https://github.com/Nanbeige/llama.cpp"),
         QStringLiteral("nanbeige42"),
         QStringLiteral("experimental"),
         QStringLiteral("Build CUDA desde la rama nanbeige42; no reemplaza al motor oficial."),
         {variant(QStringLiteral("nanbeige42-cuda-source"), QStringLiteral("Build CUDA (NVIDIA)"),
                  QStringLiteral("cuda"), {QStringLiteral("windows")},
                  {QStringLiteral("nvidia")}, false, true, QStringLiteral("experimental"), QStringLiteral("baseline"))},
         QStringLiteral("nanbeige42")},
        {QStringLiteral("ik_llama.cpp"),
         QStringLiteral("ik_llama.cpp"),
         QStringLiteral("llama-server"),
         QStringLiteral("Fork de llama.cpp con trabajo de performance y quants adicionales."),
         QStringLiteral("ikawrakow/ik_llama.cpp"),
         QStringLiteral("https://github.com/ikawrakow/ik_llama.cpp"),
         QStringLiteral("ik_llama"),
         QStringLiteral("experimental"),
         QStringLiteral("No publica prebuilts confiables: usar build-from-source guiado."),
         {variant(QStringLiteral("ik-source"), QStringLiteral("Build from source"),
                  QStringLiteral("cuda"), {QStringLiteral("windows"), QStringLiteral("linux")},
                  {}, false, true, QStringLiteral("experimental"), QStringLiteral("baseline"))}},
        {QStringLiteral("koboldcpp"),
         QStringLiteral("KoboldCpp"),
         QStringLiteral("koboldcpp"),
         QStringLiteral("Runtime GGUF single-binary con API OpenAI-compatible."),
         QStringLiteral("LostRuins/koboldcpp"),
         QStringLiteral("https://github.com/LostRuins/koboldcpp"),
         QStringLiteral("koboldcpp"),
         QStringLiteral("experimental"),
         QStringLiteral("Útil como motor alternativo; la integración profunda queda detrás del contrato llama-server."),
         {
             variant(QStringLiteral("koboldcpp-cuda"), QStringLiteral("CUDA (NVIDIA)"),
                     QStringLiteral("cuda"), {QStringLiteral("windows"), QStringLiteral("linux")},
                     {QStringLiteral("nvidia")}, true, false, QStringLiteral("experimental"), QStringLiteral("fast")),
             variant(QStringLiteral("koboldcpp-portable"), QStringLiteral("Vulkan / CPU"),
                     QStringLiteral("vulkan"), {QStringLiteral("windows"), QStringLiteral("linux")},
                     {}, true, false, QStringLiteral("experimental"), QStringLiteral("baseline")),
         }},
        {QStringLiteral("llamafile"),
         QStringLiteral("llamafile"),
         QStringLiteral("llamafile"),
         QStringLiteral("Llama.cpp empaquetado como ejecutable portable."),
         QStringLiteral("Mozilla-Ocho/llamafile"),
         QStringLiteral("https://github.com/Mozilla-Ocho/llamafile"),
         QStringLiteral("llamafile"),
         QStringLiteral("experimental"),
         QStringLiteral("Bueno para portabilidad; menor prioridad que llama-server nativo."),
         {variant(QStringLiteral("llamafile-portable"), QStringLiteral("Portable"),
                  QStringLiteral("cpu"), {QStringLiteral("windows"), QStringLiteral("linux"), QStringLiteral("macos")},
                  {}, true, false, QStringLiteral("experimental"), QStringLiteral("baseline"))}},
        {QStringLiteral("turboquant"),
         QStringLiteral("TurboQuant"),
         QStringLiteral("llama-server"),
         QStringLiteral("Fork con KV cache comprimida y variantes NextN."),
         QStringLiteral("AtomicBot-ai/atomic-llama-cpp-turboquant"),
         QStringLiteral("https://github.com/AtomicBot-ai/atomic-llama-cpp-turboquant"),
         QStringLiteral("turboquant"),
         QStringLiteral("experimental"),
         QStringLiteral("En Windows se trata como source-build hasta que haya prebuilt self-contained."),
         {
             variant(QStringLiteral("turboquant-linux-vulkan"), QStringLiteral("Vulkan (Linux x64)"),
                     QStringLiteral("vulkan"), {QStringLiteral("linux")}, {}, true, false,
                     QStringLiteral("experimental"), QStringLiteral("fast")),
             variant(QStringLiteral("turboquant-source"), QStringLiteral("Build from source"),
                     QStringLiteral("cuda"), {QStringLiteral("windows"), QStringLiteral("linux")}, {},
                     false, true, QStringLiteral("experimental"), QStringLiteral("fast")),
         }},
    };
}

EngineCatalogEntry EngineCatalog::entry(const QString &id)
{
    for (const EngineCatalogEntry &e : entries())
        if (e.id == id)
            return e;
    return {};
}

HardwareSignals EngineCatalog::detectHardware()
{
    HardwareSignals hw;
    hw.platform = currentPlatform();
    hw.arch = currentArch();
    hw.gpuVendor = QStringLiteral("unknown");

    QProcess nvidia;
    nvidia.start(QStringLiteral("nvidia-smi"), {QStringLiteral("-L")});
    if (nvidia.waitForFinished(1200) && nvidia.exitCode() == 0
        && !QString::fromUtf8(nvidia.readAllStandardOutput()).trimmed().isEmpty()) {
        hw.gpuVendor = QStringLiteral("nvidia");
        hw.hasGpu = true;
        return hw;
    }

#ifdef Q_OS_MACOS
    hw.gpuVendor = QStringLiteral("apple");
    hw.hasGpu = true;
#endif
    return hw;
}

bool EngineCatalog::isVariantCompatible(const EngineVariant &variant,
                                        const HardwareSignals &hw,
                                        QString *reason)
{
    if (!variant.platforms.isEmpty() && !variant.platforms.contains(hw.platform)) {
        if (reason)
            *reason = QStringLiteral("Requiere %1").arg(variant.platforms.join(QStringLiteral(" / ")));
        return false;
    }
    if (!variant.gpuVendors.isEmpty() && !variant.gpuVendors.contains(hw.gpuVendor)) {
        if (reason)
            *reason = QStringLiteral("Requiere GPU %1").arg(variant.gpuVendors.join(QStringLiteral(" / ")));
        return false;
    }
    if (reason)
        reason->clear();
    return true;
}

QString EngineCatalog::recommendedEngineId(const HardwareSignals &hw)
{
    if (hw.gpuVendor == QLatin1String("nvidia") && hw.platform == QLatin1String("windows"))
        return QStringLiteral("llama.cpp");
    return QStringLiteral("llama.cpp");
}

QVariantMap EngineCatalog::toVariantMap(const EngineCatalogEntry &entry, const HardwareSignals &hw)
{
    QVariantList vars;
    bool anyCompatible = false;
    QString firstReason;
    for (const EngineVariant &v : entry.variants) {
        QString reason;
        const bool compatible = isVariantCompatible(v, hw, &reason);
        anyCompatible = anyCompatible || compatible;
        if (!compatible && firstReason.isEmpty())
            firstReason = reason;
        vars.append(QVariantMap{
            {QStringLiteral("id"), v.id},
            {QStringLiteral("label"), v.label},
            {QStringLiteral("backend"), v.backend},
            {QStringLiteral("hasPrebuilt"), v.hasPrebuilt},
            {QStringLiteral("buildFromSource"), v.buildFromSource},
            {QStringLiteral("compatible"), compatible},
            {QStringLiteral("incompatibleReason"), reason},
            {QStringLiteral("stability"), v.stability},
            {QStringLiteral("speed"), v.speed},
        });
    }

    return QVariantMap{
        {QStringLiteral("id"), entry.id},
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("kind"), entry.kind},
        {QStringLiteral("description"), entry.description},
        {QStringLiteral("repo"), entry.repo},
        {QStringLiteral("homepage"), entry.homepage},
        {QStringLiteral("flavor"), entry.flavor},
        {QStringLiteral("support"), entry.support},
        {QStringLiteral("note"), entry.note},
        {QStringLiteral("sourceBranch"), entry.sourceBranch},
        {QStringLiteral("sourceCMakeArgs"), entry.sourceCMakeArgs},
        {QStringLiteral("sourceBuildTarget"), entry.sourceBuildTarget},
        {QStringLiteral("variants"), vars},
        {QStringLiteral("compatible"), anyCompatible},
        {QStringLiteral("incompatibleReason"), anyCompatible ? QString() : firstReason},
        {QStringLiteral("recommended"), entry.id == recommendedEngineId(hw)},
    };
}

QVariantList EngineCatalog::toVariantList(const HardwareSignals &hw)
{
    QVariantList out;
    for (const EngineCatalogEntry &e : entries())
        out.append(toVariantMap(e, hw));
    return out;
}

int EngineCatalog::parseBuildTag(const QString &tag)
{
    const QRegularExpression re(QStringLiteral("^v?b(\\d+)$"),
                                QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(tag.trimmed());
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

QList<int> EngineCatalog::parseDottedVersion(const QString &version)
{
    const QRegularExpression re(QStringLiteral("^v?(\\d+(?:\\.\\d+)*)"));
    const auto m = re.match(version.trimmed());
    if (!m.hasMatch())
        return {};
    QList<int> out;
    for (const QString &part : m.captured(1).split(QLatin1Char('.')))
        out.append(part.toInt());
    return out;
}

int EngineCatalog::compareVersions(const QString &installed, const QString &latest)
{
    const int ib = parseBuildTag(installed);
    const int lb = parseBuildTag(latest);
    if (ib >= 0 && lb >= 0)
        return ib == lb ? 0 : (ib < lb ? -1 : 1);

    const QList<int> iv = parseDottedVersion(installed);
    const QList<int> lv = parseDottedVersion(latest);
    if (iv.isEmpty() || lv.isEmpty())
        return 0;
    const int n = qMax(iv.size(), lv.size());
    for (int i = 0; i < n; ++i) {
        const int a = i < iv.size() ? iv.at(i) : 0;
        const int b = i < lv.size() ? lv.at(i) : 0;
        if (a != b)
            return a < b ? -1 : 1;
    }
    return 0;
}

EngineUpdateStatus EngineCatalog::computeUpdateStatus(const QString &installed,
                                                      const QString &latest,
                                                      bool sourceBuild)
{
    EngineUpdateStatus st;
    st.installed = installed.trimmed();
    st.latest = latest.trimmed();
    st.rebuild = sourceBuild;
    if (st.latest.isEmpty()) {
        st.error = QStringLiteral("offline");
        return st;
    }
    if (sourceBuild) {
        st.comparable = !st.installed.isEmpty();
        st.hasUpdate = st.comparable && st.installed.left(7).compare(st.latest.left(7), Qt::CaseInsensitive) != 0;
        if (!st.comparable)
            st.error = QStringLiteral("no_source");
        return st;
    }
    st.comparable = (parseBuildTag(st.installed) >= 0 && parseBuildTag(st.latest) >= 0)
        || (!parseDottedVersion(st.installed).isEmpty() && !parseDottedVersion(st.latest).isEmpty());
    st.hasUpdate = st.comparable && compareVersions(st.installed, st.latest) < 0;
    return st;
}

QString EngineCatalog::normalizeRepo(const QString &repoOrUrl)
{
    QString s = repoOrUrl.trimmed();
    s.remove(QRegularExpression(QStringLiteral("^https?://"), QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression(QStringLiteral("^www\\."), QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression(QStringLiteral("^github\\.com/"), QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression(QStringLiteral("\\.git$"), QRegularExpression::CaseInsensitiveOption));
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    return s;
}

QString EngineCatalog::buildDirName(const QString &repoOrUrl, const QString &branch)
{
    QString repo = normalizeRepo(repoOrUrl).section(QLatin1Char('/'), -1);
    if (repo.isEmpty())
        repo = QStringLiteral("engine");
    QString raw = branch.trimmed().isEmpty() ? repo : repo + QLatin1Char('-') + branch.trimmed();
    raw.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
    raw.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return raw.isEmpty() ? QStringLiteral("engine") : raw;
}

int EngineCatalog::parsePullRequestRef(const QString &branch)
{
    static const QRegularExpression re(QStringLiteral("^pull/([0-9]+)/head$"));
    const QRegularExpressionMatch m = re.match(branch.trimmed());
    if (!m.hasMatch())
        return 0;
    bool ok = false;
    const int n = m.captured(1).toInt(&ok);
    return (ok && n > 0) ? n : 0;
}

QString EngineCatalog::localBranchForRef(const QString &branch)
{
    const int pr = parsePullRequestRef(branch);
    if (pr > 0)
        return QStringLiteral("pr-%1").arg(pr);
    return branch.trimmed();
}

QString EngineCatalog::sourceBuildDirName(const EngineCatalogEntry &entry)
{
    QString slug = buildDirName(entry.repo);
    const QString repoLeaf = normalizeRepo(entry.repo).section(QLatin1Char('/'), -1);
    if (entry.id != repoLeaf) {
        QString suffix = entry.id;
        suffix.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
        if (!suffix.isEmpty())
            slug += QLatin1Char('-') + suffix;
    }
    return slug;
}
