#pragma once
#include <QString>
#include <functional>

struct WebFetchResult
{
    QString provider;
    QString text;
    QString error;
};

// Contrato pequeño para que web_fetch orqueste proveedores sin conocer su
// transporte. Los adapters concretos pueden ser HTTP directo, MCP o REST.
class IWebFetchProvider
{
public:
    virtual ~IWebFetchProvider() = default;
    virtual QString name() const = 0;
    virtual WebFetchResult fetch(const QString &url) = 0;
};

class CallbackWebFetchProvider final : public IWebFetchProvider
{
public:
    using Fetch = std::function<WebFetchResult(const QString &)>;
    CallbackWebFetchProvider(QString providerName, Fetch fetch)
        : m_name(std::move(providerName)), m_fetch(std::move(fetch)) {}
    QString name() const override { return m_name; }
    WebFetchResult fetch(const QString &url) override { return m_fetch(url); }

private:
    QString m_name;
    Fetch m_fetch;
};
