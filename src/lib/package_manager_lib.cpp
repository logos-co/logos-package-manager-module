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
#include "lgx.h"

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

    // Verify the source file exists
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

    // Make sure we have a valid plugins directory
    if (pluginsDirectory.isEmpty()) {
        errorMsg = "Plugins directory is not set. Cannot install plugin.";
        qWarning() << errorMsg;
        return QString();
    }

    // Create the plugins directory if it doesn't exist
    QDir pluginsDir(pluginsDirectory);
    if (!pluginsDir.exists()) {
        qDebug() << "Creating plugins directory:" << pluginsDirectory;
        if (!pluginsDir.mkpath(".")) {
            errorMsg = "Failed to create plugins directory: " + pluginsDirectory;
            qWarning() << errorMsg;
            return QString();
        }
    }

    // Check if this is an LGX package
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
        
        if (!copyLibraryFromExtracted(tempDir.path(), pluginsDirectory, errorMsg)) {
            qWarning() << "Failed to copy libraries from extracted LGX package:" << errorMsg;
            return QString();
        }
        
        qDebug() << "Successfully installed plugin from LGX package to:" << pluginsDirectory;
        
        // For LGX packages, emit signal for all installed library files
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
            emit pluginFileInstalled(libPath, isCoreModule);
        }
        
        return pluginsDirectory;
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
            errorMsg = "Failed to remove existing plugin file: " + destinationPath;
            qWarning() << errorMsg;
            return QString();
        }
    }

    // Copy the plugin file to the plugins directory
    QFile sourceFile(pluginPath);
    if (!sourceFile.copy(destinationPath)) {
        errorMsg = "Failed to copy plugin file to plugins directory: " + sourceFile.errorString();
        qWarning() << errorMsg;
        return QString();
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
    
    // Emit signal so wrapper can call processPlugin via LogosAPI
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
    QString downloadUrl = QString("https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/%1")
        .arg(packageFile);
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
    qDebug() << "Installing package async:" << packageName;
    
    if (m_isInstalling) {
        qWarning() << "Another installation is already in progress";
        emit installationFinished(packageName, false, "Another installation is already in progress");
        return;
    }
    
    m_isInstalling = true;
    
    m_asyncState.packageName = packageName;
    m_asyncState.filesToDownload.clear();
    m_asyncState.downloadedFiles.clear();
    m_asyncState.currentDownloadIndex = 0;
    m_asyncState.tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    
    if (m_asyncState.tempDir.isEmpty()) {
        finishAsyncInstallation(false, "Failed to get temp directory");
        return;
    }
    
    startAsyncPackageListFetch();
}

void PackageManagerLib::startAsyncPackageListFetch()
{
    if (!m_networkManager) {
        finishAsyncInstallation(false, "Network manager not initialized");
        return;
    }
    
    QString urlString = "https://github.com/logos-co/logos-modules/releases/download/outputs-libraries/list.json";
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
    
    // Single LGX file to download
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
    
    if (success) {
        qDebug() << "Async: Successfully installed package:" << packageName;
    } else {
        qWarning() << "Async: Failed to install package:" << packageName << "-" << error;
    }
    
    emit installationFinished(packageName, success, error);
}

QJsonArray PackageManagerLib::fetchPackageListFromOnline()
{
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

bool PackageManagerLib::extractLgxPackage(const QString& lgxPath, const QString& outputDir, QString& errorMsg)
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

bool PackageManagerLib::copyLibraryFromExtracted(const QString& extractedDir, const QString& targetDir, QString& errorMsg)
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
