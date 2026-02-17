#include "package_manager_lib.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPluginLoader>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QEventLoop>
#include <QStandardPaths>
#include <QUrl>
#include <QTemporaryDir>
#include <algorithm>
#include "lgx.h"

static const QString MODULES_DOWNLOAD_BASE_URL = QStringLiteral("https://github.com/logos-co/logos-modules/releases/latest/download");

PackageManagerLib::PackageManagerLib(QObject* parent)
    : QObject(parent)
    , m_networkManager(nullptr)
    , m_isInstalling(false)
{
    qDebug() << "PackageManagerLib created";
    m_networkManager = new QNetworkAccessManager(this);
}

PackageManagerLib::~PackageManagerLib()
{
    qDebug() << "PackageManagerLib destroyed";
}

void PackageManagerLib::setPluginsDirectory(const QString& pluginsDirectory)
{
    m_pluginsDirectory = pluginsDirectory;
    qDebug() << "Set plugins directory to:" << m_pluginsDirectory;
}

void PackageManagerLib::setUiPluginsDirectory(const QString& uiPluginsDirectory)
{
    m_uiPluginsDirectory = uiPluginsDirectory;
    qDebug() << "Set UI plugins directory to:" << m_uiPluginsDirectory;
}

QString PackageManagerLib::installPluginFile(const QString& pluginPath, bool isCoreModule, QString& errorMsg)
{
    qDebug() << "PackageManagerLib: Installing plugin file:" << pluginPath << "(isCoreModule:" << isCoreModule << ")";

    QFileInfo sourceFileInfo(pluginPath);
    if (!sourceFileInfo.exists() || !sourceFileInfo.isFile()) {
        errorMsg = "Source plugin file does not exist or is not a file: " + pluginPath;
        qWarning() << errorMsg;
        return QString();
    }

    QString pluginsDirectory;
    if (isCoreModule) {
        pluginsDirectory = m_pluginsDirectory.isEmpty()
            ? QDir::cleanPath(QCoreApplication::applicationDirPath() + "/bin/modules")
            : m_pluginsDirectory;
    } else {
        pluginsDirectory = m_uiPluginsDirectory;
        if (pluginsDirectory.isEmpty()) {
            QString modulesDir = m_pluginsDirectory.isEmpty()
                ? QDir::cleanPath(QCoreApplication::applicationDirPath() + "/bin/modules")
                : m_pluginsDirectory;
            QDir modulesDirObj(modulesDir);
            modulesDirObj.cdUp();
            pluginsDirectory = modulesDirObj.filePath("plugins");
        }
    }
    qDebug() << "Plugins directory:" << pluginsDirectory;

    if (pluginsDirectory.isEmpty()) {
        errorMsg = "Plugins directory is not set. Cannot install plugin.";
        qWarning() << errorMsg;
        return QString();
    }

    QDir pluginsDir(pluginsDirectory);
    if (!pluginsDir.exists()) {
        qDebug() << "Creating plugins directory:" << pluginsDirectory;
        if (!pluginsDir.mkpath(".")) {
            errorMsg = "Failed to create plugins directory: " + pluginsDirectory;
            qWarning() << errorMsg;
            return QString();
        }
    }

    if (sourceFileInfo.suffix().toLower() == "lgx") {
        qDebug() << "Installing LGX package:" << pluginPath;
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            errorMsg = "Failed to create temporary directory for LGX extraction";
            qWarning() << errorMsg;
            return QString();
        }
        
        if (!extractLgxPackage(pluginPath, tempDir.path(), errorMsg)) {
            qWarning() << "Failed to extract LGX package:" << errorMsg;
            return QString();
        }
        
        if (!copyLibraryFromExtracted(tempDir.path(), pluginsDirectory, isCoreModule, errorMsg)) {
            qWarning() << "Failed to copy libraries from extracted LGX package:" << errorMsg;
            return QString();
        }
        
        qDebug() << "Successfully installed plugin from LGX package to:" << pluginsDirectory;
        
        // Emit signal for each installed library file
        QString libExtension;
#if defined(Q_OS_MAC)
        libExtension = ".dylib";
#elif defined(Q_OS_WIN)
        libExtension = ".dll";
#else
        libExtension = ".so";
#endif

        // Both core modules and UI plugins use subdirectory structure
        {
            QDir dir(pluginsDirectory);
            QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& subdir : subdirs) {
                QString libPath = pluginsDirectory + "/" + subdir + "/" + subdir + libExtension;
                if (QFile::exists(libPath)) {
                    emit pluginFileInstalled(libPath, isCoreModule);
                }
            }
        }
        
        return pluginsDirectory;
    }

    QString fileName = sourceFileInfo.fileName();
    QString destinationPath;
    QDir destDir;
    
    if (isCoreModule) {
        // Core modules: flat structure
        destinationPath = pluginsDir.filePath(fileName);
        destDir = pluginsDir;
    } else {
        // UI plugins: subdirectory structure
        QString pluginName = sourceFileInfo.completeBaseName();
        QString pluginSubDir = pluginsDirectory + "/" + pluginName;
        QDir().mkpath(pluginSubDir);
        destinationPath = pluginSubDir + "/" + fileName;
        destDir = QDir(pluginSubDir);
    }

    QFileInfo destFileInfo(destinationPath);
    if (destFileInfo.exists()) {
        qDebug() << "Plugin already exists at destination. Overwriting:" << destinationPath;
        QFile destFile(destinationPath);
        if (!destFile.remove()) {
            errorMsg = "Failed to remove existing plugin file: " + destinationPath;
            qWarning() << errorMsg;
            return QString();
        }
    }

    QFile sourceFile(pluginPath);
    if (!sourceFile.copy(destinationPath)) {
        errorMsg = "Failed to copy plugin file to plugins directory: " + sourceFile.errorString();
        qWarning() << errorMsg;
        return QString();
    }

    qDebug() << "Successfully installed plugin:" << fileName << "to" << destinationPath;
    
    QPluginLoader loader(pluginPath);
    QJsonObject metadata = loader.metaData();
    if (!metadata.isEmpty()) {
        QJsonObject metaDataObj = metadata.value("MetaData").toObject();
        QJsonArray includeFiles = metaDataObj.value("include").toArray();
        
        if (!includeFiles.isEmpty()) {
            qDebug() << "Plugin has" << includeFiles.size() << "included files to copy";
            
            QDir sourceDir = sourceFileInfo.dir();
            
            for (const QJsonValue& includeVal : includeFiles) {
                QString includeFileName = includeVal.toString();
                if (includeFileName.isEmpty()) continue;
                
                QString sourceIncludePath = sourceDir.filePath(includeFileName);
                QString destIncludePath = destDir.filePath(includeFileName);
                
                qDebug() << "Checking for included file:" << sourceIncludePath;
                
                QFileInfo includeFileInfo(sourceIncludePath);
                if (includeFileInfo.exists() && includeFileInfo.isFile()) {
                    qDebug() << "Found included file:" << sourceIncludePath;
                    
                    QFileInfo destIncludeFileInfo(destIncludePath);
                    if (destIncludeFileInfo.exists()) {
                        qDebug() << "Included file already exists at destination. Overwriting:" << destIncludePath;
                        QFile destIncludeFile(destIncludePath);
                        if (!destIncludeFile.remove()) {
                            qWarning() << "Failed to remove existing included file:" << destIncludePath;
                        }
                    }
                    
                    QFile includeFile(sourceIncludePath);
                    if (includeFile.copy(destIncludePath)) {
                        qDebug() << "Successfully copied included file:" << includeFileName;
                    } else {
                        qWarning() << "Failed to copy included file:" << includeFileName 
                                  << "-" << includeFile.errorString();
                    }
                } else {
                    qDebug() << "Included file not found:" << sourceIncludePath;
                }
            }
        }
    }
    
    emit pluginFileInstalled(destinationPath, isCoreModule);
    
    return destinationPath;
}

