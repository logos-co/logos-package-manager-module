#include "package_manager_plugin.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPluginLoader>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QRemoteObjectReplica>
#include <QRemoteObjectPendingCall>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QStandardPaths>
#include <QUrl>
#include "logos_api_client.h"

PackageManagerPlugin::PackageManagerPlugin()
    : m_networkManager(nullptr)
    , m_isInstalling(false)
{
    qDebug() << "PackageManagerPlugin created";
    qDebug() << "PackageManagerPlugin: LogosAPI initialized";
    m_networkManager = new QNetworkAccessManager(this);
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
    qDebug() << "PackageManager: Installing plugin:" << pluginPath << "(isCoreModule:" << isCoreModule << ")";

    // Verify the source file exists
    QFileInfo sourceFileInfo(pluginPath);
    if (!sourceFileInfo.exists() || !sourceFileInfo.isFile()) {
        qWarning() << "Source plugin file does not exist or is not a file:" << pluginPath;
        return false;
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

    // Make sure we have a valid plugins directory
    if (pluginsDirectory.isEmpty()) {
        qWarning() << "Plugins directory is not set. Cannot install plugin.";
        return false;
    }

    // Create the plugins directory if it doesn't exist
    QDir pluginsDir(pluginsDirectory);
    if (!pluginsDir.exists()) {
        qDebug() << "Creating plugins directory:" << pluginsDirectory;
        if (!pluginsDir.mkpath(".")) {
            qWarning() << "Failed to create plugins directory:" << pluginsDirectory;
            return false;
        }
    }

    // Get the filename from the source path
    QString fileName = sourceFileInfo.fileName();
    QString destinationPath = pluginsDir.filePath(fileName);

    // Check if the destination file already exists
    QFileInfo destFileInfo(destinationPath);
    if (destFileInfo.exists()) {
        qDebug() << "Plugin already exists at destination. Overwriting:" << destinationPath;
        QFile destFile(destinationPath);
        if (!destFile.remove()) {
            qWarning() << "Failed to remove existing plugin file:" << destinationPath;
            return false;
        }
    }

    // Copy the plugin file to the plugins directory
    QFile sourceFile(pluginPath);
    if (!sourceFile.copy(destinationPath)) {
        qWarning() << "Failed to copy plugin file to plugins directory:" 
                  << sourceFile.errorString();
        return false;
    }

    qDebug() << "Successfully installed plugin:" << fileName << "to" << destinationPath;
    
    // Read the plugin metadata to check for included files
    QPluginLoader loader(pluginPath);
    QJsonObject metadata = loader.metaData();
    if (!metadata.isEmpty()) {
        QJsonObject metaDataObj = metadata.value("MetaData").toObject();
        QJsonArray includeFiles = metaDataObj.value("include").toArray();
        
        if (!includeFiles.isEmpty()) {
            qDebug() << "Plugin has" << includeFiles.size() << "included files to copy";
            
            // Get the source directory (where the plugin is)
            QDir sourceDir = sourceFileInfo.dir();
            
            // Try to copy each included file
            for (const QJsonValue& includeVal : includeFiles) {
                QString includeFileName = includeVal.toString();
                if (includeFileName.isEmpty()) continue;
                
                QString sourceIncludePath = sourceDir.filePath(includeFileName);
                QString destIncludePath = pluginsDir.filePath(includeFileName);
                
                qDebug() << "Checking for included file:" << sourceIncludePath;
                
                // Check if the source file exists
                QFileInfo includeFileInfo(sourceIncludePath);
                if (includeFileInfo.exists() && includeFileInfo.isFile()) {
                    qDebug() << "Found included file:" << sourceIncludePath;
                    
                    // Check if the destination file already exists
                    QFileInfo destIncludeFileInfo(destIncludePath);
                    if (destIncludeFileInfo.exists()) {
                        qDebug() << "Included file already exists at destination. Overwriting:" << destIncludePath;
                        QFile destIncludeFile(destIncludePath);
                        if (!destIncludeFile.remove()) {
                            qWarning() << "Failed to remove existing included file:" << destIncludePath;
                        }
                    }
                    
                    // Copy the included file
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
    
    if (isCoreModule) {
        if (!logosAPI) {
            qWarning() << "Failed to connect to Logos Core registry.";
            return false;
        }

        LogosAPIClient* coreManagerClient = logosAPI->getClient("core_manager");
        if (!coreManagerClient || !coreManagerClient->isConnected()) {
            qWarning() << "Failed to connect to Logos Core registry.";
            return false;
        }

        qDebug() << "Calling processPlugin with destinationPath:" << destinationPath;
        QVariant result = coreManagerClient->invokeRemoteMethod("core_manager_api", "processPlugin", destinationPath);
        QString pluginName = result.toString();
        if (pluginName.isEmpty()) {
            qDebug() << "ERROR: --------------------------------";
            qWarning() << "Failed to process installed plugin:" << destinationPath;
            return false;
        }
        qDebug() << "Successfully processed installed plugin:" << pluginName;
    }
    return true;
}

QJsonArray PackageManagerPlugin::getPackages() {
    QJsonArray packagesArray;

    QJsonArray onlinePackages = fetchPackageListFromOnline();
    if (onlinePackages.isEmpty()) {
        qWarning() << "Failed to fetch packages from online source or no packages available";
        return packagesArray;
    }

    QString platformKey = getPlatformKey();
    if (platformKey.isEmpty()) {
        qWarning() << "Unsupported platform";
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
        
        QJsonObject filesObj = packageObj.value("files").toObject();
        if (!filesObj.contains(platformKey)) {
            continue;
        }

        QJsonArray platformFiles = filesObj.value(platformKey).toArray();
        if (platformFiles.isEmpty()) {
            continue;
        }

        bool isInstalled = false;
        bool isCoreModule = (packageType != "ui");

        // Select the appropriate directory and check flag based on module type
        QDir* targetDir = nullptr;
        if (isCoreModule && checkModulesInstalled) {
            targetDir = &modulesDir;
        } else if (!isCoreModule && checkPluginsInstalled) {
            targetDir = &pluginsDir;
        }

        // Check if any of the platform files exist in the target directory
        if (targetDir != nullptr) {
            for (const QJsonValue& fileVal : platformFiles) {
                QString fileName = fileVal.toString();
                if (targetDir->exists(fileName)) {
                    isInstalled = true;
                    break;
                }
            }
        }

        QJsonObject resultPackage;
        resultPackage["name"] = packageName;
        resultPackage["description"] = packageObj.value("description").toString();
        resultPackage["type"] = packageType;
        resultPackage["moduleName"] = packageObj.value("moduleName").toString();
        resultPackage["category"] = packageObj.value("category").toString();
        resultPackage["author"] = packageObj.value("author").toString();
        resultPackage["dependencies"] = packageObj.value("dependencies").toArray();
        resultPackage["files"] = platformFiles;
        resultPackage["installed"] = isInstalled;
        packagesArray.append(resultPackage);
    }

    qDebug() << "Found" << packagesArray.size() << "packages for platform" << platformKey;
    return packagesArray;
}

bool PackageManagerPlugin::installPackage(const QString& packageName, const QString& pluginsDirectory) {
    qDebug() << "Installing package:" << packageName;
    
    m_pluginsDirectory = pluginsDirectory;
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
    
    QString platformKey = getPlatformKey();
    if (platformKey.isEmpty()) {
        qWarning() << "Unsupported platform";
        return false;
    }
    
    QJsonObject filesObj = packageObj.value("files").toObject();
    if (!filesObj.contains(platformKey)) {
        qWarning() << "Package" << packageName << "not available for platform" << platformKey;
        return false;
    }
    
    QJsonArray platformFiles = filesObj.value(platformKey).toArray();
    if (platformFiles.isEmpty()) {
        qWarning() << "Package" << packageName << "has no files for platform" << platformKey;
        return false;
    }
    
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        qWarning() << "Failed to get temp directory";
        return false;
    }
    
    QStringList downloadedFiles;
    for (const QJsonValue& fileVal : platformFiles) {
        QString fileName = fileVal.toString();
        if (fileName.isEmpty()) {
            continue;
        }
        
        QString downloadUrl = QString("https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/%1-%2")
            .arg(platformKey, fileName);
        QString destinationPath = QDir(tempDir).filePath(fileName);
        
        if (!downloadFile(downloadUrl, destinationPath)) {
            qWarning() << "Failed to download file:" << fileName;
            for (const QString& downloadedFile : downloadedFiles) {
                QFile::remove(downloadedFile);
            }
            return false;
        }
        
        downloadedFiles.append(destinationPath);
    }
    
    bool allInstalled = true;
    for (const QString& downloadedFile : downloadedFiles) {
        qDebug() << "Installing downloaded file:" << downloadedFile << "(type:" << packageType << ")";
        if (!installPlugin(downloadedFile, isCoreModule)) {
            qWarning() << "Failed to install file:" << downloadedFile;
            allInstalled = false;
        }
    }
    
    for (const QString& downloadedFile : downloadedFiles) {
        QFile::remove(downloadedFile);
        qDebug() << "Cleaned up temp file:" << downloadedFile;
    }
    
    if (!allInstalled) {
        qWarning() << "Some files failed to install for package:" << packageName;
        return false;
    }
    
    qDebug() << "Successfully installed package:" << packageName << "(type:" << packageType << ")";
    return true;
}

void PackageManagerPlugin::installPackageAsync(const QString& packageName, const QString& pluginsDirectory) {
    qDebug() << "Installing package async:" << packageName;
    
    if (m_isInstalling) {
        qWarning() << "Another installation is already in progress";
        emitInstallationEvent(packageName, false, "Another installation is already in progress");
        return;
    }
    
    m_isInstalling = true;
    
    m_asyncState.packageName = packageName;
    m_asyncState.pluginsDirectory = pluginsDirectory;
    m_asyncState.filesToDownload.clear();
    m_asyncState.downloadedFiles.clear();
    m_asyncState.currentDownloadIndex = 0;
    m_asyncState.tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_asyncState.platformKey = getPlatformKey();
    
    m_pluginsDirectory = pluginsDirectory;
    
    if (m_asyncState.tempDir.isEmpty()) {
        finishAsyncInstallation(false, "Failed to get temp directory");
        return;
    }
    
    if (m_asyncState.platformKey.isEmpty()) {
        finishAsyncInstallation(false, "Unsupported platform");
        return;
    }
    
    startAsyncPackageListFetch();
}

void PackageManagerPlugin::startAsyncPackageListFetch() {
    if (!m_networkManager) {
        finishAsyncInstallation(false, "Network manager not initialized");
        return;
    }
    
    QString urlString = "https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/list.json";
    QUrl url(urlString);
    QNetworkRequest request(url);
    
    qDebug() << "Async: Fetching package list from:" << urlString;
    
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &PackageManagerPlugin::onPackageListFetched);
}

void PackageManagerPlugin::onPackageListFetched() {
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
    
    QJsonObject filesObj = m_asyncState.packageObj.value("files").toObject();
    if (!filesObj.contains(m_asyncState.platformKey)) {
        finishAsyncInstallation(false, "Package not available for platform " + m_asyncState.platformKey);
        return;
    }
    
    QJsonArray platformFiles = filesObj.value(m_asyncState.platformKey).toArray();
    if (platformFiles.isEmpty()) {
        finishAsyncInstallation(false, "Package has no files for platform " + m_asyncState.platformKey);
        return;
    }
    
    for (const QJsonValue& fileVal : platformFiles) {
        QString fileName = fileVal.toString();
        if (!fileName.isEmpty()) {
            m_asyncState.filesToDownload.append(fileName);
        }
    }
    
    if (m_asyncState.filesToDownload.isEmpty()) {
        finishAsyncInstallation(false, "No files to download");
        return;
    }
    
    qDebug() << "Async: Need to download" << m_asyncState.filesToDownload.size() << "files";
    
    m_asyncState.currentDownloadIndex = 0;
    startNextFileDownload();
}

void PackageManagerPlugin::startNextFileDownload() {
    if (m_asyncState.currentDownloadIndex >= m_asyncState.filesToDownload.size()) {
        qDebug() << "Async: All files downloaded, installing...";
        
        bool allInstalled = true;
        QString packageType = m_asyncState.packageObj.value("type").toString();
        
        for (const QString& downloadedFile : m_asyncState.downloadedFiles) {
            qDebug() << "Installing downloaded file:" << downloadedFile << "(type:" << packageType << ")";
            if (!installPlugin(downloadedFile, m_asyncState.isCoreModule)) {
                qWarning() << "Failed to install file:" << downloadedFile;
                allInstalled = false;
            }
        }
        
        for (const QString& downloadedFile : m_asyncState.downloadedFiles) {
            QFile::remove(downloadedFile);
            qDebug() << "Cleaned up temp file:" << downloadedFile;
        }
        
        if (allInstalled) {
            finishAsyncInstallation(true, "");
        } else {
            finishAsyncInstallation(false, "Some files failed to install");
        }
        return;
    }
    
    QString fileName = m_asyncState.filesToDownload[m_asyncState.currentDownloadIndex];
    QString downloadUrl = QString("https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/%1-%2")
        .arg(m_asyncState.platformKey, fileName);
    
    qDebug() << "Async: Downloading file" << (m_asyncState.currentDownloadIndex + 1) 
             << "of" << m_asyncState.filesToDownload.size() << ":" << fileName;
    
    QUrl url(downloadUrl);
    QNetworkRequest request(url);
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &PackageManagerPlugin::onFileDownloaded);
}

void PackageManagerPlugin::onFileDownloaded() {
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

void PackageManagerPlugin::finishAsyncInstallation(bool success, const QString& error) {
    QString packageName = m_asyncState.packageName;
    
    m_isInstalling = false;
    m_asyncState.packageName.clear();
    m_asyncState.filesToDownload.clear();
    m_asyncState.downloadedFiles.clear();
    
    if (success) {
        qDebug() << "Async: Successfully installed package:" << packageName;
    } else {
        qWarning() << "Async: Failed to install package:" << packageName << "-" << error;
    }
    
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

QString PackageManagerPlugin::getPlatformKey() const {
#ifdef Q_OS_MAC
    return "mac";
#elif defined(Q_OS_LINUX)
    return "linux";
#elif defined(Q_OS_WIN)
    return "win";
#else
    return "";
#endif
}

QJsonArray PackageManagerPlugin::fetchPackageListFromOnline() {
    QJsonArray packagesArray;
    
    if (!m_networkManager) {
        qWarning() << "Network manager not initialized";
        return packagesArray;
    }

    QString urlString = "https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/list.json";
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

bool PackageManagerPlugin::downloadFile(const QString& url, const QString& destinationPath) {
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

void PackageManagerPlugin::setPluginsDirectory(const QString& pluginsDirectory) {
    m_pluginsDirectory = pluginsDirectory;
    qDebug() << "Set plugins directory to:" << m_pluginsDirectory;
}

void PackageManagerPlugin::setUiPluginsDirectory(const QString& uiPluginsDirectory) {
    m_uiPluginsDirectory = uiPluginsDirectory;
    qDebug() << "Set UI plugins directory to:" << m_uiPluginsDirectory;
}

QJsonObject PackageManagerPlugin::findPackageByName(const QJsonArray& packages, const QString& packageName) {
    for (const QJsonValue& packageVal : packages) {
        QJsonObject packageObj = packageVal.toObject();
        if (packageObj.value("name").toString() == packageName) {
            return packageObj;
        }
    }
    return QJsonObject();
}
