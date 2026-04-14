{
  description = "CUDA devShell (allow all unfree)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";

      pkgs = import nixpkgs {
        inherit system;
        config = {
          allowUnfree = true;
          cudaSupport = true;
          cudaVersion = "13";
        };
      };
    in
    {
      devShells.${system}.default = pkgs.mkShell rec {
        buildInputs = with pkgs; [
          gcc
          gnumake
          cmake
          ninja
          clang-tools
          gdb
          pkg-config
          bear
          fzf

          nixd
          nixfmt
          statix

          cudaPackages.cudatoolkit
          cudaPackages.cudnn
          cudaPackages.cuda_nvcc
          cudaPackages.cuda_cudart
          cudaPackages.nvidia_fs
          cudaPackages.gdrcopy

          rdma-core
          autoconf
          automake
          libtool
          pciutils
          dpdk
          numactl
        ];

        shellHook = ''
          export CUDA_PATH=${pkgs.cudatoolkit}

          # Set CC to GCC 13 to avoid the version mismatch error
          export CC=${pkgs.gcc13}/bin/gcc
          export CXX=${pkgs.gcc13}/bin/g++
          export PATH=${pkgs.gcc13}/bin:$PATH

          # Add necessary paths for dynamic linking
          export LD_LIBRARY_PATH=${
            pkgs.lib.makeLibraryPath (
              [
                "/run/opengl-driver" # Needed to find libGL.so
              ]
              ++ buildInputs
            )
          }:$LD_LIBRARY_PATH

          # Set LIBRARY_PATH to help the linker find the CUDA static libraries
          export LIBRARY_PATH=${
            pkgs.lib.makeLibraryPath [
              pkgs.cudatoolkit
              pkgs.cudaPackages.cuda_cudart
            ]
          }:$LIBRARY_PATH
        '';

      };
    };
}
