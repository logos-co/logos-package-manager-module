// Unit tests for PackageManagerImpl — PackageManagerLib is mocked
// (mock_package_manager_lib.cpp).
//
// Structured lib returns (installed packages, dependency tree, dependents)
// are registered via setMock*() helpers declared in mock_package_manager_lib.h.
// The registries reset automatically on each LogosTestContext, so tests just
// construct a context, set the mocks, and instantiate the impl.

#include <logos_test.h>
#include "package_manager_impl.h"
#include "mocks/mock_package_manager_lib.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

LOGOS_TEST(getInstalledPackages_returns_struct_registry) {
    auto t = LogosTestContext("package_manager");
    InstalledPackage pkg;
    pkg.name = "pkg1";
    pkg.version = "1.0.0";
    setMockInstalledPackages({pkg});

    PackageManagerImpl impl;

    LogosList list = impl.getInstalledPackages();
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("pkg1"));
    LOGOS_ASSERT_EQ(list[0]["version"].get<std::string>(), std::string("1.0.0"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("getInstalledPackages"));
}

LOGOS_TEST(getInstalledModules_returns_struct_registry) {
    auto t = LogosTestContext("package_manager");
    InstalledPackage mod;
    mod.name = "mod_a";
    mod.type = "core";
    setMockInstalledModules({mod});

    PackageManagerImpl impl;

    LogosList list = impl.getInstalledModules();
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("mod_a"));
    LOGOS_ASSERT_EQ(list[0]["type"].get<std::string>(), std::string("core"));
}

LOGOS_TEST(getInstalledUiPlugins_returns_struct_registry) {
    auto t = LogosTestContext("package_manager");
    InstalledPackage ui;
    ui.name = "ui_z";
    ui.type = "ui";
    setMockInstalledUiPlugins({ui});

    PackageManagerImpl impl;

    LogosList list = impl.getInstalledUiPlugins();
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("ui_z"));
}

