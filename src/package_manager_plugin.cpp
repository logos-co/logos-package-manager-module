#include "package_manager_plugin.h"
#include "lib/package_manager_lib.h"
#include <QDebug>
#include "logos_api_client.h"

PackageManagerPlugin::PackageManagerPlugin()
    : m_lib(nullptr)
{
    qDebug() << "PackageManagerPlugin created";
    qDebug() << "PackageManagerPlugin: LogosAPI initialized";
    
    m_lib = new PackageManagerLib(this);
    
    // Connect library signals to wrapper slots
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

// TODO: this isCoreModule will be removed
bool PackageManagerPlugin::installPlugin(const QString& pluginPath, bool isCoreModule)
{
    QString errorMsg;
    QString installedPath = m_lib->installPluginFile(pluginPath, isCoreModule, errorMsg);
    return !installedPath.isEmpty();
}

// TODO: this isCoreModule will be removed
void PackageManagerPlugin::onPluginFileInstalled(const QString& pluginPath, bool isCoreModule)
{
    // This is called by the library when a plugin file has been installed
    // Now we need to process it via LogosAPI (for core modules only)
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
    QVariant result = coreManagerClient->invokeRemoteMethod("core_manager_api", "processPlugin", pluginPath);
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

bool PackageManagerPlugin::installPackage(const QString& packageName, const QString& pluginsDirectory) 
{
    qDebug() << "Installing package:" << packageName;
    m_lib->setPluginsDirectory(pluginsDirectory);
    return m_lib->installPackage(packageName);
}

void PackageManagerPlugin::installPackageAsync(const QString& packageName, const QString& pluginsDirectory) 
{
    qDebug() << "Installing package async:" << packageName;
    m_lib->setPluginsDirectory(pluginsDirectory);
    m_lib->installPackageAsync(packageName);
}

// TODO: can likely be refactored to remove emitInstallationEvent
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
