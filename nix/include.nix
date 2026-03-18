{ pkgs, common, src, lib, logosSdk }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-headers";
  version = common.version;

  inherit src;
  inherit (common) meta;

  # We need the generator and the built plugin
  nativeBuildInputs = [ logosSdk ];

  # No configure phase needed
  dontConfigure = true;

  buildPhase = ''
    runHook preBuild

    # Create output directory for generated headers
    mkdir -p ./generated_headers

    # Generate native-typed consumer wrappers from the header file
    echo "Running logos-native-generator --consumer-wrappers for package_manager..."
    logos-native-generator --consumer-wrappers "$(pwd)/src/package_manager_impl.h" \
      --module-name package_manager --output-dir ./generated_headers || {
      echo "Warning: logos-native-generator failed"
      touch ./generated_headers/.no-api
    }

    echo "Generated wrapper files:"
    ls -la ./generated_headers/

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    # Install generated headers
    mkdir -p $out/include

    # Copy all generated files to include/ if they exist
    if [ -d ./generated_headers ] && [ "$(ls -A ./generated_headers 2>/dev/null)" ]; then
      echo "Copying generated headers..."
      ls -la ./generated_headers
      cp -r ./generated_headers/* $out/include/
    else
      echo "Warning: No generated headers found, creating empty include directory"
      # Create a placeholder file to indicate headers should be generated from metadata
      echo "# Generated headers from metadata.json" > $out/include/.generated
    fi

    runHook postInstall
  '';
}


