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

} // extern "C"
