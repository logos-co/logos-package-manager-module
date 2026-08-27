{
  description = "Logos Package Manager Module - Plugin manager for the Logos system";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # PINNED TO A BRANCH, DELIBERATELY, AND TEMPORARILY.
    #
    # `master` here is a logos-package-manager whose DependencyTreeNode has
    # neither `requiredSigner` nor `signerDid`, so src/package_manager_impl.cpp
    # does not COMPILE against it. CI runs `nix build -L` — the real module,
    # linking the real library — so leaving this on master is not a latent
    # problem, it is a red build.
    #
    # And the check would not have told anyone: `tests.mockCLibs = ["logos_pm"]`
    # builds the unit tests against tests/stubs/package_manager_lib.h, which is
    # a hand-maintained MIRROR of the real header. It compiles, and passes,
    # against a library it never links. The stub moving with this header is the
    # only thing that keeps the two honest.
    #
    # REVERT TO "github:logos-co/logos-package-manager" WHEN
    # fix/flatten-must-not-mask-a-deeper-mismatch MERGES.
    logos-package-manager.url = "github:logos-co/logos-package-manager/fix/flatten-must-not-mask-a-deeper-mismatch";
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