LOGOS_TEST(getInstalledPackages_empty_registry) {
    auto t = LogosTestContext("package_manager");
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

LOGOS_TEST(uninstallPackage_success_core_emits_core_event) {
    auto t = LogosTestContext("package_manager");
    // Scan returns a core package — impl uses this to route the event.
    InstalledPackage pkg;
    pkg.name = "foo";
    pkg.type = "core";
    pkg.installType = InstallType::User;
    setMockInstalledPackages({pkg});
    t.mockCFunction("uninstallPackage_success").returns(true);
    t.mockCFunction("uninstallPackage_removed").returns("/a/foo.dylib,/a/manifest.json");

    std::string lastEvent;
    std::string lastEventData;
    PackageManagerImpl impl;
    impl.emitEvent = [&](const std::string& name, const std::string& data) {
        lastEvent = name;
        lastEventData = data;
    };

    LogosMap m = impl.uninstallPackage("foo");
    LOGOS_ASSERT_TRUE(m["success"].get<bool>());
    LOGOS_ASSERT_FALSE(m.contains("error"));
    LOGOS_ASSERT_EQ(m["removedFiles"].size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(lastEvent, std::string("corePluginUninstalled"));
    LOGOS_ASSERT_EQ(lastEventData, std::string("foo"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("uninstallPackage"));
}

LOGOS_TEST(uninstallPackage_success_ui_emits_ui_event) {
    auto t = LogosTestContext("package_manager");
    InstalledPackage pkg;
    pkg.name = "widget";
    pkg.type = "ui";
    pkg.installType = InstallType::User;
    setMockInstalledPackages({pkg});
    t.mockCFunction("uninstallPackage_success").returns(true);

    std::string lastEvent;
    PackageManagerImpl impl;
    impl.emitEvent = [&](const std::string& name, const std::string&) { lastEvent = name; };

    LogosMap m = impl.uninstallPackage("widget");
    LOGOS_ASSERT_TRUE(m["success"].get<bool>());
    LOGOS_ASSERT_EQ(lastEvent, std::string("uiPluginUninstalled"));
}

LOGOS_TEST(uninstallPackage_failure_sets_error_no_event) {
    auto t = LogosTestContext("package_manager");
    InstalledPackage pkg;
    pkg.name = "foo";
    pkg.type = "core";
    pkg.installType = InstallType::Embedded;
    setMockInstalledPackages({pkg});
    t.mockCFunction("uninstallPackage_success").returns(false);
    t.mockCFunction("uninstallPackage_error").returns("Cannot uninstall embedded package");

    std::string lastEvent;
    PackageManagerImpl impl;
    impl.emitEvent = [&](const std::string& name, const std::string&) { lastEvent = name; };

    LogosMap m = impl.uninstallPackage("foo");
    LOGOS_ASSERT_FALSE(m["success"].get<bool>());
    LOGOS_ASSERT_EQ(m["error"].get<std::string>(),
                    std::string("Cannot uninstall embedded package"));
    LOGOS_ASSERT_FALSE(m.contains("removedFiles"));
    LOGOS_ASSERT_TRUE(lastEvent.empty());
}

// ---------------------------------------------------------------------------
// Dependency-resolution helpers — tree fixtures reused across tests.
// ---------------------------------------------------------------------------
// Forward tree:
//   root (installed)
//   └── a (installed)
//       └── c (installed)
// Used by the resolveDependencies + resolveFlatDependencies tests.
static DependencyTreeNode makeForwardTree() {
    DependencyTreeNode root;
    root.name = "root";
    root.status = DependencyStatus::Installed;
    DependencyTreeNode a;
    a.name = "a";
    a.status = DependencyStatus::Installed;
    DependencyTreeNode c;
    c.name = "c";
    c.status = DependencyStatus::Installed;
    a.children = {c};
    root.children = {a};
    return root;
}

// Reverse tree rooted at "dep":
//   dep
//   └── parent_direct
//       └── parent_transitive
static DependentTreeNode makeReverseTree() {
    DependentTreeNode root;
    root.name = "dep";
    DependentTreeNode directChild;
    directChild.name = "parent_direct";
    directChild.version = "1.2.3";
    DependentTreeNode transitiveChild;
    transitiveChild.name = "parent_transitive";
    transitiveChild.version = "4.5.6";
    directChild.children = {transitiveChild};
    root.children = {directChild};
    return root;
}

LOGOS_TEST(resolveDependencies_recursive_returns_full_tree) {
    auto t = LogosTestContext("package_manager");
    setMockDependencyTree(makeForwardTree());

    PackageManagerImpl impl;

    LogosMap out = impl.resolveDependencies("root", true);
    LOGOS_ASSERT_EQ(out["name"].get<std::string>(), std::string("root"));
    LOGOS_ASSERT_EQ(out["status"].get<std::string>(), std::string("installed"));
    LOGOS_ASSERT_EQ(out["children"].size(), static_cast<size_t>(1));
    // Recursive walks the full tree — grandchild survives.
    LOGOS_ASSERT_EQ(out["children"][0]["name"].get<std::string>(), std::string("a"));
    LOGOS_ASSERT_EQ(out["children"][0]["children"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(out["children"][0]["children"][0]["name"].get<std::string>(), std::string("c"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("resolveDependencies"));
}

LOGOS_TEST(resolveDependencies_non_recursive_clips_to_depth_one) {
    auto t = LogosTestContext("package_manager");
    setMockDependencyTree(makeForwardTree());

    PackageManagerImpl impl;

    LogosMap out = impl.resolveDependencies("root", false);
    LOGOS_ASSERT_EQ(out["name"].get<std::string>(), std::string("root"));
    LOGOS_ASSERT_EQ(out["children"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(out["children"][0]["name"].get<std::string>(), std::string("a"));
    // Non-recursive clips at depth 1: "a" has an empty children array, the
    // grandchild "c" does not appear.
    LOGOS_ASSERT_TRUE(out["children"][0]["children"].is_array());
    LOGOS_ASSERT_TRUE(out["children"][0]["children"].empty());
}

LOGOS_TEST(resolveDependencies_unknown_returns_empty_object) {
    auto t = LogosTestContext("package_manager");
    // No registered tree — mock returns nullopt; impl serialises to {}.
    PackageManagerImpl impl;

    LogosMap out = impl.resolveDependencies("ghost", true);
    LOGOS_ASSERT_TRUE(out.is_object());
    LOGOS_ASSERT_TRUE(out.empty());
}

LOGOS_TEST(resolveDependents_recursive_returns_full_tree) {
    auto t = LogosTestContext("package_manager");
    setMockDependentTree(makeReverseTree());

    PackageManagerImpl impl;

    LogosMap out = impl.resolveDependents("dep", true);
    LOGOS_ASSERT_EQ(out["name"].get<std::string>(), std::string("dep"));
    LOGOS_ASSERT_EQ(out["children"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(out["children"][0]["name"].get<std::string>(), std::string("parent_direct"));
    LOGOS_ASSERT_EQ(out["children"][0]["version"].get<std::string>(), std::string("1.2.3"));
    // Full tree — grandchild survives.
    LOGOS_ASSERT_EQ(out["children"][0]["children"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(out["children"][0]["children"][0]["name"].get<std::string>(),
                    std::string("parent_transitive"));
    // No `direct` field in the new wire format.
    LOGOS_ASSERT_FALSE(out["children"][0].contains("direct"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("resolveDependents"));
}

LOGOS_TEST(resolveDependents_non_recursive_clips_to_depth_one) {
    auto t = LogosTestContext("package_manager");
    setMockDependentTree(makeReverseTree());

    PackageManagerImpl impl;

    LogosMap out = impl.resolveDependents("dep", false);
    LOGOS_ASSERT_EQ(out["name"].get<std::string>(), std::string("dep"));
    LOGOS_ASSERT_EQ(out["children"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(out["children"][0]["name"].get<std::string>(), std::string("parent_direct"));
    // Clipped: grandchild does not appear.
    LOGOS_ASSERT_TRUE(out["children"][0]["children"].is_array());
    LOGOS_ASSERT_TRUE(out["children"][0]["children"].empty());
}

LOGOS_TEST(resolveDependents_unknown_returns_empty_object) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;

    LogosMap out = impl.resolveDependents("ghost", true);
    LOGOS_ASSERT_TRUE(out.is_object());
    LOGOS_ASSERT_TRUE(out.empty());
}

LOGOS_TEST(resolveFlatDependencies_recursive_flattens_tree) {
    auto t = LogosTestContext("package_manager");
    setMockDependencyTree(makeForwardTree());

    PackageManagerImpl impl;

    LogosList list = impl.resolveFlatDependencies("root", true);
    // Descendants: a + c. Root itself is NOT included.
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(2));
    std::set<std::string> names;
    for (const auto& item : list) names.insert(item["name"].get<std::string>());
    LOGOS_ASSERT_TRUE(names.count("a") != 0);
    LOGOS_ASSERT_TRUE(names.count("c") != 0);
    // No `children` field in the flat wire format.
    LOGOS_ASSERT_FALSE(list[0].contains("children"));
}

LOGOS_TEST(resolveFlatDependencies_non_recursive_emits_children_only) {
    auto t = LogosTestContext("package_manager");
    setMockDependencyTree(makeForwardTree());

    PackageManagerImpl impl;

    LogosList list = impl.resolveFlatDependencies("root", false);
    // Only direct children of root — "a". Grandchild "c" does not appear.
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("a"));
    LOGOS_ASSERT_FALSE(list[0].contains("children"));
}

LOGOS_TEST(resolveFlatDependencies_unknown_returns_empty_list) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;

    LogosList list = impl.resolveFlatDependencies("ghost", true);
    LOGOS_ASSERT_TRUE(list.is_array());
    LOGOS_ASSERT_TRUE(list.empty());
}

LOGOS_TEST(resolveFlatDependents_recursive_flattens_tree) {
    auto t = LogosTestContext("package_manager");
    setMockDependentTree(makeReverseTree());

    PackageManagerImpl impl;

    LogosList list = impl.resolveFlatDependents("dep", true);
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(2));
    std::set<std::string> names;
    for (const auto& item : list) names.insert(item["name"].get<std::string>());
    LOGOS_ASSERT_TRUE(names.count("parent_direct") != 0);
    LOGOS_ASSERT_TRUE(names.count("parent_transitive") != 0);
    LOGOS_ASSERT_FALSE(list[0].contains("direct"));
    LOGOS_ASSERT_FALSE(list[0].contains("children"));
}

LOGOS_TEST(resolveFlatDependents_non_recursive_emits_children_only) {
    auto t = LogosTestContext("package_manager");
    setMockDependentTree(makeReverseTree());

    PackageManagerImpl impl;

    LogosList list = impl.resolveFlatDependents("dep", false);
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("parent_direct"));
    LOGOS_ASSERT_FALSE(list[0].contains("children"));
}

LOGOS_TEST(resolveFlatDependents_unknown_returns_empty_list) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;

    LogosList list = impl.resolveFlatDependents("ghost", true);
    LOGOS_ASSERT_TRUE(list.is_array());
    LOGOS_ASSERT_TRUE(list.empty());
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

// ===========================================================================
// Gated uninstall / upgrade flow
// ===========================================================================
//
// Tests the two-phase listener-ack protocol:
//
//   requestXxx -> beforeXxx event -> ackPendingAction (within ackTimeout)
//               -> confirmXxx (destructive) | cancelXxx (aborts)
//
// The ack-timeout worker emits on a background thread, so every test uses
// the thread-safe EventCapture helper below. Synchronous paths (request /
// confirm / cancel) also go through the same capture to keep assertions
// uniform. Timeout-path tests shrink the timeout via setAckTimeoutMsForTest
// so they fire in milliseconds rather than the 3-second production default.

namespace {

// Thread-safe capture of (eventName, data) tuples emitted via
// PackageManagerImpl::emitEvent, with a blocking wait helper for tests that
// need to observe background-thread emissions (ack-timeout path).
struct EventCapture {
    struct Entry { std::string name; std::string data; };

    std::mutex              mu;
    std::condition_variable cv;
    std::vector<Entry>      entries;

    void record(const std::string& name, const std::string& data) {
        std::lock_guard<std::mutex> lk(mu);
        entries.push_back({name, data});
        cv.notify_all();
    }

    // Install as emitEvent on a PackageManagerImpl.
    auto callback() {
        return [this](const std::string& n, const std::string& d) { record(n, d); };
    }

    size_t size() {
        std::lock_guard<std::mutex> lk(mu);
        return entries.size();
    }

    bool has(const std::string& name) {
        std::lock_guard<std::mutex> lk(mu);
        for (const auto& e : entries) if (e.name == name) return true;
        return false;
    }

    // Returns {name,data} copies for all matching events.
    std::vector<Entry> all(const std::string& name) {
        std::lock_guard<std::mutex> lk(mu);
        std::vector<Entry> out;
        for (const auto& e : entries) if (e.name == name) out.push_back(e);
        return out;
    }

    // Block up to `timeoutMs` waiting for at least one entry with `name`.
    // Returns the first matching entry (copy), or an empty-string-name Entry
    // on timeout.
    Entry waitFor(const std::string& name, int timeoutMs) {
        std::unique_lock<std::mutex> lk(mu);
        bool got = cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
            for (const auto& e : entries) if (e.name == name) return true;
            return false;
        });
        if (!got) return {};
        for (const auto& e : entries) if (e.name == name) return e;
        return {};
    }
};

// Convenience: registers `pkgName` as an installed user-package so
// isEmbedded / installedDependentsNames return sensible values. `type` is
// "core" or "ui" — controls which *Uninstalled event doUninstall emits.
static void primeInstalledUserPackage(const std::string& pkgName,
                                      const std::string& type = "core") {
    InstalledPackage pkg;
    pkg.name = pkgName;
    pkg.type = type;
    pkg.installType = InstallType::User;
    setMockInstalledPackages({pkg});
}

} // namespace

// ---------------------------------------------------------------------------
// requestUninstall / requestUpgrade: validation, happy path, pending lock-out
// ---------------------------------------------------------------------------

LOGOS_TEST(requestUninstall_rejects_empty_name) {
    auto t = LogosTestContext("package_manager");
    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap r = impl.requestUninstall("");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(),
                    std::string("Package name cannot be empty"));
    LOGOS_ASSERT_EQ(events.size(), static_cast<size_t>(0));
}

LOGOS_TEST(requestUninstall_rejects_embedded_package) {
    auto t = LogosTestContext("package_manager");
    InstalledPackage pkg;
    pkg.name = "core_embed";
    pkg.type = "core";
    pkg.installType = InstallType::Embedded;
    setMockInstalledPackages({pkg});

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap r = impl.requestUninstall("core_embed");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(),
                    std::string("Cannot uninstall embedded module 'core_embed'"));
    LOGOS_ASSERT_FALSE(events.has("beforeUninstall"));
}

LOGOS_TEST(requestUninstall_happy_emits_beforeUninstall_with_dependents) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    // A listener-style reverse tree rooted at "foo" with one direct dependent.
    DependentTreeNode root;
    root.name = "foo";
    DependentTreeNode parent;
    parent.name = "parent_pkg";
    root.children = {parent};
    setMockDependentTree(root);

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap r = impl.requestUninstall("foo");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_FALSE(r.contains("error"));

    auto matches = events.all("beforeUninstall");
    LOGOS_ASSERT_EQ(matches.size(), static_cast<size_t>(1));
    LogosMap payload = LogosMap::parse(matches[0].data);
    LOGOS_ASSERT_EQ(payload["name"].get<std::string>(), std::string("foo"));
    LOGOS_ASSERT_EQ(payload["installedDependents"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(payload["installedDependents"][0].get<std::string>(),
                    std::string("parent_pkg"));

    // Clean up the pending action so the dtor doesn't emit a timeout event.
    impl.resetPendingAction();
}

LOGOS_TEST(requestUninstall_rejects_when_already_pending) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap first = impl.requestUninstall("foo");
    LOGOS_ASSERT_TRUE(first["success"].get<bool>());

    LogosMap second = impl.requestUninstall("foo");
    LOGOS_ASSERT_FALSE(second["success"].get<bool>());
    // Error message mentions the in-progress op ("uninstall") and its name.
    std::string err = second["error"].get<std::string>();
    LOGOS_ASSERT_TRUE(err.find("uninstall") != std::string::npos);
    LOGOS_ASSERT_TRUE(err.find("foo") != std::string::npos);

    impl.resetPendingAction();
}

LOGOS_TEST(requestUpgrade_rejects_empty_name) {
    auto t = LogosTestContext("package_manager");
    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap r = impl.requestUpgrade("", "v1.0.0", 0);
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(),
                    std::string("Package name cannot be empty"));
    LOGOS_ASSERT_FALSE(events.has("beforeUpgrade"));
}

LOGOS_TEST(requestUpgrade_rejects_embedded_package) {
    auto t = LogosTestContext("package_manager");
    InstalledPackage pkg;
    pkg.name = "core_embed";
    pkg.type = "core";
    pkg.installType = InstallType::Embedded;
    setMockInstalledPackages({pkg});

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap r = impl.requestUpgrade("core_embed", "v2.0.0", 0);
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(),
                    std::string("Cannot upgrade embedded module 'core_embed'"));
    LOGOS_ASSERT_FALSE(events.has("beforeUpgrade"));
}

LOGOS_TEST(requestUpgrade_happy_emits_beforeUpgrade_with_tag_and_mode) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap r = impl.requestUpgrade("foo", "v2.0.0", 7);
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());

    auto matches = events.all("beforeUpgrade");
    LOGOS_ASSERT_EQ(matches.size(), static_cast<size_t>(1));
    LogosMap payload = LogosMap::parse(matches[0].data);
    LOGOS_ASSERT_EQ(payload["name"].get<std::string>(), std::string("foo"));
    LOGOS_ASSERT_EQ(payload["releaseTag"].get<std::string>(), std::string("v2.0.0"));
    LOGOS_ASSERT_EQ(payload["mode"].get<int64_t>(), static_cast<int64_t>(7));

    impl.resetPendingAction();
}

LOGOS_TEST(requestUpgrade_rejects_when_already_pending) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v1.0.0", 0)["success"].get<bool>());

    // A second gated op (of either kind) must fail while another is pending.
    LogosMap blocked = impl.requestUninstall("foo");
    LOGOS_ASSERT_FALSE(blocked["success"].get<bool>());
    std::string err = blocked["error"].get<std::string>();
    LOGOS_ASSERT_TRUE(err.find("upgrade") != std::string::npos);

    impl.resetPendingAction();
}

