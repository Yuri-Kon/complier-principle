{
  description = "C-- compiler lab development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      forAllSystems = f: nixpkgs.lib.genAttrs systems (systems: f (import nixpkgs { inherit systems; }));
    in
    {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            gcc
            gnumake
            flex
            bison

            gdb
            valgrind
          ];

          shellHook = ''
            echo "C-- compiler lab environment"
            echo "gcc:  $(gcc --version | head -n 1)"
            echo  "make: $(make --version | head -n 1)"
            echo  "flex: $(flex --version)"
            echo  "bison: $(bison --version | head -n 1)"
            echo ""
            echo "Build with:"
            echo "  make"
            echo ""
          '';
        };
      });
    };
}