QJsonArray PackageManagerLib::getPackages()
{
    QJsonArray packagesArray;

    QJsonArray onlinePackages = fetchPackageListFromOnline();
    if (onlinePackages.isEmpty()) {
        qWarning() << "Failed to fetch packages from online source or no packages available";
        return packagesArray;
    }

    QString modulesDirPath = m_pluginsDirectory;
    if (modulesDirPath.isEmpty()) {
        modulesDirPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/bin/modules");
    }
    QDir modulesDir(modulesDirPath);
    bool checkModulesInstalled = modulesDir.exists();

    QString pluginsDirPath = m_uiPluginsDirectory;
    if (pluginsDirPath.isEmpty()) {
        QDir modulesDirObj(modulesDirPath);
        modulesDirObj.cdUp();
        pluginsDirPath = modulesDirObj.filePath("plugins");
    }
    QDir pluginsDir(pluginsDirPath);
    bool checkPluginsInstalled = pluginsDir.exists();

    for (const QJsonValue& packageVal : onlinePackages) {
        QJsonObject packageObj = packageVal.toObject();
        QString packageName = packageObj.value("name").toString();
        QString packageType = packageObj.value("type").toString();
        QString packageFile = packageObj.value("package").toString();
        QString moduleName = packageObj.value("moduleName").toString();
        
        if (packageFile.isEmpty()) {
            qWarning() << "Package" << packageName << "has no package file specified";
            continue;
        }

        bool isInstalled = false;
        bool isCoreModule = (packageType != "ui");

        QString libExtension;
#if defined(Q_OS_MAC)
        libExtension = ".dylib";
#elif defined(Q_OS_WIN)
        libExtension = ".dll";
#else
        libExtension = ".so";
#endif

        if (isCoreModule && checkModulesInstalled) {
            // Core modules: check subdirectory modules/<moduleName>/<moduleName>.<ext>
            QString modulePath = modulesDirPath + "/" + moduleName + "/" + moduleName + libExtension;
            isInstalled = QFile::exists(modulePath);
        } else if (!isCoreModule && checkPluginsInstalled) {
            // UI plugins: check in subdirectory plugins/<moduleName>/<moduleName>.<ext>
            QString pluginPath = pluginsDirPath + "/" + moduleName + "/" + moduleName + libExtension;
            isInstalled = QFile::exists(pluginPath);
        }

        QJsonObject resultPackage;
        resultPackage["name"] = packageName;
        resultPackage["description"] = packageObj.value("description").toString();
        resultPackage["type"] = packageType;
        resultPackage["moduleName"] = moduleName;
        resultPackage["category"] = packageObj.value("category").toString();
        resultPackage["author"] = packageObj.value("author").toString();
        resultPackage["dependencies"] = packageObj.value("dependencies").toArray();
        resultPackage["package"] = packageFile;
        resultPackage["installed"] = isInstalled;
        packagesArray.append(resultPackage);
    }

    qDebug() << "Found" << packagesArray.size() << "packages";
    return packagesArray;
}

