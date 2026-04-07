#include "package_manager_impl.h"
#include <package_manager_lib.h>
#include <lgx.h>
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>

PackageManagerImpl::PackageManagerImpl()
    : m_lib(nullptr)
{
    qDebug() << "PackageManagerImpl created (new provider API)";
    m_lib = new PackageManagerLib();
}

PackageManagerImpl::~PackageManagerImpl()
{
    delete m_lib;
    m_lib = nullptr;
}

void PackageManagerImpl::onInit(LogosAPI* api)
{
    qDebug() << "PackageManagerImpl: LogosAPI initialized (new provider API)";
}

QVariantMap PackageManagerImpl::installPlugin(const QString& pluginPath, bool skipIfNotNewerVersion)
{
    std::string errorMsg;
    std::string installedPluginPath;
    bool isCoreModule = false;
    std::string result = m_lib->installPluginFile(
        pluginPath.toStdString(), errorMsg, skipIfNotNewerVersion,
        &installedPluginPath, &isCoreModule
    );

    bool success = !result.empty();

    if (success && !installedPluginPath.empty()) {
        onPluginFileInstalled(QString::fromStdString(installedPluginPath), isCoreModule);
    }

    // Get signature info for the response
    auto sigResult = m_lib->verifyPackageSignature(pluginPath.toStdString());

    QFileInfo fi(pluginPath);
    QVariantMap response;
    response["name"] = fi.completeBaseName();
    response["path"] = success ? QString::fromStdString(installedPluginPath) : QString();
    response["isCoreModule"] = isCoreModule;
    if (!success) {
        response["error"] = QString::fromStdString(errorMsg);
    }

    // Add signature info
    if (sigResult.is_signed) {
        response["signatureStatus"] = sigResult.signature_valid && sigResult.package_valid
            ? QString("signed") : QString("invalid");
        response["signerDid"] = QString::fromStdString(sigResult.signer_did);
        if (!sigResult.signer_name.empty()) {
            response["signerName"] = QString::fromStdString(sigResult.signer_name);
        }
        if (!sigResult.signer_url.empty()) {
            response["signerUrl"] = QString::fromStdString(sigResult.signer_url);
        }
        if (!sigResult.trusted_as.empty()) {
            response["trustedAs"] = QString::fromStdString(sigResult.trusted_as);
        }
    } else {
        response["signatureStatus"] = QString("unsigned");
    }

    return response;
}

void PackageManagerImpl::onPluginFileInstalled(const QString& pluginPath, bool isCoreModule)
{
    QVariantList eventData;
    eventData << pluginPath;

    if (isCoreModule) {
        qDebug() << "Emitting corePluginFileInstalled event for:" << pluginPath;
        emitEvent("corePluginFileInstalled", eventData);
    } else {
        qDebug() << "Emitting uiPluginFileInstalled event for:" << pluginPath;
        emitEvent("uiPluginFileInstalled", eventData);
    }
}

static QVariantList jsonStringToVariantList(const std::string& jsonStr)
{
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(jsonStr));
    return doc.array().toVariantList();
}

QVariantList PackageManagerImpl::getInstalledPackages()
{
    return jsonStringToVariantList(m_lib->getInstalledPackages());
}

QVariantList PackageManagerImpl::getInstalledModules()
{
    return jsonStringToVariantList(m_lib->getInstalledModules());
}

QVariantList PackageManagerImpl::getInstalledUiPlugins()
{
    return jsonStringToVariantList(m_lib->getInstalledUiPlugins());
}

QStringList PackageManagerImpl::getValidVariants()
{
    QStringList result;
    for (const auto& v : PackageManagerLib::platformVariantsToTry()) {
        result << QString::fromStdString(v);
    }
    return result;
}

void PackageManagerImpl::setEmbeddedModulesDirectory(const QString& dir)
{
    m_lib->setEmbeddedModulesDirectory(dir.toStdString());
}

void PackageManagerImpl::addEmbeddedModulesDirectory(const QString& dir)
{
    m_lib->addEmbeddedModulesDirectory(dir.toStdString());
}

