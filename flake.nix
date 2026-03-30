{
  description = "Logos Package Manager Module - Plugin manager for the Logos system";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    nix-bundle-lgx.url = "github:logos-co/nix-bundle-lgx";
    logos-package-manager.url = "github:logos-co/logos-package-manager";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      # logos-package-manager is a pre-built flake providing
      # libpackage_manager_lib and liblgx. Pass it as an external lib input
      # so the builder makes it a build dependency in the nix sandbox.
      externalLibInputs = {
        logos_pm = inputs.logos-package-manager;
      };
      # Copy pre-built logos-package-manager libraries into lib/ and
      # generate the provider dispatch file for the LOGOS_METHOD API.
      preConfigure = ''
        mkdir -p lib
        for store_path in /nix/store/*-logos-package-manager*; do
          if [ -d "$store_path/lib" ]; then
            for f in "$store_path"/lib/libpackage_manager_lib.* "$store_path"/lib/liblgx.*; do
              [ -f "$f" ] && cp "$f" lib/ 2>/dev/null || true
            done
          fi
          if [ -d "$store_path/include" ]; then
            cp "$store_path"/include/*.h lib/ 2>/dev/null || true
          fi
        done

        echo "Generating provider dispatch for package_manager..."
        logos-cpp-generator --provider-header "$(pwd)/src/package_manager_impl.h" \
          --output-dir ./generated_code
      '';
    };
}
