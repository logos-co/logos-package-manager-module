# Common build configuration shared across all packages
{ pkgs, logosSdk, logosLiblogos }:

{
  pname = "logos-package-manager";
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
    pkgs.zstd
  ];
  
  # Common CMake flags
  cmakeFlags = [ 
    "-GNinja"
    "-DLOGOS_CPP_SDK_ROOT=${logosSdk}"
    "-DLOGOS_LIBLOGOS_ROOT=${logosLiblogos}"
    "-DLOGOS_PACKAGE_MANAGER_USE_VENDOR=OFF"
  ];
  
  # Environment variables
  env = {
    LOGOS_CPP_SDK_ROOT = "${logosSdk}";
    LOGOS_LIBLOGOS_ROOT = "${logosLiblogos}";
  };
  
  # Metadata
  meta = with pkgs.lib; {
    description = "Logos Package Manager Module - Plugin manager for the Logos system";
    platforms = platforms.unix;
  };
}

