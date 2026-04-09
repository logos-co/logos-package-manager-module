// Integration tests — real PackageManagerLib from ../lib (built by Nix / workspace).
// Skipped at configure time when the library is not present.

#include <logos_test.h>
#include "package_manager_impl.h"

#include <QDir>
#include <QTemporaryDir>

LOGOS_TEST(integration_getValidVariants_non_empty) {
    PackageManagerImpl impl;
    QStringList v = impl.getValidVariants();
    LOGOS_ASSERT_FALSE(v.isEmpty());
    for (const QString& s : v) {
        LOGOS_ASSERT_FALSE(s.isEmpty());
    }
}

LOGOS_TEST(integration_empty_user_modules_yields_empty_lists) {
    QTemporaryDir dir;
    LOGOS_ASSERT_TRUE(dir.isValid());

    PackageManagerImpl impl;
    impl.setUserModulesDirectory(dir.path());
    impl.setUserUiPluginsDirectory(dir.path());

    LOGOS_ASSERT_TRUE(impl.getInstalledModules().isEmpty());
    LOGOS_ASSERT_TRUE(impl.getInstalledUiPlugins().isEmpty());
    LOGOS_ASSERT_TRUE(impl.getInstalledPackages().isEmpty());
}

LOGOS_TEST(integration_embedded_directory_scan_empty) {
    QTemporaryDir emb;
    LOGOS_ASSERT_TRUE(emb.isValid());

    PackageManagerImpl impl;
    impl.addEmbeddedModulesDirectory(emb.path());

    // No packages under an empty embedded dir
    LOGOS_ASSERT_TRUE(impl.getInstalledModules().isEmpty());
}
