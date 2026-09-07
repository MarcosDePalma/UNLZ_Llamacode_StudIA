#pragma once
#include "ProfileTypes.h"
#include "../LlamaBinary.h"
#include "../CatalogModel.h"

class EffectiveProfileBuilder
{
public:
    struct Context {
        LaunchProfile launch;
        BackendProfile backend;
        ModelProfile model;
        RuntimePreset runtime;
        HarnessProfile harness;
        WorkspaceProfile workspace;
        LlamaBinary binary;
        CatalogModel catalogModel;
        CatalogModel mmprojModel;
        CatalogModel draftModel;
        bool reasoningEnabled = false;
    };

    static EffectiveProfile build(const Context &ctx);

private:
    static void applyBackend(const BackendProfile &bp,
                             QStringList &args, QMap<QString, QString> &env,
                             QStringList &warnings, QStringList &errors);
    static void applyModel(const ModelProfile &mp,
                           const CatalogModel &model,
                           const CatalogModel &mmproj,
                           const CatalogModel &draft,
                           const LlamaBinary &bin,
                           QStringList &args,
                           QStringList &warnings, QStringList &errors);
    static void applyRuntime(const RuntimePreset &rt,
                             const LlamaBinary &bin,
                             QStringList &args,
                             QStringList &warnings, QStringList &errors,
                             bool specDecoding = false);
    static void applyReasoningControl(const Context &ctx,
                                      QStringList &args,
                                      QStringList &warnings);
    static void applySamplingPolicy(const Context &ctx,
                                    QStringList &args,
                                    QStringList &warnings);
    static void applyAdaptiveSpeculation(const ModelProfile &mp,
                                         const LlamaBinary &bin,
                                         QStringList &args,
                                         QStringList &warnings,
                                         QStringList &errors);

    static void addFlag(const LlamaBinary &bin, const QString &flag,
                        const QString &value, QStringList &args,
                        QStringList &warnings, bool required = false,
                        QStringList *errors = nullptr);
};
