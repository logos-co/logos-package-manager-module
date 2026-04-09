// Unit tests for PackageManagerImpl — PackageManagerLib is mocked (mock_package_manager_lib.cpp).

#include <logos_test.h>
#include "package_manager_impl.h"

#include <QString>

LOGOS_TEST(onInit_does_not_throw) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    LOGOS_ASSERT_FALSE(t.moduleCalled("any_module", "any_method"));
}

LOGOS_TEST(installPlugin_success_core_emits_core_event) {
    auto t = LogosTestContext("package_manager");
    t.captureEvents();
    t.mockCFunction("installPluginFile_result").returns("/installed/core.dylib");
    t.mockCFunction("installPluginFile_installedPath").returns("/installed/core.dylib");
    t.mockCFunction("installPluginFile_error").returns("");
    t.mockCFunction("installPluginFile_isCore").returns(true);

    PackageManagerImpl impl;
    t.init(&impl);

    QVariantMap m = impl.installPlugin(QStringLiteral("/path/to/foo.lgx"), false);
    LOGOS_ASSERT_EQ(m[QStringLiteral("path")].toString(), QStringLiteral("/installed/core.dylib"));
    LOGOS_ASSERT_TRUE(m[QStringLiteral("isCoreModule")].toBool());
    LOGOS_ASSERT_FALSE(m.contains(QStringLiteral("error")));
    LOGOS_ASSERT_EQ(m[QStringLiteral("name")].toString(), QStringLiteral("foo"));
    LOGOS_ASSERT_EQ(m[QStringLiteral("signatureStatus")].toString(), QStringLiteral("unsigned"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("verifyPackageSignature"));
    LOGOS_ASSERT_TRUE(t.eventEmitted("corePluginFileInstalled"));
    LOGOS_ASSERT_FALSE(t.eventEmitted("uiPluginFileInstalled"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("installPluginFile"));
}

LOGOS_TEST(installPlugin_success_ui_emits_ui_event) {
    auto t = LogosTestContext("package_manager");
    t.captureEvents();
    t.mockCFunction("installPluginFile_result").returns("/ui/plugin.qml");
    t.mockCFunction("installPluginFile_installedPath").returns("/ui/plugin.qml");
    t.mockCFunction("installPluginFile_error").returns("");
    t.mockCFunction("installPluginFile_isCore").returns(false);

    PackageManagerImpl impl;
    t.init(&impl);

    QVariantMap m = impl.installPlugin(QStringLiteral("/path/bar.lgx"), false);
    LOGOS_ASSERT_FALSE(m[QStringLiteral("isCoreModule")].toBool());
    LOGOS_ASSERT_TRUE(t.eventEmitted("uiPluginFileInstalled"));
    LOGOS_ASSERT_FALSE(t.eventEmitted("corePluginFileInstalled"));
}

LOGOS_TEST(installPlugin_failure_sets_error_no_event) {
    auto t = LogosTestContext("package_manager");
    t.captureEvents();
    t.mockCFunction("installPluginFile_result").returns("");
    t.mockCFunction("installPluginFile_error").returns("invalid lgx");
    t.mockCFunction("installPluginFile_installedPath").returns("");

    PackageManagerImpl impl;
    t.init(&impl);

    QVariantMap m = impl.installPlugin(QStringLiteral("/bad.lgx"), false);
    LOGOS_ASSERT_TRUE(m[QStringLiteral("path")].toString().isEmpty());
    LOGOS_ASSERT_EQ(m[QStringLiteral("error")].toString(), QStringLiteral("invalid lgx"));
    LOGOS_ASSERT_FALSE(t.eventEmitted("corePluginFileInstalled"));
    LOGOS_ASSERT_FALSE(t.eventEmitted("uiPluginFileInstalled"));
}

LOGOS_TEST(installPlugin_skipIfNotNewerVersion_passed_to_mock) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("installPluginFile_result").returns("/ok");
    t.mockCFunction("installPluginFile_installedPath").returns("/ok");
    t.mockCFunction("installPluginFile_error").returns("");

    PackageManagerImpl impl;
    t.init(&impl);

    impl.installPlugin(QStringLiteral("/x.lgx"), true);
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("installPluginFile_skipIfNotNewer_true"));

    impl.installPlugin(QStringLiteral("/y.lgx"), false);
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("installPluginFile_skipIfNotNewer_false"));
}

