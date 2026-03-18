#include "package_manager_impl.h"
#include "lib/package_manager_lib.h"

#include <QDebug>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

// Qt → native conversion helpers (internal to this module)
namespace {

std::string toStd(const QString& s) { return s.toStdString(); }
QString toQt(const std::string& s) { return QString::fromStdString(s); }

QStringList toQtList(const std::vector<std::string>& v)
{
    QStringList out;
    out.reserve(static_cast<int>(v.size()));
    for (const auto& s : v)
        out << QString::fromStdString(s);
    return out;
}

std::vector<std::string> toStdList(const QStringList& v)
{
    std::vector<std::string> out;
    out.reserve(v.size());
    for (const auto& s : v)
        out.push_back(s.toStdString());
    return out;
}

LogosValue jsonValueToLogosValue(const QJsonValue& v)
{
    if (v.isBool()) return LogosValue(v.toBool());
    if (v.isDouble()) return LogosValue(v.toDouble());
    if (v.isString()) return LogosValue(v.toString().toStdString());
    if (v.isArray()) {
        LogosValue::List list;
        for (const auto& item : v.toArray())
            list.push_back(jsonValueToLogosValue(item));
        return LogosValue(list);
    }
    if (v.isObject()) {
        LogosValue::Map map;
        QJsonObject obj = v.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            map[it.key().toStdString()] = jsonValueToLogosValue(it.value());
        return LogosValue(map);
    }
    return LogosValue();
}

LogosValue jsonArrayToLogosValue(const QJsonArray& arr)
{
    LogosValue::List list;
    list.reserve(arr.size());
    for (const auto& item : arr)
        list.push_back(jsonValueToLogosValue(item));
    return LogosValue(list);
}

} // anonymous namespace

PackageManagerImpl::PackageManagerImpl()
    : m_lib(nullptr)
{
    qDebug() << "PackageManagerImpl created (native provider API)";

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

void PackageManagerImpl::onInit(NativeLogosAPI* api)
{
    qDebug() << "PackageManagerImpl: NativeLogosAPI initialized (native provider API)";
}

bool PackageManagerImpl::installPlugin(const std::string& pluginPath, bool skipIfNotNewerVersion)
{
    QString errorMsg;
    QString installedPath = m_lib->installPluginFile(toQt(pluginPath), errorMsg, skipIfNotNewerVersion);
    return !installedPath.isEmpty();
}

void PackageManagerImpl::onPluginFileInstalled(const QString& pluginPath, bool isCoreModule)
{
    if (!isCoreModule) {
        return;
    }

    qDebug() << "Emitting corePluginFileInstalled event for:" << pluginPath;
    emitEvent("corePluginFileInstalled", {LogosValue(toStd(pluginPath))});
}

LogosValue PackageManagerImpl::getPackages()
{
    return jsonArrayToLogosValue(m_lib->getPackages());
}

LogosValue PackageManagerImpl::getPackages(const std::string& category)
{
    return jsonArrayToLogosValue(m_lib->getPackages(toQt(category)));
}

std::vector<std::string> PackageManagerImpl::getCategories()
{
    return toStdList(m_lib->getCategories());
}

std::vector<std::string> PackageManagerImpl::resolveDependencies(const std::vector<std::string>& packageNames)
{
    return toStdList(m_lib->resolveDependencies(toQtList(packageNames)));
}

bool PackageManagerImpl::installPackage(const std::string& packageName, const std::string& pluginsDirectory)
{
    qDebug() << "Installing package:" << toQt(packageName);
    m_lib->setPluginsDirectory(toQt(pluginsDirectory));
    return m_lib->installPackage(toQt(packageName));
}

bool PackageManagerImpl::installPackages(const std::vector<std::string>& packageNames, const std::string& pluginsDirectory)
{
    QStringList qtNames = toQtList(packageNames);
    qDebug() << "Installing packages:" << qtNames;
    m_lib->setPluginsDirectory(toQt(pluginsDirectory));
    return m_lib->installPackages(qtNames);
}

void PackageManagerImpl::installPackageAsync(const std::string& packageName, const std::string& pluginsDirectory)
{
    qDebug() << "Installing package async:" << toQt(packageName);
    m_lib->setPluginsDirectory(toQt(pluginsDirectory));
    m_lib->installPackageAsync(toQt(packageName));
}

void PackageManagerImpl::installPackagesAsync(const std::vector<std::string>& packageNames, const std::string& pluginsDirectory)
{
    QStringList qtNames = toQtList(packageNames);
    qDebug() << "Installing packages async:" << qtNames;
    m_lib->setPluginsDirectory(toQt(pluginsDirectory));
    m_lib->installPackagesAsync(qtNames);
}

void PackageManagerImpl::onInstallationFinished(const QString& packageName, bool success, const QString& error)
{
    emitInstallationEvent(packageName, success, error);
}

void PackageManagerImpl::emitInstallationEvent(const QString& packageName, bool success, const QString& error)
{
    qDebug() << "Emitting packageInstallationFinished event:" << packageName << success << error;
    emitEvent("packageInstallationFinished", {
        LogosValue(toStd(packageName)),
        LogosValue(success),
        LogosValue(toStd(error))
    });
}

std::string PackageManagerImpl::testPluginCall(const std::string& foo)
{
    qDebug() << "--------------------------------";
    qDebug() << "[NATIVE API] testPluginCall:" << toQt(foo);
    qDebug() << "--------------------------------";
    return "hello " + foo;
}

void PackageManagerImpl::testEvent(const std::string& message)
{
    qDebug() << "[NATIVE API] testEvent called with:" << toQt(message);

    std::string timestamp = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();

    qDebug() << "[NATIVE API] Emitting testEventResponse via emitEvent()";
    emitEvent("testEventResponse", {LogosValue(message), LogosValue(timestamp)});
}

void PackageManagerImpl::setPluginsDirectory(const std::string& pluginsDirectory)
{
    m_lib->setPluginsDirectory(toQt(pluginsDirectory));
}

void PackageManagerImpl::setUiPluginsDirectory(const std::string& uiPluginsDirectory)
{
    m_lib->setUiPluginsDirectory(toQt(uiPluginsDirectory));
}

void PackageManagerImpl::setRelease(const std::string& releaseTag)
{
    m_lib->setRelease(toQt(releaseTag));
}
