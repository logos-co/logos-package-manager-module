/**
 * Tests for keyring management methods on PackageManagerImpl.
 */
#include <logos_test.h>
#include "package_manager_impl.h"
#include <package_manager_lib.h>
#include <lgx.h>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>

/**
 * Helper: read DID from a .did file.
 */
static QString readDid(const QString& keysDir, const QString& keyName) {
    QFile f(keysDir + "/" + keyName + ".did");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

/**
 * Helper: generate a keypair and return the DID.
 */
static QString generateKeyAndGetDid(const QString& keysDir, const QString& keyName) {
    lgx_result_t res = lgx_keygen(keyName.toStdString().c_str(), keysDir.toStdString().c_str());
    if (!res.success) return {};

    QFile f(keysDir + "/" + keyName + ".did");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

// =============================================================================
// Add trusted key
// =============================================================================

LOGOS_TEST(add_trusted_key_success) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    QString did = generateKeyAndGetDid(keysDir, "addkey");
    LOGOS_ASSERT_FALSE(did.isEmpty());

    QVariantMap result = impl.addTrustedKey("publisher", did, "Test Publisher", "https://test.com");
    LOGOS_ASSERT_TRUE(result["success"].toBool());
}

LOGOS_TEST(add_trusted_key_invalid_did) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    QVariantMap result = impl.addTrustedKey("bad", "not-a-did", "", "");
    LOGOS_ASSERT_FALSE(result["success"].toBool());
    LOGOS_ASSERT_TRUE(result.contains("error"));
}

// =============================================================================
// Remove trusted key
// =============================================================================

LOGOS_TEST(remove_trusted_key_success) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    QString did = generateKeyAndGetDid(keysDir, "rmkey");
    LOGOS_ASSERT_FALSE(did.isEmpty());

    // Add then remove
    QVariantMap addResult = impl.addTrustedKey("to-remove", did, "", "");
    LOGOS_ASSERT_TRUE(addResult["success"].toBool());

    QVariantMap rmResult = impl.removeTrustedKey("to-remove");
    LOGOS_ASSERT_TRUE(rmResult["success"].toBool());
}

LOGOS_TEST(remove_nonexistent_key) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    QVariantMap result = impl.removeTrustedKey("does-not-exist");
    LOGOS_ASSERT_FALSE(result["success"].toBool());
}

// =============================================================================
// List trusted keys
// =============================================================================

LOGOS_TEST(list_trusted_keys_empty) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    QVariantList keys = impl.listTrustedKeys();
    LOGOS_ASSERT_EQ(keys.size(), 0);
}

LOGOS_TEST(list_trusted_keys_after_add) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    QString did1 = generateKeyAndGetDid(keysDir, "listkey1");
    QString did2 = generateKeyAndGetDid(keysDir, "listkey2");
    LOGOS_ASSERT_FALSE(did1.isEmpty());
    LOGOS_ASSERT_FALSE(did2.isEmpty());

    impl.addTrustedKey("key-one", did1, "Publisher One", "https://one.com");
    impl.addTrustedKey("key-two", did2, "Publisher Two", "");

    QVariantList keys = impl.listTrustedKeys();
    LOGOS_ASSERT_EQ(keys.size(), 2);

    // Collect names
    QStringList names;
    for (const auto& key : keys) {
        names << key.toMap()["name"].toString();
    }
    LOGOS_ASSERT_TRUE(names.contains("key-one"));
    LOGOS_ASSERT_TRUE(names.contains("key-two"));
}

LOGOS_TEST(list_trusted_keys_contains_metadata) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    QString did = generateKeyAndGetDid(keysDir, "metakey");
    LOGOS_ASSERT_FALSE(did.isEmpty());

    impl.addTrustedKey("meta-publisher", did, "Test Name", "https://test.com");

    QVariantList keys = impl.listTrustedKeys();
    LOGOS_ASSERT_EQ(keys.size(), 1);

    QVariantMap key = keys[0].toMap();
    LOGOS_ASSERT_EQ(key["name"].toString(), QString("meta-publisher"));
    LOGOS_ASSERT_EQ(key["did"].toString(), did);
    LOGOS_ASSERT_EQ(key["displayName"].toString(), QString("Test Name"));
    LOGOS_ASSERT_EQ(key["url"].toString(), QString("https://test.com"));
    LOGOS_ASSERT_FALSE(key["addedAt"].toString().isEmpty());
}

// =============================================================================
// Add + verify round-trip
// =============================================================================

LOGOS_TEST(keyring_add_then_verify_trusted) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir);

    // Create and sign a package
    QString lgxPath = tmpDir.path() + "/rt_test.lgx";
    QString contentDir = tmpDir.path() + "/rt_content";
    QDir().mkpath(contentDir);

#if defined(__APPLE__)
    QString libName = "rt_test_plugin.dylib";
#else
    QString libName = "rt_test_plugin.so";
#endif
    { QFile f(contentDir + "/" + libName); f.open(QIODevice::WriteOnly); f.write("fake"); }

    lgx_create(lgxPath.toStdString().c_str(), "rt_test");
    lgx_package_t pkg = lgx_load(lgxPath.toStdString().c_str());
    lgx_set_version(pkg, "1.0.0");

    QString variant = [] {
        auto v = PackageManagerLib::platformVariantsToTry();
        return v.empty() ? "unknown" : QString::fromStdString(v.front());
    }();

    lgx_add_variant(pkg, variant.toStdString().c_str(),
                    contentDir.toStdString().c_str(),
                    libName.toStdString().c_str());
    lgx_save(pkg, lgxPath.toStdString().c_str());
    lgx_free_package(pkg);

    // Generate key and sign
    QString keyPath = keysDir + "/rtkey.jwk";
    lgx_keygen("rtkey", keysDir.toStdString().c_str());
    lgx_sign(lgxPath.toStdString().c_str(), keyPath.toStdString().c_str(), nullptr, nullptr);

    // Verify: not yet trusted
    QVariantMap r1 = impl.verifyPackage(lgxPath);
    LOGOS_ASSERT_TRUE(r1["isSigned"].toBool());
    LOGOS_ASSERT_TRUE(r1["signatureValid"].toBool());
    LOGOS_ASSERT_TRUE(r1["trustedAs"].toString().isEmpty());

    // Trust the key
    QString did = readDid(keysDir, "rtkey");
    impl.addTrustedKey("rt-publisher", did, "", "");

    // Verify: now trusted
    QVariantMap r2 = impl.verifyPackage(lgxPath);
    LOGOS_ASSERT_TRUE(r2["isSigned"].toBool());
    LOGOS_ASSERT_EQ(r2["trustedAs"].toString(), QString("rt-publisher"));
}

