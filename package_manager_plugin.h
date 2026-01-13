#pragma once

#include <QtCore/QObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QNetworkReply>
#include "package_manager_interface.h"
#include "logos_api.h"
#include "logos_api_client.h"

class PackageManagerPlugin : public QObject, public PackageManagerInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PackageManagerInterface_iid FILE "metadata.json")
    Q_INTERFACES(PackageManagerInterface PluginInterface)

public:
    PackageManagerPlugin();
    ~PackageManagerPlugin();

    // Implementation of PackageManagerInterface
    Q_INVOKABLE bool installPlugin(const QString& pluginPath) override;
    bool installPlugin(const QString& pluginPath, bool isCoreModule);
    QString name() const override { return "package_manager"; }
    QString version() const override { return "1.0.0"; }
    Q_INVOKABLE QJsonArray getPackages();
    Q_INVOKABLE void setPluginsDirectory(const QString& pluginsDirectory);
    Q_INVOKABLE void setUiPluginsDirectory(const QString& uiPluginsDirectory);
    Q_INVOKABLE bool installPackage(const QString& packageName, const QString& pluginsDirectory);
    Q_INVOKABLE void installPackageAsync(const QString& packageName, const QString& pluginsDirectory);

    // LogosAPI initialization
    Q_INVOKABLE void initLogos(LogosAPI* logosAPIInstance);
    
    // Test method
    Q_INVOKABLE QString testPluginCall(const QString& foo);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private slots:
    void onPackageListFetched();
    void onFileDownloaded();

private:
    QString m_pluginsDirectory;
    QString m_uiPluginsDirectory;
    QNetworkAccessManager* m_networkManager;
    
    struct AsyncInstallState {
        QString packageName;
        QString pluginsDirectory;
        QJsonObject packageObj;
        QStringList filesToDownload;
        QStringList downloadedFiles;
        int currentDownloadIndex;
        bool isCoreModule;
        QString tempDir;
        QString platformKey;
    };
    AsyncInstallState m_asyncState;
    bool m_isInstalling = false;
    
    QString getPlatformKey() const;
    QJsonArray fetchPackageListFromOnline();
    bool downloadFile(const QString& url, const QString& destinationPath);
    QJsonObject findPackageByName(const QJsonArray& packages, const QString& packageName);
    void startAsyncPackageListFetch();
    void startNextFileDownload();
    void finishAsyncInstallation(bool success, const QString& error);
    void emitInstallationEvent(const QString& packageName, bool success, const QString& error);
};