QJsonArray PackageManagerLib::getPackages(const QString& category)
{
    QJsonArray allPackages = getPackages();
    
    if (category.isEmpty() || category.compare("All", Qt::CaseInsensitive) == 0) {
        return allPackages;
    }
    
    return filterPackagesByCategory(allPackages, category);
}

QStringList PackageManagerLib::getCategories()
{
    QJsonArray packages = fetchPackageListFromOnline();
    return extractCategories(packages);
}

QStringList PackageManagerLib::resolveDependencies(const QStringList& packageNames)
{
    QJsonArray allPackages = fetchPackageListFromOnline();
    if (allPackages.isEmpty()) {
        qWarning() << "Failed to fetch package list for dependency resolution";
        return packageNames;
    }
    
    QSet<QString> processed;
    QStringList result;
    
    for (const QString& packageName : packageNames) {
        QStringList deps = resolveDependenciesRecursive(packageName, allPackages, processed);
        for (const QString& dep : deps) {
            if (!result.contains(dep)) {
                result.append(dep);
            }
        }
    }
    
    return result;
}

bool PackageManagerLib::installPackages(const QStringList& packageNames)
{
    if (packageNames.isEmpty()) {
        qWarning() << "No packages to install";
        return false;
    }
    
    // Resolve dependencies
    QStringList packagesToInstall = resolveDependencies(packageNames);
    
    qDebug() << "Installing packages with dependencies:" << packagesToInstall;
    
    bool allSuccess = true;
    for (const QString& packageName : packagesToInstall) {
        if (!installPackage(packageName)) {
            qWarning() << "Failed to install package:" << packageName;
            allSuccess = false;
        }
    }
    
    return allSuccess;
}

