# AI-first OpenSCAD — platform builds

This fork targets three platforms:

| Platform | Arch / runtime | AI Agent |
|----------|----------------|----------|
| **macOS** | Apple Silicon (`arm64`) only | Yes |
| **Windows** | x86_64 (MSYS2 UCRT64) | Yes |
| **Web** | WebAssembly (Emscripten) | No (headless WASM stub) |

Intel macOS (`x86_64` / universal) is **not** supported.

## macOS (Apple Silicon)

Requirements: macOS on Apple Silicon, Xcode / CLT, Homebrew deps via `./scripts/macosx-build-homebrew.sh qt6`.

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DEXPERIMENTAL=ON
cmake --build build -j"$(($(sysctl -n hw.ncpu) * 3 / 2))"
```

Release packaging:

```bash
./scripts/release-common.sh snapshot
```

Dependency bootstrap (arm64 only; `-x` is rejected):

```bash
./scripts/macosx-build-dependencies.sh -a -d
```

## Windows

See [win-build.md](win-build.md) for a full native setup. CI uses MSYS2 UCRT64 + Qt6:

```bash
# Inside MSYS2 UCRT64 after installing deps via scripts/msys2-install-dependencies.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEXPERIMENTAL=ON -DUSE_QT6=ON
cmake --build build -j4
```

Cross-build from Linux (MXE) is still documented in the root [README.md](../README.md).

## Web (WASM)

Uses the premade Emscripten image. On Apple Silicon Docker hosts you may need QEMU (`tonistiigi/binfmt`) as noted in `scripts/wasm-base-docker-run.sh`.

```bash
./scripts/wasm-base-docker-run.sh emcmake cmake -B build-web \
  -DCMAKE_BUILD_TYPE=Release \
  -DWASM_BUILD_TYPE=web \
  -DEXPERIMENTAL=ON
./scripts/wasm-base-docker-run.sh cmake --build build-web -j2
```

Outputs: `build-web/openscad.js` and `build-web/openscad.wasm` (for playground-style UIs).

**Note:** AI chat / OpenAI-compatible APIs are desktop-only. WASM builds remain headless geometry engines.

## CI

GitHub Actions workflows:

- [`.github/workflows/macos-tests.yml`](../.github/workflows/macos-tests.yml) — `macos-latest` arm64
- [`.github/workflows/windows.yml`](../.github/workflows/windows.yml) — Windows MSYS2 Qt6
- [`.github/workflows/wasm.yml`](../.github/workflows/wasm.yml) — `openscad/wasm-base-release` web build

CircleCI macOS jobs also build **arm64 only** (no Rosetta / universal deps).
