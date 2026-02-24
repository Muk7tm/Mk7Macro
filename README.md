# Mk7Macro

Low-latency Win32 macro clicker built for predictable timing and practical control of multiple click profiles.

## Why This Project Exists

Mk7Macro was created to solve two common issues in typical macro tools:

- inconsistent click timing caused by coarse timers and UI-thread-driven loops
- poor usability when managing multiple hotkeys/profiles quickly

This project focuses on **timing stability first** and keeps the UI as a control surface, not the click engine.

## What It Does

- Runs **4 independent clickers**
- Each clicker has its own config:
  - `CPS`
  - `Delay (ms)`
  - `Offset (ms)`
  - `Key Press (ms)`
  - `Output key`
  - `Hotkey`
  - `Mode` (`Hold` or `Toggle`)
  - `Enabled`
- Global `Down Click Only` switch
- Per-clicker actions: `Start`, `Settings`
- Global actions: `Record`, `Apply`, `Start All`, `Stop All`, `Save`, `Defaults`
- Live session stats in footer (`Total`, `Live CPS`, `Avg CPS`, `Session`)

## How It Works

### 1) Main/UI Thread (`main.cpp`)

- Owns the Win32 window and controls.
- Uses two timers:
  - poll timer: `1 ms` (`kPollMs`) for hotkey state processing
  - UI timer: `120 ms` (`kUiRefreshMs`) for rendering/stat refresh
- Translates inputs into an internal model (`settings_`, `manual_on_`, `toggle_latched_`).
- Applies config changes to clicker workers without blocking the render loop.

### 2) Click Engine (`clicker.cpp`)

- Creates **one worker thread per clicker** (`AdvancedClicker`).
- Uses `QueryPerformanceCounter` for high-resolution scheduling.
- Computes interval as:

```text
interval = max(1 / CPS, Delay)
```

- Applies `Offset` only at activation/start.
- Emits input with `SendInput`.
- Uses hybrid waiting strategy for lower jitter:
  - `Sleep(n)` for long waits
  - `Sleep(0)` for medium waits
  - `_mm_pause()` spin for final short phase
- Maintains each clicker on its own timeline and catches up if behind (anti-drift scheduling).

### 3) Stats Monitor (`monitor.cpp`)

- Tracks per-clicker and total click counts.
- `Live CPS`: rolling-window estimate (about 1 second of recent samples).
- `Avg CPS`: session-wide `total_clicks / elapsed_seconds`.

### 4) Settings Layer (`settings.cpp`)

- Loads/saves binary settings file with versioned schema.
- File name: `mk7macro_settings.bin`
- Location: same directory as `Mk7Macro.exe`
- Includes migration support for older format version.

## Activation Logic

A clicker can run from three sources:

- manual state (`Start` button / `Start All`)
- toggle latch (`Toggle` hotkey mode)
- hold state (key currently down in `Hold` mode)

If `Enabled` is off, final active state is forced off.

## Why Avg CPS Starts Low Then Rises

This is expected behavior.

- `Avg CPS` is session average since start.
- At session start, elapsed time is small and click count is near zero.
- As more clicks accumulate, the average converges toward the configured operating rate.

That does **not** mean the clicker is �speeding up over time�; it means the average is stabilizing.

## Default Profiles

| Clicker | Hotkey   | Output | Mode   | CPS | Delay | Offset | Key Press | Enabled |
|:-------:|:---------|:-------|:-------|----:|------:|-------:|----------:|:-------:|
| 1       | X        | F      | Toggle | 167 | 6 ms  | 0 ms   | 1 ms      | ✅       |
| 2       | XBUTTON1 | LMB    | Toggle | 100 | 10 ms | 0 ms   | 1 ms      | ✅       |
| 3       | SPACE    | SPACE  | Hold   | 300 | 10 ms | 0 ms   | 1 ms      | ❌       |
| 4       | Q        | Q      | Hold   | 50  | 10 ms | 0 ms   | 1 ms      | ❌       |

## Build

### Requirements

- Windows 10/11 (x64)
- CMake 3.22+
- Visual Studio 2022 Build Tools (MSVC)
- Windows SDK

### Commands

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output:

```text
build\\Release\\Mk7Macro.exe
```

## Run

```powershell
.\\build\\Release\\Mk7Macro.exe
```

## Project Layout

```text
main.cpp       # Win32 app, custom UI, layout, message handling, state orchestration
clicker.cpp/h  # high-resolution click worker threads + SendInput emission
monitor.cpp/h  # live/session CPS and click counters
settings.cpp/h # defaults + binary persistence + version handling
keybinds.cpp/h # VK mapping and key-state helpers
CMakeLists.txt # build configuration and output target naming
```

## Notes

- The project uses pure Win32 API (no external UI framework).
- Release config is tuned for speed (`/O2`, `/GL`, `/LTCG`, `AVX2`).
- UI styling changes should not affect click engine timing paths.

## Disclaimer

Use only in contexts where automation is allowed.