bool PackageManagerLib::installPackage(const QString& packageName)
{
    qDebug() << "Installing package:" << packageName;
    
    QJsonArray packages = fetchPackageListFromOnline();
    if (packages.isEmpty()) {
        qWarning() << "Failed to fetch package list or package list is empty";
        return false;
    }
    
    QJsonObject packageObj = findPackageByName(packages, packageName);
    if (packageObj.isEmpty()) {
        qWarning() << "Package not found:" << packageName;
        return false;
    }
    
    QString packageType = packageObj.value("type").toString();
    bool isCoreModule = (packageType != "ui");
    
    if (!isCoreModule && m_uiPluginsDirectory.isEmpty()) {
        QDir modulesDir(m_pluginsDirectory.isEmpty() 
            ? QDir::cleanPath(QCoreApplication::applicationDirPath() + "/bin/modules")
            : m_pluginsDirectory);
        modulesDir.cdUp();
        m_uiPluginsDirectory = modulesDir.filePath("plugins");
        qDebug() << "Auto-derived UI plugins directory:" << m_uiPluginsDirectory;
    }
    
    QString packageFile = packageObj.value("package").toString();
    if (packageFile.isEmpty()) {
        qWarning() << "Package" << packageName << "has no package file specified";
        return false;
    }
    
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        qWarning() << "Failed to get temp directory";
        return false;
    }
    
    // Download the LGX package file
    QString downloadUrl = QString("%1/%2").arg(MODULES_DOWNLOAD_BASE_URL, packageFile);
    QString destinationPath = QDir(tempDir).filePath(packageFile);
    
    qDebug() << "Downloading package file:" << packageFile;
    if (!downloadFile(downloadUrl, destinationPath)) {
        qWarning() << "Failed to download package file:" << packageFile;
        return false;
    }
    
    // Install the LGX package (installPluginFile handles LGX extraction)
    qDebug() << "Installing downloaded package:" << destinationPath << "(type:" << packageType << ")";
    QString errorMsg;
    QString installedPath = installPluginFile(destinationPath, isCoreModule, errorMsg);
    
    // Clean up temp file
    QFile::remove(destinationPath);
    qDebug() << "Cleaned up temp file:" << destinationPath;
    
    if (installedPath.isEmpty()) {
        qWarning() << "Failed to install package:" << packageName << "-" << errorMsg;
        return false;
    }
    
    qDebug() << "Successfully installed package:" << packageName << "(type:" << packageType << ")";
    return true;
}

void PackageManagerLib::installPackageAsync(const QString& packageName)
{
    installPackagesAsync(QStringList() << packageName);
}

void PackageManagerLib::installPackagesAsync(const QStringList& packageNames)
{
    qDebug() << "Installing packages async:" << packageNames;
    
    if (packageNames.isEmpty()) {
        qWarning() << "No packages to install";
        emit installationFinished("", false, "No packages to install");
        return;
    }
    
    // If already installing, add request to queue and return
    if (m_isInstalling) {
        qDebug() << "Installation in progress, queuing packages:" << packageNames;
        m_requestQueue.enqueue(packageNames);
        return;
    }
    
    m_isInstalling = true;
    
    QStringList packagesToInstall = resolveDependencies(packageNames);
    qDebug() << "Packages to install (with dependencies):" << packagesToInstall;
    
    m_asyncState.packageQueue = packagesToInstall;
    m_asyncState.currentPackageIndex = 0;
    m_asyncState.filesToDownload.clear();
    m_asyncState.downloadedFiles.clear();
    m_asyncState.currentDownloadIndex = 0;
    m_asyncState.tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    
    if (m_asyncState.tempDir.isEmpty()) {
        finishAsyncInstallation(false, "Failed to get temp directory");
        return;
    }
    
    startNextPackageInQueue();
}

void PackageManagerLib::startNextPackageInQueue()
{
    if (m_asyncState.currentPackageIndex >= m_asyncState.packageQueue.size()) {
        // All packages in current batch are done, clean up and process next request
        m_isInstalling = false;
        m_asyncState.packageName.clear();
        m_asyncState.packageQueue.clear();
        m_asyncState.currentPackageIndex = 0;
        processNextRequestInQueue();
        return;
    }
    
    QString packageName = m_asyncState.packageQueue[m_asyncState.currentPackageIndex];
    m_asyncState.packageName = packageName;
    
    qDebug() << "Starting installation for package:" << packageName 
             << "(" << (m_asyncState.currentPackageIndex + 1) << "/" << m_asyncState.packageQueue.size() << ")";
    
    startAsyncPackageListFetch();
}

void PackageManagerLib::startAsyncPackageListFetch()
{
    if (!m_networkManager) {
        finishAsyncInstallation(false, "Network manager not initialized");
        return;
    }
    
    QString urlString = QString("%1/list.json").arg(MODULES_DOWNLOAD_BASE_URL);
    QUrl url(urlString);
    QNetworkRequest request(url);
    
    qDebug() << "Async: Fetching package list from:" << urlString;
    
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &PackageManagerLib::onPackageListFetched);
}

