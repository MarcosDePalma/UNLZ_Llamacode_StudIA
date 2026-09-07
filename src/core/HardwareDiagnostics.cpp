#include "HardwareDiagnostics.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <utility>

namespace {

QString normalized(QString value)
{
    return value.trimmed().remove(QRegularExpression(QStringLiteral("\\s+"))).toLower();
}

double number(const QString &value)
{
    bool ok = false;
    const double result = value.trimmed().toDouble(&ok);
    return ok ? result : 0.0;
}

QStringList splitCsvLine(const QString &line)
{
    // nvidia-smi no escapa comas en los campos consultados actualmente. Se
    // conserva una función pequeña y explícita para no introducir un parser
    // CSV general en el camino crítico del diagnóstico.
    return line.split(QLatin1Char(','), Qt::KeepEmptyParts);
}

struct GpuMemoryEntry
{
    int index = -1;
    QString name;
    double totalMb = 0.0;
    double freeMb = 0.0;
    double modelMb = 0.0;
};

double memoryValue(const QVariantMap &gpu, const QString &key)
{
    return qMax(0.0, gpu.value(key).toDouble());
}

QString compactNumber(double value)
{
    QString out = QString::number(value, 'f', 3);
    while (out.endsWith(QLatin1Char('0'))) out.chop(1);
    if (out.endsWith(QLatin1Char('.'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("0") : out;
}

QStringList stringListValue(const QVariantMap &map, const QString &key)
{
    const QVariant value = map.value(key);
    if (value.canConvert<QStringList>())
        return value.toStringList();
    if (value.canConvert<QVariantList>()) {
        QStringList out;
        for (const QVariant &item : value.toList()) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) out.append(text);
        }
        return out;
    }
    const QString scalar = value.toString().trimmed();
    return scalar.isEmpty() ? QStringList{} : QStringList{scalar};
}

}  // namespace

QVariantList HardwareDiagnostics::parseNvidiaSmiCsv(const QString &csv)
{
    QVariantList out;
    const QStringList lines = csv.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                        Qt::SkipEmptyParts);
    // index,name,memory.total,memory.free,pci.bus_id,pcie.link.gen.current,
    // pcie.link.width.current,temperature.gpu,power.draw,power.limit
    for (const QString &raw : lines) {
        const QStringList fields = splitCsvLine(raw);
        if (fields.size() < 7)
            continue;
        bool indexOk = false;
        const int index = fields.at(0).trimmed().toInt(&indexOk);
        if (!indexOk)
            continue;
        QVariantMap gpu;
        gpu[QStringLiteral("index")] = index;
        gpu[QStringLiteral("name")] = fields.at(1).trimmed();
        gpu[QStringLiteral("totalMb")] = number(fields.at(2));
        gpu[QStringLiteral("freeMb")] = number(fields.at(3));
        gpu[QStringLiteral("busId")] = fields.at(4).trimmed();
        gpu[QStringLiteral("pcieGeneration")] = number(fields.at(5));
        gpu[QStringLiteral("pcieLanes")] = number(fields.at(6));
        if (fields.size() > 7) gpu[QStringLiteral("temperatureC")] = number(fields.at(7));
        if (fields.size() > 8) gpu[QStringLiteral("powerDrawW")] = number(fields.at(8));
        if (fields.size() > 9) gpu[QStringLiteral("powerLimitW")] = number(fields.at(9));
        out.append(gpu);
    }
    return out;
}

