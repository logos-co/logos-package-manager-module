{
  description = "Logos Package Manager Module - Plugin manager for the Logos system";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-module.url = "github:logos-co/logos-module";
    logos-package-manager.url = "github:logos-co/logos-package-manager";
    nix-bundle-dir.url = "github:logos-co/nix-bundle-dir";
    nix-bundle-appimage.url = "github:logos-co/nix-bundle-appimage";
  };

  outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-module, logos-package-manager, nix-bundle-dir, nix-bundle-appimage }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosModule = logos-module.packages.${system}.default;
        logosPackageManager = logos-package-manager.packages.${system}.lib;
        logosPackageManagerPortable = logos-package-manager.packages.${system}.lib-portable;
        dirBundler = nix-bundle-dir.bundlers.${system}.qtApp;
      });
    in
    {
      packages = forAllSystems ({ pkgs, system, logosSdk, logosModule, logosPackageManager, logosPackageManagerPortable, dirBundler }:
        let
          # Common configuration (dev, default)
          common = import ./nix/default.nix { inherit pkgs logosSdk logosModule logosPackageManager; };
          # Common configuration (portable)
          commonPortable = import ./nix/default.nix { inherit pkgs logosSdk logosModule; logosPackageManager = logosPackageManagerPortable; portableBuild = true; };
          src = ./.;

          # Library package (dev)
          lib = import ./nix/lib.nix { inherit pkgs common src logosPackageManager logosSdk; };

          # Library package (portable)
          libPortable = import ./nix/lib.nix { inherit pkgs src logosSdk; logosPackageManager = logosPackageManagerPortable; common = commonPortable; };

          # Include package (generated headers from plugin)
          include = import ./nix/include.nix { inherit pkgs common src lib logosSdk; };

          # Combined package
          combined = pkgs.symlinkJoin {
            name = "logos-package-manager-module";
            paths = [ lib include ];
          };
        in
        {
          # Individual outputs
          logos-package-manager-lib = lib;
          logos-package-manager-include = include;
          lib = lib;
          lib-portable = libPortable;

        } // pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
          lib-appimage = nix-bundle-appimage.lib.${system}.mkAppImage {
            drv = lib;
            name = "package_manager_plugin";
            bundle = dirBundler lib;
          };
        } // {
          # Default package (combined)
          default = combined;
        }
      );

      devShells = forAllSystems ({ pkgs, logosSdk, logosModule, logosPackageManager, ... }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
          ];

          shellHook = ''
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
            export LOGOS_MODULE_ROOT="${logosModule}"
            export LOGOS_PACKAGE_MANAGER_ROOT="${logosPackageManager}"
            echo "Logos Package Manager Module development environment"
            echo "LOGOS_CPP_SDK_ROOT: $LOGOS_CPP_SDK_ROOT"
            echo "LOGOS_MODULE_ROOT: $LOGOS_MODULE_ROOT"
            echo "LOGOS_PACKAGE_MANAGER_ROOT: $LOGOS_PACKAGE_MANAGER_ROOT"
          '';
        };
      });
    };
}