void PackageManagerLib::onPackageListFetched()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishAsyncInstallation(false, "Invalid network reply");
        return;
    }
    
    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        reply->deleteLater();
        finishAsyncInstallation(false, "Failed to fetch package list: " + error);
        return;
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        finishAsyncInstallation(false, "Failed to parse package list JSON: " + parseError.errorString());
        return;
    }
    
    if (!doc.isArray()) {
        finishAsyncInstallation(false, "Package list JSON is not an array");
        return;
    }
    
    QJsonArray packages = doc.array();
    qDebug() << "Async: Fetched" << packages.size() << "packages";
    
    m_asyncState.packageObj = findPackageByName(packages, m_asyncState.packageName);
    if (m_asyncState.packageObj.isEmpty()) {
        finishAsyncInstallation(false, "Package not found: " + m_asyncState.packageName);
        return;
    }
    
    QString packageType = m_asyncState.packageObj.value("type").toString();
    m_asyncState.isCoreModule = (packageType != "ui");
    
    if (!m_asyncState.isCoreModule && m_uiPluginsDirectory.isEmpty()) {
        QDir modulesDir(m_pluginsDirectory.isEmpty() 
            ? QDir::cleanPath(QCoreApplication::applicationDirPath() + "/bin/modules")
            : m_pluginsDirectory);
        modulesDir.cdUp();
        m_uiPluginsDirectory = modulesDir.filePath("plugins");
        qDebug() << "Auto-derived UI plugins directory:" << m_uiPluginsDirectory;
    }
    
    QString packageFile = m_asyncState.packageObj.value("package").toString();
    if (packageFile.isEmpty()) {
        finishAsyncInstallation(false, "Package has no package file specified");
        return;
    }
    
    m_asyncState.filesToDownload.append(packageFile);
    
    qDebug() << "Async: Need to download package file:" << packageFile;
    
    m_asyncState.currentDownloadIndex = 0;
    startNextFileDownload();
}

void PackageManagerLib::startNextFileDownload()
{
    if (m_asyncState.currentDownloadIndex >= m_asyncState.filesToDownload.size()) {
        qDebug() << "Async: All files downloaded, installing...";
        
        bool allInstalled = true;
        QString packageType = m_asyncState.packageObj.value("type").toString();
        
        for (const QString& downloadedFile : m_asyncState.downloadedFiles) {
            qDebug() << "Installing downloaded file:" << downloadedFile << "(type:" << packageType << ")";
            QString errorMsg;
            QString installedPath = installPluginFile(downloadedFile, m_asyncState.isCoreModule, errorMsg);
            if (installedPath.isEmpty()) {
                qWarning() << "Failed to install file:" << downloadedFile << "-" << errorMsg;
                allInstalled = false;
            }
        }
        
        for (const QString& downloadedFile : m_asyncState.downloadedFiles) {
            QFile::remove(downloadedFile);
            qDebug() << "Cleaned up temp file:" << downloadedFile;
        }
        
        QString currentPackage = m_asyncState.packageName;
        emit installationFinished(currentPackage, allInstalled, allInstalled ? "" : "Some files failed to install");
        
        m_asyncState.filesToDownload.clear();
        m_asyncState.downloadedFiles.clear();
        m_asyncState.currentDownloadIndex = 0;
        m_asyncState.currentPackageIndex++;
        
        startNextPackageInQueue();
        return;
    }
    
    QString fileName = m_asyncState.filesToDownload[m_asyncState.currentDownloadIndex];
    QString downloadUrl = QString("%1/%2").arg(MODULES_DOWNLOAD_BASE_URL, fileName);
    
    qDebug() << "Async: Downloading package file:" << fileName;
    
    QUrl url(downloadUrl);
    QNetworkRequest request(url);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &PackageManagerLib::onFileDownloaded);
}

