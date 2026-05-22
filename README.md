# PlotJuggler-Drone Plugins

A self-maintained collection of [PlotJuggler](https://github.com/facontidavide/PlotJuggler) plugins for drone-related data visualization and streaming.

## Prerequisites

- CMake >= 3.16
- Visual Studio 2022 (MSVC 19+)
- Qt5 (Core, Widgets, Xml, Network)
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

If PlotJuggler is not yet installed, build it first:

```batch
cmake -S src\Plotjuggler -B build\Plotjuggler -DCMAKE_INSTALL_PREFIX=install
cmake --build build\Plotjuggler --config Release --target install
```

## Configure

```batch
cmake -S src\PlotJuggler-Drone -B build\PlotJuggler-Drone
```

CMake looks for PlotJuggler at `../../install` relative to this repo by default.
Override if your install is elsewhere:

```batch
cmake -S src\PlotJuggler-Drone -B build\PlotJuggler-Drone -DPJ_INSTALL=C:\path\to\install
```

## Build and Install

```batch
cmake --build build\PlotJuggler-Drone --config Release --target install
```

The compiled `.dll` is copied to `install\bin\` alongside `plotjuggler.exe` and is
loaded automatically when PlotJuggler starts.

## Repository Structure

```
src/PlotJuggler-Drone/
├── CMakeLists.txt       <- top-level: finds Qt5 and PlotJuggler, adds plugin subdirs
├── cmake/               <- CMake modules (e.g. CPM.cmake)
├── README.md
└── <PluginName>/        <- one subdirectory per plugin
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
    DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY})
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
