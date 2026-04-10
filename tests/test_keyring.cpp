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
#include <algorithm>

/**
 * Helper: read DID from a .did file.
 */
static std::string readDid(const QString& keysDir, const QString& keyName) {
    QFile f(keysDir + "/" + keyName + ".did");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed().toStdString();
}

/**
 * Helper: generate a keypair and return the DID.
 */
static std::string generateKeyAndGetDid(const QString& keysDir, const QString& keyName) {
    lgx_result_t res = lgx_keygen(keyName.toStdString().c_str(), keysDir.toStdString().c_str());
    if (!res.success) return {};
    return readDid(keysDir, keyName);
}

// =============================================================================
// Add trusted key
// =============================================================================

LOGOS_TEST(add_trusted_key_success) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    std::string did = generateKeyAndGetDid(keysDir, "addkey");
    LOGOS_ASSERT_FALSE(did.empty());

    LogosMap result = impl.addTrustedKey("publisher", did, "Test Publisher", "https://test.com");
    LOGOS_ASSERT_TRUE(result["success"].get<bool>());
}

LOGOS_TEST(add_trusted_key_invalid_did) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    LogosMap result = impl.addTrustedKey("bad", "not-a-did", "", "");
    LOGOS_ASSERT_FALSE(result["success"].get<bool>());
    LOGOS_ASSERT_TRUE(result.contains("error"));
}

// =============================================================================
// Remove trusted key
// =============================================================================

LOGOS_TEST(remove_trusted_key_success) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    std::string did = generateKeyAndGetDid(keysDir, "rmkey");
    LOGOS_ASSERT_FALSE(did.empty());

    // Add then remove
    LogosMap addResult = impl.addTrustedKey("to-remove", did, "", "");
    LOGOS_ASSERT_TRUE(addResult["success"].get<bool>());

    LogosMap rmResult = impl.removeTrustedKey("to-remove");
    LOGOS_ASSERT_TRUE(rmResult["success"].get<bool>());
}

LOGOS_TEST(remove_nonexistent_key) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    LogosMap result = impl.removeTrustedKey("does-not-exist");
    LOGOS_ASSERT_FALSE(result["success"].get<bool>());
}

// =============================================================================
// List trusted keys
// =============================================================================

LOGOS_TEST(list_trusted_keys_empty) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    LogosList keys = impl.listTrustedKeys();
    LOGOS_ASSERT_EQ(keys.size(), static_cast<size_t>(0));
}

LOGOS_TEST(list_trusted_keys_after_add) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    std::string did1 = generateKeyAndGetDid(keysDir, "listkey1");
    std::string did2 = generateKeyAndGetDid(keysDir, "listkey2");
    LOGOS_ASSERT_FALSE(did1.empty());
    LOGOS_ASSERT_FALSE(did2.empty());

    impl.addTrustedKey("key-one", did1, "Publisher One", "https://one.com");
    impl.addTrustedKey("key-two", did2, "Publisher Two", "");

    LogosList keys = impl.listTrustedKeys();
    LOGOS_ASSERT_EQ(keys.size(), static_cast<size_t>(2));

    // Collect names
    std::vector<std::string> names;
    for (const auto& key : keys) {
        names.push_back(key["name"].get<std::string>());
    }
    LOGOS_ASSERT_TRUE(std::find(names.begin(), names.end(), "key-one") != names.end());
    LOGOS_ASSERT_TRUE(std::find(names.begin(), names.end(), "key-two") != names.end());
}

LOGOS_TEST(list_trusted_keys_contains_metadata) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    std::string did = generateKeyAndGetDid(keysDir, "metakey");
    LOGOS_ASSERT_FALSE(did.empty());

    impl.addTrustedKey("meta-publisher", did, "Test Name", "https://test.com");

    LogosList keys = impl.listTrustedKeys();
    LOGOS_ASSERT_EQ(keys.size(), static_cast<size_t>(1));

    LogosMap key = keys[0];
    LOGOS_ASSERT_EQ(key["name"].get<std::string>(), std::string("meta-publisher"));
    LOGOS_ASSERT_EQ(key["did"].get<std::string>(), did);
    LOGOS_ASSERT_EQ(key["displayName"].get<std::string>(), std::string("Test Name"));
    LOGOS_ASSERT_EQ(key["url"].get<std::string>(), std::string("https://test.com"));
    LOGOS_ASSERT_FALSE(key["addedAt"].get<std::string>().empty());
}

// =============================================================================
// Add + verify round-trip
// =============================================================================

LOGOS_TEST(keyring_add_then_verify_trusted) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

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

    std::string variant = [] {
        auto v = PackageManagerLib::platformVariantsToTry();
        return v.empty() ? std::string("unknown") : v.front();
    }();

    lgx_add_variant(pkg, variant.c_str(),
                    contentDir.toStdString().c_str(),
                    libName.toStdString().c_str());
    lgx_save(pkg, lgxPath.toStdString().c_str());
    lgx_free_package(pkg);

    // Generate key and sign
    QString keyPath = keysDir + "/rtkey.jwk";
    lgx_keygen("rtkey", keysDir.toStdString().c_str());
    lgx_sign(lgxPath.toStdString().c_str(), keyPath.toStdString().c_str(), nullptr, nullptr);

    // Verify: not yet trusted
    LogosMap r1 = impl.verifyPackage(lgxPath.toStdString());
    LOGOS_ASSERT_TRUE(r1["isSigned"].get<bool>());
    LOGOS_ASSERT_TRUE(r1["signatureValid"].get<bool>());
    LOGOS_ASSERT_TRUE(r1["trustedAs"].get<std::string>().empty());

    // Trust the key
    std::string did = readDid(keysDir, "rtkey");
    impl.addTrustedKey("rt-publisher", did, "", "");

    // Verify: now trusted
    LogosMap r2 = impl.verifyPackage(lgxPath.toStdString());
    LOGOS_ASSERT_TRUE(r2["isSigned"].get<bool>());
    LOGOS_ASSERT_EQ(r2["trustedAs"].get<std::string>(), std::string("rt-publisher"));
}
