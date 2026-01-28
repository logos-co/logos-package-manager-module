# Builds the logos-package-manager CLI (lgpm)
{ pkgs, common, src }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-cli";
  version = common.version;
  
  inherit src;
  inherit (common) nativeBuildInputs buildInputs cmakeFlags meta env;
  
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
