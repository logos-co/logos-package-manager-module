#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

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

    QString installPluginFile(const QString& pluginPath, bool isCoreModule, QString& errorMsg);
    
    QJsonArray getPackages();
    
    QJsonArray getPackages(const QString& category);
    
    QStringList getCategories();
    
    QStringList resolveDependencies(const QStringList& packageNames);
    
    bool installPackage(const QString& packageName);
    
    bool installPackages(const QStringList& packageNames);
    
    void installPackageAsync(const QString& packageName);
    void installPackagesAsync(const QStringList& packageNames);
    bool isInstalling() const { return m_isInstalling; }

    QJsonArray fetchPackageListFromOnline();
    bool downloadFile(const QString& url, const QString& destinationPath);
    
    QJsonObject findPackageByName(const QJsonArray& packages, const QString& packageName);
    QString currentPlatformVariant() const;
    
    bool extractLgxPackage(const QString& lgxPath, const QString& outputDir, QString& errorMsg);
    bool copyLibraryFromExtracted(const QString& extractedDir, const QString& targetDir, bool isCoreModule, QString& errorMsg);

signals:
    void pluginFileInstalled(const QString& pluginPath, bool isCoreModule);
    
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
        QStringList packageQueue;
        int currentPackageIndex;
    };
    AsyncInstallState m_asyncState;
    bool m_isInstalling;
    
    void startAsyncPackageListFetch();
    void startNextFileDownload();
    void finishAsyncInstallation(bool success, const QString& error);
    void startNextPackageInQueue();
    
    QStringList resolveDependenciesRecursive(const QString& packageName, const QJsonArray& allPackages, QSet<QString>& processed);
    QJsonArray filterPackagesByCategory(const QJsonArray& packages, const QString& category);
    QStringList extractCategories(const QJsonArray& packages);
};
