#include "TeachKeyBuffer.h"

void TeachKeyBuffer::feedChar(QChar c)
{
    m_text.append(c);
}

QVariantList TeachKeyBuffer::feedKey(const QString &keyName, const QStringList &modifiers)
{
    QVariantList steps = flush();
    if (!keyName.isEmpty()) steps.append(keyStep(keyName, modifiers));
    return steps;
}

QVariantList TeachKeyBuffer::flush()
{
    QVariantList steps;
    if (!m_text.isEmpty()) {
        steps.append(typeStep(m_text));
        m_text.clear();
    }
    return steps;
}

QVariantMap TeachKeyBuffer::typeStep(const QString &text)
{
    return {{QStringLiteral("kind"), QStringLiteral("type")},
            {QStringLiteral("intent"), QStringLiteral("Escribir: \"%1\"").arg(text)},
            {QStringLiteral("text"), text}};
}

QVariantMap TeachKeyBuffer::keyStep(const QString &keyName, const QStringList &modifiers)
{
    QString label = keyName;
    if (!modifiers.isEmpty())
        label = modifiers.join(QLatin1Char('+')) + QLatin1Char('+') + keyName;
    QVariantMap step{{QStringLiteral("kind"), QStringLiteral("key")},
                     {QStringLiteral("intent"), QStringLiteral("Tecla %1").arg(label)},
                     {QStringLiteral("key"), keyName}};
    if (!modifiers.isEmpty())
        step.insert(QStringLiteral("modifiers"), QVariant(modifiers));
    return step;
}
