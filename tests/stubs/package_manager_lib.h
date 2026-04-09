#pragma once

#include <string>
#include <vector>

// Stub for unit tests — aligns with logos-package-manager PackageManagerLib public API
// used by PackageManagerImpl.

enum class SignaturePolicy {
    NONE,
    WARN,
    REQUIRE
};

struct SignatureVerificationResult {
    bool is_signed = false;
    bool signature_valid = false;
    bool package_valid = false;
    std::string signer_did;
    std::string signer_name;
    std::string signer_url;
    std::string trusted_as;
    std::string error;
};

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

    void setSignaturePolicy(SignaturePolicy policy);
    void setKeyringDirectory(const std::string& dir);
    std::string keyringDirectory();

    SignatureVerificationResult verifyPackageSignature(const std::string& lgxPath);
};
