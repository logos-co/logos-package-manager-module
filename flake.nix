{
  description = "Logos Package Manager Module - Plugin manager for the Logos system";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-module.url = "github:logos-co/logos-module";
    logos-package.url = "github:logos-co/logos-package";
    nix-bundle-dir.url = "github:logos-co/nix-bundle-dir";
    nix-bundle-appimage.url = "github:logos-co/nix-bundle-appimage";
  };

  outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-module, logos-package, nix-bundle-dir, nix-bundle-appimage }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosModule = logos-module.packages.${system}.default;
        logosPackageLib = logos-package.packages.${system}.lib;
        dirBundler = nix-bundle-dir.bundlers.${system}.qtApp;
      });
    in
    {
      packages = forAllSystems ({ pkgs, system, logosSdk, logosModule, logosPackageLib, dirBundler }:
        let
          # Common configuration (dev, default)
          common = import ./nix/default.nix { inherit pkgs logosSdk logosModule logosPackageLib; };
          # Common configuration (portable)
          commonPortable = import ./nix/default.nix { inherit pkgs logosSdk logosModule logosPackageLib; portableBuild = true; };
          src = ./.;

          # Library package (dev)
          lib = import ./nix/lib.nix { inherit pkgs common src logosPackageLib; };

          # Library package (portable)
          libPortable = import ./nix/lib.nix { inherit pkgs src logosPackageLib; common = commonPortable; };

          # Include package (generated headers from plugin)
          include = import ./nix/include.nix { inherit pkgs common src lib logosSdk; };

          # CLI package (dev)
          cli = import ./nix/cli.nix { inherit pkgs common src; };

          # CLI package (portable)
          cliPortable = import ./nix/cli.nix { inherit pkgs src; common = commonPortable; };

          # Combined package
          combined = pkgs.symlinkJoin {
            name = "logos-package-manager";
            paths = [ lib include ];
          };
        in
        {
          # Individual outputs
          logos-package-manager-lib = lib;
          logos-package-manager-include = include;
          logos-package-manager-cli = cli;
          lib = lib;
          lib-portable = libPortable;
          cli = cli;
          cli-portable = cliPortable;

          # Bundle outputs
          cli-bundle-dir = dirBundler cliPortable;
        } // pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
          cli-appimage = nix-bundle-appimage.lib.${system}.mkAppImage {
            drv = cli;
            name = "lgpm";
            bundle = dirBundler cli;
            desktopFile = ./assets/lgpm.desktop;
            icon = ./assets/lgpm.png;
          };
        } // {
          # Default package (combined)
          default = combined;
        }
      );

      devShells = forAllSystems ({ pkgs, logosSdk, logosModule, logosPackageLib }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
            pkgs.zstd
          ];
          
          shellHook = ''
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
            export LOGOS_MODULE_ROOT="${logosModule}"
            export LGX_ROOT="${logosPackageLib}"
            echo "Logos Package Manager development environment"
            echo "LOGOS_CPP_SDK_ROOT: $LOGOS_CPP_SDK_ROOT"
            echo "LOGOS_MODULE_ROOT: $LOGOS_MODULE_ROOT"
            echo "LGX_ROOT: $LGX_ROOT"
          '';
        };
      });
    };
}
