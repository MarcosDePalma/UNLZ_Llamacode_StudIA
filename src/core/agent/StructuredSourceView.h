#pragma once

#include <QString>
#include <QVector>

// Vista efimera: nunca escribe el archivo compacto. Cada token conserva su rango
// exacto para proyectar evidencia al original y rechazar rangos ambiguos.
class StructuredSourceView
{
public:
    struct Segment { int compactStart = 0; int originalStart = 0; int length = 0; };
    struct Result {
        QString compact;
        QVector<Segment> segments;
        QString error;
        QString parserBackend = QStringLiteral("lexical");
        bool parserValidated = false;
        bool safe = false;
        int originalBytes = 0;
        double reductionPct() const;
    };

    static Result build(const QString &source, const QString &fileName,
                        bool keepComments = true);
    static bool projectRange(const Result &view, int compactStart, int compactLength,
                             int *originalStart, int *originalLength);
};
