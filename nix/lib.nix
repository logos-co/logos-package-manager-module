# Builds the logos-package-manager library
{ pkgs, common, src, logosPackageLib, logosSdk }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-lib";
  version = common.version;
  
  inherit src;
  inherit (common) nativeBuildInputs buildInputs cmakeFlags meta env;

  preConfigure = ''
    echo "Running logos-cpp-generator --provider-header for package_manager..."
    ${logosSdk}/bin/logos-cpp-generator --provider-header "$(pwd)/src/package_manager_impl.h" --output-dir "$(pwd)"
    echo "Generated provider dispatch:"
    ls -la logos_provider_dispatch.cpp 2>/dev/null || echo "WARNING: dispatch file not found"
  '';

  installPhase = ''
    runHook preInstall
    
    mkdir -p $out/lib
    # Find and copy the built library file from the modules directory
    if [ -f modules/package_manager_plugin.dylib ]; then
      cp modules/package_manager_plugin.dylib $out/lib/
    elif [ -f modules/package_manager_plugin.so ]; then
      cp modules/package_manager_plugin.so $out/lib/
    else
      echo "Error: No library file found"
      exit 1
    fi
    
    # Bundle liblgx library alongside the plugin so it can be found at runtime
    # The plugin's rpath is set to @loader_path (macOS) / $ORIGIN (Linux)
    if [ -f ${logosPackageLib}/lib/liblgx.dylib ]; then
      cp ${logosPackageLib}/lib/liblgx.dylib $out/lib/
    elif [ -f ${logosPackageLib}/lib/liblgx.so ]; then
      cp ${logosPackageLib}/lib/liblgx.so $out/lib/
    else
      echo "Warning: liblgx library not found in ${logosPackageLib}/lib/"
    fi
    
    runHook postInstall
  '';
}

