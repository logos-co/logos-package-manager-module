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
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setEmbeddedModulesDirectory(tmpDir.path());

    QVariantList packages = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(packages.size(), 0);
}

LOGOS_TEST(get_installed_modules_empty_dir) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setEmbeddedModulesDirectory(tmpDir.path());

    QVariantList modules = impl.getInstalledModules();
    LOGOS_ASSERT_EQ(modules.size(), 0);
}

LOGOS_TEST(get_installed_ui_plugins_empty_dir) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setEmbeddedUiPluginsDirectory(tmpDir.path());

    QVariantList plugins = impl.getInstalledUiPlugins();
    LOGOS_ASSERT_EQ(plugins.size(), 0);
}

// =============================================================================
// Scanning populated directories
// =============================================================================

LOGOS_TEST(get_installed_modules_finds_core_modules) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "core_mod", "core");
    createFakeModule(tmpDir.path(), "ui_mod", "ui");

    impl.setEmbeddedModulesDirectory(tmpDir.path());

    QVariantList modules = impl.getInstalledModules();
    // Only core modules should be returned
    LOGOS_ASSERT_EQ(modules.size(), 1);

    QVariantMap mod = modules[0].toMap();
    LOGOS_ASSERT_EQ(mod["name"].toString(), QString("core_mod"));
}

LOGOS_TEST(get_installed_ui_plugins_finds_ui_modules) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "core_mod", "core");
    createFakeModule(tmpDir.path(), "ui_mod", "ui");
    createFakeModule(tmpDir.path(), "qml_mod", "ui_qml");

    impl.setEmbeddedUiPluginsDirectory(tmpDir.path());

    QVariantList plugins = impl.getInstalledUiPlugins();
    // Should find ui and ui_qml modules
    LOGOS_ASSERT_EQ(plugins.size(), 2);
}

LOGOS_TEST(get_installed_packages_returns_all_types) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "core_mod", "core");
    createFakeModule(tmpDir.path(), "ui_mod", "ui");
    createFakeModule(tmpDir.path(), "qml_mod", "ui_qml");

    impl.setEmbeddedModulesDirectory(tmpDir.path());

    QVariantList packages = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(packages.size(), 3);
}

LOGOS_TEST(scanned_modules_contain_manifest_fields) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModule(tmpDir.path(), "test_mod", "core", "2.1.0");

    impl.setEmbeddedModulesDirectory(tmpDir.path());

    QVariantList modules = impl.getInstalledModules();
    LOGOS_ASSERT_EQ(modules.size(), 1);

    QVariantMap mod = modules[0].toMap();
    LOGOS_ASSERT_EQ(mod["name"].toString(), QString("test_mod"));
    LOGOS_ASSERT_EQ(mod["version"].toString(), QString("2.1.0"));
    LOGOS_ASSERT_EQ(mod["type"].toString(), QString("core"));
}