LOGOS_TEST(setEmbeddedModulesDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.setEmbeddedModulesDirectory(QStringLiteral("/emb/mod"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setEmbeddedModulesDirectory"));
}

LOGOS_TEST(addEmbeddedModulesDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.addEmbeddedModulesDirectory(QStringLiteral("/emb/m2"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("addEmbeddedModulesDirectory"));
}

LOGOS_TEST(setEmbeddedUiPluginsDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.setEmbeddedUiPluginsDirectory(QStringLiteral("/emb/ui"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setEmbeddedUiPluginsDirectory"));
}

LOGOS_TEST(addEmbeddedUiPluginsDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.addEmbeddedUiPluginsDirectory(QStringLiteral("/emb/ui2"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("addEmbeddedUiPluginsDirectory"));
}

LOGOS_TEST(setUserModulesDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.setUserModulesDirectory(QStringLiteral("/user/mod"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setUserModulesDirectory"));
}

LOGOS_TEST(setUserUiPluginsDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.setUserUiPluginsDirectory(QStringLiteral("/user/ui"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setUserUiPluginsDirectory"));
}

LOGOS_TEST(getInstalledPackages_parses_json_array) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("getInstalledPackages").returns(R"([{"name":"pkg1","version":"1.0.0"}])");

    PackageManagerImpl impl;
    t.init(&impl);

    QVariantList list = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(list.size(), 1);
    QVariantMap row = list[0].toMap();
    LOGOS_ASSERT_EQ(row[QStringLiteral("name")].toString(), QStringLiteral("pkg1"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("getInstalledPackages"));
}

LOGOS_TEST(getInstalledModules_parses_json) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("getInstalledModules").returns(R"([{"name":"mod_a"}])");

    PackageManagerImpl impl;
    t.init(&impl);

    QVariantList list = impl.getInstalledModules();
    LOGOS_ASSERT_EQ(list.size(), 1);
    LOGOS_ASSERT_EQ(list[0].toMap()[QStringLiteral("name")].toString(), QStringLiteral("mod_a"));
}

LOGOS_TEST(getInstalledUiPlugins_parses_json) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("getInstalledUiPlugins").returns(R"([{"name":"ui_z"}])");

    PackageManagerImpl impl;
    t.init(&impl);

    QVariantList list = impl.getInstalledUiPlugins();
    LOGOS_ASSERT_EQ(list.size(), 1);
    LOGOS_ASSERT_EQ(list[0].toMap()[QStringLiteral("name")].toString(), QStringLiteral("ui_z"));
}

LOGOS_TEST(getInstalledPackages_empty_json_array) {
    auto t = LogosTestContext("package_manager");
    // Default mock returns "[]" when getInstalledPackages string not set; force explicit
    t.mockCFunction("getInstalledPackages").returns("[]");

    PackageManagerImpl impl;
    t.init(&impl);

    LOGOS_ASSERT_TRUE(impl.getInstalledPackages().isEmpty());
}

LOGOS_TEST(getValidVariants_uses_platformVariantsToTry) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("platformVariantsToTry_first").returns("custom-variant");

    PackageManagerImpl impl;
    t.init(&impl);

    QStringList v = impl.getValidVariants();
    LOGOS_ASSERT_EQ(v.size(), 1);
    LOGOS_ASSERT_EQ(v[0], QStringLiteral("custom-variant"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("platformVariantsToTry"));
}

LOGOS_TEST(getValidVariants_default_mock_variant) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QStringList v = impl.getValidVariants();
    LOGOS_ASSERT_FALSE(v.isEmpty());
    LOGOS_ASSERT_EQ(v[0], QStringLiteral("mock-variant"));
}

LOGOS_TEST(no_cross_module_calls_by_default) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    LOGOS_ASSERT_EQ(t.moduleCallCount("capability_module", "requestModule"), 0);
}

LOGOS_TEST(setSignaturePolicy_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.setSignaturePolicy(QStringLiteral("warn"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setSignaturePolicy"));
}

LOGOS_TEST(setKeyringDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);
    impl.setKeyringDirectory(QStringLiteral("/kr"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setKeyringDirectory"));
}

LOGOS_TEST(verifyPackage_maps_signature_result) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("verifyPackageSignature_is_signed").returns(true);
    t.mockCFunction("verifyPackageSignature_signature_valid").returns(true);
    t.mockCFunction("verifyPackageSignature_package_valid").returns(true);
    t.mockCFunction("verifyPackageSignature_signer_did").returns("did:jwk:test");

    PackageManagerImpl impl;
    t.init(&impl);

    QVariantMap m = impl.verifyPackage(QStringLiteral("/any.lgx"));
    LOGOS_ASSERT_TRUE(m[QStringLiteral("isSigned")].toBool());
    LOGOS_ASSERT_TRUE(m[QStringLiteral("signatureValid")].toBool());
    LOGOS_ASSERT_TRUE(m[QStringLiteral("packageValid")].toBool());
    LOGOS_ASSERT_EQ(m[QStringLiteral("signerDid")].toString(), QStringLiteral("did:jwk:test"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("verifyPackageSignature"));
}