// ---------------------------------------------------------------------------
// ackPendingAction: success / missing / mismatch / idempotent
// ---------------------------------------------------------------------------

LOGOS_TEST(ackPendingAction_success_on_pending_uninstall) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());

    LogosMap ack = impl.ackPendingAction("foo");
    LOGOS_ASSERT_TRUE(ack["success"].get<bool>());
    LOGOS_ASSERT_FALSE(ack.contains("error"));

    impl.resetPendingAction();
}

LOGOS_TEST(ackPendingAction_no_pending_returns_error) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;

    LogosMap ack = impl.ackPendingAction("anything");
    LOGOS_ASSERT_FALSE(ack["success"].get<bool>());
    LOGOS_ASSERT_TRUE(ack["error"].get<std::string>().find("No matching pending") != std::string::npos);
}

LOGOS_TEST(ackPendingAction_name_mismatch_rejected) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());

    LogosMap ack = impl.ackPendingAction("not_foo");
    LOGOS_ASSERT_FALSE(ack["success"].get<bool>());
    LOGOS_ASSERT_TRUE(ack["error"].get<std::string>().find("not_foo") != std::string::npos);

    impl.resetPendingAction();
}

LOGOS_TEST(ackPendingAction_idempotent_on_repeat) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());
    // Second ack should still succeed (same pending, already acked).
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    impl.resetPendingAction();
}