QVariantMap HardwareDiagnostics::parseTopologyMatrix(const QString &text)
{
    QVariantMap result;
    QVariantList links;
    bool p2p = false;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    QStringList columns;
    QSet<QString> seenLinks;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("GPU")) && columns.isEmpty()) {
            columns = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            while (!columns.isEmpty() && !columns.last().startsWith(QStringLiteral("GPU")))
                columns.removeLast();
            continue;
        }
        if (!line.startsWith(QStringLiteral("GPU")) || columns.size() < 2)
            continue;
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        const QString source = fields.first();
        for (int i = 1; i < fields.size(); ++i) {
            const int columnIndex = i - 1;
            if (columnIndex < 0 || columnIndex >= columns.size()
                || !columns.at(columnIndex).startsWith(QStringLiteral("GPU")))
                continue;
            const QString relation = fields.at(i).toUpper();
            if (relation == QLatin1String("X") || relation == QLatin1String("N/A"))
                continue;
            const bool direct = relation.startsWith(QStringLiteral("NV"))
                                || relation == QLatin1String("PIX")
                                || relation == QLatin1String("PXB");
            if (!direct || source == columns.at(columnIndex))
                continue;
            p2p = p2p || direct;
            QStringList canonical{source, columns.at(columnIndex)};
            std::sort(canonical.begin(), canonical.end());
            const QString linkKey = canonical.join(QLatin1Char('|'));
            if (seenLinks.contains(linkKey))
                continue;
            seenLinks.insert(linkKey);
            const QStringList endpoints{source, columns.at(columnIndex)};
            links.append(QVariantMap{{QStringLiteral("from"), source},
                                     {QStringLiteral("to"), columns.at(columnIndex)},
                                     {QStringLiteral("relation"), relation},
                                     {QStringLiteral("direct"), direct}});
        }
    }
    result[QStringLiteral("links")] = links;
    result[QStringLiteral("p2pAvailable")] = p2p;
    return result;
}

bool HardwareDiagnostics::parseNvlinkActive(const QString &text)
{
    const QString lower = text.toLower();
    return lower.contains(QStringLiteral("active"))
           && !lower.contains(QStringLiteral("inactive"))
           && !lower.contains(QStringLiteral("disabled"));
}

QVariantMap HardwareDiagnostics::enrichTopology(const QVariantMap &hardware,
                                                const QString &topologyText,
                                                const QString &nvlinkText)
{
    QVariantMap result = hardware;
    const QVariantMap topology = parseTopologyMatrix(topologyText);
    const bool topologyP2p = topology.value(QStringLiteral("p2pAvailable")).toBool();
    const bool nvlink = parseNvlinkActive(nvlinkText);
    result[QStringLiteral("topology")] = topology.value(QStringLiteral("links"));
    result[QStringLiteral("p2pAvailable")] = topologyP2p || nvlink;
    result[QStringLiteral("nvlinkAvailable")] = nvlink;
    result[QStringLiteral("topologySource")] = !topologyText.trimmed().isEmpty()
        ? QStringLiteral("nvidia-smi topo -m") : QStringLiteral("unavailable");
    return result;
}

