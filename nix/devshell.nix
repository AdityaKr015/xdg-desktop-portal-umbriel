{
  pkgs,
  xdg-desktop-portal-umbriel,
}:
pkgs.mkShell {
  inputsFrom = [ xdg-desktop-portal-umbriel ];

  nativeBuildInputs = with pkgs; [
    just
    lefthook
    meson
    ninja
    pkg-config
    wayland-scanner
    llvmPackages_22.clang-tools
    llvmPackages_22.libclang
    gnugrep
    gnused
    findutils
    gdb
  ];

  shellHook = ''
    echo " xdg-desktop-portal-umbriel dev-shell | 'just --list' to see available tasks"
  '';
}
