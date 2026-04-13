// Unit tests for PackageManagerImpl — PackageManagerLib is mocked (mock_package_manager_lib.cpp).

#include <logos_test.h>
#include "package_manager_impl.h"

#include <string>

LOGOS_TEST(onInit_does_not_throw) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    LOGOS_ASSERT_FALSE(t.moduleCalled("any_module", "any_method"));
}

LOGOS_TEST(installPlugin_success_core_emits_core_event) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("installPluginFile_result").returns("/installed/core.dylib");
    t.mockCFunction("installPluginFile_installedPath").returns("/installed/core.dylib");
    t.mockCFunction("installPluginFile_error").returns("");
    t.mockCFunction("installPluginFile_isCore").returns(true);

    std::string lastEvent;
    std::string lastEventData;
    PackageManagerImpl impl;
    impl.emitEvent = [&](const std::string& name, const std::string& data) {
        lastEvent = name;
        lastEventData = data;
    };

    LogosMap m = impl.installPlugin("/path/to/foo.lgx", false);
    LOGOS_ASSERT_EQ(m["path"].get<std::string>(), std::string("/installed/core.dylib"));
    LOGOS_ASSERT_TRUE(m["isCoreModule"].get<bool>());
    LOGOS_ASSERT_FALSE(m.contains("error"));
    LOGOS_ASSERT_EQ(m["name"].get<std::string>(), std::string("foo"));
    LOGOS_ASSERT_EQ(m["signatureStatus"].get<std::string>(), std::string("unsigned"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("verifyPackageSignature"));
    LOGOS_ASSERT_EQ(lastEvent, std::string("corePluginFileInstalled"));
    LOGOS_ASSERT_EQ(lastEventData, std::string("/installed/core.dylib"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("installPluginFile"));
}

LOGOS_TEST(installPlugin_success_ui_emits_ui_event) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("installPluginFile_result").returns("/ui/plugin.qml");
    t.mockCFunction("installPluginFile_installedPath").returns("/ui/plugin.qml");
    t.mockCFunction("installPluginFile_error").returns("");
    t.mockCFunction("installPluginFile_isCore").returns(false);

    std::string lastEvent;
    PackageManagerImpl impl;
    impl.emitEvent = [&](const std::string& name, const std::string&) { lastEvent = name; };

    LogosMap m = impl.installPlugin("/path/bar.lgx", false);
    LOGOS_ASSERT_FALSE(m["isCoreModule"].get<bool>());
    LOGOS_ASSERT_EQ(lastEvent, std::string("uiPluginFileInstalled"));
}

LOGOS_TEST(installPlugin_failure_sets_error_no_event) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("installPluginFile_result").returns("");
    t.mockCFunction("installPluginFile_error").returns("invalid lgx");
    t.mockCFunction("installPluginFile_installedPath").returns("");

    std::string lastEvent;
    PackageManagerImpl impl;
    impl.emitEvent = [&](const std::string& name, const std::string&) { lastEvent = name; };

    LogosMap m = impl.installPlugin("/bad.lgx", false);
    LOGOS_ASSERT_TRUE(m["path"].get<std::string>().empty());
    LOGOS_ASSERT_EQ(m["error"].get<std::string>(), std::string("invalid lgx"));
    LOGOS_ASSERT_TRUE(lastEvent.empty());
}

LOGOS_TEST(installPlugin_skipIfNotNewerVersion_passed_to_mock) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("installPluginFile_result").returns("/ok");
    t.mockCFunction("installPluginFile_installedPath").returns("/ok");
    t.mockCFunction("installPluginFile_error").returns("");

    PackageManagerImpl impl;

    impl.installPlugin("/x.lgx", true);
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("installPluginFile_skipIfNotNewer_true"));

    impl.installPlugin("/y.lgx", false);
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("installPluginFile_skipIfNotNewer_false"));
}

LOGOS_TEST(setEmbeddedModulesDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.setEmbeddedModulesDirectory("/emb/mod");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setEmbeddedModulesDirectory"));
}

