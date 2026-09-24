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
      externalLibInputs = {
        logos_pm = {
          input = inputs.logos-package-manager;
          packages = {
            default = "lib";
            portable = "lib-portable";
          };
        };
      };
      tests = {
        dir = ./tests;
        # Same key as nix.external_libraries[].name — documents intent; go_static filtering is N/A here.
        mockCLibs = [ "logos_pm" ];
      };
    };
}