QVariantMap HardwareDiagnostics::voiceGpuPlan(const QVariantMap &hardware,
                                              double voiceReserveMb,
                                              double modelRequiredMb)
{
    const QVariantList rawGpus = hardware.value(QStringLiteral("gpus")).toList();
    QVariantMap result{
        {QStringLiteral("enabled"), false},
        {QStringLiteral("mode"), QStringLiteral("single-gpu")},
        {QStringLiteral("gpuCount"), rawGpus.size()},
        {QStringLiteral("auxiliaryGpuIndex"), -1},
        {QStringLiteral("voiceGpuIndex"), -1},
        {QStringLiteral("voiceGpuMask"), QString()},
        {QStringLiteral("modelGpuMask"), QString()},
        {QStringLiteral("modelGpuIndices"), QVariantList{}},
        {QStringLiteral("modelTensorSplit"), QString()},
        {QStringLiteral("voiceReserveMb"), qMax(0.0, voiceReserveMb)},
        {QStringLiteral("voiceReserveAvailable"), false},
        {QStringLiteral("modelAvailableMb"), 0.0},
        {QStringLiteral("modelAvailableGb"), 0.0},
        {QStringLiteral("modelRequiredMb"), qMax(0.0, modelRequiredMb)},
        {QStringLiteral("modelRequiredGb"), qMax(0.0, modelRequiredMb) / 1024.0},
        {QStringLiteral("modelFitKnown"), modelRequiredMb > 0.0},
        {QStringLiteral("modelFitsCapacity"), true},
        {QStringLiteral("modelFitMarginMb"), 0.0},
        {QStringLiteral("voicePlacementSafe"), false},
        {QStringLiteral("modelPlacementSafe"), false},
        {QStringLiteral("reason"), QStringLiteral(
            "Se mantiene el modo de una sola GPU hasta detectar al menos dos GPU." )}
    };
    if (rawGpus.size() < 2)
        return result;

    QVector<GpuMemoryEntry> gpus;
    gpus.reserve(rawGpus.size());
    for (int position = 0; position < rawGpus.size(); ++position) {
        const QVariantMap raw = rawGpus.at(position).toMap();
        GpuMemoryEntry gpu;
        gpu.index = raw.value(QStringLiteral("index"), position).toInt();
        if (gpu.index < 0) gpu.index = position;
        gpu.name = raw.value(QStringLiteral("name")).toString();
        gpu.totalMb = memoryValue(raw, QStringLiteral("totalMb"));
        // Si el driver no publica memory.free, totalMb es el único límite seguro
        // disponible. Un freeMb explícito igual a cero se respeta: la placa está
        // ocupada y no se debe prometer VRAM que no existe.
        gpu.freeMb = raw.contains(QStringLiteral("freeMb"))
            ? memoryValue(raw, QStringLiteral("freeMb")) : gpu.totalMb;
        gpus.append(gpu);
    }
    std::sort(gpus.begin(), gpus.end(), [](const GpuMemoryEntry &a, const GpuMemoryEntry &b) {
        return a.index < b.index;
    });

    // "Más débil" significa primero menos VRAM total; en un empate se usa la
    // VRAM libre observada y finalmente el índice estable de CUDA. No se decide
    // por el nombre, porque eso no es portable entre fabricantes/idiomas.
    int weakPosition = 0;
    for (int i = 1; i < gpus.size(); ++i) {
        const GpuMemoryEntry &candidate = gpus.at(i);
        const GpuMemoryEntry &current = gpus.at(weakPosition);
        const double candidateCapacity = candidate.totalMb > 0.0
            ? candidate.totalMb : candidate.freeMb;
        const double currentCapacity = current.totalMb > 0.0
            ? current.totalMb : current.freeMb;
        if (candidateCapacity < currentCapacity
            || (qFuzzyCompare(candidateCapacity + 1.0, currentCapacity + 1.0)
                && (candidate.freeMb < current.freeMb
                    || (qFuzzyCompare(candidate.freeMb + 1.0, current.freeMb + 1.0)
                        && candidate.index < current.index)))) {
            weakPosition = i;
        }
    }

    const int weakIndex = gpus.at(weakPosition).index;
    const double reserve = qMax(0.0, voiceReserveMb);
    QVariantList modelByGpu;
    QVariantList modelIndices;
    QStringList modelMask;
    QStringList splitByCudaIndex;
    double totalModelMb = 0.0;
    double smallestPositiveModelMb = 0.0;
    int maxIndex = -1;
    for (const GpuMemoryEntry &gpu : std::as_const(gpus))
        maxIndex = qMax(maxIndex, gpu.index);

    for (GpuMemoryEntry &gpu : gpus) {
        gpu.modelMb = qMax(0.0, gpu.freeMb - (gpu.index == weakIndex ? reserve : 0.0));
        totalModelMb += gpu.modelMb;
        if (gpu.modelMb > 0.0 && (smallestPositiveModelMb <= 0.0
                                  || gpu.modelMb < smallestPositiveModelMb))
            smallestPositiveModelMb = gpu.modelMb;
        if (gpu.modelMb > 0.0) {
            modelIndices.append(gpu.index);
            modelMask.append(QString::number(gpu.index));
        }
        modelByGpu.append(QVariantMap{
            {QStringLiteral("index"), gpu.index},
            {QStringLiteral("name"), gpu.name},
            {QStringLiteral("totalMb"), gpu.totalMb},
            {QStringLiteral("freeMb"), gpu.freeMb},
            {QStringLiteral("modelFreeMb"), gpu.modelMb},
            {QStringLiteral("role"), gpu.index == weakIndex
                ? QStringLiteral("voice-and-model") : QStringLiteral("model")},
        });
    }

    // llama.cpp indexes tensor-split by CUDA device, so preserve holes if a
    // driver exposes non-contiguous physical indexes. The values are ratios;
    // normalizing against the smallest positive capacity makes the policy
    // readable in logs while retaining the measured free-VRAM proportion.
    for (int cudaIndex = 0; cudaIndex <= maxIndex; ++cudaIndex) {
        double modelMb = 0.0;
        bool found = false;
        for (const GpuMemoryEntry &gpu : std::as_const(gpus)) {
            if (gpu.index == cudaIndex) {
                modelMb = gpu.modelMb;
                found = true;
                break;
            }
        }
        if (!found || modelMb <= 0.0 || smallestPositiveModelMb <= 0.0)
            splitByCudaIndex.append(QStringLiteral("0"));
        else
            splitByCudaIndex.append(compactNumber(modelMb / smallestPositiveModelMb));
    }
    if (splitByCudaIndex.isEmpty())
        splitByCudaIndex.append(QStringLiteral("1"));

    const GpuMemoryEntry &weak = gpus.at(weakPosition);
    const QVariantList topology = hardware.value(QStringLiteral("topology")).toList();
    const QString splitMode = recommendedSplitMode(hardware);
    result[QStringLiteral("enabled")] = true;
    result[QStringLiteral("mode")] = QStringLiteral("voice-on-weak-gpu");
    result[QStringLiteral("auxiliaryGpuIndex")] = weakIndex;
    result[QStringLiteral("voiceGpuIndex")] = weakIndex;
    result[QStringLiteral("voiceGpuMask")] = QString::number(weakIndex);
    result[QStringLiteral("modelGpuMask")] = modelMask.join(QLatin1Char(','));
    result[QStringLiteral("modelGpuIndices")] = modelIndices;
    result[QStringLiteral("modelByGpu")] = modelByGpu;
    result[QStringLiteral("weakGpuName")] = weak.name;
    result[QStringLiteral("weakGpuTotalMb")] = weak.totalMb;
    result[QStringLiteral("weakGpuFreeMb")] = weak.freeMb;
    result[QStringLiteral("weakGpuModelFreeMb")] = weak.modelMb;
    const bool voiceReserveAvailable = weak.freeMb + 0.001 >= reserve;
    result[QStringLiteral("voiceReserveAvailable")] = voiceReserveAvailable;
    result[QStringLiteral("modelTensorSplit")] = splitByCudaIndex.join(QLatin1Char(','));
    result[QStringLiteral("modelSplitMode")] = splitMode;
    result[QStringLiteral("modelAvailableMb")] = totalModelMb;
    result[QStringLiteral("modelAvailableGb")] = totalModelMb / 1024.0;
    const bool modelFitKnown = modelRequiredMb > 0.0;
    const bool modelFitsCapacity = !modelFitKnown || modelRequiredMb <= totalModelMb + 0.001;
    result[QStringLiteral("modelFitKnown")] = modelFitKnown;
    result[QStringLiteral("modelFitsCapacity")] = modelFitsCapacity;
    result[QStringLiteral("modelFitMarginMb")] = modelFitKnown
        ? totalModelMb - modelRequiredMb : 0.0;
    result[QStringLiteral("voicePlacementSafe")] = voiceReserveAvailable;
    result[QStringLiteral("modelPlacementSafe")] = voiceReserveAvailable
        && smallestPositiveModelMb > 0.0 && modelFitsCapacity;
    result[QStringLiteral("p2pAvailable")] = hardware.value(QStringLiteral("p2pAvailable")).toBool();
    result[QStringLiteral("topologyKnown")] = !topology.isEmpty();
    result[QStringLiteral("reason")] = QStringLiteral(
        "GPU %1 (%2) queda reservada para STT, TTS y auxiliares: %3 MB de reserva; "
        "el LLM puede repartir %4 GB restantes entre %5 usando split-mode %6.")
        .arg(weakIndex)
        .arg(weak.name.isEmpty() ? QStringLiteral("GPU") : weak.name)
        .arg(QString::number(reserve, 'f', 0))
        .arg(QString::number(totalModelMb / 1024.0, 'f', 1))
        .arg(modelMask.join(QLatin1Char(',')))
        .arg(splitMode);
    if (!voiceReserveAvailable) {
        result[QStringLiteral("reason")] = QStringLiteral(
            "Se detectaron dos GPU, pero la GPU reservada para voz sólo tiene %1 MB "
            "libres y necesita %2 MB; no se activa el reparto automático seguro.")
            .arg(QString::number(weak.freeMb, 'f', 0))
            .arg(QString::number(reserve, 'f', 0));
    } else if (smallestPositiveModelMb <= 0.0) {
        result[QStringLiteral("reason")] = QStringLiteral(
            "Se detectaron dos GPU, pero la GPU de voz no tiene VRAM libre suficiente "
            "después de la reserva; se evita prometer un reparto seguro para el LLM.");
    } else if (!modelFitsCapacity) {
        result[QStringLiteral("reason")] = QStringLiteral(
            "El perfil actual estima %1 GB y el reparto seguro deja %2 GB; "
            "se conserva el perfil normal y no se fuerza un split que pueda provocar OOM.")
            .arg(QString::number(modelRequiredMb / 1024.0, 'f', 1))
            .arg(QString::number(totalModelMb / 1024.0, 'f', 1));
    }
    return result;
}