// ---------------------------------------------------------------------------
// confirmUninstall: happy / failed-uninstall / missing / mismatch
// ---------------------------------------------------------------------------

LOGOS_TEST(confirmUninstall_happy_performs_uninstall_and_emits_core_event) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");
    t.mockCFunction("uninstallPackage_success").returns(true);
    t.mockCFunction("uninstallPackage_removed").returns("/m/foo.dylib");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.confirmUninstall("foo");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["removedFiles"].size(), static_cast<size_t>(1));
    LOGOS_ASSERT_TRUE(events.has("corePluginUninstalled"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("uninstallPackage"));

    // Pending cleared — a subsequent confirm should fail with "no matching".
    LogosMap again = impl.confirmUninstall("foo");
    LOGOS_ASSERT_FALSE(again["success"].get<bool>());
}

LOGOS_TEST(confirmUninstall_surfaces_uninstall_failure) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");
    t.mockCFunction("uninstallPackage_success").returns(false);
    t.mockCFunction("uninstallPackage_error").returns("delete failed");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.confirmUninstall("foo");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(), std::string("delete failed"));
    LOGOS_ASSERT_FALSE(events.has("corePluginUninstalled"));
    LOGOS_ASSERT_FALSE(events.has("uiPluginUninstalled"));
}

LOGOS_TEST(confirmUninstall_no_pending_returns_error) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;

    LogosMap r = impl.confirmUninstall("foo");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(r["error"].get<std::string>().find("No matching pending uninstall") != std::string::npos);
}

