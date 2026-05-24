# PlotJuggler-Drone Plugins

A self-maintained collection of [PlotJuggler](https://github.com/facontidavide/PlotJuggler) plugins for drone-related data visualization and streaming.

## Prerequisites

- CMake >= 3.16
- Visual Studio 2019 (MSVC 19+)
- [vcpkg](https://github.com/microsoft/vcpkg) (Qt5 is resolved automatically via `vcpkg.json`)
- PlotJuggler built and installed into the workspace `install/` directory

Expected workspace layout:

```
plotjuggler_ws/
├── src/
│   ├── Plotjuggler/         <- PlotJuggler source
│   └── PlotJuggler-Drone/   <- this repo
├── build/
└── install/                 <- PlotJuggler install prefix (bin/, include/, lib/)
```

If PlotJuggler is not yet installed, follow the [PlotJuggler compile document](https://github.com/PlotJuggler/PlotJuggler/blob/main/COMPILE.md) to build it first.

Clone with submodules in one step:

```batch
git clone --recurse-submodules https://github.com/RickyWu18/plotjuggler-drone.git
```

Or if already cloned, initialise manually:

```batch
git submodule update --init --recursive
```


## Configure

Qt5 is resolved via **vcpkg** (same toolchain used by PlotJuggler). Set `VCPKG_ROOT` to your vcpkg installation if it is not already in your environment:

```batch
set VCPKG_ROOT=C:\path\to\vcpkg
```

Then configure:

```batch
cmake -G "Visual Studio 16" ^
      -S src\PlotJuggler-Drone -B build\PlotJuggler-Drone ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
```

CMake looks for PlotJuggler at `../../install` relative to this repo by default.
Override if your install is elsewhere:

```batch
cmake -G "Visual Studio 16" ^
      -S src\PlotJuggler-Drone -B build\PlotJuggler-Drone ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
      -DPJ_INSTALL=C:\path\to\install
```

## Build and Install

```batch
cmake --build build\PlotJuggler-Drone --config Release --target install
```

The compiled `.dll` is copied to `build\Plotjuggler\bin\<Config>\` alongside `plotjuggler.exe` and is
loaded automatically when PlotJuggler starts.

## Repository Structure

```
src/PlotJuggler-Drone/
├── CMakeLists.txt             <- top-level: finds Qt5 and PlotJuggler, adds plugin subdirs
├── vcpkg.json                 <- vcpkg dependencies (Qt5)
├── conanfile.txt              <- Conan generator stubs
├── README.md
├── 3rdparty/
│   └── c_library_v2/          <- MAVLink C headers (git submodule)
├── PluginTemplate/            <- minimal template to copy when adding a new plugin
└── <PluginName>/              <- one subdirectory per plugin
    ├── CMakeLists.txt
    ├── <plugin>.h
    └── <plugin>.cpp
```

## Adding a New Plugin

1. Create a new subdirectory, e.g. `DataStreamDrone/`.

2. Add `DataStreamDrone/CMakeLists.txt`, modelled after `PluginTemplate/CMakeLists.txt`:

```cmake
add_library(DataStreamDrone SHARED
    datastream_drone.h
    datastream_drone.cpp)

target_include_directories(DataStreamDrone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PJ_INCLUDE_DIR})

target_compile_definitions(DataStreamDrone PRIVATE
    QT_PLUGIN
    FMT_HEADER_ONLY
    PJ_MAJOR_VERSION=${PJ_MAJOR_VERSION}
    PJ_MINOR_VERSION=${PJ_MINOR_VERSION}
    PJ_PATCH_VERSION=${PJ_PATCH_VERSION})

target_link_libraries(DataStreamDrone PRIVATE
    Qt5::Core Qt5::Widgets Qt5::Xml Qt5::Network
    ${PJ_BASE_LIB} ${PJ_QWT_LIB})

install(TARGETS DataStreamDrone
    RUNTIME DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY}/$<CONFIG>)
```

3. Inherit the appropriate base class and implement its interface:

| Plugin type    | Base class         | IID |
|----------------|--------------------|-----|
| Live streaming | `PJ::DataStreamer` | `facontidavide.PlotJuggler3.DataStreamer` |
| File loading   | `PJ::DataLoader`   | `facontidavide.PlotJuggler3.DataLoader`  |

4. Register the subdirectory in the top-level `CMakeLists.txt`:

```cmake
add_subdirectory(DataStreamDrone)
```

5. Re-configure and build:

```batch
cmake -S src\PlotJuggler-Drone -B build\PlotJuggler-Drone
cmake --build build\PlotJuggler-Drone --config Release --target install
```

## Development Notes

- Set `isDebugPlugin()` to `true` during development; change to `false` before release.
- Always hold `mutex()` when writing to `dataMap()` from a background thread.
- Call `emit dataReceived()` after each data push to notify PlotJuggler to refresh.
