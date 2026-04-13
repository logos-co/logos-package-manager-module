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
static std::string currentVariant() {
    std::vector<std::string> variants = PackageManagerLib::platformVariantsToTry();
    return variants.empty() ? "unknown" : variants.front();
}

/**
 * Helper: create a minimal unsigned .lgx package in a temp directory.
 * Returns path to the .lgx file as std::string.
 */
static std::string createUnsignedPackage(const QString& dir, const QString& name) {
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
    std::string lgxStd = lgxPath.toStdString();
    std::string nameStd = name.toStdString();
    lgx_result_t res = lgx_create(lgxStd.c_str(), nameStd.c_str());
    if (!res.success) return {};

    lgx_package_t pkg = lgx_load(lgxStd.c_str());
    if (!pkg) return {};

    lgx_set_version(pkg, "1.0.0");
    lgx_set_description(pkg, "Test package");

    std::string variant = currentVariant();
    res = lgx_add_variant(pkg, variant.c_str(),
                          contentDir.toStdString().c_str(),
                          libName.toStdString().c_str());
    if (!res.success) { lgx_free_package(pkg); return {}; }

    res = lgx_save(pkg, lgxStd.c_str());
    lgx_free_package(pkg);

    return res.success ? lgxStd : std::string();
}

/**
 * Helper: generate a keypair. Returns path to .jwk file as std::string.
 */
static std::string generateKey(const QString& keysDir, const QString& keyName) {
    lgx_result_t res = lgx_keygen(keyName.toStdString().c_str(), keysDir.toStdString().c_str());
    if (!res.success) return {};
    return (keysDir + "/" + keyName + ".jwk").toStdString();
}

/**
 * Helper: read DID from a .did file.
 */
static std::string readDid(const QString& keysDir, const QString& keyName) {
    QFile f(keysDir + "/" + keyName + ".did");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed().toStdString();
}

/**
 * Helper: sign a package with a key.
 */
static bool signPackage(const std::string& lgxPath, const std::string& keyPath,
                         const std::string& signerName = {}, const std::string& signerUrl = {}) {
    lgx_result_t res = lgx_sign(
        lgxPath.c_str(),
        keyPath.c_str(),
        signerName.empty() ? nullptr : signerName.c_str(),
        signerUrl.empty() ? nullptr : signerUrl.c_str()
    );
    return res.success;
}

// =============================================================================
// Signature policy configuration
// =============================================================================

LOGOS_TEST(set_signature_policy_none) {
    PackageManagerImpl impl;

    // Should not crash or warn
    impl.setSignaturePolicy("none");
    impl.setSignaturePolicy("NONE");
    impl.setSignaturePolicy("None");
}

LOGOS_TEST(set_signature_policy_warn) {
    PackageManagerImpl impl;

    impl.setSignaturePolicy("warn");
}

LOGOS_TEST(set_signature_policy_require) {
    PackageManagerImpl impl;

    impl.setSignaturePolicy("require");
}

LOGOS_TEST(set_keyring_directory) {
    PackageManagerImpl impl;

    impl.setKeyringDirectory("/custom/keyring");
}

// =============================================================================
// Verify unsigned package
// =============================================================================

LOGOS_TEST(verify_unsigned_package) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    std::string lgxPath = createUnsignedPackage(tmpDir.path(), "verify_unsigned");
    LOGOS_ASSERT_FALSE(lgxPath.empty());

    LogosMap result = impl.verifyPackage(lgxPath);

    LOGOS_ASSERT_FALSE(result["isSigned"].get<bool>());
    LOGOS_ASSERT_FALSE(result["signatureValid"].get<bool>());
    LOGOS_ASSERT_TRUE(result["signerDid"].get<std::string>().empty());
}

// =============================================================================
// Verify signed package
// =============================================================================

