// Integration tests — real PackageManagerLib from ../lib (built by Nix / workspace).
// Skipped at configure time when the library is not present.

#include <logos_test.h>
#include "package_manager_impl.h"

#include <QDir>
#include <QTemporaryDir>

LOGOS_TEST(integration_getValidVariants_non_empty) {
    PackageManagerImpl impl;
    std::vector<std::string> v = impl.getValidVariants();
    LOGOS_ASSERT_FALSE(v.empty());
    for (const auto& s : v) {
        LOGOS_ASSERT_FALSE(s.empty());
    }
}

LOGOS_TEST(integration_empty_user_modules_yields_empty_lists) {
    QTemporaryDir dir;
    LOGOS_ASSERT_TRUE(dir.isValid());

    PackageManagerImpl impl;
    impl.setUserModulesDirectory(dir.path().toStdString());
    impl.setUserUiPluginsDirectory(dir.path().toStdString());

    LOGOS_ASSERT_TRUE(impl.getInstalledModules().empty());
    LOGOS_ASSERT_TRUE(impl.getInstalledUiPlugins().empty());
    LOGOS_ASSERT_TRUE(impl.getInstalledPackages().empty());
}

LOGOS_TEST(integration_embedded_directory_scan_empty) {
    QTemporaryDir emb;
    LOGOS_ASSERT_TRUE(emb.isValid());

    PackageManagerImpl impl;
    impl.addEmbeddedModulesDirectory(emb.path().toStdString());

    // No packages under an empty embedded dir
    LOGOS_ASSERT_TRUE(impl.getInstalledModules().empty());
}
