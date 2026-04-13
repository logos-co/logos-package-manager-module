/**
 * Tests for module/plugin scanning methods on PackageManagerImpl.
 */
#include <logos_test.h>
#include "package_manager_impl.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

/**
 * Helper: create a fake installed module directory with manifest.json.
 */
static void createFakeModule(const QString& baseDir, const QString& name,
                              const QString& type, const QString& version = "1.0.0") {
    QString moduleDir = baseDir + "/" + name;
    QDir().mkpath(moduleDir);

    QJsonObject manifest;
    manifest["name"] = name;
    manifest["type"] = type;
    manifest["version"] = version;
    manifest["description"] = "Test module " + name;
    manifest["category"] = "test";
    manifest["main"] = name + ".so";

    QFile f(moduleDir + "/manifest.json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(manifest).toJson());
}

// =============================================================================
// Empty / non-existent directories
// =============================================================================

LOGOS_TEST(get_installed_packages_empty_dir) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosList packages = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(packages.size(), static_cast<size_t>(0));
}

LOGOS_TEST(get_installed_modules_empty_dir) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosList modules = impl.getInstalledModules();
    LOGOS_ASSERT_EQ(modules.size(), static_cast<size_t>(0));
}

LOGOS_TEST(get_installed_ui_plugins_empty_dir) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setEmbeddedUiPluginsDirectory(tmpDir.path().toStdString());

    LogosList plugins = impl.getInstalledUiPlugins();
    LOGOS_ASSERT_EQ(plugins.size(), static_cast<size_t>(0));
}

// =============================================================================
// Scanning populated directories
// =============================================================================

LOGOS_TEST(get_installed_modules_finds_core_modules) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "core_mod", "core");
    createFakeModule(tmpDir.path(), "ui_mod", "ui");

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosList modules = impl.getInstalledModules();
    // Only core modules should be returned
    LOGOS_ASSERT_EQ(modules.size(), static_cast<size_t>(1));

    LogosMap mod = modules[0];
    LOGOS_ASSERT_EQ(mod["name"].get<std::string>(), std::string("core_mod"));
}

LOGOS_TEST(get_installed_ui_plugins_finds_ui_modules) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "core_mod", "core");
    createFakeModule(tmpDir.path(), "ui_mod", "ui");
    createFakeModule(tmpDir.path(), "qml_mod", "ui_qml");

    impl.setEmbeddedUiPluginsDirectory(tmpDir.path().toStdString());

    LogosList plugins = impl.getInstalledUiPlugins();
    // Should find ui and ui_qml modules
    LOGOS_ASSERT_EQ(plugins.size(), static_cast<size_t>(2));
}

LOGOS_TEST(get_installed_packages_returns_all_types) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "core_mod", "core");
    createFakeModule(tmpDir.path(), "ui_mod", "ui");
    createFakeModule(tmpDir.path(), "qml_mod", "ui_qml");

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosList packages = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(packages.size(), static_cast<size_t>(3));
}

LOGOS_TEST(scanned_modules_contain_manifest_fields) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "test_mod", "core", "2.1.0");

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosList modules = impl.getInstalledModules();
    LOGOS_ASSERT_EQ(modules.size(), static_cast<size_t>(1));

    LogosMap mod = modules[0];
    LOGOS_ASSERT_EQ(mod["name"].get<std::string>(), std::string("test_mod"));
    LOGOS_ASSERT_EQ(mod["version"].get<std::string>(), std::string("2.1.0"));
    LOGOS_ASSERT_EQ(mod["type"].get<std::string>(), std::string("core"));
}