LOGOS_TEST(confirmUninstall_name_mismatch_returns_error) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.confirmUninstall("bar");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    // Original pending still valid.
    LOGOS_ASSERT_TRUE(impl.cancelUninstall("foo")["success"].get<bool>());
}

// ---------------------------------------------------------------------------
// confirmUpgrade: happy / tag-mismatch / failed-uninstall suppresses event
// ---------------------------------------------------------------------------

LOGOS_TEST(confirmUpgrade_happy_emits_upgradeUninstallDone) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");
    t.mockCFunction("uninstallPackage_success").returns(true);

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 3)["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.confirmUpgrade("foo", "v2.0.0");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());

    auto done = events.all("upgradeUninstallDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(1));
    LogosMap payload = LogosMap::parse(done[0].data);
    LOGOS_ASSERT_EQ(payload["name"].get<std::string>(), std::string("foo"));
    LOGOS_ASSERT_EQ(payload["releaseTag"].get<std::string>(), std::string("v2.0.0"));
    LOGOS_ASSERT_EQ(payload["mode"].get<int64_t>(), static_cast<int64_t>(3));
}

LOGOS_TEST(confirmUpgrade_suppresses_event_on_uninstall_failure) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");
    // Uninstall step inside confirmUpgrade fails — upgradeUninstallDone must NOT fire.
    t.mockCFunction("uninstallPackage_success").returns(false);
    t.mockCFunction("uninstallPackage_error").returns("cannot remove");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 0)["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.confirmUpgrade("foo", "v2.0.0");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_FALSE(events.has("upgradeUninstallDone"));
}