LOGOS_TEST(addEmbeddedModulesDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.addEmbeddedModulesDirectory("/emb/m2");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("addEmbeddedModulesDirectory"));
}

LOGOS_TEST(setEmbeddedUiPluginsDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.setEmbeddedUiPluginsDirectory("/emb/ui");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setEmbeddedUiPluginsDirectory"));
}

LOGOS_TEST(addEmbeddedUiPluginsDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.addEmbeddedUiPluginsDirectory("/emb/ui2");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("addEmbeddedUiPluginsDirectory"));
}

LOGOS_TEST(setUserModulesDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.setUserModulesDirectory("/user/mod");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setUserModulesDirectory"));
}

LOGOS_TEST(setUserUiPluginsDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.setUserUiPluginsDirectory("/user/ui");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setUserUiPluginsDirectory"));
}

LOGOS_TEST(getInstalledPackages_parses_json_array) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("getInstalledPackages").returns(R"([{"name":"pkg1","version":"1.0.0"}])");

    PackageManagerImpl impl;

    LogosList list = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("pkg1"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("getInstalledPackages"));
}

LOGOS_TEST(getInstalledModules_parses_json) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("getInstalledModules").returns(R"([{"name":"mod_a"}])");

    PackageManagerImpl impl;

    LogosList list = impl.getInstalledModules();
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("mod_a"));
}

LOGOS_TEST(getInstalledUiPlugins_parses_json) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("getInstalledUiPlugins").returns(R"([{"name":"ui_z"}])");

    PackageManagerImpl impl;

    LogosList list = impl.getInstalledUiPlugins();
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("ui_z"));
}

LOGOS_TEST(getInstalledPackages_empty_json_array) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("getInstalledPackages").returns("[]");

    PackageManagerImpl impl;

    LOGOS_ASSERT_TRUE(impl.getInstalledPackages().empty());
}

LOGOS_TEST(getValidVariants_uses_platformVariantsToTry) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("platformVariantsToTry_first").returns("custom-variant");

    PackageManagerImpl impl;

    std::vector<std::string> v = impl.getValidVariants();
    LOGOS_ASSERT_EQ(static_cast<int>(v.size()), 1);
    LOGOS_ASSERT_EQ(v[0], std::string("custom-variant"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("platformVariantsToTry"));
}

LOGOS_TEST(getValidVariants_default_mock_variant) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;

    std::vector<std::string> v = impl.getValidVariants();
    LOGOS_ASSERT_FALSE(v.empty());
    LOGOS_ASSERT_EQ(v[0], std::string("mock-variant"));
}

LOGOS_TEST(no_cross_module_calls_by_default) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    LOGOS_ASSERT_EQ(t.moduleCallCount("capability_module", "requestModule"), 0);
}

LOGOS_TEST(setSignaturePolicy_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.setSignaturePolicy("warn");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setSignaturePolicy"));
}

LOGOS_TEST(setKeyringDirectory_forwards_to_lib) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    impl.setKeyringDirectory("/kr");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setKeyringDirectory"));
}

LOGOS_TEST(verifyPackage_maps_signature_result) {
    auto t = LogosTestContext("package_manager");
    t.mockCFunction("verifyPackageSignature_is_signed").returns(true);
    t.mockCFunction("verifyPackageSignature_signature_valid").returns(true);
    t.mockCFunction("verifyPackageSignature_package_valid").returns(true);
    t.mockCFunction("verifyPackageSignature_signer_did").returns("did:jwk:test");

    PackageManagerImpl impl;

    LogosMap m = impl.verifyPackage("/any.lgx");
    LOGOS_ASSERT_TRUE(m["isSigned"].get<bool>());
    LOGOS_ASSERT_TRUE(m["signatureValid"].get<bool>());
    LOGOS_ASSERT_TRUE(m["packageValid"].get<bool>());
    LOGOS_ASSERT_EQ(m["signerDid"].get<std::string>(), std::string("did:jwk:test"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("verifyPackageSignature"));
}
