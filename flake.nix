{
  description = "Logos Package Manager Module - Plugin manager for the Logos system";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-package-manager.url = "github:logos-co/logos-package-manager";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      # logos-package-manager is a pre-built flake providing
      # libpackage_manager_lib and liblgx. Use structured format to map
      # variants: #lib links dev libs, #lib-portable links portable libs.
      externalLibInputs = {
        logos_pm = {
          input = inputs.logos-package-manager;
          packages = {
            default = "lib";
            portable = "lib-portable";
          };
        };
      };
      # preConfigure as a function receives { externalLibs } with resolved
      # store paths — no env vars or store globbing needed.
      preConfigure = { externalLibs }: let pm = externalLibs.logos_pm; in ''
        mkdir -p lib
        cp ${pm}/lib/libpackage_manager_lib.* lib/ 2>/dev/null || true
        cp ${pm}/lib/liblgx.* lib/ 2>/dev/null || true
        cp ${pm}/include/*.h lib/ 2>/dev/null || true

        echo "Generating provider dispatch for package_manager..."
        logos-cpp-generator --provider-header "$(pwd)/src/package_manager_impl.h" \
          --output-dir ./generated_code
      '';
    };
}
