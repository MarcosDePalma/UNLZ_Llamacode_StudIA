#pragma once

#include <QVariantList>
#include <QVariantMap>

class WorkflowVisualModel
{
public:
    static QVariantList rows(const QVariantMap &definition);
    static QVariantMap merge(const QVariantMap &definition, const QVariantList &rows);
};
