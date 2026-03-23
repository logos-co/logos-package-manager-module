# Builds the logos-package-manager CLI (lgpm)
{ pkgs, common, src, logosSdk }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-cli";
  version = common.version;

  inherit src;
  inherit (common) buildInputs cmakeFlags meta env;

  preConfigure = ''
    echo "Running logos-cpp-generator --provider-header for package_manager..."
    ${logosSdk}/bin/logos-cpp-generator --provider-header "$(pwd)/src/package_manager_impl.h" --output-dir "$(pwd)"
    echo "Generated provider dispatch:"
    ls -la logos_provider_dispatch.cpp 2>/dev/null || echo "WARNING: dispatch file not found"
  '';
  
  # Add autoPatchelfHook on Linux to fix RPATHs
  nativeBuildInputs = common.nativeBuildInputs ++
    pkgs.lib.optionals pkgs.stdenv.isLinux [ pkgs.autoPatchelfHook ];
  
  # Clear LD_LIBRARY_PATH to prevent external Qt installations from interfering
  qtWrapperArgs = [ "--unset LD_LIBRARY_PATH" ];
  
  installPhase = ''
    runHook preInstall
    
    mkdir -p $out/bin
    # Copy the CLI executable
    if [ -f bin/lgpm ]; then
      cp bin/lgpm $out/bin/
    else
      echo "Error: lgpm executable not found"
      exit 1
    fi
    
    runHook postInstall
  '';
}
