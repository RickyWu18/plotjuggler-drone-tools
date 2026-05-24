# Compile from Source

## Setup the Environment

**1. Workspace layout.** PlotJuggler must be built and installed before building these plugins. The expected layout is:

```
plotjuggler_ws/
├── src/
│   ├── PlotJuggler/         <- PlotJuggler source
│   └── PlotJuggler-Drone/   <- this repo
├── build/
└── install/                 <- PlotJuggler install prefix (bin/, include/, lib/)
```

**2. Build PlotJuggler.** If PlotJuggler is not yet installed, follow the [PlotJuggler compile guide](https://github.com/PlotJuggler/PlotJuggler/blob/main/COMPILE.md) and install it into `plotjuggler_ws/install/`.

**3. Clone this repository** with submodules in one step:

```batch
git clone --recurse-submodules https://github.com/RickyWu18/plotjuggler-drone.git
```

If you already cloned without submodules, initialize them manually:

```batch
git submodule update --init --recursive
```

> The `3rdparty/c_library_v2` submodule provides the MAVLink C headers used by the MAVLink streamer plugin.

## Build

```batch
set VCPKG_ROOT=C:\path\to\vcpkg

cmake -G "Visual Studio 16 2019" ^
      -S src\PlotJuggler-Drone ^
      -B build\PlotJuggler-Drone ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

cmake --build build\PlotJuggler-Drone --config Release --target install
```

> **Non-default install path?** Add `-DPJ_INSTALL=C:\path\to\install` if PlotJuggler is not at the default `..\..\install` relative to the source root.

The compiled `.dll` is copied directly into the same folder as `plotjuggler.exe` and loaded automatically on the next startup — no manual file copying required.