LOGOS_TEST(confirmUpgrade_tag_mismatch_returns_error) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 0)["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.confirmUpgrade("foo", "v3.0.0");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(r["error"].get<std::string>().find("No matching pending upgrade") != std::string::npos);

    // Original upgrade still pending — the mismatched tag must not have cleared state.
    LOGOS_ASSERT_TRUE(impl.cancelUpgrade("foo", "v2.0.0")["success"].get<bool>());
}

LOGOS_TEST(confirmUpgrade_no_pending_returns_error) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;

    LogosMap r = impl.confirmUpgrade("foo", "v1.0.0");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(r["error"].get<std::string>().find("No matching pending upgrade") != std::string::npos);
}

LOGOS_TEST(confirmUpgrade_requires_ack_before_confirm) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 0)["success"].get<bool>());

    LogosMap r = impl.confirmUpgrade("foo", "v2.0.0");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(r["error"].get<std::string>().find("has not been acknowledged") != std::string::npos);
    LOGOS_ASSERT_FALSE(events.has("upgradeUninstallDone"));

    // Pending action should remain active until acknowledged/cancelled/reset.
    // Ack first because cancelUpgrade (like confirmUpgrade) now requires a
    // prior ack for protocol uniformity — see cancelUpgrade_requires_ack_before_cancel.
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.cancelUpgrade("foo", "v2.0.0")["success"].get<bool>());
}

