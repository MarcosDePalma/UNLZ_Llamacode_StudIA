#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Expande variantes declarativas de benchmark sin duplicar en el bundle todos los
// metadatos del modelo, descarga y binario. Cada variante hereda la entrada base y
// puede sobreescribir runtime y flags con valor; JSON null elimina el flag.
inline QJsonArray expandSystemProfileVariants(const QJsonArray &source)
{
    QJsonArray expanded;
    for (const QJsonValue &value : source) {
        QJsonObject base = value.toObject();
        const QJsonArray variants = base.take(QStringLiteral("benchmarkVariants")).toArray();
        expanded.append(base);

        for (const QJsonValue &variantValue : variants) {
            const QJsonObject variant = variantValue.toObject();
            const QString id = variant.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                continue;

            QJsonObject derived = base;
            derived[QStringLiteral("id")] = id;
            derived[QStringLiteral("displayName")] =
                variant.value(QStringLiteral("displayName")).toString();
            // Algunas comparaciones necesitan cambiar el template además de los
            // flags. Propagarlo permite que ProfileManager materialice el archivo
            // correcto desde el bundle, en vez de dejar el template del padre.
            if (variant.contains(QStringLiteral("chatTemplate")))
                derived[QStringLiteral("chatTemplate")] =
                    variant.value(QStringLiteral("chatTemplate"));
            // Una variante puede cambiar el perfil de agente/harness sin
            // duplicar el modelo, runtime ni sus flags. Esto permite hacer A/B
            // de harnesses con la misma huella de llama-server.
            if (variant.contains(QStringLiteral("agentProfileId")))
                derived[QStringLiteral("agentProfileId")] =
                    variant.value(QStringLiteral("agentProfileId"));
            // Las variantes pueden cambiar la profundidad del MTP sin dejar
            // argumentos heredados duplicados. Esto es importante porque el
            // bloque `mtp` se materializa después de `extraArgs`.
            if (variant.contains(QStringLiteral("mtp")))
                derived[QStringLiteral("mtp")] = variant.value(QStringLiteral("mtp"));
            // Speculative decoding también puede aislarse en una variante. Esto
            // permite comparar el mismo GGUF MTP con MTP2/MTP3 y sin speculative
            // sin duplicar el modelo ni dejar flags heredados en el perfil frío.
            if (variant.contains(QStringLiteral("spec")))
                derived[QStringLiteral("spec")] = variant.value(QStringLiteral("spec"));
            // Una variante de tuning no hereda las insignias obtenidas por el
            // perfil base: debe medirse y marcarse de forma explicita.
            derived[QStringLiteral("best")] = variant.value(QStringLiteral("best")).toBool(false);
            derived[QStringLiteral("favorite")] = variant.value(QStringLiteral("favorite")).toBool(false);
            derived[QStringLiteral("benchmark")] = variant.value(QStringLiteral("benchmark")).toBool(false);
            if (variant.contains(QStringLiteral("order")))
                derived[QStringLiteral("order")] = variant.value(QStringLiteral("order"));
            // Las copias de benchmark son comparadores, no recomendaciones del
            // showcase: no deben duplicar el perfil base en la portada.
            derived.remove(QStringLiteral("showcaseGroup"));
            derived.remove(QStringLiteral("showcaseLabel"));
            derived.remove(QStringLiteral("contextPresets"));

            QJsonObject runtime = derived.value(QStringLiteral("runtime")).toObject();
            const QJsonObject runtimeOverrides = variant.value(QStringLiteral("runtime")).toObject();
            for (auto it = runtimeOverrides.begin(); it != runtimeOverrides.end(); ++it)
                runtime[it.key()] = it.value();
            derived[QStringLiteral("runtime")] = runtime;

            QStringList args;
            for (const QJsonValue &arg : derived.value(QStringLiteral("extraArgs")).toArray())
                args.append(arg.toString());
            const QJsonObject flagOverrides =
                variant.value(QStringLiteral("extraArgOverrides")).toObject();
            for (auto it = flagOverrides.begin(); it != flagOverrides.end(); ++it) {
                const int index = args.indexOf(it.key());
                if (index >= 0) {
                    args.removeAt(index);
                    if (index < args.size() && !args.at(index).startsWith(u'-'))
                        args.removeAt(index);
                }
                if (!it.value().isNull())
                    args << it.key() << it.value().toVariant().toString();
            }
            // Some llama.cpp switches are boolean flags without a value
            // (e.g. --cache-prompt and --no-mmproj-offload). Keep them
            // declarative too instead of manufacturing an empty argument.
            for (const QJsonValue &addition :
                 variant.value(QStringLiteral("extraArgAdds")).toArray()) {
                const QString arg = addition.toString();
                if (!arg.isEmpty())
                    args.append(arg);
            }
            QJsonArray extraArgs;
            for (const QString &arg : args)
                extraArgs.append(arg);
            derived[QStringLiteral("extraArgs")] = extraArgs;
            derived[QStringLiteral("comment")] =
                variant.value(QStringLiteral("comment")).toString();
            expanded.append(derived);
        }
    }
    return expanded;
}
