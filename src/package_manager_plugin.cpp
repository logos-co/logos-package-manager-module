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
#include <QTemporaryDir>
#include "logos_api_client.h"
#include "lgx.h"

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

    // Check if this is an LGX package
    if (sourceFileInfo.suffix().toLower() == "lgx") {
        qDebug() << "Installing LGX package:" << pluginPath;
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            qWarning() << "Failed to create temporary directory for LGX extraction";
            return false;
        }
        
        QString errorMsg;
        if (!extractLgxPackage(pluginPath, tempDir.path(), errorMsg)) {
            qWarning() << "Failed to extract LGX package:" << errorMsg;
            return false;
        }
        
        if (!copyLibraryFromExtracted(tempDir.path(), pluginsDirectory, errorMsg)) {
            qWarning() << "Failed to copy libraries from extracted LGX package:" << errorMsg;
            return false;
        }
        
        qDebug() << "Successfully installed plugin from LGX package to:" << pluginsDirectory;
        
        // For core modules, process the plugins
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

            // Process all installed library files
            QDir dir(pluginsDirectory);
            QStringList filters;
#if defined(Q_OS_MAC)
            filters << "*.dylib";
#elif defined(Q_OS_WIN)
            filters << "*.dll";
#else
            filters << "*.so";
#endif
            QFileInfoList libraryFiles = dir.entryInfoList(filters, QDir::Files);
            
            for (const QFileInfo& libFileInfo : libraryFiles) {
                QString libPath = libFileInfo.absoluteFilePath();
                QVariant result = coreManagerClient->invokeRemoteMethod("core_manager_api", "processPlugin", libPath);
                QString pluginName = result.toString();
                if (!pluginName.isEmpty()) {
                    qDebug() << "Successfully processed plugin:" << pluginName;
                }
            }
        }
        
        return true;
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

        // Select the appropriate directory based on module type
        QDir* targetDir = nullptr;
        if (isCoreModule && checkModulesInstalled) {
            targetDir = &modulesDir;
        } else if (!isCoreModule && checkPluginsInstalled) {
            targetDir = &pluginsDir;
        }

        // Check if the module is installed by looking for common library patterns
        if (targetDir != nullptr) {
            QStringList filters;
#if defined(Q_OS_MAC)
            filters << QString("%1*.dylib").arg(moduleName);
#elif defined(Q_OS_WIN)
            filters << QString("%1*.dll").arg(moduleName);
#else
            filters << QString("%1*.so").arg(moduleName);
#endif
            QFileInfoList matchingFiles = targetDir->entryInfoList(filters, QDir::Files);
            isInstalled = !matchingFiles.isEmpty();
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
    QString downloadUrl = QString("https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/%1")
        .arg(packageFile);
    QString destinationPath = QDir(tempDir).filePath(packageFile);
    
    qDebug() << "Downloading package file:" << packageFile;
    if (!downloadFile(downloadUrl, destinationPath)) {
        qWarning() << "Failed to download package file:" << packageFile;
        return false;
    }
    
    // Install the LGX package (installPlugin handles LGX extraction)
    qDebug() << "Installing downloaded package:" << destinationPath << "(type:" << packageType << ")";
    bool success = installPlugin(destinationPath, isCoreModule);
    
    // Clean up temp file
    QFile::remove(destinationPath);
    qDebug() << "Cleaned up temp file:" << destinationPath;
    
    if (!success) {
        qWarning() << "Failed to install package:" << packageName;
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
    
    m_pluginsDirectory = pluginsDirectory;
    
    if (m_asyncState.tempDir.isEmpty()) {
        finishAsyncInstallation(false, "Failed to get temp directory");
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
    
    QString packageFile = m_asyncState.packageObj.value("package").toString();
    if (packageFile.isEmpty()) {
        finishAsyncInstallation(false, "Package has no package file specified");
        return;
    }
    
    // Single LGX file to download
    m_asyncState.filesToDownload.append(packageFile);
    
    qDebug() << "Async: Need to download package file:" << packageFile;
    
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
    QString downloadUrl = QString("https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/%1")
        .arg(fileName);
    
    qDebug() << "Async: Downloading package file:" << fileName;
    
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

QString PackageManagerPlugin::currentPlatformVariant() const {
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

bool PackageManagerPlugin::extractLgxPackage(const QString& lgxPath, const QString& outputDir, QString& errorMsg)
{
    lgx_package_t pkg = lgx_load(lgxPath.toUtf8().constData());
    if (!pkg) {
        errorMsg = QString("Failed to load LGX package: %1").arg(lgx_get_last_error());
        return false;
    }
    
    QString variant = currentPlatformVariant();
    qDebug() << "Extracting variant:" << variant << "from LGX package";
    
    if (!lgx_has_variant(pkg, variant.toUtf8().constData())) {
        errorMsg = QString("Package does not contain variant for platform: %1").arg(variant);
        lgx_free_package(pkg);
        return false;
    }
    
    lgx_result_t result = lgx_extract(pkg, variant.toUtf8().constData(), outputDir.toUtf8().constData());
    
    if (!result.success) {
        errorMsg = QString("Failed to extract variant: %1").arg(result.error ? result.error : "unknown error");
        lgx_free_package(pkg);
        return false;
    }
    
    lgx_free_package(pkg);
    return true;
}

bool PackageManagerPlugin::copyLibraryFromExtracted(const QString& extractedDir, const QString& targetDir, QString& errorMsg)
{
    QString variant = currentPlatformVariant();
    QString variantDir = extractedDir + "/" + variant;
    
    if (!QDir(variantDir).exists()) {
        errorMsg = QString("Extracted variant directory not found: %1").arg(variantDir);
        return false;
    }
    
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
    
    // Copy ALL library files, preserving their original filenames
    for (const QFileInfo& fileInfo : libraryFiles) {
        QString sourceFile = fileInfo.absoluteFilePath();
        QString targetPath = targetDir + "/" + fileInfo.fileName();
        
        // Remove target if it exists
        if (QFile::exists(targetPath)) {
            if (!QFile::remove(targetPath)) {
                errorMsg = QString("Failed to remove existing file: %1").arg(targetPath);
                return false;
            }
        }
        
        // Copy the library
        if (!QFile::copy(sourceFile, targetPath)) {
            errorMsg = QString("Failed to copy library from %1 to %2").arg(sourceFile, targetPath);
            return false;
        }
        
        qDebug() << "Copied library file:" << targetPath;
    }
    
    return true;
}
