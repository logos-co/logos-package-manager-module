/**
 * Tests for directory configuration methods on PackageManagerImpl.
 */
#include <logos_test.h>
#include "package_manager_impl.h"

// =============================================================================
// Embedded modules directory
// =============================================================================

LOGOS_TEST(set_embedded_modules_directory) {
    PackageManagerImpl impl;

    impl.setEmbeddedModulesDirectory("/some/path");
    // No crash, no error — method delegates to underlying lib
}

LOGOS_TEST(add_embedded_modules_directory) {
    PackageManagerImpl impl;

    impl.setEmbeddedModulesDirectory("/first");
    impl.addEmbeddedModulesDirectory("/second");
}

// =============================================================================
// Embedded UI plugins directory
// =============================================================================

LOGOS_TEST(set_embedded_ui_plugins_directory) {
    PackageManagerImpl impl;

    impl.setEmbeddedUiPluginsDirectory("/ui/path");
}

LOGOS_TEST(add_embedded_ui_plugins_directory) {
    PackageManagerImpl impl;

    impl.setEmbeddedUiPluginsDirectory("/ui/first");
    impl.addEmbeddedUiPluginsDirectory("/ui/second");
}

// =============================================================================
// User directories
// =============================================================================

LOGOS_TEST(set_user_modules_directory) {
    PackageManagerImpl impl;

    impl.setUserModulesDirectory("/user/modules");
}

LOGOS_TEST(set_user_ui_plugins_directory) {
    PackageManagerImpl impl;

    impl.setUserUiPluginsDirectory("/user/ui");
}

// =============================================================================
// Valid variants
// =============================================================================

LOGOS_TEST(get_valid_variants_returns_nonempty) {
    PackageManagerImpl impl;

    std::vector<std::string> variants = impl.getValidVariants();
    LOGOS_ASSERT_FALSE(variants.empty());
    for (const auto& v : variants) {
        LOGOS_ASSERT_FALSE(v.empty());
    }
}

LOGOS_TEST(get_valid_variants_contains_platform) {
    PackageManagerImpl impl;

    std::vector<std::string> variants = impl.getValidVariants();
    bool hasPlatform = false;
    for (const auto& v : variants) {
#if defined(__APPLE__)
        if (v.find("darwin") != std::string::npos) hasPlatform = true;
#elif defined(__linux__)
        if (v.find("linux") != std::string::npos) hasPlatform = true;
#endif
    }
    LOGOS_ASSERT_TRUE(hasPlatform);
}