void PackageManagerLib::onFileDownloaded()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishAsyncInstallation(false, "Invalid network reply during download");
        return;
    }
    
    QString fileName = m_asyncState.filesToDownload[m_asyncState.currentDownloadIndex];
    
    if (reply->error() != QNetworkReply::NoError) {
        QString error = reply->errorString();
        reply->deleteLater();
        
        for (const QString& downloadedFile : m_asyncState.downloadedFiles) {
            QFile::remove(downloadedFile);
        }
        
        finishAsyncInstallation(false, "Failed to download " + fileName + ": " + error);
        return;
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QString destinationPath = QDir(m_asyncState.tempDir).filePath(fileName);
    
    QFileInfo fileInfo(destinationPath);
    QDir destDir = fileInfo.dir();
    if (!destDir.exists()) {
        if (!destDir.mkpath(".")) {
            finishAsyncInstallation(false, "Failed to create destination directory");
            return;
        }
    }
    
    QFile file(destinationPath);
    if (!file.open(QIODevice::WriteOnly)) {
        finishAsyncInstallation(false, "Failed to open file for writing: " + destinationPath);
        return;
    }
    
    qint64 bytesWritten = file.write(data);
    file.close();
    
    if (bytesWritten != data.size()) {
        finishAsyncInstallation(false, "Failed to write all data to file");
        return;
    }
    
    qDebug() << "Async: Downloaded file:" << destinationPath << "(" << bytesWritten << "bytes)";
    
    m_asyncState.downloadedFiles.append(destinationPath);
    m_asyncState.currentDownloadIndex++;
    
    startNextFileDownload();
}

void PackageManagerLib::finishAsyncInstallation(bool success, const QString& error)
{
    QString packageName = m_asyncState.packageName;
    
    m_isInstalling = false;
    m_asyncState.packageName.clear();
    m_asyncState.filesToDownload.clear();
    m_asyncState.downloadedFiles.clear();
    m_asyncState.packageQueue.clear();
    m_asyncState.currentPackageIndex = 0;
    
    if (success) {
        qDebug() << "Async: Successfully completed installation";
    } else {
        qWarning() << "Async: Installation failed:" << error;
    }
    
    emit installationFinished(packageName, success, error);
    
    // Process next queued installation if any
    processNextRequestInQueue();
}

void PackageManagerLib::processNextRequestInQueue()
{
    if (m_requestQueue.isEmpty()) {
        qDebug() << "No more packages in queue";
        return;
    }
    
    qDebug() << "Processing next queued installation. Queue size:" << m_requestQueue.size();
    QStringList nextPackages = m_requestQueue.dequeue();
    installPackagesAsync(nextPackages);
}

QJsonArray PackageManagerLib::fetchPackageListFromOnline()
{
    QJsonArray packagesArray;
    
    if (!m_networkManager) {
        qWarning() << "Network manager not initialized";
        return packagesArray;
    }

    QString urlString = QString("%1/list.json").arg(MODULES_DOWNLOAD_BASE_URL);
    QUrl url(urlString);
    QNetworkRequest request(url);
    
    qDebug() << "Fetching package list from:" << urlString;
    
    QNetworkReply* reply = m_networkManager->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Failed to fetch package list:" << reply->errorString();
        reply->deleteLater();
        return packagesArray;
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse package list JSON:" << parseError.errorString();
        return packagesArray;
    }
    
    if (!doc.isArray()) {
        qWarning() << "Package list JSON is not an array";
        return packagesArray;
    }
    
    packagesArray = doc.array();
    qDebug() << "Successfully fetched" << packagesArray.size() << "packages from online source";
    
    return packagesArray;
}

bool PackageManagerLib::downloadFile(const QString& url, const QString& destinationPath)
{
    if (!m_networkManager) {
        qWarning() << "Network manager not initialized";
        return false;
    }

    QUrl fileUrl(url);
    QNetworkRequest request(fileUrl);
    
    qDebug() << "Downloading file from:" << url << "to" << destinationPath;
    
    QNetworkReply* reply = m_networkManager->get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Failed to download file:" << reply->errorString();
        reply->deleteLater();
        return false;
    }
    
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QFileInfo fileInfo(destinationPath);
    QDir destDir = fileInfo.dir();
    if (!destDir.exists()) {
        if (!destDir.mkpath(".")) {
            qWarning() << "Failed to create destination directory:" << destDir.absolutePath();
            return false;
        }
    }
    
    QFile file(destinationPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << destinationPath << "-" << file.errorString();
        return false;
    }
    
    qint64 bytesWritten = file.write(data);
    file.close();
    
    if (bytesWritten != data.size()) {
        qWarning() << "Failed to write all data to file. Expected:" << data.size() << "bytes, wrote:" << bytesWritten;
        return false;
    }
    
    qDebug() << "Successfully downloaded file:" << destinationPath << "(" << bytesWritten << "bytes)";
    return true;
}

