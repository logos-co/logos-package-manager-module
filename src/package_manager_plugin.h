#pragma once

#include <QtCore/QObject>
#include <QJsonArray>
#include "package_manager_interface.h"
#include "logos_api.h"
#include "logos_api_client.h"

class PackageManagerLib;

class PackageManagerPlugin : public QObject, public PackageManagerInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PackageManagerInterface_iid FILE "metadata.json")
    Q_INTERFACES(PackageManagerInterface PluginInterface)

public:
    PackageManagerPlugin();
    ~PackageManagerPlugin();

    Q_INVOKABLE bool installPlugin(const QString& pluginPath, bool skipIfNotNewerVersion = false) override;
    QString name() const override { return "package_manager"; }
    QString version() const override { return "1.0.0"; }
    Q_INVOKABLE QJsonArray getPackages();
    Q_INVOKABLE QJsonArray getPackages(const QString& category);
    Q_INVOKABLE QStringList getCategories();
    Q_INVOKABLE QStringList resolveDependencies(const QStringList& packageNames);
    Q_INVOKABLE void setPluginsDirectory(const QString& pluginsDirectory);
    Q_INVOKABLE void setUiPluginsDirectory(const QString& uiPluginsDirectory);
    Q_INVOKABLE bool installPackage(const QString& packageName, const QString& pluginsDirectory);
    Q_INVOKABLE bool installPackages(const QStringList& packageNames, const QString& pluginsDirectory);
    Q_INVOKABLE void installPackageAsync(const QString& packageName, const QString& pluginsDirectory);
    Q_INVOKABLE void installPackagesAsync(const QStringList& packageNames, const QString& pluginsDirectory);

    Q_INVOKABLE void initLogos(LogosAPI* logosAPIInstance);
    
    Q_INVOKABLE QString testPluginCall(const QString& foo);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private slots:
    void onPluginFileInstalled(const QString& pluginPath, bool isCoreModule);
    void onInstallationFinished(const QString& packageName, bool success, const QString& error);

private:
    PackageManagerLib* m_lib;
    
    void emitInstallationEvent(const QString& packageName, bool success, const QString& error);
};
