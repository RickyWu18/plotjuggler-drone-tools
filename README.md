# PlotJuggler-Drone Plugins

> A self-maintained collection of [PlotJuggler](https://github.com/facontidavide/PlotJuggler) plugins for drone telemetry visualization and live data streaming.

[![License: MPL 2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](https://opensource.org/licenses/MPL-2.0)

---

## Overview

PlotJuggler is a fast, open-source time-series visualization tool. This repository extends it with drone-specific plugins — primarily targeting MAVLink-based systems such as PX4 and ArduPilot — so that engineers and researchers can stream, inspect, and analyze flight telemetry directly inside PlotJuggler without any extra middleware.

<!-- TODO: Add a screenshot or GIF of the plugin streaming data inside PlotJuggler -->

---

## Available Plugins

| Plugin | Description |
|---|---|
| **DataStreamMavlink** | Streams live MAVLink telemetry over UDP, TCP, or Serial into PlotJuggler. Automatically discovers all message fields and populates them as time-series. Includes a built-in **Message Interval** dialog to inspect and tune per-message update rates on the vehicle. |

### DataStreamMavlink — Feature Highlights

- **Three transports:** UDP (default port 14550), TCP client, and Serial port — switchable from the connection dialog.
- **Zero-config field discovery:** every numeric field in every MAVLink message is automatically mapped to a plot series named `mav/<sysid>.<compid>/<MSG_NAME>/<field>`.
- **Multi-vehicle:** differentiates streams by `sysid.compid`, so data from multiple vehicles on the same link is kept separate.
- **Message Interval control:** via the **"Message Intervals…"** toolbar action, view live message rates and send `SET_MESSAGE_INTERVAL` commands back to the vehicle to tune what gets streamed and how fast.

---

## How to Use

### 1. Install PlotJuggler

Download and install the latest PlotJuggler release from the [official GitHub releases page](https://github.com/facontidavide/PlotJuggler/releases).

### 2. Download the plugin

Grab the pre-built plugin binary for your platform from the [Releases](https://github.com/RickyWu18/plotjuggler-drone/releases) page of this repository:

| Platform | File |
|---|---|
| Windows | `DataStreamMavlink.dll` |
| Linux | `libDataStreamMavlink.so` |

### 3. Drop it into the PlotJuggler plugin folder

Place the file in the directory where PlotJuggler looks for plugins:

| Platform | Plugin path |
|---|---|
| Windows | Same folder as `plotjuggler.exe` (e.g. `C:\Program Files\PlotJuggler\`) |
| Linux | Same folder as the `plotjuggler` binary (e.g. `/usr/local/bin/`) |

Restart PlotJuggler. The **Plugin** will load in new session.

---

## Building from Source

See [docs/build.md](docs/build.md) for workspace layout, PlotJuggler setup, and CMake build instructions.

---

## Development

See [docs/develop.md](docs/develop.md) for step-by-step instructions on scaffolding a new plugin from the template, the CMake wiring, and key rules for thread-safe `dataMap()` access.

---

## License

This project is licensed under the [Mozilla Public License 2.0](https://opensource.org/licenses/MPL-2.0).
