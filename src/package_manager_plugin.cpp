#include "package_manager_plugin.h"
#include <package_manager_lib.h>
#include <QDebug>
#include "logos_api_client.h"

PackageManagerPlugin::PackageManagerPlugin()
    : m_lib(nullptr)
{
    qDebug() << "PackageManagerPlugin created";
    qDebug() << "PackageManagerPlugin: LogosAPI initialized";
    
    m_lib = new PackageManagerLib(this);
    
    connect(m_lib, &PackageManagerLib::pluginFileInstalled,
            this, &PackageManagerPlugin::onPluginFileInstalled);
    connect(m_lib, &PackageManagerLib::installationFinished,
            this, &PackageManagerPlugin::onInstallationFinished);
}

PackageManagerPlugin::~PackageManagerPlugin() 
{
    if (logosAPI) {
        delete logosAPI;
        logosAPI = nullptr;
    }
}

bool PackageManagerPlugin::installPlugin(const QString& pluginPath)
{
    return installPlugin(pluginPath, true);
}

bool PackageManagerPlugin::installPlugin(const QString& pluginPath, bool isCoreModule)
{
    QString errorMsg;
    QString installedPath = m_lib->installPluginFile(pluginPath, isCoreModule, errorMsg);
    return !installedPath.isEmpty();
}

void PackageManagerPlugin::onPluginFileInstalled(const QString& pluginPath, bool isCoreModule)
{
    if (!isCoreModule) {
        return;
    }
    
    if (!logosAPI) {
        qWarning() << "Cannot process plugin: LogosAPI not initialized";
        return;
    }

    LogosAPIClient* coreManagerClient = logosAPI->getClient("core_manager");
    if (!coreManagerClient || !coreManagerClient->isConnected()) {
        qWarning() << "Failed to connect to Logos Core registry.";
        return;
    }

    qDebug() << "Calling processPlugin with path:" << pluginPath;
    QVariant result = coreManagerClient->invokeRemoteMethod("core_manager", "processPlugin", pluginPath);
    QString pluginName = result.toString();
    if (pluginName.isEmpty()) {
        qDebug() << "ERROR: --------------------------------";
        qWarning() << "Failed to process installed plugin:" << pluginPath;
    } else {
        qDebug() << "Successfully processed installed plugin:" << pluginName;
    }
}

QJsonArray PackageManagerPlugin::getPackages() 
{
    return m_lib->getPackages();
}

QJsonArray PackageManagerPlugin::getPackages(const QString& category)
{
    return m_lib->getPackages(category);
}

QStringList PackageManagerPlugin::getCategories()
{
    return m_lib->getCategories();
}

QStringList PackageManagerPlugin::resolveDependencies(const QStringList& packageNames)
{
    return m_lib->resolveDependencies(packageNames);
}

bool PackageManagerPlugin::installPackage(const QString& packageName, const QString& pluginsDirectory) 
{
    qDebug() << "Installing package:" << packageName;
    m_lib->setPluginsDirectory(pluginsDirectory);
    return m_lib->installPackage(packageName);
}

bool PackageManagerPlugin::installPackages(const QStringList& packageNames, const QString& pluginsDirectory)
{
    qDebug() << "Installing packages:" << packageNames;
    m_lib->setPluginsDirectory(pluginsDirectory);
    return m_lib->installPackages(packageNames);
}

void PackageManagerPlugin::installPackageAsync(const QString& packageName, const QString& pluginsDirectory) 
{
    qDebug() << "Installing package async:" << packageName;
    m_lib->setPluginsDirectory(pluginsDirectory);
    m_lib->installPackageAsync(packageName);
}

void PackageManagerPlugin::installPackagesAsync(const QStringList& packageNames, const QString& pluginsDirectory)
{
    qDebug() << "Installing packages async:" << packageNames;
    m_lib->setPluginsDirectory(pluginsDirectory);
    m_lib->installPackagesAsync(packageNames);
}

void PackageManagerPlugin::onInstallationFinished(const QString& packageName, bool success, const QString& error)
{
    emitInstallationEvent(packageName, success, error);
}

void PackageManagerPlugin::emitInstallationEvent(const QString& packageName, bool success, const QString& error) {
    if (!logosAPI) {
        qWarning() << "Cannot emit installation event: LogosAPI not initialized";
        return;
    }
    
    LogosAPIClient* client = logosAPI->getClient("package_manager");
    if (!client) {
        qWarning() << "Cannot emit installation event: package_manager client not available";
        return;
    }
    
    QVariantList eventData;
    eventData << packageName << success << error;
    
    qDebug() << "Emitting packageInstallationFinished event:" << packageName << success << error;
    client->onEventResponse(this, "packageInstallationFinished", eventData);
}

void PackageManagerPlugin::initLogos(LogosAPI* logosAPIInstance) {
    if (logosAPI) {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}

QString PackageManagerPlugin::testPluginCall(const QString& foo) {
    qDebug() << "--------------------------------";
    qDebug() << "testPluginCall: " << foo;
    qDebug() << "--------------------------------";
    return "hello " + foo;
}

void PackageManagerPlugin::setPluginsDirectory(const QString& pluginsDirectory) {
    m_lib->setPluginsDirectory(pluginsDirectory);
}

void PackageManagerPlugin::setUiPluginsDirectory(const QString& uiPluginsDirectory) {
    m_lib->setUiPluginsDirectory(uiPluginsDirectory);
}