QJsonObject PackageManagerLib::findPackageByName(const QJsonArray& packages, const QString& packageName)
{
    for (const QJsonValue& packageVal : packages) {
        QJsonObject packageObj = packageVal.toObject();
        if (packageObj.value("name").toString() == packageName) {
            return packageObj;
        }
    }
    return QJsonObject();
}

QString PackageManagerLib::currentPlatformVariant() const
{
#if defined(Q_OS_MAC)
    #if defined(Q_PROCESSOR_ARM)
        return "darwin-arm64";
    #else
        return "darwin-x86_64";
    #endif
#elif defined(Q_OS_LINUX)
    #if defined(Q_PROCESSOR_X86_64)
        return "linux-x86_64";
    #elif defined(Q_PROCESSOR_ARM_64)
        return "linux-arm64";
    #else
        return "linux-x86";
    #endif
#elif defined(Q_OS_WIN)
    #if defined(Q_PROCESSOR_X86_64)
        return "windows-x86_64";
    #else
        return "windows-x86";
    #endif
#else
    return "unknown";
#endif
}

QStringList PackageManagerLib::platformVariantsToTry() const
{
    QString primary = currentPlatformVariant();
    QStringList variants;
    variants << primary;
    
    if (primary == "linux-x86_64") {
        variants << "linux-amd64";
    } else if (primary == "linux-amd64") {
        variants << "linux-x86_64";
    } else if (primary == "linux-arm64") {
        variants << "linux-aarch64";
    } else if (primary == "linux-aarch64") {
        variants << "linux-arm64";
    }
    
    return variants;
}

bool PackageManagerLib::copyDirectoryContents(const QString& srcDir, const QString& destDir, QString& errorMsg)
{
    QDir src(srcDir);
    if (!src.exists()) {
        errorMsg = QString("Source directory does not exist: %1").arg(srcDir);
        return false;
    }

    QDir dest(destDir);
    if (!dest.exists()) {
        if (!dest.mkpath(".")) {
            errorMsg = QString("Failed to create destination directory: %1").arg(destDir);
            return false;
        }
    }

    QFileInfoList entries = src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        QString srcPath = entry.absoluteFilePath();
        QString destPath = destDir + "/" + entry.fileName();

        if (entry.isDir()) {
            if (!copyDirectoryContents(srcPath, destPath, errorMsg)) {
                return false;
            }
        } else {
            if (QFile::exists(destPath)) {
                if (!QFile::remove(destPath)) {
                    errorMsg = QString("Failed to remove existing file: %1").arg(destPath);
                    return false;
                }
            }
            if (!QFile::copy(srcPath, destPath)) {
                errorMsg = QString("Failed to copy file from %1 to %2").arg(srcPath, destPath);
                return false;
            }
        }
    }

    return true;
}

bool PackageManagerLib::extractLgxPackage(const QString& lgxPath, const QString& outputDir, QString& errorMsg)
{
    lgx_package_t pkg = lgx_load(lgxPath.toUtf8().constData());
    if (!pkg) {
        errorMsg = QString("Failed to load LGX package: %1").arg(lgx_get_last_error());
        return false;
    }
    
    QStringList variants = platformVariantsToTry();
    QString matchedVariant;
    
    qDebug() << "Trying platform variants:" << variants;
    
    for (const QString& v : variants) {
        if (lgx_has_variant(pkg, v.toUtf8().constData())) {
            matchedVariant = v;
            qDebug() << "Found matching variant:" << matchedVariant;
            break;
        }
    }
    
    if (matchedVariant.isEmpty()) {
        errorMsg = QString("Package does not contain variant for platform: %1 (tried: %2)")
            .arg(variants.first(), variants.join(", "));
        lgx_free_package(pkg);
        return false;
    }
    
    lgx_result_t result = lgx_extract(pkg, matchedVariant.toUtf8().constData(), outputDir.toUtf8().constData());
    
    if (!result.success) {
        errorMsg = QString("Failed to extract variant: %1").arg(result.error ? result.error : "unknown error");
        lgx_free_package(pkg);
        return false;
    }

    // Write manifest.json into the extracted variant directory
    QJsonObject manifest;
    const char* name = lgx_get_name(pkg);
    const char* version = lgx_get_version(pkg);
    const char* description = lgx_get_description(pkg);
    if (name) manifest["name"] = QString::fromUtf8(name);
    if (version) manifest["version"] = QString::fromUtf8(version);
    if (description) manifest["description"] = QString::fromUtf8(description);

    QString variantOutputDir = outputDir + "/" + matchedVariant;
    QString manifestPath = variantOutputDir + "/manifest.json";
    QFile manifestFile(manifestPath);
    if (manifestFile.open(QIODevice::WriteOnly)) {
        QJsonDocument manifestDoc(manifest);
        manifestFile.write(manifestDoc.toJson(QJsonDocument::Indented));
        manifestFile.close();
        qDebug() << "Wrote manifest.json to:" << manifestPath;
    } else {
        qWarning() << "Failed to write manifest.json to:" << manifestPath;
    }

    lgx_free_package(pkg);
    return true;
}