QString HardwareDiagnostics::hardwareFingerprint(const QVariantMap &hardware)
{
    QStringList parts;
    parts << hardware.value(QStringLiteral("cpuModel")).toString();
    QVariantList gpus = hardware.value(QStringLiteral("gpus")).toList();
    if (gpus.isEmpty() && hardware.value(QStringLiteral("gpuName")).toString().size())
        parts << hardware.value(QStringLiteral("gpuName")).toString();
    for (const QVariant &value : gpus) {
        const QVariantMap gpu = value.toMap();
        parts << QStringLiteral("%1/%2/%3/%4/%5")
                     .arg(gpu.value(QStringLiteral("name")).toString())
                     .arg(gpu.value(QStringLiteral("totalMb")).toInt())
                     .arg(gpu.value(QStringLiteral("pcieGeneration")).toDouble())
                     .arg(gpu.value(QStringLiteral("pcieLanes")).toDouble())
                     .arg(gpu.value(QStringLiteral("busId")).toString());
    }
    const QByteArray digest = QCryptographicHash::hash(
        normalized(parts.join(QLatin1Char('|'))).toUtf8(), QCryptographicHash::Sha256);
    return QStringLiteral("hw-%1").arg(QString::fromLatin1(digest.toHex().left(16)));
}

QString HardwareDiagnostics::recommendedSplitMode(const QVariantMap &hardware)
{
    const QVariantList gpus = hardware.value(QStringLiteral("gpus")).toList();
    if (gpus.size() < 2)
        return QStringLiteral("layer");
    double minBandwidth = 1e9;
    for (const QVariant &value : gpus) {
        const QVariantMap gpu = value.toMap();
        const double gen = gpu.value(QStringLiteral("pcieGeneration")).toDouble();
        const double lanes = gpu.value(QStringLiteral("pcieLanes")).toDouble();
        if (gen > 0.0 && lanes > 0.0)
            minBandwidth = qMin(minBandwidth, gen * lanes);
    }
    // Tensor/row/graph requieren transferencias frecuentes. Layer es el
    // fallback conservador cuando el enlace mínimo es desconocido o débil.
    if (!hardware.value(QStringLiteral("p2pAvailable")).toBool() || minBandwidth < 16.0)
        return QStringLiteral("layer");
    return QStringLiteral("tensor");
}

