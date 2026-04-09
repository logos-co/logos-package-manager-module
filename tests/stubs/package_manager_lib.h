#pragma once

#include <string>
#include <vector>

// Stub header for unit tests — matches the subset of PackageManagerLib used by
// PackageManagerImpl. The real header is installed under lib/ from logos-package-manager.

class PackageManagerLib {
public:
    PackageManagerLib();
    ~PackageManagerLib();

    void setEmbeddedModulesDirectory(const std::string& dir);
    void addEmbeddedModulesDirectory(const std::string& dir);
    void setEmbeddedUiPluginsDirectory(const std::string& dir);
    void addEmbeddedUiPluginsDirectory(const std::string& dir);
    void setUserModulesDirectory(const std::string& dir);
    void setUserUiPluginsDirectory(const std::string& dir);

    std::string installPluginFile(const std::string& pluginPath, std::string& errorMsg,
                                  bool skipIfNotNewerVersion = false,
                                  std::string* installedPluginPath = nullptr,
                                  bool* isCoreModule = nullptr);

    std::string getInstalledPackages();
    std::string getInstalledModules();
    std::string getInstalledUiPlugins();

    static std::vector<std::string> platformVariantsToTry();
};
