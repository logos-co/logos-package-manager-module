#pragma once

#include "logos_provider_object.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include <QJsonArray>
#include <QStringList>

class PackageManagerLib;

class PackageManagerImpl : public LogosProviderBase
{
    LOGOS_PROVIDER(PackageManagerImpl, "package_manager", "1.0.0")

protected:
    void onInit(LogosAPI* api) override;

public:
    PackageManagerImpl();
    ~PackageManagerImpl();

    LOGOS_METHOD bool installPlugin(const QString& pluginPath, bool skipIfNotNewerVersion);
    LOGOS_METHOD QJsonArray getPackages();
    LOGOS_METHOD QJsonArray getPackages(const QString& category);
    LOGOS_METHOD QStringList getCategories();
    LOGOS_METHOD QStringList resolveDependencies(const QStringList& packageNames);
    LOGOS_METHOD void setPluginsDirectory(const QString& pluginsDirectory);
    LOGOS_METHOD void setUiPluginsDirectory(const QString& uiPluginsDirectory);
    LOGOS_METHOD void setRelease(const QString& releaseTag);
    LOGOS_METHOD bool installPackage(const QString& packageName, const QString& pluginsDirectory);
    LOGOS_METHOD bool installPackages(const QStringList& packageNames, const QString& pluginsDirectory);
    LOGOS_METHOD void installPackageAsync(const QString& packageName, const QString& pluginsDirectory);
    LOGOS_METHOD void installPackagesAsync(const QStringList& packageNames, const QString& pluginsDirectory);
    LOGOS_METHOD QString testPluginCall(const QString& foo);
    LOGOS_METHOD void testEvent(const QString& message);

private:
    PackageManagerLib* m_lib;

    void onPluginFileInstalled(const QString& pluginPath, bool isCoreModule);
    void onInstallationFinished(const QString& packageName, bool success, const QString& error);
    void emitInstallationEvent(const QString& packageName, bool success, const QString& error);
};