QVariantMap HardwareDiagnostics::profileHardwareAffinity(const QVariantMap &hardware,
                                                         const QVariantMap &profile)
{
    const QVariantList rawGpus = hardware.value(QStringLiteral("gpus")).toList();
    const int gpuCount = rawGpus.isEmpty()
        ? hardware.value(QStringLiteral("gpuCount")).toInt()
        : rawGpus.size();
    double maxGpuGb = qMax(0.0, hardware.value(QStringLiteral("vramGb")).toDouble());
    const double reportedTotalGpuGb =
        qMax(0.0, hardware.value(QStringLiteral("vramTotalGb")).toDouble());
    double totalGpuGb = 0.0;
    QStringList gpuNames;
    for (const QVariant &value : rawGpus) {
        const QVariantMap gpu = value.toMap();
        const double totalMb = qMax(0.0, gpu.value(QStringLiteral("totalMb")).toDouble());
        maxGpuGb = qMax(maxGpuGb, totalMb / 1024.0);
        totalGpuGb += totalMb / 1024.0;
        const QString name = gpu.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) gpuNames.append(name);
    }
    if (reportedTotalGpuGb > totalGpuGb + 0.01)
        totalGpuGb = reportedTotalGpuGb;
    if (totalGpuGb <= 0.0)
        totalGpuGb = reportedTotalGpuGb > 0.0 ? reportedTotalGpuGb : maxGpuGb;
    if (gpuNames.isEmpty()) {
        const QString name = hardware.value(QStringLiteral("gpuName")).toString().trimmed();
        if (!name.isEmpty()) gpuNames.append(name);
    }

    const QVariantMap affinity = profile.value(QStringLiteral("hardwareAffinity")).toMap();
    const QStringList patterns = stringListValue(affinity, QStringLiteral("gpuNamePatterns"));
    const bool requireGpuName = affinity.value(QStringLiteral("requireGpuName")).toBool();
    const int minGpuCount = qMax(0, affinity.value(QStringLiteral("minGpuCount")).toInt());
    const int preferredGpuCount = qMax(0, affinity.value(QStringLiteral("preferredGpuCount")).toInt());
    const double minVramGb = qMax(0.0, profile.value(QStringLiteral("minVramGb")).toDouble());

    bool nameMatch = patterns.isEmpty();
    if (!patterns.isEmpty()) {
        nameMatch = false;
        for (const QString &name : gpuNames) {
            const QString foldedName = name.toCaseFolded();
            for (const QString &pattern : patterns) {
                if (!pattern.trimmed().isEmpty()
                    && foldedName.contains(pattern.trimmed().toCaseFolded())) {
                    nameMatch = true;
                    break;
                }
            }
            if (nameMatch) break;
        }
    }

    const bool aggregateFit = gpuCount >= 2 && minVramGb > maxGpuGb + 0.01
                               && totalGpuGb + 0.01 >= minVramGb;
    const bool explicitMismatch = requireGpuName && !nameMatch;
    const bool countMismatch = minGpuCount > 0 && gpuCount < minGpuCount;
    int score = 0;
    QStringList reasons;
    QString kind = QStringLiteral("generic");
    QString label;

    if (aggregateFit && !explicitMismatch && !countMismatch) {
        score += 60;
        kind = QStringLiteral("dual-gpu");
        label = QStringLiteral("Aprovecha %1 GPU · %2 GB combinados")
                    .arg(gpuCount).arg(QString::number(totalGpuGb, 'f', 0));
        reasons.append(QStringLiteral("requiere repartir el modelo entre varias GPU"));
    }
    if (!patterns.isEmpty() && nameMatch && !countMismatch) {
        score += 35;
        kind = aggregateFit ? QStringLiteral("dual-gpu-exact") : QStringLiteral("exact-gpu");
        reasons.append(QStringLiteral("coincide con %1").arg(patterns.join(QStringLiteral(" / "))));
    }
    if (preferredGpuCount > 0 && preferredGpuCount == gpuCount && !countMismatch) {
        score += 5;
        reasons.append(QStringLiteral("cantidad de GPU preferida: %1").arg(preferredGpuCount));
    }

    score = qBound(0, score, 100);
    const bool matched = score > 0 && !explicitMismatch && !countMismatch;
    if (!matched) {
        if (explicitMismatch) {
            kind = QStringLiteral("other-gpu");
            reasons = {QStringLiteral("el perfil pide %1, pero no coincide con la GPU detectada")
                           .arg(patterns.join(QStringLiteral(" / "))) };
        } else if (countMismatch) {
            kind = QStringLiteral("insufficient-gpus");
            reasons = {QStringLiteral("requiere al menos %1 GPU").arg(minGpuCount)};
        } else {
            reasons.clear();
        }
    }

    if (matched && !affinity.value(QStringLiteral("label")).toString().trimmed().isEmpty())
        label = affinity.value(QStringLiteral("label")).toString().trimmed();

    return QVariantMap{
        {QStringLiteral("score"), score},
        {QStringLiteral("matched"), matched},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("label"), label},
        {QStringLiteral("reason"), reasons.join(QStringLiteral("; "))},
        {QStringLiteral("gpuCount"), gpuCount},
        {QStringLiteral("maxGpuGb"), maxGpuGb},
        {QStringLiteral("totalGpuGb"), totalGpuGb},
        {QStringLiteral("minVramGb"), minVramGb}
    };
}

