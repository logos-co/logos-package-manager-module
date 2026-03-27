#include "package_manager_impl.h"
#include <package_manager_lib.h>
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

    QFileInfo fi(pluginPath);
    QVariantMap response;
    response["name"] = fi.completeBaseName();
    response["path"] = success ? QString::fromStdString(installedPluginPath) : QString();
    response["isCoreModule"] = isCoreModule;
    if (!success) {
        response["error"] = QString::fromStdString(errorMsg);
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

void PackageManagerImpl::setUserModulesDirectory(const QString& dir)
{
    m_lib->setUserModulesDirectory(dir.toStdString());
}

void PackageManagerImpl::setEmbeddedUiPluginsDirectory(const QString& dir)
{
    m_lib->setEmbeddedUiPluginsDirectory(dir.toStdString());
}

void PackageManagerImpl::setUserUiPluginsDirectory(const QString& dir)
{
    m_lib->setUserUiPluginsDirectory(dir.toStdString());
}
