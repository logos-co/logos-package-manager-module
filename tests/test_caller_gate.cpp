// Install locations and signature trust decide what counts as bundled, so no
// module but core_service may change them.
#include <logos_test.h>
#include <logos_caller.h>
#include "package_manager_impl.h"
#include "mocks/mock_package_manager_lib.h"

namespace {

struct AsCaller {
    explicit AsCaller(const char* json) { logos::detail::setCallCaller(json); }
    ~AsCaller() { logos::detail::setCallCaller(nullptr); }
};

void configureEverything(PackageManagerImpl& impl)
{
    impl.setEmbeddedModulesDirectory("/elsewhere");
    impl.addEmbeddedModulesDirectory("/elsewhere");
    impl.setEmbeddedUiPluginsDirectory("/elsewhere");
    impl.addEmbeddedUiPluginsDirectory("/elsewhere");
    impl.setUserModulesDirectory("/elsewhere");
    impl.setUserUiPluginsDirectory("/elsewhere");
    impl.setSignaturePolicy("none");
    impl.setKeyringDirectory("/elsewhere");
}

const char* const kSetters[] = {
    "setEmbeddedModulesDirectory", "addEmbeddedModulesDirectory",
    "setEmbeddedUiPluginsDirectory", "addEmbeddedUiPluginsDirectory",
    "setUserModulesDirectory", "setUserUiPluginsDirectory",
    "setSignaturePolicy", "setKeyringDirectory",
};

} // namespace

LOGOS_TEST(another_module_cannot_configure_locations_or_trust) {
    for (const char* caller : {R"({"kind":"module","name":"some_module"})",
                               R"({"kind":"operator","name":"alice"})",
                               R"({"kind":"unknown"})"}) {
        auto t = LogosTestContext("package_manager");
        PackageManagerImpl impl;
        AsCaller as(caller);
        configureEverything(impl);
        for (const char* setter : kSetters)
            LOGOS_ASSERT_FALSE(t.cFunctionCalled(setter));
        LOGOS_ASSERT_FALSE(impl.addTrustedKey("k", "did:key:z", "", "")["success"].get<bool>());
        LOGOS_ASSERT_FALSE(impl.removeTrustedKey("k")["success"].get<bool>());
        LOGOS_ASSERT_FALSE(t.cFunctionCalled("lgx_keyring_add"));
        LOGOS_ASSERT_FALSE(t.cFunctionCalled("lgx_keyring_remove"));
    }
}

LOGOS_TEST(core_service_may_configure_them) {
    auto t = LogosTestContext("package_manager");
    PackageManagerImpl impl;
    AsCaller as(R"({"kind":"module","name":"core_service"})");
    configureEverything(impl);
    for (const char* setter : kSetters)
        LOGOS_ASSERT_TRUE(t.cFunctionCalled(setter));
    (void)impl.addTrustedKey("k", "did:key:z", "", "");
    (void)impl.removeTrustedKey("k");
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("lgx_keyring_add"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("lgx_keyring_remove"));
}
