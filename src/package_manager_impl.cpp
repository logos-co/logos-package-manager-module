#include "package_manager_impl.h"
#include "lib/package_manager_lib.h"
#include <QDebug>
#include <QDateTime>

PackageManagerImpl::PackageManagerImpl()
    : m_lib(nullptr)
{
    qDebug() << "PackageManagerImpl created (new provider API)";

    m_lib = new PackageManagerLib(nullptr);

    QObject::connect(m_lib, &PackageManagerLib::pluginFileInstalled,
        m_lib, [this](const QString& pluginPath, bool isCoreModule) {
            onPluginFileInstalled(pluginPath, isCoreModule);
        });
    QObject::connect(m_lib, &PackageManagerLib::installationFinished,
        m_lib, [this](const QString& packageName, bool success, const QString& error) {
            onInstallationFinished(packageName, success, error);
        });
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

bool PackageManagerImpl::installPlugin(const QString& pluginPath, bool skipIfNotNewerVersion)
{
    QString errorMsg;
    QString installedPath = m_lib->installPluginFile(pluginPath, errorMsg, skipIfNotNewerVersion);
    return !installedPath.isEmpty();
}

void PackageManagerImpl::onPluginFileInstalled(const QString& pluginPath, bool isCoreModule)
{
    if (!isCoreModule) {
        return;
    }

    qDebug() << "Emitting corePluginFileInstalled event for:" << pluginPath;
    QVariantList eventData;
    eventData << pluginPath;
    emitEvent("corePluginFileInstalled", eventData);
}

QJsonArray PackageManagerImpl::getPackages()
{
    return m_lib->getPackages();
}

QJsonArray PackageManagerImpl::getPackages(const QString& category)
{
    return m_lib->getPackages(category);
}

QStringList PackageManagerImpl::getCategories()
{
    return m_lib->getCategories();
}

QStringList PackageManagerImpl::resolveDependencies(const QStringList& packageNames)
{
    return m_lib->resolveDependencies(packageNames);
}

bool PackageManagerImpl::installPackage(const QString& packageName, const QString& pluginsDirectory)
{
    qDebug() << "Installing package:" << packageName;
    m_lib->setPluginsDirectory(pluginsDirectory);
    return m_lib->installPackage(packageName);
}

bool PackageManagerImpl::installPackages(const QStringList& packageNames, const QString& pluginsDirectory)
{
    qDebug() << "Installing packages:" << packageNames;
    m_lib->setPluginsDirectory(pluginsDirectory);
    return m_lib->installPackages(packageNames);
}

void PackageManagerImpl::installPackageAsync(const QString& packageName, const QString& pluginsDirectory)
{
    qDebug() << "Installing package async:" << packageName;
    m_lib->setPluginsDirectory(pluginsDirectory);
    m_lib->installPackageAsync(packageName);
}

void PackageManagerImpl::installPackagesAsync(const QStringList& packageNames, const QString& pluginsDirectory)
{
    qDebug() << "Installing packages async:" << packageNames;
    m_lib->setPluginsDirectory(pluginsDirectory);
    m_lib->installPackagesAsync(packageNames);
}

void PackageManagerImpl::onInstallationFinished(const QString& packageName, bool success, const QString& error)
{
    emitInstallationEvent(packageName, success, error);
}

void PackageManagerImpl::emitInstallationEvent(const QString& packageName, bool success, const QString& error)
{
    QVariantList eventData;
    eventData << packageName << success << error;

    qDebug() << "Emitting packageInstallationFinished event:" << packageName << success << error;
    emitEvent("packageInstallationFinished", eventData);
}

QString PackageManagerImpl::testPluginCall(const QString& foo)
{
    qDebug() << "--------------------------------";
    qDebug() << "[NEW] testPluginCall: " << foo;
    qDebug() << "--------------------------------";
    return "hello " + foo;
}

void PackageManagerImpl::testEvent(const QString& message)
{
    qDebug() << "[NEW] [LogosProviderObject] testEvent called with:" << message;

    QVariantList eventData;
    eventData << message << QDateTime::currentDateTime().toString(Qt::ISODate);

    qDebug() << "[LogosProviderObject] Emitting testEventResponse via emitEvent()";
    emitEvent("testEventResponse", eventData);
}

void PackageManagerImpl::setPluginsDirectory(const QString& pluginsDirectory)
{
    m_lib->setPluginsDirectory(pluginsDirectory);
}

void PackageManagerImpl::setUiPluginsDirectory(const QString& uiPluginsDirectory)
{
    m_lib->setUiPluginsDirectory(uiPluginsDirectory);
}

void PackageManagerImpl::setRelease(const QString& releaseTag)
{
    m_lib->setRelease(releaseTag);
}
