/**
 * Tests for module/plugin scanning methods on PackageManagerImpl.
 */
#include <logos_test.h>
#include "package_manager_impl.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
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

// =============================================================================
// Dependency constraints, end to end through the REAL PackageManagerLib
//
// test_package_manager.cpp mocks the library, so it pins the serialiser but not
// the evaluation. These run the real scanner and semver engine over an on-disk
// manifest — the only place the range surviving the scan, being evaluated,
// and crossing THIS ABI intact are all exercised together.
// =============================================================================

/**
 * Fake installed module whose `dependencies` array is written verbatim, so a
 * test can use the object form { name, version, signer } that the plain-string
 * helper above cannot express.
 */
static void createFakeModuleWithRawDeps(const QString& baseDir, const QString& name,
                                        const QJsonArray& deps,
                                        const QString& version = "1.0.0") {
    QString moduleDir = baseDir + "/" + name;
    QDir().mkpath(moduleDir);

    QJsonObject manifest;
    manifest["name"] = name;
    manifest["type"] = "core";
    manifest["version"] = version;
    manifest["main"] = name + ".so";
    manifest["dependencies"] = deps;

    QFile f(moduleDir + "/manifest.json");
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(manifest).toJson());
}

LOGOS_TEST(resolve_dependencies_reports_version_mismatch_end_to_end) {
    // app needs lib ^2.0.0; lib 1.0.0 is installed.
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QJsonObject dep;
    dep["name"] = "lib";
    dep["version"] = "^2.0.0";
    createFakeModuleWithRawDeps(tmpDir.path(), "app", QJsonArray{dep});
    createFakeModule(tmpDir.path(), "lib", "core", "1.0.0");

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosMap tree = impl.resolveDependencies("app", true);
    LOGOS_ASSERT_EQ(tree["children"].size(), static_cast<size_t>(1));
    LogosMap child = tree["children"][0];
    LOGOS_ASSERT_EQ(child["name"].get<std::string>(), std::string("lib"));
    LOGOS_ASSERT_EQ(child["status"].get<std::string>(), std::string("version_mismatch"));
    // Both numbers reach the caller: what was asked for, and what is there.
    LOGOS_ASSERT_EQ(child["requiredVersion"].get<std::string>(), std::string("^2.0.0"));
    LOGOS_ASSERT_EQ(child["version"].get<std::string>(), std::string("1.0.0"));
}

LOGOS_TEST(resolve_dependencies_satisfied_range_is_installed_end_to_end) {
    // The control: same shape, lib inside the range.
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QJsonObject dep;
    dep["name"] = "lib";
    dep["version"] = "^2.0.0";
    createFakeModuleWithRawDeps(tmpDir.path(), "app", QJsonArray{dep});
    createFakeModule(tmpDir.path(), "lib", "core", "2.1.0");

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosMap tree = impl.resolveDependencies("app", true);
    LogosMap child = tree["children"][0];
    LOGOS_ASSERT_EQ(child["status"].get<std::string>(), std::string("installed"));
    LOGOS_ASSERT_EQ(child["requiredVersion"].get<std::string>(), std::string("^2.0.0"));
}

LOGOS_TEST(resolve_dependencies_absent_outranks_mismatch_end_to_end) {
    // Absent AND constrained reports absence, the stronger fact; the range
    // still travels so a caller can name the version to install.
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QJsonObject dep;
    dep["name"] = "lib";
    dep["version"] = "^2.0.0";
    createFakeModuleWithRawDeps(tmpDir.path(), "app", QJsonArray{dep});
    // lib deliberately not created.

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosMap tree = impl.resolveDependencies("app", true);
    LogosMap child = tree["children"][0];
    LOGOS_ASSERT_EQ(child["status"].get<std::string>(), std::string("not_installed"));
    LOGOS_ASSERT_EQ(child["version"].get<std::string>(), std::string(""));
    LOGOS_ASSERT_EQ(child["requiredVersion"].get<std::string>(), std::string("^2.0.0"));
}

LOGOS_TEST(get_installed_packages_carries_constraints_end_to_end) {
    // Over a real manifest, `lgpm --json info` and this API report the same
    // constraint.
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QJsonObject dep;
    dep["name"] = "lib";
    dep["version"] = "^2.0.0";
    dep["signer"] = "did:jwk:eyJrdHkiOiJPS1AifQ";
    createFakeModuleWithRawDeps(tmpDir.path(), "app", QJsonArray{QString("plain"), dep});

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosList packages = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(packages.size(), static_cast<size_t>(1));
    LogosMap app = packages[0];

    // The edge set is untouched: still plain strings, both entries, in order.
    LOGOS_ASSERT_EQ(app["dependencies"].size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(app["dependencies"][0].get<std::string>(), std::string("plain"));
    LOGOS_ASSERT_EQ(app["dependencies"][1].get<std::string>(), std::string("lib"));

    LOGOS_ASSERT_TRUE(app.contains("dependencyConstraints"));
    LOGOS_ASSERT_EQ(app["dependencyConstraints"].size(), static_cast<size_t>(1));
    LogosMap c = app["dependencyConstraints"][0];
    LOGOS_ASSERT_EQ(c["name"].get<std::string>(), std::string("lib"));
    LOGOS_ASSERT_EQ(c["version"].get<std::string>(), std::string("^2.0.0"));
    LOGOS_ASSERT_EQ(c["signer"].get<std::string>(), std::string("did:jwk:eyJrdHkiOiJPS1AifQ"));
}

LOGOS_TEST(get_installed_packages_omits_constraints_for_bare_names_end_to_end) {
    // The key must be absent entirely.
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    createFakeModuleWithRawDeps(tmpDir.path(), "app", QJsonArray{QString("lib")});

    impl.setEmbeddedModulesDirectory(tmpDir.path().toStdString());

    LogosList packages = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(packages.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(packages[0]["dependencies"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_FALSE(packages[0].contains("dependencyConstraints"));
}
