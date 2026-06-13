# Vocal Doubler — VST3 Plugin

Artificial Double Tracking (ADT) plugin. Simulates the effect of recording a
vocal track a second time by independently randomising pitch, timing, phase, and
timbre of a copied voice.

## How it works

| What | How |
|---|---|
| **Pitch** | Two independent Brownian-motion delay modulators cause the read pointer to drift, producing a Doppler pitch shift |
| **Timing** | A fixed pre-delay (Timing knob) offsets both voices from the dry signal |
| **Phase** | All-pass filter chains at different frequencies colour each voice's phase differently |
| **Timbre** | High-shelf and low-shelf EQ applied in opposite directions to each voice |

Both voices are panned L/R (Width controls spread), creating a wide stereo image
when blended with the centre dry signal.

---

## Prerequisites

- **Visual Studio 2022** with the *Desktop development with C++* workload  
  Download: https://visualstudio.microsoft.com/
- **CMake 3.22+**  
  Download: https://cmake.org/download/  
  ✔ Tick "Add CMake to the system PATH" during install
- **Git**  
  Download: https://git-scm.com/ (needed so CMake can fetch JUCE)

> JUCE is downloaded automatically on first build — no manual installation needed.

---

## Building

### Option A — Double-click

Double-click **`build.bat`**.  
The first run downloads JUCE (~250 MB) and may take 5–10 minutes.  
Subsequent builds are fast.

### Option B — Command line

```bat
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release --parallel
```

### Output

After a successful build the VST3 is automatically copied to:

```
C:\Program Files\Common Files\VST3\Vocal Doubler.vst3
```

Reaper will find it there automatically on the next scan
(*Options → Preferences → Plug-ins → VST → Re-scan*).

---

## Parameters

| Knob | Range | Default | Description |
|---|---|---|---|
| **Mix** | 0–100 % | 50 % | Wet/dry blend |
| **Pitch Var** | 0–50 cents | 12 cents | How much each voice's pitch wanders |
| **Timing** | 10–60 ms | 20 ms | Pre-delay / timing offset of the doubled track |
| **Width** | 0–100 % | 100 % | Stereo spread (0 = mono double, 100 = full L/R) |
| **Timbre Var** | 0–100 % | 40 % | EQ difference between voices (±3 dB shelf) |
| **Rate** | 0.1–3.0 Hz | 0.3 Hz | How fast the pitch wanders (lower = more natural) |

### Recommended starting points

**Natural double** — Mix 60 %, Pitch 10 c, Timing 18 ms, Width 80 %, Timbre 35 %, Rate 0.2 Hz  
**Thicker chorus-like** — Mix 80 %, Pitch 25 c, Timing 30 ms, Width 100 %, Timbre 60 %, Rate 1.0 Hz

---

## Project structure

```
VocalDoubler/
├── CMakeLists.txt          Build configuration (JUCE via FetchContent)
├── build.bat               One-click build script
├── Source/
│   ├── PluginProcessor.h   DSP class declaration
│   ├── PluginProcessor.cpp DSP implementation
│   ├── PluginEditor.h      UI class declaration
│   └── PluginEditor.cpp    UI implementation
└── README.md
```
