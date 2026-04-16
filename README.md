# logos-package-manager-module

Logos module that wraps [logos-package-manager](https://github.com/logos-co/logos-package-manager) as a process-isolated service, exposing local package installation and module scanning via LogosAPI.

This module handles **local operations** only (installing from `.lgx` files, scanning installed modules/UI plugins). For online catalog browsing and downloading, see [logos-package-downloader-module](https://github.com/logos-co/logos-package-downloader-module).

The underlying `logos-package-manager` library is plain C++17 (no Qt). This module provides the Qt/IPC boundary, converting between `std::string`/`std::vector` and `QString`/`QVariantList`.

## Module API

All methods are accessible via LogosAPI from other modules and UI plugins.

### Directory Configuration

**Embedded directories** (multiple, read-only at runtime):

| Method | Description |
|--------|-------------|
| `setEmbeddedModulesDirectory(dir)` | Clear and set a single embedded modules directory |
| `addEmbeddedModulesDirectory(dir)` | Append an additional embedded modules directory |
| `setEmbeddedUiPluginsDirectory(dir)` | Clear and set a single embedded UI plugins directory |
| `addEmbeddedUiPluginsDirectory(dir)` | Append an additional embedded UI plugins directory |

**User directories** (single, writable, where new packages are installed):

| Method | Description |
|--------|-------------|
| `setUserModulesDirectory(dir)` | Set directory for user-installed core modules |
| `setUserUiPluginsDirectory(dir)` | Set directory for user-installed UI plugins |

### Installation & Inspection

| Method | Return | Description |
|--------|--------|-------------|
| `installPlugin(path, skipIfNotNewer)` | `QVariantMap` | Install a local `.lgx` file. Returns `{name, path, isCoreModule, signatureStatus, error}`. When `signatureStatus` is `"signed"` or `"invalid"`, also includes `signerDid`, `signerName`, `signerUrl`, `trustedAs`. When `signatureStatus` is `"error"`, includes `signatureError`. |
| `inspectPackage(lgxPath)` | `QVariantMap` | Inspect an LGX file **without installing**. Returns metadata + install status: `{name, version, type, description, category, rootHash, signatureStatus, signerDid?, signerName?, isAlreadyInstalled, installedVersion?, installedHash?, installedDependents?, variants}`. `rootHash` is the Merkle tree root from `manifest.hashes.root` — the same identifier the online catalog exposes. When `isAlreadyInstalled` is true, `installedHash` is the corresponding value from the on-disk manifest. Used by callers (e.g. Basecamp) to show a confirmation dialog before committing. |
| `uninstallPackage(packageName)` | `QVariantMap` | Remove a user-installed package immediately (ungated). Refuses embedded packages. Returns `{success, error?, removedFiles?}`. On success emits `corePluginUninstalled` or `uiPluginUninstalled`. **Headless callers** (lgpm, scripts) should use this. GUI callers should prefer `requestUninstall` below. |

### Scanning

| Method | Return | Description |
|--------|--------|-------------|
| `getInstalledPackages()` | `QVariantList` | All installed packages (modules + UI plugins) |
| `getInstalledModules()` | `QVariantList` | Installed core modules only |
| `getInstalledUiPlugins()` | `QVariantList` | Installed UI plugins only |
| `getValidVariants()` | `QStringList` | Platform variants this build accepts (e.g. `["darwin-arm64-dev"]`) |

Each item in the scan results contains all `manifest.json` fields plus `installDir`, `mainFilePath`, and `installType` (`"embedded"` or `"user"`).

### Dependency Resolution

| Method | Return | Description |
|--------|--------|-------------|
| `resolveDependencies(packageName, recursive)` | `QVariantMap` | Forward dependency tree rooted at `packageName`. Shape: `{name, status, version, installType, children: [...]}`. `recursive=false` walks only depth-1 (children have empty `children`); `recursive=true` walks the full tree, stopping at NotInstalled/Cycle nodes. Unknown root → `{}`. |
| `resolveDependents(packageName, recursive)` | `QVariantMap` | Reverse dependency tree rooted at `packageName`. Shape: `{name, version, type, installType, installDir, children: [...]}`. Same depth semantics as `resolveDependencies`. Unknown root → `{}`. |
| `resolveFlatDependencies(packageName, recursive)` | `QVariantList` | Flat projection of the forward walk. Each entry: `{name, status, version, installType}` (no `children`). `recursive=false` → direct children only; `recursive=true` → every descendant, BFS-ordered, deduped by name. |
| `resolveFlatDependents(packageName, recursive)` | `QVariantList` | Flat projection of the reverse walk. Each entry: `{name, version, type, installType, installDir}`. Same `recursive` semantics as `resolveFlatDependencies`. |

### Gated Uninstall / Upgrade Flow

For **GUI callers** that need to show a confirmation dialog before destructive operations. The protocol ensures destructive work never runs without a live listener driving the dialog.

**Protocol:**

1. Caller invokes `requestUninstall(name)` or `requestUpgrade(name, releaseTag, mode)`. Returns `{success, error?}` synchronously. On success, sets pending state, emits `beforeUninstall` / `beforeUpgrade`, and starts a **3-second ack timer**.

2. A listener receiving the event **must immediately** call `ackPendingAction(name)` to cancel the ack timer. This says "I'm driving the dialog — wait indefinitely for the user decision."

3. If the ack timer fires without an ack (no listener present, or event loop stalled >3s), the module clears pending state and emits `uninstallCancelled` / `upgradeCancelled`. No files are removed.

4. Once acked, the listener shows a confirmation dialog. On user confirm: `confirmUninstall(name)` / `confirmUpgrade(name, releaseTag)`. On cancel: `cancelUninstall(name)` / `cancelUpgrade(name, releaseTag)`.

Only **one** gated flow can be pending globally (across all packages and both operations). A second `requestXxx` while one is pending returns `{success: false, error: "Another <op> is in progress for '<name>'"}`.

| Method | Return | Description |
|--------|--------|-------------|
| `requestUninstall(name)` | `QVariantMap` | Start gated uninstall. Emits `beforeUninstall`, starts ack timer. |
| `requestUpgrade(name, releaseTag, mode)` | `QVariantMap` | Start gated upgrade. `mode`: 0=upgrade, 1=downgrade, 2=sidegrade. Emits `beforeUpgrade`, starts ack timer. |
| `ackPendingAction(name)` | `QVariantMap` | Acknowledge receipt of a `before*` event. Cancels the ack timer. Idempotent. |
| `confirmUninstall(name)` | `QVariantMap` | Proceed with uninstall. Removes files, emits `corePluginUninstalled` / `uiPluginUninstalled`. |
| `cancelUninstall(name)` | `QVariantMap` | Abort uninstall. Emits `uninstallCancelled(name, "user cancelled")`. |
| `confirmUpgrade(name, releaseTag)` | `QVariantMap` | Proceed with upgrade. Uninstalls old version, emits `upgradeUninstallDone` for the caller to drive the download+install of the new version. |
| `cancelUpgrade(name, releaseTag)` | `QVariantMap` | Abort upgrade. Emits `upgradeCancelled(name, releaseTag, "user cancelled")`. |
| `resetPendingAction()` | `QVariantMap` | Clear any pending state. Called by Basecamp at startup to recover from a prior crash mid-dialog. |

### Signature Policy

| Method | Description |
|--------|-------------|
| `setSignaturePolicy(policy)` | Set policy: `"none"`, `"warn"` (default), or `"require"` |
| `setKeyringDirectory(dir)` | Override trusted keys directory (default: `~/.config/logos/trusted-keys/`) |
| `verifyPackage(lgxPath)` | Standalone verification. Returns `{isSigned, signatureValid, packageValid, signerDid, signerName, signerUrl, trustedAs, error}` |

### Keyring Management

| Method | Return | Description |
|--------|--------|-------------|
| `addTrustedKey(name, did, displayName, url)` | `QVariantMap` | Add a trusted signing key. Returns `{success, error}` |
| `removeTrustedKey(name)` | `QVariantMap` | Remove a trusted key by name. Returns `{success, error}` |
| `listTrustedKeys()` | `QVariantList` | List all trusted keys. Each entry: `{name, did, displayName, url, addedAt}` |

### Events

**Installation events:**

| Event | Data | Description |
|-------|------|-------------|
| `corePluginFileInstalled` | `[path]` | Emitted after a core module `.lgx` is installed |
| `uiPluginFileInstalled` | `[path]` | Emitted after a UI plugin `.lgx` is installed |
| `corePluginUninstalled` | `[name]` | Emitted after a core module is uninstalled |
| `uiPluginUninstalled` | `[name]` | Emitted after a UI plugin is uninstalled |

**Gated flow events** (see "Gated Uninstall / Upgrade Flow" above):

| Event | Data | Description |
|-------|------|-------------|
| `beforeUninstall` | `{name, installedDependents}` | A gated uninstall was requested. Listener must ack within 3s. |
| `beforeUpgrade` | `{name, releaseTag, mode, installedDependents}` | A gated upgrade was requested. Listener must ack within 3s. |
| `uninstallCancelled` | `{name, reason}` | Uninstall was cancelled — either by ack timeout or user cancel. |
| `upgradeCancelled` | `{name, releaseTag, reason}` | Upgrade was cancelled — either by ack timeout or user cancel. |
| `upgradeUninstallDone` | `{name, releaseTag, mode}` | Old version uninstalled during upgrade; caller should now download+install the new version. |

### Usage from another module

```cpp
LogosModules logos(logosAPI);

// Configure embedded directories (multiple, read-only)
logos.package_manager.setEmbeddedModulesDirectory("/app/modules");
logos.package_manager.addEmbeddedModulesDirectory("/app/extra-modules");
logos.package_manager.setEmbeddedUiPluginsDirectory("/app/ui-plugins");

// Configure user directories (single, writable)
logos.package_manager.setUserModulesDirectory("/home/user/.logos/modules");
logos.package_manager.setUserUiPluginsDirectory("/home/user/.logos/ui-plugins");

// Scan installed packages
QVariantList all = logos.package_manager.getInstalledPackages();
QVariantList modules = logos.package_manager.getInstalledModules();
QVariantList uiPlugins = logos.package_manager.getInstalledUiPlugins();

// Configure signature policy
logos.package_manager.setSignaturePolicy("require");
logos.package_manager.setKeyringDirectory("/home/user/.config/logos/trusted-keys");

// Manage trusted signing keys
logos.package_manager.addTrustedKey("logos-official", "did:jwk:eyJj...", "Logos Foundation", "https://logos.co");
QVariantList keys = logos.package_manager.listTrustedKeys();
// Each entry: {name, did, displayName, url, addedAt}
logos.package_manager.removeTrustedKey("logos-official");

// Verify a package before installing
QVariantMap sigInfo = logos.package_manager.verifyPackage("/path/to/waku_module.lgx");
// sigInfo: {isSigned, signatureValid, packageValid, signerDid, signerName, signerUrl, trustedAs, error}

// Inspect an LGX before installing (shows metadata in a confirmation dialog)
QVariantMap info = logos.package_manager.inspectPackage("/path/to/waku_module.lgx");
// info: {name, version, type, signatureStatus, isAlreadyInstalled, installedVersion?, ...}

// Install a downloaded .lgx file (ungated — for headless/scripted use)
QVariantMap result = logos.package_manager.installPlugin("/path/to/waku_module.lgx", false);
if (result.contains("error")) {
    qWarning() << "Install failed:" << result["error"].toString();
}
// result also includes: signatureStatus ("signed"/"unsigned"/"invalid"), signerDid, signerName, signerUrl, trustedAs

// Flat deduped list of every reverse dependent (BFS over the reverse tree).
QVariantList dependents = logos.package_manager.resolveFlatDependents("my_module", true);
// Or fetch the tree shape directly when you want parent/child structure.
QVariantMap dependentsTree = logos.package_manager.resolveDependents("my_module", true);

// GUI-mode gated uninstall (requires a listener to drive the confirmation dialog)
logos.package_manager.requestUninstallAsync("my_module", [](QVariantMap r) {
    if (!r.value("success").toBool()) qWarning() << r.value("error").toString();
});

// Listen for gated flow events
logos.package_manager.on("beforeUninstall", [&logos](const QVariantList& data) {
    QString name = data[0].toMap().value("name").toString();
    // Ack immediately to cancel the 3s timer
    logos.package_manager.ackPendingActionAsync(name, [](QVariantMap) {});
    // Show dialog... then confirm or cancel:
    // logos.package_manager.confirmUninstallAsync(name, ...);
    // logos.package_manager.cancelUninstallAsync(name, ...);
});

// Listen for installation events
logos.package_manager.on("corePluginFileInstalled", [](const QVariantList& data) {
    QString path = data[0].toString();
    qDebug() << "Core module installed at:" << path;
});

logos.package_manager.on("uiPluginFileInstalled", [](const QVariantList& data) {
    QString path = data[0].toString();
    qDebug() << "UI plugin installed at:" << path;
});
```

### Typical flow with the downloader module

```cpp
// 1. Download via downloader module
QVariantMap result = logos.package_downloader.downloadPackage("waku_module");

// 2. Install via package manager
if (!result.contains("error")) {
    logos.package_manager.installPlugin(result["path"].toString(), false);
}
```

## Building

Uses [logos-module-builder](https://github.com/logos-co/logos-module-builder) for minimal build configuration.

```bash
nix build                # module plugin (lib + include)
nix build .#lib          # plugin .so/.dylib only
nix build .#lgx          # .lgx package
nix build .#lgx-portable # portable .lgx package
```

## Dependencies

- `logos-module-builder` — shared Nix/CMake build infrastructure
- `logos-package-manager` — package management library (plain C++, linked at build time)
