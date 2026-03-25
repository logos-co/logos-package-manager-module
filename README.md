# logos-package-manager-module

Logos module that wraps [logos-package-manager](https://github.com/logos-co/logos-package-manager) as a process-isolated service, exposing local package installation and module scanning via LogosAPI.

This module handles **local operations** only (installing from `.lgx` files, scanning installed modules/UI plugins). For online catalog browsing and downloading, see [logos-package-downloader-module](https://github.com/logos-co/logos-package-downloader-module).

The underlying `logos-package-manager` library is plain C++17 (no Qt). This module provides the Qt/IPC boundary, converting between `std::string`/`std::vector` and `QString`/`QVariantList`.

## Module API

All methods are accessible via LogosAPI from other modules and UI plugins.

### Directory Configuration (4-directory model)

| Method | Description |
|--------|-------------|
| `setEmbeddedModulesDirectory(dir)` | Set directory for bundled core modules |
| `setUserModulesDirectory(dir)` | Set directory for user-installed core modules |
| `setEmbeddedUiPluginsDirectory(dir)` | Set directory for bundled UI plugins |
| `setUserUiPluginsDirectory(dir)` | Set directory for user-installed UI plugins |

### Installation

| Method | Return | Description |
|--------|--------|-------------|
| `installPlugin(path, skipIfNotNewer)` | `QVariantMap` | Install a local `.lgx` file. Returns `{name, path, isCoreModule, error}` |

### Scanning

| Method | Return | Description |
|--------|--------|-------------|
| `getInstalledPackages()` | `QVariantList` | All installed packages (modules + UI plugins) |
| `getInstalledModules()` | `QVariantList` | Installed core modules only |
| `getInstalledUiPlugins()` | `QVariantList` | Installed UI plugins only |
| `getValidVariants()` | `QStringList` | Platform variants this build accepts (e.g. `["darwin-arm64-dev"]`) |

Each item in the scan results contains all `manifest.json` fields plus `installDir` and `mainFilePath`.

### Events

| Event | Data | Description |
|-------|------|-------------|
| `corePluginFileInstalled` | `[path]` | Emitted after a core module `.lgx` is installed |
| `uiPluginFileInstalled` | `[path]` | Emitted after a UI plugin `.lgx` is installed |

### Usage from another module

```cpp
LogosModules logos(logosAPI);

// Configure directories
logos.package_manager.setEmbeddedModulesDirectory("/app/modules");
logos.package_manager.setUserModulesDirectory("/home/user/.logos/modules");
logos.package_manager.setEmbeddedUiPluginsDirectory("/app/ui-plugins");
logos.package_manager.setUserUiPluginsDirectory("/home/user/.logos/ui-plugins");

// Scan installed packages
QVariantList all = logos.package_manager.getInstalledPackages();
QVariantList modules = logos.package_manager.getInstalledModules();
QVariantList uiPlugins = logos.package_manager.getInstalledUiPlugins();

// Install a downloaded .lgx file
QVariantMap result = logos.package_manager.installPlugin("/path/to/waku_module.lgx", false);
if (result.contains("error")) {
    qWarning() << "Install failed:" << result["error"].toString();
}

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

```bash
nix build          # module plugin (lib + include)
nix build .#lib    # plugin .so/.dylib only
```

## Dependencies

- `logos-cpp-sdk` — SDK, LogosAPI, IPC
- `logos-module` — plugin introspection
- `logos-package-manager` — package management library (plain C++, linked at build time)