// ---------------------------------------------------------------------------
// cancelUninstall / cancelUpgrade: happy / missing / mismatch
// ---------------------------------------------------------------------------

LOGOS_TEST(cancelUninstall_emits_uninstallCancelled_with_user_reason) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.cancelUninstall("foo");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());

    auto cancelled = events.all("uninstallCancelled");
    LOGOS_ASSERT_EQ(cancelled.size(), static_cast<size_t>(1));
    LogosMap payload = LogosMap::parse(cancelled[0].data);
    LOGOS_ASSERT_EQ(payload["name"].get<std::string>(), std::string("foo"));
    LOGOS_ASSERT_EQ(payload["reason"].get<std::string>(), std::string("user cancelled"));

    // State cleared — a second cancel is a no-match error.
    LogosMap again = impl.cancelUninstall("foo");
    LOGOS_ASSERT_FALSE(again["success"].get<bool>());
}

LOGOS_TEST(cancelUninstall_no_pending_returns_error) {
    auto t = LogosTestContext("package_manager");
    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LogosMap r = impl.cancelUninstall("foo");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_FALSE(events.has("uninstallCancelled"));
}

LOGOS_TEST(cancelUpgrade_emits_upgradeCancelled_with_tag_and_reason) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 0)["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.cancelUpgrade("foo", "v2.0.0");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());

    auto cancelled = events.all("upgradeCancelled");
    LOGOS_ASSERT_EQ(cancelled.size(), static_cast<size_t>(1));
    LogosMap payload = LogosMap::parse(cancelled[0].data);
    LOGOS_ASSERT_EQ(payload["name"].get<std::string>(), std::string("foo"));
    LOGOS_ASSERT_EQ(payload["releaseTag"].get<std::string>(), std::string("v2.0.0"));
    LOGOS_ASSERT_EQ(payload["reason"].get<std::string>(), std::string("user cancelled"));
}

LOGOS_TEST(cancelUpgrade_tag_mismatch_returns_error) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 0)["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    LogosMap r = impl.cancelUpgrade("foo", "v9.9.9");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_FALSE(events.has("upgradeCancelled"));

    // Original pending survives the mismatched cancel.
    LOGOS_ASSERT_TRUE(impl.cancelUpgrade("foo", "v2.0.0")["success"].get<bool>());
}

LOGOS_TEST(cancelUninstall_requires_ack_before_cancel) {
    // Symmetric with confirmUninstall_requires_ack_before_confirm: cancel
    // also demands a prior ack so the gated protocol's two-phase contract
    // is uniform across all four confirm/cancel slots. An un-acked pending
    // state belongs to the ack-reception timer — cancelling it directly
    // would suppress the "no listener acknowledged" timeout event that
    // initiators rely on for uniform cancellation handling.
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());

    LogosMap r = impl.cancelUninstall("foo");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(r["error"].get<std::string>().find("has not been acknowledged") != std::string::npos);
    // No uninstallCancelled event fires — the un-acked request is still
    // owned by the timer, not by a confirming listener.
    LOGOS_ASSERT_FALSE(events.has("uninstallCancelled"));

    // Pending survives — an ack+cancel still succeeds.
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.cancelUninstall("foo")["success"].get<bool>());
}

LOGOS_TEST(cancelUpgrade_requires_ack_before_cancel) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 0)["success"].get<bool>());

    LogosMap r = impl.cancelUpgrade("foo", "v2.0.0");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(r["error"].get<std::string>().find("has not been acknowledged") != std::string::npos);
    LOGOS_ASSERT_FALSE(events.has("upgradeCancelled"));

    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.cancelUpgrade("foo", "v2.0.0")["success"].get<bool>());
}

// ---------------------------------------------------------------------------
// Ack-timeout worker: fires cancellation on no-ack; ack / confirm / cancel /
// reset all cancel the timer.
// ---------------------------------------------------------------------------

