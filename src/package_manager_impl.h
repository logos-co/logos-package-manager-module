#pragma once

#include "logos_provider_object.h"
#include "logos_api.h"
#include <QVariantList>
#include <QVariantMap>
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

    // Install from local LGX file — returns QVariantMap {name, path, error, isCoreModule}
    LOGOS_METHOD QVariantMap installPlugin(const QString& pluginPath, bool skipIfNotNewerVersion);

    // Directory configuration (4-directory model)
    LOGOS_METHOD void setEmbeddedModulesDirectory(const QString& dir);
    LOGOS_METHOD void setUserModulesDirectory(const QString& dir);
    LOGOS_METHOD void setEmbeddedUiPluginsDirectory(const QString& dir);
    LOGOS_METHOD void setUserUiPluginsDirectory(const QString& dir);

    // Scanning — each returns JSON array with all manifest fields + installDir + mainFilePath
    LOGOS_METHOD QVariantList getInstalledPackages();
    LOGOS_METHOD QVariantList getInstalledModules();
    LOGOS_METHOD QVariantList getInstalledUiPlugins();

    // Platform variants this build accepts (e.g. ["darwin-arm64-dev"] or ["darwin-arm64"])
    LOGOS_METHOD QStringList getValidVariants();

private:
    PackageManagerLib* m_lib;

    void onPluginFileInstalled(const QString& pluginPath, bool isCoreModule);
};
