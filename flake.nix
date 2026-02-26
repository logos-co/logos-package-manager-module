{
  description = "Logos Package Manager Module - Plugin manager for the Logos system";

  inputs = {
    # Follow the same nixpkgs as logos-liblogos to ensure compatibility
    nixpkgs.follows = "logos-liblogos/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-liblogos.url = "github:logos-co/logos-liblogos";
    logos-package.url = "github:logos-co/logos-package";
    nix-bundle-dir.url = "github:logos-co/nix-bundle-dir/bundle-glib-schemas";
    nix-bundle-appimage.url = "github:logos-co/nix-bundle-appimage/bundle-glib-schemas";
  };

  outputs = { self, nixpkgs, logos-cpp-sdk, logos-liblogos, logos-package, nix-bundle-dir, nix-bundle-appimage }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system:
        let
          # Apply the portable overlay so bundled libraries don't hardcode
          # /nix/store paths (e.g. libproxy's gsettings schema paths).
          pkgs = import nixpkgs {
            inherit system;
            overlays = [ nix-bundle-dir.overlays.portable ];
          };
        in f {
        inherit system pkgs;
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosLiblogos = logos-liblogos.packages.${system}.default;
        logosPackageLib = logos-package.packages.${system}.lib;
        dirBundler = nix-bundle-dir.bundlers.${system}.qtApp;
      });
    in
    {
      packages = forAllSystems ({ pkgs, system, logosSdk, logosLiblogos, logosPackageLib, dirBundler }:
        let
          # Common configuration
          common = import ./nix/default.nix { inherit pkgs logosSdk logosLiblogos logosPackageLib; };
          src = ./.;
          
          # Library package
          lib = import ./nix/lib.nix { inherit pkgs common src logosPackageLib; };

          # Include package (generated headers from plugin)
          include = import ./nix/include.nix { inherit pkgs common src lib logosSdk; };

          # CLI package
          cli = (import ./nix/cli.nix { inherit pkgs common src; }).overrideAttrs (old: {
            passthru = (old.passthru or {}) // {
              extraClosurePaths = [ pkgs.gsettings-desktop-schemas ];
            };
          });

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
          cli = cli;

          # Bundle outputs
          cli-bundle-dir = dirBundler cli;
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

      devShells = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosPackageLib }: {
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
            export LOGOS_LIBLOGOS_ROOT="${logosLiblogos}"
            export LGX_ROOT="${logosPackageLib}"
            echo "Logos Package Manager development environment"
            echo "LOGOS_CPP_SDK_ROOT: $LOGOS_CPP_SDK_ROOT"
            echo "LOGOS_LIBLOGOS_ROOT: $LOGOS_LIBLOGOS_ROOT"
            echo "LGX_ROOT: $LGX_ROOT"
          '';
        };
      });
    };
}
