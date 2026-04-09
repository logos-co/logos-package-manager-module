/**
 * Minimal lgx.h for unit tests only — only symbols referenced by package_manager_impl.cpp.
 * Integration tests use the real header from ../lib.
 */
#ifndef LGX_H
#define LGX_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool success;
    const char* error;
} lgx_result_t;

typedef struct {
    const char* name;
    const char* did;
    const char* display_name;
    const char* url;
    const char* added_at;
} lgx_keyring_key_t;

typedef struct {
    lgx_keyring_key_t* keys;
    size_t count;
} lgx_keyring_list_t;

lgx_result_t lgx_keyring_add(const char* keyring_dir,
                             const char* name,
                             const char* did,
                             const char* display_name,
                             const char* url);

lgx_result_t lgx_keyring_remove(const char* keyring_dir, const char* name);

lgx_keyring_list_t lgx_keyring_list(const char* keyring_dir);

void lgx_free_keyring_list(lgx_keyring_list_t list);

#ifdef __cplusplus
}
#endif

#endif /* LGX_H */
