# Build Instructions for 86Box on macOS (arm64)

These steps reproduce the working build of `86Box.app` described earlier. Feed this document to an AI coding agent that has access to a macOS arm64 environment with Homebrew.

1. **Install base dependencies via Homebrew**
   ```sh
   brew install cmake ninja pkg-config qt@6 sdl2 rtmidi fluid-synth libpng freetype libslirp openal-soft libserialport webp
   ```
   * Qt6 must be installed via Homebrew because the CMake scripts rely on `Qt6` components (`Qt6::Widgets`, etc.) and the bundled Qt plugins (`QCocoaIntegrationPlugin`, `QMacStylePlugin`, `QICOPlugin`, `QICNSPlugin`).
   * The `webp` and `libserialport` formulae provide runtimes that get packaged into the bundle (the build links to `libsharpyuv`, `libwebp`, and serial port libraries).  

2. **Prepare build environment variables before configuring**
   ```sh
   cd /path/to/86Box-optimizations
   rm -rf build dist
   BREW_PREFIX=$(brew --prefix)
   export PATH="$BREW_PREFIX/opt/qt@6/bin:$PATH"
   export PKG_CONFIG_PATH="$BREW_PREFIX/opt/freetype/lib/pkgconfig:$BREW_PREFIX/opt/libpng/lib/pkgconfig:$BREW_PREFIX/opt/libslirp/lib/pkgconfig:$BREW_PREFIX/opt/openal-soft/lib/pkgconfig:$BREW_PREFIX/opt/rtmidi/lib/pkgconfig:$BREW_PREFIX/opt/fluidsynth/lib/pkgconfig:$BREW_PREFIX/opt/sdl2/lib/pkgconfig:$BREW_PREFIX/opt/qt@6/lib/pkgconfig:$BREW_PREFIX/opt/libserialport/lib/pkgconfig:$BREW_PREFIX/opt/webp/lib/pkgconfig"
   export CMAKE_PREFIX_PATH="$BREW_PREFIX:$BREW_PREFIX/opt/qt@6/lib/cmake:$BREW_PREFIX/opt/qt@6:$BREW_PREFIX/opt/sdl2:$BREW_PREFIX/opt/freetype:$BREW_PREFIX/opt/libpng:$BREW_PREFIX/opt/libslirp:$BREW_PREFIX/opt/openal-soft:$BREW_PREFIX/opt/rtmidi:$BREW_PREFIX/opt/fluidsynth:$BREW_PREFIX/opt/libserialport"
   ```
   * `CMAKE_PREFIX_PATH` includes the Homebrew root so `BundleUtilities` finds shared libraries like `libsharpyuv`, `libwebp`, and the Qt frameworks/plugins.  
   * `PKG_CONFIG_PATH` exposes pkg-config metadata for dependencies such as `freetype`, `SDL2`, `serialport`, and `webp`.

3. **Configure with CMake (Ninja generator)**
   ```sh
   cmake -S . -B build -G Ninja \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_INSTALL_PREFIX=$PWD/dist \
     -DCMAKE_OSX_ARCHITECTURES=arm64 \
     -DCMAKE_MACOSX_BUNDLE=ON \
     -DQT=ON \
     -DUSE_QT6=ON \
     -DOPENAL=ON \
     -DRTMIDI=ON \
     -DFLUIDSYNTH=ON \
     -DMUNT=OFF \
     -DDISCORD=OFF \
     -DNEW_DYNAREC=ON \
     -DLIBSERIALPORT_ROOT="$BREW_PREFIX/opt/libserialport" \
     -DQT_QMAKE_EXECUTABLE="$BREW_PREFIX/opt/qt@6/bin/qmake"
   ```
   * `LIBSERIALPORT_ROOT` must point to the Homebrew installation so that the mac build links against the correct `.dylib` and `fixup_bundle` can find it.
   * The bundle option ensures `cmake --install` produces `86Box.app` with the Qt macOS plugins copied in.

4. **Build and install**
   ```sh
   cmake --build build --config Release
   cmake --install build --config Release
   ```
   * The install step runs CMake’s `BundleUtilities` fixup, which copies Qt frameworks/plugins and all Homebrew dependencies (`libwebp`, `libopenal`, `libserialport`, etc.) into `dist/86Box.app`.
   * `fixup_bundle` rewrites the `@rpath`s, so expect `install_name_tool` warnings about invalidating Homebrew signatures (normal for redistributed libs).

5. **Verify the bundle**
   ```sh
   open dist/86Box.app
   ```
   * Running the app ensures the UI, audio, and optional serial/OPl accessories load correctly.
   * If additional SDKs (Vulkan, MoltenVK, etc.) are needed, install their headers/libraries and enable the matching CMake options before reconfiguring.

## Running Benchmarks
After building, verify the optimizations with the benchmark suite:
```sh
# MMX NEON vs Scalar performance (30M iterations)
./build/benchmarks/mmx_neon_micro.app/Contents/MacOS/mmx_neon_micro --iters=30000000 --impl=neon

# Dynarec IR integration testing (30M iterations)
./build/benchmarks/dynarec_micro.app/Contents/MacOS/dynarec_micro --iters=30000000 --impl=neon

# IR validation and cache metrics sanity check
./build/benchmarks/dynarec_sanity.app/Contents/MacOS/dynarec_sanity

# PSHUFB high-bit masking regression (included in suite)
# PSHUFB_MASKED runs as part of mmx_neon_micro to ensure masked lanes zero correctly on NEON vs scalar
```

Notes:
- All relevant Build instructions reflect the final working configuration described earlier; no extra patches are required besides the Qt plugin copy fix introduced in `src/qt/CMakeLists.txt`.
- Use Ninja for builds to ensure fast incremental rebuilds; switching generators requires adjusting the `cmake` configure/build commands accordingly.
- **Benchmark Suite**: Use `mmx_neon_micro` and `dynarec_micro` for performance verification, and `dynarec_sanity` for IR validation. Executables are in `build/benchmarks/*.app/Contents/MacOS/`.

## Automated Performance Profiling
For repeatable profiling runs (microbenchmarks + sanity harness) use the helper script:
```sh
./tools/run_perf_profiling.sh 30000000   # optional iteration override (defaults to 30M)
```
- Script checks that all benchmark bundles exist, writes logs into `perf_logs/<timestamp>/`, and parses ratio tables into `.json` using `tools/parse_mmx_neon_log.py` with a 0.5x floor.
- Outputs `mmx_neon.log/json`, `dynarec_micro.log/json`, and `dynarec_sanity.log` so Instruments or CI jobs can ingest results directly.
