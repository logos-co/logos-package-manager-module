// Minimal lgx C API mock for unit tests (package_manager_impl keyring calls).

#include <logos_clib_mock.h>
#include <lgx.h>

extern "C" {

lgx_result_t lgx_keyring_add(const char* keyring_dir,
                             const char* name,
                             const char* did,
                             const char* display_name,
                             const char* url) {
    LOGOS_CMOCK_RECORD("lgx_keyring_add");
    (void)keyring_dir;
    (void)name;
    (void)did;
    (void)display_name;
    (void)url;
    lgx_result_t r;
    r.success = true;
    r.error = nullptr;
    return r;
}

lgx_result_t lgx_keyring_remove(const char* keyring_dir, const char* name) {
    LOGOS_CMOCK_RECORD("lgx_keyring_remove");
    (void)keyring_dir;
    (void)name;
    lgx_result_t r;
    r.success = true;
    r.error = nullptr;
    return r;
}

lgx_keyring_list_t lgx_keyring_list(const char* keyring_dir) {
    LOGOS_CMOCK_RECORD("lgx_keyring_list");
    (void)keyring_dir;
    lgx_keyring_list_t list;
    list.keys = nullptr;
    list.count = 0;
    return list;
}

void lgx_free_keyring_list(lgx_keyring_list_t list) {
    LOGOS_CMOCK_RECORD("lgx_free_keyring_list");
    (void)list;
}

// ---------------------------------------------------------------------------
// Package loading / inspection — unit tests never invoke inspectPackage, but
// the symbols must exist at link time because package_manager_impl.cpp
// references them unconditionally. Stubs return benign zeroes/nulls.
// ---------------------------------------------------------------------------

lgx_package_t lgx_load(const char* path) {
    LOGOS_CMOCK_RECORD("lgx_load");
    (void)path;
    return nullptr;
}

void lgx_free_package(lgx_package_t pkg) {
    LOGOS_CMOCK_RECORD("lgx_free_package");
    (void)pkg;
}

const char* lgx_get_last_error(void) {
    LOGOS_CMOCK_RECORD("lgx_get_last_error");
    return nullptr;
}

const char* lgx_get_name(lgx_package_t pkg) {
    LOGOS_CMOCK_RECORD("lgx_get_name");
    (void)pkg;
    return nullptr;
}

const char* lgx_get_version(lgx_package_t pkg) {
    LOGOS_CMOCK_RECORD("lgx_get_version");
    (void)pkg;
    return nullptr;
}

const char* lgx_get_description(lgx_package_t pkg) {
    LOGOS_CMOCK_RECORD("lgx_get_description");
    (void)pkg;
    return nullptr;
}

const char* lgx_get_manifest_json(lgx_package_t pkg) {
    LOGOS_CMOCK_RECORD("lgx_get_manifest_json");
    (void)pkg;
    return nullptr;
}

const char** lgx_get_variants(lgx_package_t pkg) {
    LOGOS_CMOCK_RECORD("lgx_get_variants");
    (void)pkg;
    return nullptr;
}

void lgx_free_string_array(const char** array) {
    LOGOS_CMOCK_RECORD("lgx_free_string_array");
    (void)array;
}

} // extern "C"
