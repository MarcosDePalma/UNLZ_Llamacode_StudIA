#include "CapabilityDetector.h"
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

DetectedCapabilities CapabilityDetector::detect(const QString &binaryPath, int timeoutMs)
{
    DetectedCapabilities failed;
    if (!QFile::exists(binaryPath)) {
        failed.error = QStringLiteral("Binary not found: %1").arg(binaryPath);
        return failed;
    }

    auto run = [&](const QString &arg, QString *out) -> bool {
        QProcess proc;
        proc.setProgram(binaryPath);
        proc.setArguments({arg});
        proc.setWorkingDirectory(QFileInfo(binaryPath).absolutePath());
        proc.start();

        if (!proc.waitForFinished(timeoutMs)) {
            proc.kill();
            failed.error = QStringLiteral("Timeout running %1: %2").arg(arg, proc.errorString());
            return false;
        }
        *out = QString::fromUtf8(proc.readAllStandardOutput())
             + QString::fromUtf8(proc.readAllStandardError());
        return proc.exitStatus() == QProcess::NormalExit || !out->trimmed().isEmpty();
    };

    QString versionOutput;
    QString helpOutput;
    const bool versionOk = run(QStringLiteral("--version"), &versionOutput);
    const QString firstError = failed.error;
    failed.error.clear();
    const bool helpOk = run(QStringLiteral("--help"), &helpOutput);

    if (!versionOk && !helpOk) {
        failed.error = firstError.isEmpty() ? QStringLiteral("Could not run --version or --help.")
                                            : firstError;
        return failed;
    }
    return parseProbeOutput(versionOutput, helpOutput);
}

DetectedCapabilities CapabilityDetector::parse(const QString &helpOutput)
{
    return parseProbeOutput(QString(), helpOutput);
}

DetectedCapabilities CapabilityDetector::parseProbeOutput(const QString &versionOutput,
                                                          const QString &helpOutput)
{
    DetectedCapabilities cap;
    cap.success = true;

    // Strip ANSI escape codes
    static const QRegularExpression ansiRe(R"(\x1B\[[0-9;]*[A-Za-z])");
    QString clean = helpOutput;
    clean.remove(ansiRe);
    QString versionClean = versionOutput;
    versionClean.remove(ansiRe);

    static const QRegularExpression versionRe(
        QStringLiteral("^\\s*version:\\s*(.+?)\\s*$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const auto versionMatch = versionRe.match(versionClean + QLatin1Char('\n') + clean);
    if (versionMatch.hasMatch())
        cap.version = versionMatch.captured(1).trimmed();
    if (cap.version.isEmpty()) {
        const QStringList lines = versionClean.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            cap.version = lines.first().trimmed().left(120);
    }

    // Match flag lines: 0–16 spaces indent, then --flag or -f (but not 17+ spaces = description continuation)
    static const QRegularExpression re(
        R"(^\s{0,16}(-{1,2}[\w][\w-]*(?:,\s*-{1,2}[\w][\w-]*)*))",
        QRegularExpression::MultilineOption
    );

    auto it = re.globalMatch(clean);
    while (it.hasNext()) {
        const auto match = it.next();
        const QStringList parts = match.captured(1).split(QRegularExpression(R"(,\s*)"));
        QString canonical;
        for (const QString &part : parts) {
            const QString flag = part.trimmed();
            if (flag.startsWith("--")) { canonical = flag; break; }
        }
        if (canonical.isEmpty() && !parts.isEmpty())
            canonical = parts.first().trimmed();

        // Keep all long variants as explicit supported flags (e.g. --mmap and --no-mmap),
        // and only alias short variants to the selected canonical long flag.
        for (const QString &part : parts) {
            const QString flag = part.trimmed();
            if (flag.isEmpty())
                continue;
            if (flag.startsWith("--")) {
                if (!cap.flags.contains(flag))
                    cap.flags.append(flag);
            }
        }
        if (!cap.flags.contains(canonical))
            cap.flags.append(canonical);

        for (const QString &part : parts) {
            const QString flag = part.trimmed();
            if (flag.isEmpty() || flag == canonical)
                continue;
            if (flag.startsWith("-") && !flag.startsWith("--"))
                cap.flagAliases[flag] = canonical;
        }
    }

    // Normalize known renames: add legacy aliases so LlamaCode internals keep working
    // --draft-model was renamed to --spec-draft-model in newer forks
    if (cap.flags.contains("--spec-draft-model") && !cap.flags.contains("--draft-model"))
        cap.flagAliases["--draft-model"] = "--spec-draft-model";

    if (cap.flags.contains(QStringLiteral("--cache-type-k"))
        || cap.flags.contains(QStringLiteral("--cache-type-v"))) {
        cap.kvTypes = {
            QStringLiteral("f16"), QStringLiteral("q8_0"), QStringLiteral("q4_0"),
            QStringLiteral("q4_1"), QStringLiteral("q5_0"), QStringLiteral("q5_1"),
            QStringLiteral("q8_1")
        };
    } else {
        cap.kvTypes = {QStringLiteral("f16")};
    }

    const QString combinedLower = (versionClean + QLatin1Char('\n') + clean).toLower();
    if (combinedLower.contains(QStringLiteral("turbo"))) {
        for (const QString &kv : {QStringLiteral("turbo2"), QStringLiteral("turbo3"), QStringLiteral("turbo4")})
            if (!cap.kvTypes.contains(kv))
                cap.kvTypes.append(kv);
    }

    static const QRegularExpression specRe(
        QStringLiteral("--spec-type\\s+(\\[[^\\]\\n]+\\]|[a-z][a-z0-9_-]*(?:[,|][a-z0-9_-]+)+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto specIt = specRe.globalMatch(clean);
    while (specIt.hasNext()) {
        QString raw = specIt.next().captured(1);
        raw.remove(QLatin1Char('['));
        raw.remove(QLatin1Char(']'));
        for (const QString &value : raw.split(QRegularExpression(QStringLiteral("[,|]")), Qt::SkipEmptyParts)) {
            const QString pseudo = QStringLiteral("spec-type:%1").arg(value.trimmed());
            if (!cap.flags.contains(pseudo))
                cap.flags.append(pseudo);
        }
    }

    return cap;
}