QVariantMap HardwareDiagnostics::performanceRecommendation(const QVariantMap &hardware,
                                                            const QString &target)
{
    const QString mode = recommendedSplitMode(hardware);
    const QVariantList gpus = hardware.value(QStringLiteral("gpus")).toList();
    const QString requested = target.trimmed().toLower();
    QVariantMap result;
    result[QStringLiteral("splitMode")] = mode;
    result[QStringLiteral("target")] = requested.isEmpty() ? QStringLiteral("balanced") : requested;
    result[QStringLiteral("confidence")] = gpus.size() >= 2 ? QStringLiteral("measured-topology")
                                                               : QStringLiteral("conservative-default");
    if (mode == QLatin1String("layer")) {
        result[QStringLiteral("reason")] = QStringLiteral(
            "Se recomienda layer porque el enlace PCIe mínimo entre las GPU es desconocido o débil; "
            "reduce las transferencias durante la generación.");
        result[QStringLiteral("alternatives")] = QVariantList{QStringLiteral("tensor")};
    } else {
        result[QStringLiteral("reason")] = QStringLiteral(
            "Las GPU tienen un enlace PCIe suficiente para probar tensor; validar pp/s y tg/s con benchmark.");
        result[QStringLiteral("alternatives")] = QVariantList{QStringLiteral("layer"), QStringLiteral("row")};
    }
    if (requested == QLatin1String("long-context")) {
        result[QStringLiteral("kvCache")] = QStringLiteral("q8_0");
        result[QStringLiteral("note")] = QStringLiteral("KV q8_0 prioriza capacidad de contexto con pérdida acotada.");
    } else if (requested == QLatin1String("quality")) {
        result[QStringLiteral("kvCache")] = QStringLiteral("f16");
        result[QStringLiteral("note")] = QStringLiteral("KV f16 prioriza fidelidad; confirmar que el contexto entra en VRAM.");
    } else {
        result[QStringLiteral("kvCache")] = QStringLiteral("q8_0");
        result[QStringLiteral("note")] = QStringLiteral("Perfil equilibrado; comparar contra KV f16 si sobra VRAM.");
    }
    result[QStringLiteral("hardwareFingerprint")] = hardwareFingerprint(hardware);
    return result;
}

