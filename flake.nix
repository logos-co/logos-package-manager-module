{
  description = "Logos Package Manager Module - Plugin manager for the Logos system";

  inputs = {
    # Follow the same nixpkgs as logos-liblogos to ensure compatibility
    nixpkgs.follows = "logos-liblogos/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-liblogos.url = "github:logos-co/logos-liblogos";
    logos-package.url = "github:logos-co/logos-package";
  };

  outputs = { self, nixpkgs, logos-cpp-sdk, logos-liblogos, logos-package }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosLiblogos = logos-liblogos.packages.${system}.default;
        logosPackageLib = logos-package.packages.${system}.lib;
      });
    in
    {
      packages = forAllSystems ({ pkgs, logosSdk, logosLiblogos, logosPackageLib }: 
        let
          # Common configuration
          common = import ./nix/default.nix { inherit pkgs logosSdk logosLiblogos logosPackageLib; };
          src = ./.;
          
          # Library package
          lib = import ./nix/lib.nix { inherit pkgs common src; };

          # Include package (generated headers from plugin)
          include = import ./nix/include.nix { inherit pkgs common src lib logosSdk; };

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
          lib = lib;

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
