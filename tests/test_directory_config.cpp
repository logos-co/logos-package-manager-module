/**
 * Tests for directory configuration methods on PackageManagerImpl.
 */
#include <logos_test.h>
#include "package_manager_impl.h"

// =============================================================================
// Embedded modules directory
// =============================================================================

LOGOS_TEST(set_embedded_modules_directory) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setEmbeddedModulesDirectory("/some/path");
    // No crash, no error — method delegates to underlying lib
}

LOGOS_TEST(add_embedded_modules_directory) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setEmbeddedModulesDirectory("/first");
    impl.addEmbeddedModulesDirectory("/second");
}

// =============================================================================
// Embedded UI plugins directory
// =============================================================================

LOGOS_TEST(set_embedded_ui_plugins_directory) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setEmbeddedUiPluginsDirectory("/ui/path");
}

LOGOS_TEST(add_embedded_ui_plugins_directory) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setEmbeddedUiPluginsDirectory("/ui/first");
    impl.addEmbeddedUiPluginsDirectory("/ui/second");
}

// =============================================================================
// User directories
// =============================================================================

LOGOS_TEST(set_user_modules_directory) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setUserModulesDirectory("/user/modules");
}

LOGOS_TEST(set_user_ui_plugins_directory) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setUserUiPluginsDirectory("/user/ui");
}

// =============================================================================
// Valid variants
// =============================================================================

LOGOS_TEST(get_valid_variants_returns_nonempty) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QStringList variants = impl.getValidVariants();
    LOGOS_ASSERT_FALSE(variants.isEmpty());
    // Each variant should be a non-empty string
    for (const auto& v : variants) {
        LOGOS_ASSERT_FALSE(v.isEmpty());
    }
}

LOGOS_TEST(get_valid_variants_contains_platform) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QStringList variants = impl.getValidVariants();
    // Should contain the current platform (possibly with -dev suffix)
    bool hasPlatform = false;
    for (const auto& v : variants) {
#if defined(__APPLE__)
        if (v.contains("darwin")) hasPlatform = true;
#elif defined(__linux__)
        if (v.contains("linux")) hasPlatform = true;
#endif
    }
    LOGOS_ASSERT_TRUE(hasPlatform);
}
