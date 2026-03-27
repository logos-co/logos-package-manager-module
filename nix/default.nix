# Common build configuration shared across all packages
{ pkgs, logosSdk, logosModule, logosPackageManager, portableBuild ? false }:

{
  pname = "logos-package-manager-module";
  version = "1.0.0";

  # Common native build inputs
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.qt6.wrapQtAppsNoGuiHook
  ];

  # Common runtime dependencies
  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtremoteobjects
    logosPackageManager  # Required for libpackage_manager_lib + liblgx (needed by autoPatchelfHook)
  ];

  # Common CMake flags
  cmakeFlags = [
    "-GNinja"
    "-DLOGOS_CPP_SDK_ROOT=${logosSdk}"
    "-DLOGOS_MODULE_ROOT=${logosModule}"
    "-DLOGOS_PACKAGE_MANAGER_ROOT=${logosPackageManager}"
    "-DLOGOS_PACKAGE_MANAGER_USE_VENDOR=OFF"
  ] ++ pkgs.lib.optionals portableBuild [
    "-DLGPM_PORTABLE_BUILD=ON"
  ];

  # Environment variables
  env = {
    LOGOS_CPP_SDK_ROOT = "${logosSdk}";
    LOGOS_MODULE_ROOT = "${logosModule}";
    LOGOS_PACKAGE_MANAGER_ROOT = "${logosPackageManager}";
  };

  # Metadata
  meta = with pkgs.lib; {
    description = "Logos Package Manager Module - Plugin manager for the Logos system";
    platforms = platforms.unix;
  };
}