bool PackageManagerLib::copyLibraryFromExtracted(const QString& extractedDir, const QString& targetDir, bool isCoreModule, QString& errorMsg)
{
    Q_UNUSED(isCoreModule);

    QStringList variants = platformVariantsToTry();
    QString variantDir;

    for (const QString& v : variants) {
        QString candidate = extractedDir + "/" + v;
        if (QDir(candidate).exists()) {
            variantDir = candidate;
            qDebug() << "Found extracted variant directory:" << variantDir;
            break;
        }
    }

    if (variantDir.isEmpty()) {
        errorMsg = QString("Extracted variant directory not found for: %1").arg(variants.join(", "));
        return false;
    }

    // Find the main library file to determine the module name
    QDir dir(variantDir);
    QStringList filters;
#if defined(Q_OS_MAC)
    filters << "*.dylib";
#elif defined(Q_OS_WIN)
    filters << "*.dll";
#else
    filters << "*.so";
#endif

    QFileInfoList libraryFiles = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    if (libraryFiles.isEmpty()) {
        errorMsg = QString("No library files found in extracted variant directory: %1").arg(variantDir);
        return false;
    }

    // Use the first library's base name as the module name
    QString moduleName = libraryFiles.first().completeBaseName();
    QString moduleSubDir = targetDir + "/" + moduleName;

    qDebug() << "Installing module" << moduleName << "to subdirectory:" << moduleSubDir;

    if (!copyDirectoryContents(variantDir, moduleSubDir, errorMsg)) {
        return false;
    }

    qDebug() << "Copied variant directory contents to:" << moduleSubDir;
    return true;
}

QStringList PackageManagerLib::resolveDependenciesRecursive(const QString& packageName, const QJsonArray& allPackages, QSet<QString>& processed)
{
    QStringList result;
    
    if (processed.contains(packageName)) {
        return result;
    }
    
    processed.insert(packageName);
    
    QJsonObject packageObj = findPackageByName(allPackages, packageName);
    if (packageObj.isEmpty()) {
        qWarning() << "Package not found during dependency resolution:" << packageName;
        return result;
    }
    
    QJsonArray dependencies = packageObj.value("dependencies").toArray();
    for (const QJsonValue& depVal : dependencies) {
        QString depName = depVal.toString();
        if (!depName.isEmpty()) {
            QStringList depList = resolveDependenciesRecursive(depName, allPackages, processed);
            for (const QString& dep : depList) {
                if (!result.contains(dep)) {
                    result.append(dep);
                }
            }
        }
    }
    
    result.append(packageName);
    
    return result;
}

QJsonArray PackageManagerLib::filterPackagesByCategory(const QJsonArray& packages, const QString& category)
{
    QJsonArray filtered;
    
    for (const QJsonValue& packageVal : packages) {
        QJsonObject packageObj = packageVal.toObject();
        QString packageCategory = packageObj.value("category").toString();
        
        if (packageCategory.compare(category, Qt::CaseInsensitive) == 0) {
            filtered.append(packageVal);
        }
    }
    
    return filtered;
}

QStringList PackageManagerLib::extractCategories(const QJsonArray& packages)
{
    QSet<QString> categorySet;
    
    for (const QJsonValue& packageVal : packages) {
        QJsonObject packageObj = packageVal.toObject();
        QString category = packageObj.value("category").toString();
        
        if (!category.isEmpty()) {
            QString capitalizedCategory = category;
            if (!capitalizedCategory.isEmpty()) {
                capitalizedCategory[0] = capitalizedCategory[0].toUpper();
            }
            categorySet.insert(capitalizedCategory);
        }
    }
    
    QStringList sortedCategories = categorySet.values();
    std::sort(sortedCategories.begin(), sortedCategories.end());
    
    sortedCategories.prepend("All");
    
    return sortedCategories;
}
