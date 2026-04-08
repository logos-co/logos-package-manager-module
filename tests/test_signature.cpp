/**
 * Tests for signature policy and verification on PackageManagerImpl.
 */
#include <logos_test.h>
#include "package_manager_impl.h"
#include <package_manager_lib.h>
#include <lgx.h>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>

/**
 * Helper to get the preferred platform variant (matches what the library expects).
 */
static QString currentVariant() {
    QStringList variants;
    for (const auto& v : PackageManagerLib::platformVariantsToTry()) {
        variants << QString::fromStdString(v);
    }
    return variants.isEmpty() ? "unknown" : variants.first();
}

/**
 * Helper: create a minimal unsigned .lgx package in a temp directory.
 * Returns path to the .lgx file.
 */
static QString createUnsignedPackage(const QString& dir, const QString& name) {
    QString lgxPath = dir + "/" + name + ".lgx";
    QString contentDir = dir + "/" + name + "_content";
    QDir().mkpath(contentDir);

    // Create a fake library file
#if defined(__APPLE__)
    QString libName = name + "_plugin.dylib";
#elif defined(_WIN32)
    QString libName = name + "_plugin.dll";
#else
    QString libName = name + "_plugin.so";
#endif
    {
        QFile f(contentDir + "/" + libName);
        f.open(QIODevice::WriteOnly);
        f.write("fake library content");
    }

    // Create a manifest.json
    {
        QFile f(contentDir + "/manifest.json");
        f.open(QIODevice::WriteOnly);
        f.write(QString("{"
            "\"name\":\"%1\","
            "\"version\":\"1.0.0\","
            "\"type\":\"core\","
            "\"description\":\"Test\","
            "\"category\":\"test\""
            "}").arg(name).toUtf8());
    }

    // Create LGX package
    lgx_result_t res = lgx_create(lgxPath.toStdString().c_str(), name.toStdString().c_str());
    if (!res.success) return {};

    lgx_package_t pkg = lgx_load(lgxPath.toStdString().c_str());
    if (!pkg) return {};

    lgx_set_version(pkg, "1.0.0");
    lgx_set_description(pkg, "Test package");

    std::string variant = currentVariant().toStdString();
    res = lgx_add_variant(pkg, variant.c_str(),
                          contentDir.toStdString().c_str(),
                          libName.toStdString().c_str());
    if (!res.success) { lgx_free_package(pkg); return {}; }

    res = lgx_save(pkg, lgxPath.toStdString().c_str());
    lgx_free_package(pkg);

    return res.success ? lgxPath : QString();
}

/**
 * Helper: generate a keypair. Returns path to .jwk file.
 */
static QString generateKey(const QString& keysDir, const QString& keyName) {
    lgx_result_t res = lgx_keygen(keyName.toStdString().c_str(), keysDir.toStdString().c_str());
    if (!res.success) return {};
    return keysDir + "/" + keyName + ".jwk";
}

/**
 * Helper: read DID from a .did file.
 */
static QString readDid(const QString& keysDir, const QString& keyName) {
    QFile f(keysDir + "/" + keyName + ".did");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

/**
 * Helper: sign a package with a key.
 */
static bool signPackage(const QString& lgxPath, const QString& keyPath,
                         const QString& signerName = {}, const QString& signerUrl = {}) {
    lgx_result_t res = lgx_sign(
        lgxPath.toStdString().c_str(),
        keyPath.toStdString().c_str(),
        signerName.isEmpty() ? nullptr : signerName.toStdString().c_str(),
        signerUrl.isEmpty() ? nullptr : signerUrl.toStdString().c_str()
    );
    return res.success;
}

// =============================================================================
// Signature policy configuration
// =============================================================================

LOGOS_TEST(set_signature_policy_none) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    // Should not crash or warn
    impl.setSignaturePolicy("none");
    impl.setSignaturePolicy("NONE");
    impl.setSignaturePolicy("None");
}

LOGOS_TEST(set_signature_policy_warn) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setSignaturePolicy("warn");
}

LOGOS_TEST(set_signature_policy_require) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setSignaturePolicy("require");
}

LOGOS_TEST(set_keyring_directory) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    impl.setKeyringDirectory("/custom/keyring");
}

// =============================================================================
// Verify unsigned package
// =============================================================================