LOGOS_TEST(verify_signed_package) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QDir().mkpath(keysDir);

    std::string lgxPath = createUnsignedPackage(tmpDir.path(), "verify_signed");
    LOGOS_ASSERT_FALSE(lgxPath.empty());

    std::string keyPath = generateKey(keysDir, "testkey");
    LOGOS_ASSERT_FALSE(keyPath.empty());
    LOGOS_ASSERT_TRUE(signPackage(lgxPath, keyPath, "Test Signer", "https://example.com"));

    LogosMap result = impl.verifyPackage(lgxPath);

    LOGOS_ASSERT_TRUE(result["isSigned"].get<bool>());
    LOGOS_ASSERT_TRUE(result["signatureValid"].get<bool>());
    LOGOS_ASSERT_TRUE(result["packageValid"].get<bool>());
    LOGOS_ASSERT_EQ(result["signerName"].get<std::string>(), std::string("Test Signer"));
    LOGOS_ASSERT_EQ(result["signerUrl"].get<std::string>(), std::string("https://example.com"));
    // DID should start with did:jwk:
    LOGOS_ASSERT_TRUE(result["signerDid"].get<std::string>().substr(0, 8) == "did:jwk:");
    // Not in keyring, so not trusted
    LOGOS_ASSERT_TRUE(result["trustedAs"].get<std::string>().empty());
}

// =============================================================================
// Verify signed + trusted package
// =============================================================================

LOGOS_TEST(verify_signed_trusted_package) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    QString keysDir = tmpDir.path() + "/keys";
    QString keyringDir = tmpDir.path() + "/keyring";
    QDir().mkpath(keysDir);
    QDir().mkpath(keyringDir);

    impl.setKeyringDirectory(keyringDir.toStdString());

    std::string lgxPath = createUnsignedPackage(tmpDir.path(), "verify_trusted");
    LOGOS_ASSERT_FALSE(lgxPath.empty());

    std::string keyPath = generateKey(keysDir, "trustkey");
    LOGOS_ASSERT_FALSE(keyPath.empty());
    LOGOS_ASSERT_TRUE(signPackage(lgxPath, keyPath));

    // Add key to keyring
    std::string did = readDid(keysDir, "trustkey");
    LOGOS_ASSERT_FALSE(did.empty());

    LogosMap addResult = impl.addTrustedKey("my-publisher", did, "Publisher Name", "https://pub.com");
    LOGOS_ASSERT_TRUE(addResult["success"].get<bool>());

    LogosMap result = impl.verifyPackage(lgxPath);

    LOGOS_ASSERT_TRUE(result["isSigned"].get<bool>());
    LOGOS_ASSERT_TRUE(result["signatureValid"].get<bool>());
    LOGOS_ASSERT_EQ(result["trustedAs"].get<std::string>(), std::string("my-publisher"));
}

// =============================================================================
// Install with signature policies
// =============================================================================

LOGOS_TEST(install_unsigned_with_policy_none_succeeds) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setSignaturePolicy("none");
    impl.setUserModulesDirectory((tmpDir.path() + "/modules").toStdString());
    impl.setUserUiPluginsDirectory((tmpDir.path() + "/ui").toStdString());

    std::string lgxPath = createUnsignedPackage(tmpDir.path(), "install_none");
    LOGOS_ASSERT_FALSE(lgxPath.empty());

    LogosMap result = impl.installPlugin(lgxPath, false);
    LOGOS_ASSERT_FALSE(result.contains("error"));
}

LOGOS_TEST(install_unsigned_with_policy_require_rejected) {
    PackageManagerImpl impl;

    QTemporaryDir tmpDir;
    LOGOS_ASSERT_TRUE(tmpDir.isValid());

    impl.setSignaturePolicy("require");
    impl.setKeyringDirectory((tmpDir.path() + "/keyring").toStdString());
    impl.setUserModulesDirectory((tmpDir.path() + "/modules").toStdString());
    impl.setUserUiPluginsDirectory((tmpDir.path() + "/ui").toStdString());

    std::string lgxPath = createUnsignedPackage(tmpDir.path(), "install_req");
    LOGOS_ASSERT_FALSE(lgxPath.empty());

    LogosMap result = impl.installPlugin(lgxPath, false);
    LOGOS_ASSERT_TRUE(result.contains("error"));
    LOGOS_ASSERT_TRUE(result["error"].get<std::string>().find("unsigned") != std::string::npos);
}
