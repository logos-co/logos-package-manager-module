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
    // m_networkManager is a child of this, so it will be deleted automatically
}

bool PackageManagerPlugin::installPlugin(const QString& pluginPath)
{
    qDebug() << "PackageManager: Installing plugin:" << pluginPath;

    // Verify the source file exists
    QFileInfo sourceFileInfo(pluginPath);
    if (!sourceFileInfo.exists() || !sourceFileInfo.isFile()) {
        qWarning() << "Source plugin file does not exist or is not a file:" << pluginPath;
        return false;
    }

    // Use m_pluginsDirectory if set, otherwise default
    QString pluginsDirectory = m_pluginsDirectory.isEmpty()
        ? QDir::cleanPath(QCoreApplication::applicationDirPath() + "/bin/modules")
        : m_pluginsDirectory;
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
                            // Continue anyway - this isn't a fatal error
                        }
                    }
                    
                    // Copy the included file
                    QFile includeFile(sourceIncludePath);
                    if (includeFile.copy(destIncludePath)) {
                        qDebug() << "Successfully copied included file:" << includeFileName;
                    } else {
                        qWarning() << "Failed to copy included file:" << includeFileName 
                                  << "-" << includeFile.errorString();
                        // Continue anyway - this isn't a fatal error
                    }
                } else {
                    qDebug() << "Included file not found:" << sourceIncludePath;
                    // It's ok if the file doesn't exist - continue with other files
                }
            }
        }
    }
    
    // Use LogosAPI to call the remote method
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

    QString checkDir = m_pluginsDirectory;
    if (checkDir.isEmpty()) {
        checkDir = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/bin/modules");
    }
    QDir modulesDir(checkDir);
    bool checkInstalled = modulesDir.exists();

    for (const QJsonValue& packageVal : onlinePackages) {
        QJsonObject packageObj = packageVal.toObject();
        QString packageName = packageObj.value("name").toString();
        
        QJsonObject filesObj = packageObj.value("files").toObject();
        if (!filesObj.contains(platformKey)) {
            continue;
        }

        QJsonArray platformFiles = filesObj.value(platformKey).toArray();
        if (platformFiles.isEmpty()) {
            continue;
        }

        bool isInstalled = false;
        if (checkInstalled) {
            for (const QJsonValue& fileVal : platformFiles) {
                QString fileName = fileVal.toString();
                if (modulesDir.exists(fileName)) {
                    isInstalled = true;
                    break;
                }
            }
        }

        QJsonObject resultPackage;
        resultPackage["name"] = packageName;
        resultPackage["description"] = "";
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
        qDebug() << "Installing downloaded file:" << downloadedFile;
        if (!installPlugin(downloadedFile)) {
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
    
    qDebug() << "Successfully installed package:" << packageName;
    return true;
}

void PackageManagerPlugin::initLogos(LogosAPI* logosAPIInstance) {
    if (logosAPI) {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}

QString PackageManagerPlugin::testPluginCall(const QString& foo) {
    // print something to the terminal
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

QJsonObject PackageManagerPlugin::findPackageByName(const QJsonArray& packages, const QString& packageName) {
    for (const QJsonValue& packageVal : packages) {
        QJsonObject packageObj = packageVal.toObject();
        if (packageObj.value("name").toString() == packageName) {
            return packageObj;
        }
    }
    return QJsonObject();
}
