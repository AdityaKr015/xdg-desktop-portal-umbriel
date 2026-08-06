{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  wayland-scanner,
  wayland,
  wayland-protocols,
  sdbus-cpp,
  pipewire,
  libdrm,
  mesa,
  cairo,
  tomlplusplus,
  gtk4,
}:
let
  inherit (builtins) head match readFile;
  version = head (match ".*\n  version: '([0-9][^']+)'.*" (readFile ../meson.build));
in
stdenv.mkDerivation {
  pname = "xdg-desktop-portal-umbriel";
  inherit version;

  src = lib.cleanSource ./..;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wayland-scanner
  ];

  buildInputs = [
    wayland
    wayland-protocols
    sdbus-cpp
    pipewire
    libdrm
    mesa
    cairo
    tomlplusplus
    gtk4
  ];

  mesonBuildType = "release";

  meta = with lib; {
    description = "xdg-desktop-portal backend for the Umbriel compositor";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
