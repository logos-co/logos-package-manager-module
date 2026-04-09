// Mock PackageManagerLib for package_manager_module unit tests (link-time substitution).

#include <logos_clib_mock.h>
#include <package_manager_lib.h>

static std::string mockDupCStr(const char* key, const char* fallback) {
    const char* ret = LOGOS_CMOCK_RETURN_STRING(key);
    if (ret && ret[0])
        return std::string(ret);
    return fallback ? std::string(fallback) : std::string();
}

PackageManagerLib::PackageManagerLib() {
    LOGOS_CMOCK_RECORD("PackageManagerLib_ctor");
}

PackageManagerLib::~PackageManagerLib() {
    LOGOS_CMOCK_RECORD("PackageManagerLib_dtor");
}

void PackageManagerLib::setEmbeddedModulesDirectory(const std::string& dir) {
    LOGOS_CMOCK_RECORD("setEmbeddedModulesDirectory");
    (void)dir;
}

void PackageManagerLib::addEmbeddedModulesDirectory(const std::string& dir) {
    LOGOS_CMOCK_RECORD("addEmbeddedModulesDirectory");
    (void)dir;
}

void PackageManagerLib::setEmbeddedUiPluginsDirectory(const std::string& dir) {
    LOGOS_CMOCK_RECORD("setEmbeddedUiPluginsDirectory");
    (void)dir;
}

void PackageManagerLib::addEmbeddedUiPluginsDirectory(const std::string& dir) {
    LOGOS_CMOCK_RECORD("addEmbeddedUiPluginsDirectory");
    (void)dir;
}

void PackageManagerLib::setUserModulesDirectory(const std::string& dir) {
    LOGOS_CMOCK_RECORD("setUserModulesDirectory");
    (void)dir;
}

void PackageManagerLib::setUserUiPluginsDirectory(const std::string& dir) {
    LOGOS_CMOCK_RECORD("setUserUiPluginsDirectory");
    (void)dir;
}

std::string PackageManagerLib::installPluginFile(const std::string& pluginPath, std::string& errorMsg,
                                                 bool skipIfNotNewerVersion,
                                                 std::string* installedPluginPath,
                                                 bool* isCoreModule) {
    LOGOS_CMOCK_RECORD("installPluginFile");
    if (skipIfNotNewerVersion) {
        LOGOS_CMOCK_RECORD("installPluginFile_skipIfNotNewer_true");
    } else {
        LOGOS_CMOCK_RECORD("installPluginFile_skipIfNotNewer_false");
    }
    (void)pluginPath;

    errorMsg = mockDupCStr("installPluginFile_error", "");

    if (installedPluginPath) {
        *installedPluginPath = mockDupCStr("installPluginFile_installedPath", "");
    }
    if (isCoreModule) {
        *isCoreModule = LOGOS_CMOCK_RETURN(bool, "installPluginFile_isCore");
    }

    return mockDupCStr("installPluginFile_result", "");
}

std::string PackageManagerLib::getInstalledPackages() {
    LOGOS_CMOCK_RECORD("getInstalledPackages");
    return mockDupCStr("getInstalledPackages", "[]");
}

std::string PackageManagerLib::getInstalledModules() {
    LOGOS_CMOCK_RECORD("getInstalledModules");
    return mockDupCStr("getInstalledModules", "[]");
}

std::string PackageManagerLib::getInstalledUiPlugins() {
    LOGOS_CMOCK_RECORD("getInstalledUiPlugins");
    return mockDupCStr("getInstalledUiPlugins", "[]");
}

std::vector<std::string> PackageManagerLib::platformVariantsToTry() {
    LOGOS_CMOCK_RECORD("platformVariantsToTry");
    const char* v = LOGOS_CMOCK_RETURN_STRING("platformVariantsToTry_first");
    if (v && v[0]) {
        return {std::string(v)};
    }
    return {"mock-variant"};
}
