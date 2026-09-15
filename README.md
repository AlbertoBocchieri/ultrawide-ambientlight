# ultrawide-ambientlight

---

A utility program that renders ambient light effects in the black bar areas when playing a game or video content.

Reduces the risk of uneven display wear or burn-in on OLED displays.

Supports any borderless fullscreen game and non-DRM videos in a media player or browser.

Scenarios:
- Fixed 16:9 aspect game on a 21:9 display
- 21:9 content on a 32:9 display
- Ultrawide content on a regular 16:9 display

## Usage

### VLC ambient background

`Effects > VLC ambient background` enables the new default effect: a centered,
aspect-preserving background with Dual Kawase blur, FP16 linear-light color
history, and a transparent video rectangle. Defaults match the personal VLC
build: radius **360 px**, strength **0.32**, transition **500 ms**, and a maximum
history dimension of **320 px**. Disable this option to use the classic effects.

Automatic detection now checks the captured frame before rendering. Band
shrinkage is immediate; growth still needs four confirmations at the configured
detection interval. The current-frame luma mask rejects visible pixels and full
presentation clears previous band positions. Desktop capture cannot distinguish
true bars from perfectly black video content with certainty; use manual aspect
ratio selection when the video's dimensions are known.

This is a D3D11 implementation of the VLC effect's rendering approach, not a
pixel-identical libplacebo integration: the source is the already composed
desktop, not decoded video with timestamps and source HDR metadata. Capture
gaps reset history, but seeks cannot be identified from media timestamps.

Validation: `ctest --test-dir out/build/vlc --output-on-failure` includes production
GPU shader tests through WARP. Run `ambient_gpu_test.exe hardware` for a hardware
GPU check, including deferred rendering and D3D11 debug validation when available.

1. Launch `ambientlight.exe` and choose auto-detection or manual resolution configuration.
2. Start your game in borderless fullscreen, or play video content in fullscreen in a media player or browser.
3. Use the configuration UI to adjust the effects to your liking.
4. Leave the program running in the background when using auto-detection mode.

## Build on Windows

The shortest build path uses CMake, Ninja, and a MinGW-w64 GCC toolchain:

```powershell
cmake --preset mingw-x64-release
cmake --build --preset mingw-x64-release
cmake --install out/build/mingw-x64-release
```

The executables and `config.ini` are installed in `out/install/mingw-x64-release/bin`.
On the first configure, CMake downloads the pinned ImGui and DirectXMath revisions if they are not already available locally.

The existing MSVC presets also work from a Visual Studio Developer PowerShell with the **Desktop development with C++** workload and a Windows 10 or 11 SDK installed.

## Screenshot

![screenshot](images/screen1.png)
![screenshot](images/screen2.png)
![screenshot](images/screen3.png)
![screenshot](images/screen4.png)

## Configurations

- `Resolution`: Auto-detection is recommended. For manual mode you can enter a resolution (e.g. `1920x1080`) or an aspect ratio (e.g. `16:9`).
- `Blur`: Adjust blur intensity to taste.
- `Vignette`: Allow semi-transparency in the corners so overlays (e.g. FPS counters) remain visible.
- `Transition`: Smooth color changes over time. `0 ms` disables smoothing; `300-1000 ms` is a useful range.
- `Mirror`: Apply a horizontal mirror to the effects to simulate a reflecting surface.
- `Frame rate`: Rendering frame rate for the effects.

## Third-party Libraries
- [inipp](https://github.com/mcmtroffaes/inipp)
- [DirectXTK](https://github.com/microsoft/DirectXTK)
- [imgui](https://github.com/ocornut/imgui)