double HardwareDiagnostics::performanceScore(const QVariantMap &sample, const QString &target)
{
    // La matriz acepta tanto muestras del benchmark técnico como artefactos de
    // benchmark de agente. Estos últimos usan avgTps y qualityScore/Total.
    const double pp = sample.contains(QStringLiteral("promptTps"))
        ? sample.value(QStringLiteral("promptTps")).toDouble()
        : sample.value(QStringLiteral("prefillTps")).toDouble();
    const double tg = sample.contains(QStringLiteral("generationTps"))
        ? sample.value(QStringLiteral("generationTps")).toDouble()
        : sample.value(QStringLiteral("avgTps")).toDouble();
    double quality = 1.0;
    if (sample.contains(QStringLiteral("quality"))) {
        quality = sample.value(QStringLiteral("quality")).toDouble();
    } else {
        const double total = sample.value(QStringLiteral("qualityTotal")).toDouble();
        if (total > 0.0)
            quality = sample.value(QStringLiteral("qualityScore")).toDouble() / total;
    }
    const bool stable = sample.contains(QStringLiteral("stable"))
        ? sample.value(QStringLiteral("stable")).toBool()
        : !sample.value(QStringLiteral("failed")).toBool()
          && !sample.value(QStringLiteral("timedOut")).toBool();
    const double stability = stable ? 1.0 : 0.0;
    const QString objective = target.trimmed().toLower();
    double speed = objective == QLatin1String("prefill") ? pp
                   : objective == QLatin1String("decode") ? tg
                   : 0.65 * tg + 0.35 * pp;
    return speed * qBound(0.0, quality, 1.0) * (0.5 + 0.5 * stability);
}