LOGOS_TEST(ackTimeout_fires_uninstallCancelled_with_timeout_reason) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    // Hook must be set before requestUninstall — otherwise the worker is
    // already running with the 3s default.
    impl.setAckTimeoutMsForTest(30);
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());

    // No ack within 30 ms — worker should fire uninstallCancelled.
    auto e = events.waitFor("uninstallCancelled", 1000);
    LOGOS_ASSERT_EQ(e.name, std::string("uninstallCancelled"));
    LogosMap payload = LogosMap::parse(e.data);
    LOGOS_ASSERT_EQ(payload["name"].get<std::string>(), std::string("foo"));
    std::string reason = payload["reason"].get<std::string>();
    LOGOS_ASSERT_TRUE(reason.find("no listener acknowledged") != std::string::npos);
    LOGOS_ASSERT_TRUE(reason.find("30ms") != std::string::npos);
}

LOGOS_TEST(ackTimeout_fires_upgradeCancelled_with_timeout_reason) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.setAckTimeoutMsForTest(30);
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUpgrade("foo", "v2.0.0", 0)["success"].get<bool>());

    auto e = events.waitFor("upgradeCancelled", 1000);
    LOGOS_ASSERT_EQ(e.name, std::string("upgradeCancelled"));
    LogosMap payload = LogosMap::parse(e.data);
    LOGOS_ASSERT_EQ(payload["name"].get<std::string>(), std::string("foo"));
    LOGOS_ASSERT_EQ(payload["releaseTag"].get<std::string>(), std::string("v2.0.0"));
    LOGOS_ASSERT_TRUE(payload["reason"].get<std::string>().find("no listener acknowledged") != std::string::npos);
}

LOGOS_TEST(ack_cancels_timer_no_timeout_event) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.setAckTimeoutMsForTest(30);
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());

    // Sleep past the timeout window — no cancellation should be emitted.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    LOGOS_ASSERT_FALSE(events.has("uninstallCancelled"));

    impl.resetPendingAction();
}

LOGOS_TEST(confirmUninstall_cancels_timer_no_timeout_event) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo", "core");
    t.mockCFunction("uninstallPackage_success").returns(true);

    EventCapture events;
    PackageManagerImpl impl;
    impl.setAckTimeoutMsForTest(30);
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.confirmUninstall("foo")["success"].get<bool>());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    LOGOS_ASSERT_FALSE(events.has("uninstallCancelled"));
    // The real uninstall event did still fire.
    LOGOS_ASSERT_TRUE(events.has("corePluginUninstalled"));
}

LOGOS_TEST(cancelUninstall_cancels_timer_only_user_reason_emitted) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.setAckTimeoutMsForTest(30);
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.ackPendingAction("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.cancelUninstall("foo")["success"].get<bool>());

    // Wait well past the short timeout — exactly one uninstallCancelled
    // (from cancelUninstall) with reason "user cancelled", no duplicate
    // from the worker.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto cancelled = events.all("uninstallCancelled");
    LOGOS_ASSERT_EQ(cancelled.size(), static_cast<size_t>(1));
    LogosMap payload = LogosMap::parse(cancelled[0].data);
    LOGOS_ASSERT_EQ(payload["reason"].get<std::string>(), std::string("user cancelled"));
}

LOGOS_TEST(resetPendingAction_cancels_timer_no_timeout_event) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.setAckTimeoutMsForTest(30);
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.resetPendingAction()["success"].get<bool>());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    LOGOS_ASSERT_FALSE(events.has("uninstallCancelled"));
    LOGOS_ASSERT_FALSE(events.has("upgradeCancelled"));
}

// ---------------------------------------------------------------------------
// resetPendingAction: clears pending state so the next request goes through.
// ---------------------------------------------------------------------------

LOGOS_TEST(resetPendingAction_allows_new_request) {
    auto t = LogosTestContext("package_manager");
    primeInstalledUserPackage("foo");

    EventCapture events;
    PackageManagerImpl impl;
    impl.emitEvent = events.callback();

    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());
    // Without reset the second request fails:
    LOGOS_ASSERT_FALSE(impl.requestUninstall("foo")["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.resetPendingAction()["success"].get<bool>());
    // Now it goes through again.
    LOGOS_ASSERT_TRUE(impl.requestUninstall("foo")["success"].get<bool>());

    impl.resetPendingAction();
}
