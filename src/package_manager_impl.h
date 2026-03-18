#pragma once

#include "logos_native_provider.h"
#include "logos_native_api.h"
#include "logos_native_types.h"

#include <string>
#include <vector>
#include <QString>

class PackageManagerLib;

class PackageManagerImpl : public NativeProviderBase
{
    NATIVE_LOGOS_PROVIDER(PackageManagerImpl, "package_manager", "1.0.0")

protected:
    void onInit(NativeLogosAPI* api) override;

public:
    PackageManagerImpl();
    ~PackageManagerImpl();

    LOGOS_METHOD bool installPlugin(const std::string& pluginPath, bool skipIfNotNewerVersion);
    LOGOS_METHOD LogosValue getPackages();
    LOGOS_METHOD LogosValue getPackages(const std::string& category);
    LOGOS_METHOD std::vector<std::string> getCategories();
    LOGOS_METHOD std::vector<std::string> resolveDependencies(const std::vector<std::string>& packageNames);
    LOGOS_METHOD void setPluginsDirectory(const std::string& pluginsDirectory);
    LOGOS_METHOD void setUiPluginsDirectory(const std::string& uiPluginsDirectory);
    LOGOS_METHOD void setRelease(const std::string& releaseTag);
    LOGOS_METHOD bool installPackage(const std::string& packageName, const std::string& pluginsDirectory);
    LOGOS_METHOD bool installPackages(const std::vector<std::string>& packageNames, const std::string& pluginsDirectory);
    LOGOS_METHOD void installPackageAsync(const std::string& packageName, const std::string& pluginsDirectory);
    LOGOS_METHOD void installPackagesAsync(const std::vector<std::string>& packageNames, const std::string& pluginsDirectory);
    LOGOS_METHOD std::string testPluginCall(const std::string& foo);
    LOGOS_METHOD void testEvent(const std::string& message);

private:
    PackageManagerLib* m_lib;

    void onPluginFileInstalled(const QString& pluginPath, bool isCoreModule);
    void onInstallationFinished(const QString& packageName, bool success, const QString& error);
    void emitInstallationEvent(const QString& packageName, bool success, const QString& error);
};
