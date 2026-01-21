#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Core package manager library without Logos API dependencies
// This library handles all the business logic for package management
class PackageManagerLib : public QObject
{
    Q_OBJECT

public:
    explicit PackageManagerLib(QObject* parent = nullptr);
    ~PackageManagerLib();

    // Directory management
    void setPluginsDirectory(const QString& pluginsDirectory);
    void setUiPluginsDirectory(const QString& uiPluginsDirectory);
    QString pluginsDirectory() const { return m_pluginsDirectory; }
    QString uiPluginsDirectory() const { return m_uiPluginsDirectory; }

    // Core installation operations
    // Returns the destination path where the plugin was installed, or empty string on failure
    QString installPluginFile(const QString& pluginPath, bool isCoreModule, QString& errorMsg);
    
    // Get list of packages with installation status
    QJsonArray getPackages();
    
    // Package operations (synchronous)
    bool installPackage(const QString& packageName);
    
    // Package operations (asynchronous)
    void installPackageAsync(const QString& packageName);
    bool isInstalling() const { return m_isInstalling; }

    // Network operations
    QJsonArray fetchPackageListFromOnline();
    bool downloadFile(const QString& url, const QString& destinationPath);
    
    // Utility methods
    QJsonObject findPackageByName(const QJsonArray& packages, const QString& packageName);
    QString currentPlatformVariant() const;
    
    // LGX operations
    bool extractLgxPackage(const QString& lgxPath, const QString& outputDir, QString& errorMsg);
    bool copyLibraryFromExtracted(const QString& extractedDir, const QString& targetDir, QString& errorMsg);

signals:
    // Emitted when a plugin file has been installed and needs to be processed by the core
    // (wrapper should call processPlugin via LogosAPI)
    void pluginFileInstalled(const QString& pluginPath, bool isCoreModule);
    
    // Emitted when async installation completes
    void installationFinished(const QString& packageName, bool success, const QString& error);

private slots:
    void onPackageListFetched();
    void onFileDownloaded();

private:
    QString m_pluginsDirectory;
    QString m_uiPluginsDirectory;
    QNetworkAccessManager* m_networkManager;
    
    struct AsyncInstallState {
        QString packageName;
        QJsonObject packageObj;
        QStringList filesToDownload;
        QStringList downloadedFiles;
        int currentDownloadIndex;
        bool isCoreModule;
        QString tempDir;
    };
    AsyncInstallState m_asyncState;
    bool m_isInstalling;
    
    void startAsyncPackageListFetch();
    void startNextFileDownload();
    void finishAsyncInstallation(bool success, const QString& error);
};
