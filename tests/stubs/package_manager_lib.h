#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Stub for unit tests — aligns with logos-package-manager PackageManagerLib public API
// used by PackageManagerImpl.

enum class SignaturePolicy {
    NONE,
    WARN,
    REQUIRE
};

enum class InstallType {
    Embedded,
    User,
};

enum class DependencyStatus {
    Installed,
    NotInstalled,
    Cycle,
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

struct UninstallResult {
    bool success = false;
    std::string errorMsg;
    std::vector<std::string> removedFiles;
};

// Mirrors the real lib's Hashes struct. Only `root` is read today but keeping
// the nested shape matches manifest.json and leaves room for additions.
struct Hashes {
    std::string root;
};

// Mirrors InstalledPackage in package_manager_lib.h.
struct InstalledPackage {
    std::string name;
    std::string version;
    std::string description;
    std::string type;
    std::string category;
    std::string author;
    std::string license;
    std::string icon;
    std::string view;
    std::vector<std::string> dependencies;
    Hashes hashes;
    InstallType installType = InstallType::User;
    std::string installDir;
    std::string mainFilePath;
};

// Mirrors DependencyTreeNode in package_manager_lib.h.
struct DependencyTreeNode {
    std::string name;
    DependencyStatus status = DependencyStatus::NotInstalled;
    std::string version;
    InstallType installType = InstallType::User;
    std::vector<DependencyTreeNode> children;

    // Matches the real lib — descendants-only BFS, name-deduped, children
    // cleared on returned copies. Implementation lives in the mock .cpp so
    // the stub header stays declaration-only.
    std::vector<DependencyTreeNode> flatten() const;
};

// Mirrors DependentTreeNode in package_manager_lib.h.
struct DependentTreeNode {
    std::string name;
    std::string version;
    std::string type;
    InstallType installType = InstallType::User;
    std::string installDir;
    std::vector<DependentTreeNode> children;

    std::vector<DependentTreeNode> flatten() const;
};

const char* installTypeToString(InstallType t);
const char* dependencyStatusToString(DependencyStatus s);

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

    std::vector<InstalledPackage> getInstalledPackages();
    std::vector<InstalledPackage> getInstalledModules();
    std::vector<InstalledPackage> getInstalledUiPlugins();

    static std::vector<std::string> platformVariantsToTry();

    void setSignaturePolicy(SignaturePolicy policy);
    void setKeyringDirectory(const std::string& dir);
    std::string keyringDirectory();

    SignatureVerificationResult verifyPackageSignature(const std::string& lgxPath);

    UninstallResult uninstallPackage(const std::string& packageName);

    std::optional<DependencyTreeNode> resolveDependencies(const std::string& packageName);
    std::optional<DependentTreeNode>  resolveDependents(const std::string& packageName);
};