LOGOS_TEST(verify_unsigned_package) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString lgxPath = createUnsignedPackage(tmpDir.path(), "verify_unsigned");
    LOGOS_ASSERT_FALSE(lgxPath.isEmpty());

    QVariantMap result = impl.verifyPackage(lgxPath);

    LOGOS_ASSERT_FALSE(result["isSigned"].toBool());
    LOGOS_ASSERT_FALSE(result["signatureValid"].toBool());
    LOGOS_ASSERT_TRUE(result["signerDid"].toString().isEmpty());
}

// =============================================================================
// Verify signed package
// =============================================================================

LOGOS_TEST(verify_signed_package) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QDir().mkpath(keysDir);

    QString lgxPath = createUnsignedPackage(tmpDir.path(), "verify_signed");
    LOGOS_ASSERT_FALSE(lgxPath.isEmpty());

    QString keyPath = generateKey(keysDir, "testkey");
    LOGOS_ASSERT_FALSE(keyPath.isEmpty());
    LOGOS_ASSERT_TRUE(signPackage(lgxPath, keyPath, "Test Signer", "https://example.com"));

    QVariantMap result = impl.verifyPackage(lgxPath);

    LOGOS_ASSERT_TRUE(result["isSigned"].toBool());
    LOGOS_ASSERT_TRUE(result["signatureValid"].toBool());
    LOGOS_ASSERT_TRUE(result["packageValid"].toBool());
    LOGOS_ASSERT_EQ(result["signerName"].toString(), QString("Test Signer"));
    LOGOS_ASSERT_EQ(result["signerUrl"].toString(), QString("https://example.com"));
    // DID should start with did:jwk:
    LOGOS_ASSERT_TRUE(result["signerDid"].toString().startsWith("did:jwk:"));
    // Not in keyring, so not trusted
    LOGOS_ASSERT_TRUE(result["trustedAs"].toString().isEmpty());
}

// =============================================================================
// Verify signed + trusted package
// =============================================================================

LOGOS_TEST(verify_signed_trusted_package) {
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

    QString lgxPath = createUnsignedPackage(tmpDir.path(), "verify_trusted");
    LOGOS_ASSERT_FALSE(lgxPath.isEmpty());

    QString keyPath = generateKey(keysDir, "trustkey");
    LOGOS_ASSERT_FALSE(keyPath.isEmpty());
    LOGOS_ASSERT_TRUE(signPackage(lgxPath, keyPath));

    // Add key to keyring
    QString did = readDid(keysDir, "trustkey");
    LOGOS_ASSERT_FALSE(did.isEmpty());

    QVariantMap addResult = impl.addTrustedKey("my-publisher", did, "Publisher Name", "https://pub.com");
    LOGOS_ASSERT_TRUE(addResult["success"].toBool());

    QVariantMap result = impl.verifyPackage(lgxPath);

    LOGOS_ASSERT_TRUE(result["isSigned"].toBool());
    LOGOS_ASSERT_TRUE(result["signatureValid"].toBool());
    LOGOS_ASSERT_EQ(result["trustedAs"].toString(), QString("my-publisher"));
}

// =============================================================================
// Install with signature policies
// =============================================================================

LOGOS_TEST(install_unsigned_with_policy_none_succeeds) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setSignaturePolicy("none");
    impl.setUserModulesDirectory(tmpDir.path() + "/modules");
    impl.setUserUiPluginsDirectory(tmpDir.path() + "/ui");

    QString lgxPath = createUnsignedPackage(tmpDir.path(), "install_none");
    LOGOS_ASSERT_FALSE(lgxPath.isEmpty());

    QVariantMap result = impl.installPlugin(lgxPath, false);
    LOGOS_ASSERT_FALSE(result.contains("error"));
}

LOGOS_TEST(install_unsigned_with_policy_require_rejected) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    t.init(&impl);

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setSignaturePolicy("require");
    impl.setKeyringDirectory(tmpDir.path() + "/keyring");
    impl.setUserModulesDirectory(tmpDir.path() + "/modules");
    impl.setUserUiPluginsDirectory(tmpDir.path() + "/ui");

    QString lgxPath = createUnsignedPackage(tmpDir.path(), "install_req");
    LOGOS_ASSERT_FALSE(lgxPath.isEmpty());

    QVariantMap result = impl.installPlugin(lgxPath, false);
    LOGOS_ASSERT_TRUE(result.contains("error"));
    LOGOS_ASSERT_TRUE(result["error"].toString().contains("unsigned"));
}
