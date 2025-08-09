# ParticleViewer

Visualize particle outputs from RAMSES simulations using OpenGL. This viewer loads RAMSES snapshot metadata (`info_XXXXX.txt`) and renders particles as points with a free-fly camera.

This project was originally written circa 2016 for Visual Studio 2015 and OpenGL via GLEW/GLFW. It includes a header-only snapshot reader (libRAMSES++) under GPLv3 inside `ParticleViewer/include/ramses`.

---

## Features
- RAMSES particle file reader (version 1–3) bundled in the repo (`include/ramses/*`)
- OpenGL point rendering via GLEW + GLFW
- Camera navigation (W/A/S/D + Q/Z, mouse look, scroll zoom)
- Simple GLSL shaders in `resources/shaders`

---

## Repository layout
```
ParticleViewer.sln
ParticleViewer/
  main.cpp
  Display.cpp
  Particle.cpp
  RAMSES_Particle_Manager.cpp
  include/
    Camera.h, Display.h, Shader.h, ...
    ramses/  # libRAMSES++ headers (GPLv3)
  resources/
    shaders/ # GLSL vertex/fragment shaders
```

---

## Prerequisites (Windows)
- Windows 10/11
- Visual Studio 2019 or 2022 (Desktop development with C++)
- Windows 10/11 SDK (comes with VS)
- GPU/driver supporting OpenGL 3.3+
- RAMSES snapshot on disk (folder containing `info_XXXXX.txt` and `part_XXXXX.*`)

Optional
- Git
- [vcpkg](https://github.com/microsoft/vcpkg) for dependency management (recommended)

---

## Dependencies
Required libraries:
- OpenGL (system) → `opengl32.lib`
- GLEW (used as static lib in code) → `glew32s.lib`
- GLFW → `glfw3.lib`
- GLM (header-only)
- OpenMP (optional; code compiles with or without) → enable if desired

The original project file (`ParticleViewer/ParticleViewer.vcxproj`) references historical, manually-installed paths under `C:\OpenGL\...` and the VS2015 toolset (v140). Two setup paths are provided below:

- Option A (modern): VS2019/VS2022 + vcpkg
- Option B (legacy/manual): VS2015-style manual library downloads and include/lib path wiring

---

## Option A: Build with vcpkg (recommended)
This route avoids manual library downloads and works with VS2019/VS2022.

1) Install vcpkg and integrate with MSBuild
```
> git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
> %USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
> %USERPROFILE%\vcpkg\vcpkg integrate install
```

2) Install dependencies (x64 recommended)
- The code defines `GLEW_STATIC`, so install static triplets to match:
```
> %USERPROFILE%\vcpkg\vcpkg install glew:x64-windows-static glfw3:x64-windows glm:x64-windows
```
Notes
- If you prefer dynamic libraries, remove or comment out `#define GLEW_STATIC` in `ParticleViewer/include/Display.h` and `ParticleViewer/main.cpp`, then install dynamic triplets (e.g., `glew:x64-windows`).
- SOIL is no longer required (and has been removed). If you later add texture loading, prefer `stb_image` or SOIL2.

3) Open the solution in VS2019/VS2022
- File → Open → `ParticleViewer.sln`

4) Retarget the project
- Right-click solution → "Retarget solution" → pick latest Windows SDK
- Right-click project → Properties → General:
  - Platform Toolset: `v143` (VS2022) or `v142` (VS2019)
  - Use `x64` platform

5) Ensure vcpkg MSBuild integration is active
- With `vcpkg integrate install`, VS/MSBuild will auto-add include/lib paths for installed ports. Build should locate GLEW/GLFW/GLM/SOIL automatically.

6) Copy runtime resources to output (one-time project tweak)
- Project → Properties → Build Events → Post-Build Event → Command Line:
```
xcopy /E /I /Y "$(ProjectDir)resources" "$(OutDir)resources\"
```
This ensures shaders are next to the built executable.

7) Build and run
- Set configuration: `Debug` | `x64`
- Build → Build Solution
- Debug → Start Without Debugging

8) Point to your RAMSES dataset
- Edit `ParticleViewer/main.cpp` and set:
```cpp
std::string fname = "D:\\path\\to\\ramses\\output_00215\\info_00215.txt";
```
- Rebuild and run. See Controls below.

---

## Option B: Legacy/manual setup (as originally written)
This mirrors the VS2015-era configuration referenced in the `.vcxproj`.

1) Download prebuilt libraries (matching your architecture)
- GLEW 2.0.0 (or newer) — pick Win32 or x64
- GLFW 3.4.x — Visual C++ binaries for your VS version
- GLM — header-only

