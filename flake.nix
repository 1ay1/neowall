{
  description = "NeoWall — run GLSL shaders as your Linux wallpaper (Wayland + X11)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        neowall = pkgs.stdenv.mkDerivation (finalAttrs: {
          pname = "neowall";
          version = "0.5.0";

          src = self;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            libGL
            libglvnd
            libxkbcommon
            xorg.libX11
            xorg.libXrandr
            libpng
            libjpeg
          ];

          # Pure unit tests run headless (no display server); enable them.
          doCheck = true;
          checkPhase = ''
            runHook preCheck
            meson test --print-errorlogs
            runHook postCheck
          '';

          meta = with pkgs.lib; {
            description = "Run GLSL (Shadertoy) shaders as your Wayland/X11 wallpaper";
            homepage = "https://github.com/1ay1/neowall";
            license = licenses.mit;
            platforms = platforms.linux;
            mainProgram = "neowall";
          };
        });
      in
      {
        packages = {
          inherit neowall;
          default = neowall;
        };

        apps.default = {
          type = "app";
          program = "${neowall}/bin/neowall";
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ neowall ];
          packages = with pkgs; [ clang-tools cppcheck gdb ];
        };
      });
}
