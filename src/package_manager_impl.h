#pragma once

#include <string>
#include <vector>
#include <functional>
#include <logos_json.h>

class PackageManagerLib;

class PackageManagerImpl {
public:
    PackageManagerImpl();
    ~PackageManagerImpl();

    // Event callback — wired automatically by the generated glue layer.
    // Call this to emit named events to other modules / the host application.
    std::function<void(const std::string& eventName, const std::string& data)> emitEvent;

    // Install from local LGX file — returns LogosMap {name, path, error, isCoreModule, ...}
    LogosMap installPlugin(const std::string& pluginPath, bool skipIfNotNewerVersion);

    // Directory configuration — embedded (multiple, read-only)
    void setEmbeddedModulesDirectory(const std::string& dir);
    void addEmbeddedModulesDirectory(const std::string& dir);
    void setEmbeddedUiPluginsDirectory(const std::string& dir);
    void addEmbeddedUiPluginsDirectory(const std::string& dir);

    // Directory configuration — user (single, writable)
    void setUserModulesDirectory(const std::string& dir);
    void setUserUiPluginsDirectory(const std::string& dir);

    // Scanning — each returns LogosList (JSON array with all manifest fields + installDir + mainFilePath)
    LogosList getInstalledPackages();
    LogosList getInstalledModules();
    LogosList getInstalledUiPlugins();

    // Platform variants this build accepts (e.g. ["darwin-arm64-dev"] or ["darwin-arm64"])
    std::vector<std::string> getValidVariants();

    // Signature policy configuration
    void setSignaturePolicy(const std::string& policy);
    void setKeyringDirectory(const std::string& dir);

    // Standalone signature verification — returns {isSigned, signatureValid, packageValid, signerDid, ...}
    LogosMap verifyPackage(const std::string& lgxPath);

    // Keyring management — add/remove/list trusted signing keys
    LogosMap addTrustedKey(const std::string& name, const std::string& did,
                           const std::string& displayName, const std::string& url);
    LogosMap removeTrustedKey(const std::string& name);
    LogosList listTrustedKeys();

private:
    PackageManagerLib* m_lib;
};
