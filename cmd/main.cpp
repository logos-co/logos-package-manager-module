#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <iostream>

#include "package_manager_lib.h"

// Output streams
QTextStream out(stdout);
QTextStream err(stderr);

void printPackageTable(const QJsonArray& packages) {
    // Header
    out << QString("%1 %2 %3 %4")
           .arg("NAME", -30)
           .arg("CATEGORY", -15)
           .arg("TYPE", -10)
           .arg("INSTALLED", -10)
        << Qt::endl;
    out << QString("-").repeated(65) << Qt::endl;
    
    // Rows
    for (const QJsonValue& val : packages) {
        QJsonObject pkg = val.toObject();
        QString name = pkg.value("name").toString();
        QString category = pkg.value("category").toString();
        QString type = pkg.value("type").toString();
        bool installed = pkg.value("installed").toBool();
        
        out << QString("%1 %2 %3 %4")
               .arg(name, -30)
               .arg(category, -15)
               .arg(type, -10)
               .arg(installed ? "yes" : "no", -10)
            << Qt::endl;
    }
}

void printPackageJson(const QJsonArray& packages) {
    QJsonDocument doc(packages);
    out << doc.toJson(QJsonDocument::Indented) << Qt::endl;
}

int cmdSearch(PackageManagerLib& pm, const QStringList& args, bool jsonOutput) {
    if (args.isEmpty()) {
        err << "Error: search requires a query argument" << Qt::endl;
        return 1;
    }
    
    QString query = args.first().toLower();
    QJsonArray allPackages = pm.getPackages();
    QJsonArray results;
    
    for (const QJsonValue& val : allPackages) {
        QJsonObject pkg = val.toObject();
        QString name = pkg.value("name").toString().toLower();
        QString description = pkg.value("description").toString().toLower();
        
        if (name.contains(query) || description.contains(query)) {
            results.append(pkg);
        }
    }
    
    if (results.isEmpty()) {
        out << "No packages found matching '" << args.first() << "'" << Qt::endl;
        return 0;
    }
    
    if (jsonOutput) {
        printPackageJson(results);
    } else {
        out << "Found " << results.size() << " package(s) matching '" << args.first() << "':" << Qt::endl << Qt::endl;
        printPackageTable(results);
    }
    
    return 0;
}

int cmdList(PackageManagerLib& pm, const QString& category, bool installedOnly, bool jsonOutput) {
    QJsonArray packages;
    
    if (category.isEmpty()) {
        packages = pm.getPackages();
    } else {
        packages = pm.getPackages(category);
    }
    
    if (installedOnly) {
        QJsonArray filtered;
        for (const QJsonValue& val : packages) {
            QJsonObject pkg = val.toObject();
            if (pkg.value("installed").toBool()) {
                filtered.append(pkg);
            }
        }
        packages = filtered;
    }
    
    if (packages.isEmpty()) {
        out << "No packages found" << Qt::endl;
        return 0;
    }
    
    if (jsonOutput) {
        printPackageJson(packages);
    } else {
        out << "Found " << packages.size() << " package(s):" << Qt::endl << Qt::endl;
        printPackageTable(packages);
    }
    
    return 0;
}

int cmdInstallFile(PackageManagerLib& pm, const QString& filePath) {
    if (!QFile::exists(filePath)) {
        err << "Error: file not found: " << filePath << Qt::endl;
        return 1;
    }

    out << "Installing from file: " << filePath << "..." << Qt::flush;

    QString errorMsg;
    QString installedPath = pm.installPluginFile(filePath, errorMsg);

    if (installedPath.isEmpty()) {
        out << " FAILED" << Qt::endl;
        err << "Error: " << errorMsg << Qt::endl;
        return 1;
    }

    out << " done" << Qt::endl;
    out << "Installed to: " << installedPath << Qt::endl;
    return 0;
}