2) Create a folder layout similar to what the project expects (example)
```
C:\OpenGL\
  glew-2.0.0\include\...
  glew-2.0.0\lib\Release\Win32\glew32s.lib   (or x64 variant)
  glfw-3.2.1.bin.WIN32\include\...
  glfw-3.2.1.bin.WIN32\lib-vc2015\glfw3.lib   (or x64 / newer)
  glm\glm\...                                  (headers)
```

3) Open solution in Visual Studio
- File → Open → `ParticleViewer.sln`

4) Retarget if prompted (Windows SDK) and choose your platform (Win32 or x64)
- IMPORTANT: Match library architecture to your Platform (Win32 ↔ 32-bit, x64 ↔ 64-bit). Do not mix.

5) Project include/library settings (verify/update)
- Project → Properties → C/C++ → General → Additional Include Directories:
  - `C:\OpenGL\glm`
  - `C:\OpenGL\glew-2.0.0\include`
  - `C:\OpenGL\glfw-3.4.0.bin.WIN64\include` (adjust for your version/arch)
- Project → Properties → Linker → General → Additional Library Directories:
  - `C:\OpenGL\glew-2.0.0\lib\Release\x64` (or `Win32`)
  - `C:\OpenGL\glfw-3.4.0.bin.WIN64\lib-vc2022` (or your version/arch)
- Project → Properties → Linker → Input → Additional Dependencies:
  - `opengl32.lib; glew32s.lib; glfw3.lib`

Notes
- The legacy project file also references `freeglut` include paths; `freeglut` is not required to build the current code.
- `#define GLEW_STATIC` is present in code; ensure you link `glew32s.lib` (static). If you only have `glew32.lib` (DLL), remove `GLEW_STATIC` and link/import the DLL accordingly.

6) Post-build step to copy resources (shaders)
- Same as Option A; add an `xcopy` post-build step to copy `resources` to `$(OutDir)`.

7) Set your RAMSES data path
- Edit `ParticleViewer/main.cpp` and set the `info_XXXXX.txt` path (see below).

8) Build and run

---

## Configure RAMSES dataset path
`main.cpp` currently contains a hard-coded info file path:
```cpp
std::string fname = "D:\\data\\ramses\\output_00215\\info_00215.txt";
```
Change this to point to your dataset. The viewer expects RAMSES file layout so it can load particle files via the included libRAMSES++ reader.

Tip: You can refactor to accept the path via command-line arguments in the future.

---

## Controls
- Move: `W` (forward), `S` (back), `A` (left), `D` (right), `Q` (up), `Z` (down)
- Look: mouse move (first movement captures cursor)
- Zoom: mouse scroll
- Print camera stats: `P`
- Reset camera: `R`
- Exit: `Esc`

The window size defaults to 960×540 (see `screenWidth`, `screenHeight` in `main.cpp`).

---

## Common issues & fixes
- Unresolved externals for GLEW/GLFW
  - Architecture mismatch (x64 vs Win32) → make libs and platform match
  - Using `glew32s.lib` but not defining `GLEW_STATIC` → either define it (code already does) or link the DLL import lib (`glew32.lib`) and remove the define
  - Missing `opengl32.lib` → add to Linker → Input (usually auto via Windows SDK)
- Black screen / nothing rendered
  - Ensure shaders were copied to `$(OutDir)\resources\shaders` (post-build step)
  - Confirm RAMSES dataset path points to an existing `info_XXXXX.txt` with particle files present
- GLFW window creation fails
  - Old OpenGL drivers; ensure GPU supports OpenGL 3.3+ and drivers are up to date
- x86 vs x64 library directory paths in the legacy project
  - The original `.vcxproj` contains some inconsistent paths (e.g., x64 lib dir while building Win32). Adjust as needed.
- OpenMP
  - The code uses `#include <omp.h>` but leaves OpenMP disabled by default. To enable: Project → Properties → C/C++ → Language → OpenMP Support → Yes (/openmp)

---

## Development notes
- The rendering path uploads `npartDraw = 32^3` points by default; see `RAMSES_Particle_Manager.h` / `.cpp`
- Shaders are created by `Shader` class and loaded from `resources/shaders`
- Core loop is in `main.cpp`

---

## License
- The libRAMSES++ headers under `ParticleViewer/include/ramses` are GPLv3 (see headers). You must comply with GPLv3 terms when distributing derivatives that incorporate this code.
- Other third-party dependencies (GLFW, GLEW, GLM, SOIL) have their own licenses.
- Your own code in this repository remains under your chosen license; be mindful of GPLv3 implications due to libRAMSES++ inclusion.

---

## Acknowledgements
- RAMSES by R. Teyssier
- libRAMSES++ by O. Hahn
- LearnOpenGL-style shader/GL scaffolding conventions
