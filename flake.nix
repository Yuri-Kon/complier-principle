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

      forAllSystems =
        f:
        nixpkgs.lib.genAttrs systems (
          system:
          let
            pkgs = nixpkgs.legacyPackages.${system};
          in
          f pkgs
        );
    in
    {
      devShells = forAllSystems (
        pkgs:
        let
          linuxOnlyPackages = pkgs.lib.optionals pkgs.stdenv.isLinux [
            pkgs.gdb
            pkgs.valgrind
          ];
        in
        {
          default = pkgs.mkShell {
            packages =
              [
                pkgs.gcc
                pkgs.gnumake
                pkgs.flex
                pkgs.bison
              ]
              ++ linuxOnlyPackages;

            shellHook = ''
              echo "C-- compiler lab environment"
              echo "system: ${pkgs.stdenv.hostPlatform.system}"
              echo "gcc:   $(gcc --version | head -n 1)"
              echo "make:  $(make --version | head -n 1)"
              echo "flex:  $(flex --version)"
              echo "bison: $(bison --version | head -n 1)"
              echo ""
              echo "Build with:"
              echo "  make"
              echo ""
            '';
          };
        }
      );
    };
}
