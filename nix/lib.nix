# Builds the logos-package-manager-module plugin library
{ pkgs, common, src, logosPackageManager, logosSdk }:

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
    # Find and copy the built plugin library from the modules directory
    if [ -f modules/package_manager_plugin.dylib ]; then
      cp modules/package_manager_plugin.dylib $out/lib/
    elif [ -f modules/package_manager_plugin.so ]; then
      cp modules/package_manager_plugin.so $out/lib/
    else
      echo "Error: No plugin library file found"
      exit 1
    fi

    # Bundle the package-manager library and liblgx alongside the plugin
    # so they can be found at runtime via rpath
    for libname in libpackage_manager_lib liblgx; do
      if [ -f ${logosPackageManager}/lib/$libname.dylib ]; then
        cp ${logosPackageManager}/lib/$libname.dylib $out/lib/
      elif [ -f ${logosPackageManager}/lib/$libname.so ]; then
        cp ${logosPackageManager}/lib/$libname.so $out/lib/
      fi
    done

    runHook postInstall
  '';
}
