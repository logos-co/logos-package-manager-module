#include "package_manager_impl.h"
#include <package_manager_lib.h>
#include <lgx.h>
#include <algorithm>
#include <filesystem>
#include <iostream>

PackageManagerImpl::PackageManagerImpl()
    : m_lib(nullptr)
{
    m_lib = new PackageManagerLib();
}

PackageManagerImpl::~PackageManagerImpl()
{
    delete m_lib;
    m_lib = nullptr;
}

LogosMap PackageManagerImpl::installPlugin(const std::string& pluginPath, bool skipIfNotNewerVersion)
{
    std::string errorMsg;
    std::string installedPluginPath;
    bool isCoreModule = false;
    std::string result = m_lib->installPluginFile(
        pluginPath, errorMsg, skipIfNotNewerVersion,
        &installedPluginPath, &isCoreModule
    );

    bool success = !result.empty();

    if (success && !installedPluginPath.empty() && emitEvent) {
        if (isCoreModule) {
            emitEvent("corePluginFileInstalled", installedPluginPath);
        } else {
            emitEvent("uiPluginFileInstalled", installedPluginPath);
        }
    }

    // Get signature info for the response
    auto sigResult = m_lib->verifyPackageSignature(pluginPath);

    std::string stem = std::filesystem::path(pluginPath).stem().string();

    LogosMap response;
    response["name"] = stem;
    response["path"] = success ? installedPluginPath : std::string();
    response["isCoreModule"] = isCoreModule;
    if (!success) {
        response["error"] = errorMsg;
    }

    // Add signature info
    if (sigResult.is_signed) {
        bool valid = sigResult.signature_valid && sigResult.package_valid;
        response["signatureStatus"] = valid ? std::string("signed") : std::string("invalid");
        response["signerDid"] = sigResult.signer_did;
        if (!sigResult.signer_name.empty())
            response["signerName"] = sigResult.signer_name;
        if (!sigResult.signer_url.empty())
            response["signerUrl"] = sigResult.signer_url;
        if (!sigResult.trusted_as.empty())
            response["trustedAs"] = sigResult.trusted_as;
    } else if (!sigResult.error.empty()) {
        response["signatureStatus"] = std::string("error");
        response["signatureError"] = sigResult.error;
    } else {
        response["signatureStatus"] = std::string("unsigned");
    }

    return response;
}

LogosList PackageManagerImpl::getInstalledPackages()
{
    return LogosList::parse(m_lib->getInstalledPackages());
}

LogosList PackageManagerImpl::getInstalledModules()
{
    return LogosList::parse(m_lib->getInstalledModules());
}

LogosList PackageManagerImpl::getInstalledUiPlugins()
{
    return LogosList::parse(m_lib->getInstalledUiPlugins());
}

std::vector<std::string> PackageManagerImpl::getValidVariants()
{
    return PackageManagerLib::platformVariantsToTry();
}

void PackageManagerImpl::setEmbeddedModulesDirectory(const std::string& dir)
{
    m_lib->setEmbeddedModulesDirectory(dir);
}

void PackageManagerImpl::addEmbeddedModulesDirectory(const std::string& dir)
{
    m_lib->addEmbeddedModulesDirectory(dir);
}

void PackageManagerImpl::setEmbeddedUiPluginsDirectory(const std::string& dir)
{
    m_lib->setEmbeddedUiPluginsDirectory(dir);
}

void PackageManagerImpl::addEmbeddedUiPluginsDirectory(const std::string& dir)
{
    m_lib->addEmbeddedUiPluginsDirectory(dir);
}

void PackageManagerImpl::setUserModulesDirectory(const std::string& dir)
{
    m_lib->setUserModulesDirectory(dir);
}

void PackageManagerImpl::setUserUiPluginsDirectory(const std::string& dir)
{
    m_lib->setUserUiPluginsDirectory(dir);
}

void PackageManagerImpl::setSignaturePolicy(const std::string& policy)
{
    std::string p = policy;
    std::transform(p.begin(), p.end(), p.begin(), ::tolower);
    if (p == "none") m_lib->setSignaturePolicy(SignaturePolicy::NONE);
    else if (p == "warn") m_lib->setSignaturePolicy(SignaturePolicy::WARN);
    else if (p == "require") m_lib->setSignaturePolicy(SignaturePolicy::REQUIRE);
    else {
        std::cerr << "PackageManagerImpl::setSignaturePolicy: invalid policy '"
                  << policy << "' - expected one of: none, warn, require\n";
    }
}

void PackageManagerImpl::setKeyringDirectory(const std::string& dir)
{
    m_lib->setKeyringDirectory(dir);
}

LogosMap PackageManagerImpl::verifyPackage(const std::string& lgxPath)
{
    auto result = m_lib->verifyPackageSignature(lgxPath);

    LogosMap response;
    response["isSigned"] = result.is_signed;
    response["signatureValid"] = result.signature_valid;
    response["packageValid"] = result.package_valid;
    response["signerDid"] = result.signer_did;
    response["signerName"] = result.signer_name;
    response["signerUrl"] = result.signer_url;
    response["trustedAs"] = result.trusted_as;
    if (!result.error.empty())
        response["error"] = result.error;
    return response;
}

LogosMap PackageManagerImpl::addTrustedKey(const std::string& name, const std::string& did,
                                            const std::string& displayName, const std::string& url)
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_result_t res = lgx_keyring_add(
        keyringDirPtr,
        name.c_str(),
        did.c_str(),
        displayName.empty() ? nullptr : displayName.c_str(),
        url.empty() ? nullptr : url.c_str()
    );

    LogosMap response;
    response["success"] = static_cast<bool>(res.success);
    if (!res.success && res.error)
        response["error"] = std::string(res.error);
    return response;
}

LogosMap PackageManagerImpl::removeTrustedKey(const std::string& name)
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_result_t res = lgx_keyring_remove(
        keyringDirPtr,
        name.c_str()
    );

    LogosMap response;
    response["success"] = static_cast<bool>(res.success);
    if (!res.success && res.error)
        response["error"] = std::string(res.error);
    return response;
}

LogosList PackageManagerImpl::listTrustedKeys()
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_keyring_list_t list = lgx_keyring_list(keyringDirPtr);

    LogosList result = LogosList::array();
    for (size_t i = 0; i < list.count; ++i) {
        LogosMap entry;
        if (list.keys[i].name)         entry["name"]        = std::string(list.keys[i].name);
        if (list.keys[i].did)          entry["did"]         = std::string(list.keys[i].did);
        if (list.keys[i].display_name) entry["displayName"] = std::string(list.keys[i].display_name);
        if (list.keys[i].url)          entry["url"]         = std::string(list.keys[i].url);
        if (list.keys[i].added_at)     entry["addedAt"]     = std::string(list.keys[i].added_at);
        result.push_back(entry);
    }

    lgx_free_keyring_list(list);
    return result;
}