void PackageManagerImpl::setEmbeddedUiPluginsDirectory(const QString& dir)
{
    m_lib->setEmbeddedUiPluginsDirectory(dir.toStdString());
}

void PackageManagerImpl::addEmbeddedUiPluginsDirectory(const QString& dir)
{
    m_lib->addEmbeddedUiPluginsDirectory(dir.toStdString());
}

void PackageManagerImpl::setUserModulesDirectory(const QString& dir)
{
    m_lib->setUserModulesDirectory(dir.toStdString());
}

void PackageManagerImpl::setUserUiPluginsDirectory(const QString& dir)
{
    m_lib->setUserUiPluginsDirectory(dir.toStdString());
}

void PackageManagerImpl::setSignaturePolicy(const QString& policy)
{
    std::string p = policy.toLower().toStdString();
    if (p == "none") m_lib->setSignaturePolicy(SignaturePolicy::NONE);
    else if (p == "warn") m_lib->setSignaturePolicy(SignaturePolicy::WARN);
    else if (p == "require") m_lib->setSignaturePolicy(SignaturePolicy::REQUIRE);
}

void PackageManagerImpl::setKeyringDirectory(const QString& dir)
{
    m_lib->setKeyringDirectory(dir.toStdString());
}

QVariantMap PackageManagerImpl::verifyPackage(const QString& lgxPath)
{
    auto result = m_lib->verifyPackageSignature(lgxPath.toStdString());

    QVariantMap response;
    response["isSigned"] = result.is_signed;
    response["signatureValid"] = result.signature_valid;
    response["packageValid"] = result.package_valid;
    response["signerDid"] = QString::fromStdString(result.signer_did);
    response["signerName"] = QString::fromStdString(result.signer_name);
    response["signerUrl"] = QString::fromStdString(result.signer_url);
    response["trustedAs"] = QString::fromStdString(result.trusted_as);
    if (!result.error.empty()) {
        response["error"] = QString::fromStdString(result.error);
    }
    return response;
}

QVariantMap PackageManagerImpl::addTrustedKey(const QString& name, const QString& did,
                                               const QString& displayName, const QString& url)
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_result_t result = lgx_keyring_add(
        keyringDirPtr,
        name.toStdString().c_str(),
        did.toStdString().c_str(),
        displayName.isEmpty() ? nullptr : displayName.toStdString().c_str(),
        url.isEmpty() ? nullptr : url.toStdString().c_str()
    );

    QVariantMap response;
    response["success"] = result.success;
    if (!result.success && result.error) {
        response["error"] = QString::fromUtf8(result.error);
    }
    return response;
}

QVariantMap PackageManagerImpl::removeTrustedKey(const QString& name)
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_result_t result = lgx_keyring_remove(
        keyringDirPtr,
        name.toStdString().c_str()
    );

    QVariantMap response;
    response["success"] = result.success;
    if (!result.success && result.error) {
        response["error"] = QString::fromUtf8(result.error);
    }
    return response;
}

QVariantList PackageManagerImpl::listTrustedKeys()
{
    std::string keyringDir = m_lib->keyringDirectory();
    const char* keyringDirPtr = keyringDir.empty() ? nullptr : keyringDir.c_str();

    lgx_keyring_list_t list = lgx_keyring_list(keyringDirPtr);

    QVariantList result;
    for (size_t i = 0; i < list.count; ++i) {
        QVariantMap entry;
        if (list.keys[i].name) entry["name"] = QString::fromUtf8(list.keys[i].name);
        if (list.keys[i].did) entry["did"] = QString::fromUtf8(list.keys[i].did);
        if (list.keys[i].display_name) entry["displayName"] = QString::fromUtf8(list.keys[i].display_name);
        if (list.keys[i].url) entry["url"] = QString::fromUtf8(list.keys[i].url);
        if (list.keys[i].added_at) entry["addedAt"] = QString::fromUtf8(list.keys[i].added_at);
        result.append(entry);
    }

    lgx_free_keyring_list(list);
    return result;
}