int cmdInstall(PackageManagerLib& pm, const QStringList& args) {
    if (args.isEmpty()) {
        err << "Error: install requires at least one package name" << Qt::endl;
        return 1;
    }

    out << "Resolving dependencies..." << Qt::endl;
    QStringList packagesToInstall = pm.resolveDependencies(args);

    out << "Will install " << packagesToInstall.size() << " package(s): "
        << packagesToInstall.join(", ") << Qt::endl << Qt::endl;

    int installed = 0;
    int failed = 0;

    for (const QString& packageName : packagesToInstall) {
        out << "Installing: " << packageName << "..." << Qt::flush;

        if (pm.installPackage(packageName)) {
            out << " done" << Qt::endl;
            installed++;
        } else {
            out << " FAILED" << Qt::endl;
            failed++;
        }
    }

    out << Qt::endl;
    if (failed == 0) {
        out << "Done. " << installed << " package(s) installed successfully." << Qt::endl;
        return 0;
    } else {
        out << "Completed with errors. " << installed << " installed, " << failed << " failed." << Qt::endl;
        return 1;
    }
}

int cmdCategories(PackageManagerLib& pm, bool jsonOutput) {
    QStringList categories = pm.getCategories();
    
    if (jsonOutput) {
        QJsonArray arr;
        for (const QString& cat : categories) {
            arr.append(cat);
        }
        QJsonDocument doc(arr);
        out << doc.toJson(QJsonDocument::Indented) << Qt::endl;
    } else {
        out << "Available categories:" << Qt::endl;
        for (const QString& cat : categories) {
            out << "  " << cat << Qt::endl;
        }
    }
    
    return 0;
}

int cmdInfo(PackageManagerLib& pm, const QStringList& args, bool jsonOutput) {
    if (args.isEmpty()) {
        err << "Error: info requires a package name" << Qt::endl;
        return 1;
    }
    
    QString packageName = args.first();
    QJsonArray packages = pm.getPackages();
    QJsonObject pkg = pm.findPackageByName(packages, packageName);
    
    if (pkg.isEmpty()) {
        err << "Error: package '" << packageName << "' not found" << Qt::endl;
        return 1;
    }
    
    if (jsonOutput) {
        QJsonDocument doc(pkg);
        out << doc.toJson(QJsonDocument::Indented) << Qt::endl;
    } else {
        out << "Name: " << pkg.value("name").toString() << Qt::endl;
        out << "Description: " << pkg.value("description").toString() << Qt::endl;
        out << "Category: " << pkg.value("category").toString() << Qt::endl;
        out << "Type: " << pkg.value("type").toString() << Qt::endl;
        out << "Author: " << pkg.value("author").toString() << Qt::endl;
        out << "Module Name: " << pkg.value("moduleName").toString() << Qt::endl;
        
        QJsonArray deps = pkg.value("dependencies").toArray();
        if (!deps.isEmpty()) {
            QStringList depList;
            for (const QJsonValue& d : deps) {
                depList << d.toString();
            }
            out << "Dependencies: " << depList.join(", ") << Qt::endl;
        } else {
            out << "Dependencies: none" << Qt::endl;
        }
        
        out << "Installed: " << (pkg.value("installed").toBool() ? "yes" : "no") << Qt::endl;
    }
    
    return 0;
}

void printHelp() {
    out << "lgpm - Logos Package Manager CLI" << Qt::endl;
    out << Qt::endl;
    out << "Usage: lgpm [options] <command> [arguments]" << Qt::endl;
    out << Qt::endl;
    out << "Commands:" << Qt::endl;
    out << "  search <query>          Search packages by name or description" << Qt::endl;
    out << "  list                    List all available packages" << Qt::endl;
    out << "  install <pkg> [pkgs...] Install one or more packages" << Qt::endl;
    out << "  install --file <path>   Install from a local LGX file" << Qt::endl;
    out << "  categories              List available categories" << Qt::endl;
    out << "  info <package>          Show detailed package information" << Qt::endl;
    out << Qt::endl;
    out << "Options:" << Qt::endl;
    out << "  --modules-dir <path>    Set core modules directory" << Qt::endl;
    out << "  --ui-plugins-dir <path> Set UI plugins directory" << Qt::endl;
    out << "  --release <tag>         GitHub release tag to use (default: latest)" << Qt::endl;
    out << "  --category <cat>        Filter by category (for list command)" << Qt::endl;
    out << "  --installed             Show only installed packages (for list command)" << Qt::endl;
    out << "  --file <path>           Install from a local LGX file (for install command)" << Qt::endl;
    out << "  --json                  Output in JSON format" << Qt::endl;
    out << "  -h, --help              Show this help message" << Qt::endl;
    out << "  -v, --version           Show version information" << Qt::endl;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("lgpm");
    app.setApplicationVersion("1.0.0");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("Logos Package Manager CLI");
    
    // Options
    QCommandLineOption helpOption(QStringList() << "h" << "help", "Show help message");
    QCommandLineOption versionOption(QStringList() << "v" << "version", "Show version");
    QCommandLineOption pluginsDirOption("modules-dir", "Core modules directory", "path");
    QCommandLineOption uiPluginsDirOption("ui-plugins-dir", "UI plugins directory", "path");
    QCommandLineOption categoryOption("category", "Filter by category", "category");
    QCommandLineOption installedOption("installed", "Show only installed packages");
    QCommandLineOption jsonOption("json", "Output in JSON format");
    QCommandLineOption fileOption("file", "Install from a local LGX file path", "path");
    QCommandLineOption releaseOption("release", "GitHub release tag to use (default: latest)", "tag", "latest");

    parser.addOption(helpOption);
    parser.addOption(versionOption);
    parser.addOption(pluginsDirOption);
    parser.addOption(uiPluginsDirOption);
    parser.addOption(categoryOption);
    parser.addOption(installedOption);
    parser.addOption(jsonOption);
    parser.addOption(fileOption);
    parser.addOption(releaseOption);
    parser.addPositionalArgument("command", "Command to run");
    parser.addPositionalArgument("args", "Command arguments", "[args...]");
    
    parser.process(app);
    
    if (parser.isSet(helpOption)) {
        printHelp();
        return 0;
    }
    
    if (parser.isSet(versionOption)) {
        out << "lgpm version " << app.applicationVersion() << Qt::endl;
        return 0;
    }
    
    QStringList positionalArgs = parser.positionalArguments();
    
    if (positionalArgs.isEmpty()) {
        printHelp();
        return 1;
    }
    
    QString command = positionalArgs.takeFirst();
    bool jsonOutput = parser.isSet(jsonOption);
    
    PackageManagerLib pm;
    
    if (parser.isSet(pluginsDirOption)) {
        pm.setPluginsDirectory(parser.value(pluginsDirOption));
    }
    
    if (parser.isSet(uiPluginsDirOption)) {
        pm.setUiPluginsDirectory(parser.value(uiPluginsDirOption));
    }

    if (parser.isSet(releaseOption)) {
        pm.setRelease(parser.value(releaseOption));
    }
    
    if (command == "search") {
        return cmdSearch(pm, positionalArgs, jsonOutput);
    } else if (command == "list") {
        QString category = parser.value(categoryOption);
        bool installedOnly = parser.isSet(installedOption);
        return cmdList(pm, category, installedOnly, jsonOutput);
    } else if (command == "install") {
        if (parser.isSet(fileOption)) {
            return cmdInstallFile(pm, parser.value(fileOption));
        }
        return cmdInstall(pm, positionalArgs);
    } else if (command == "categories") {
        return cmdCategories(pm, jsonOutput);
    } else if (command == "info") {
        return cmdInfo(pm, positionalArgs, jsonOutput);
    } else {
        err << "Error: unknown command '" << command << "'" << Qt::endl;
        err << "Run 'lgpm --help' for usage information" << Qt::endl;
        return 1;
    }
}
