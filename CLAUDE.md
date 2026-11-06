# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## What this project is

A faithful recreation of the classic Windows "3D Pipes" screensaver
(`sspipes.scr`, Windows NT 4 – XP). The entire program is **one C file**,
[pipes.c](pipes.c), using Win32 + fixed-function OpenGL 1.1 — deliberately the
same technology as the original. Keep it that way: no build system beyond the
gcc command (plus a second one for the test suite), no external libraries
beyond `opengl32`/`gdi32`/`user32`, no C++ — unless the user explicitly asks
otherwise.

The project was AI-generated (Anthropic Claude via Claude Code: Claude Fable 5
wrote the original program, Claude Opus 5.5 the later features) and the README
discloses this; keep that notice intact.

## Build

```powershell
.\build.ps1
```

or directly (MSYS2 ucrt64 gcc lives at `C:\msys64\ucrt64\bin\gcc.exe` on the
dev machine; it is NOT on PATH in Claude's shell):

```
gcc -O2 -Wall -Wextra -o pipes.exe pipes.c -lopengl32 -lgdi32 -luser32 -mwindows
```

Treat `-Wall -Wextra` warnings as errors (the test build uses `-Werror`).
The `wglGetProcAddress` function-pointer cast warning is silenced via an
intermediate `(void *)` cast.

## Tests

```powershell
.\build.ps1 -Test
```

builds `pipes.exe` and `test_pipes.exe` with `-Werror` and runs the suite.
`tests/test_pipes.c` does `#define WinMain pipesProgramEntry` and then
`#include "../pipes.c"`, so every `static` function in pipes.c is directly
callable from the tests while pipes.c stays a single file. The suite has two
kinds of test:

- **In-process**: argument parsing, the simulation (stepping pipes in
  hand-built scenes), monitor-to-viewport mapping, and a real OpenGL render
  through `createGLWindow` + `saveBMP`.
- **Whole program**: it launches `pipes.exe` (path = first argument, default
  `pipes.exe`) and drives it with window messages: `/shot` output, windowed
  and `/s` input handling, `/c`, `/p`. Renders land in `test-output/`.

Tests are functions whose names are sentences (`a_pipe_never_moves_into_a_node_that_is_already_taken`);
the runner prints them with spaces as `PASS`/`FAIL` lines, followed by
plain-English expected/actual notes. Every behaviour change needs a test in
the same PR, and the whole suite must pass.

CI (`.github/workflows/tests.yml`) runs `build.ps1 -Test` on `windows-latest`
for every pull request and push to main, and keeps `test-output/` as an
artifact.

In a Linux cloud session, cross-compile and run under Wine instead:

```
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -o pipes.exe pipes.c -lopengl32 -lgdi32 -luser32 -mwindows
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -o test_pipes.exe tests/test_pipes.c -lopengl32 -lgdi32 -luser32
WINEDEBUG=-all xvfb-run -a -s "-screen 0 1920x1080x24" wine test_pipes.exe
```

(packages: `mingw-w64 wine64 xvfb mesa-utils libgl1-mesa-dri imagemagick`).

## Verifying changes (important)

Never claim a visual change works without looking at output. The program has
a built-in self-test mode for exactly this:

```
pipes.exe /shot out.bmp [steps] [seed]
```

simulates N growth steps (default 700; ~140 gives a pleasing mid-stage
scene), renders one frame, writes a BMP, and exits with code 0 (1 if the BMP
could not be written). A seed makes the picture reproducible byte for byte.
The shot window is a borderless 1280×720 popup at the screen origin, so its
size does not depend on the desktop size.

`pipes.exe` is a GUI app (`-mwindows`), so PowerShell does not wait for it;
use `Start-Process -Wait`, then convert the BMP to PNG so the Read tool can
display it (on Linux: `convert out.bmp out.png`):

```powershell
$p = Start-Process -FilePath .\pipes.exe -ArgumentList '/shot','out.bmp','140' -PassThru -Wait
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile("$PWD\out.bmp")
$b.Save("$PWD\out.png", [System.Drawing.Imaging.ImageFormat]::Png)
$b.Dispose()
```

Then Read the PNG and judge it against the reference look (see
[preview.png](preview.png)). Delete test BMP/PNGs before committing (the
.gitignore catches them, but keep the tree clean).

For fullscreen/multi-monitor checks: launch `pipes.exe /s` with
`Start-Process -PassThru` (no `-Wait`), inspect via `GetWindowRect` /
`Graphics.CopyFromScreen`, then `Stop-Process`. The saver exits on its own if
the real mouse moves more than 4 px, so capture quickly.

Smoke-test both interactive modes after any change to the window/GL plumbing:
windowed (no args) and `/s`, each left running a few seconds, then killed.

## Architecture of pipes.c

Single file, top-to-bottom in this order. All state lives in structs or
file-scope statics; no dynamic allocation except one `calloc` per `Scene`.

- **Tunables** — every visual/behavioral knob is a `#define` at the top
  (pipe/elbow/ball radii, `NUM_PIPES`, `STEP_MS`, turn/joint probabilities,
  `DEATHS_RESET`, grid extents, tessellation). Tune look-and-feel here, not
  in the drawing code.
- **`Prim`** — one drawn primitive: cylinder (start point + axis direction +
  length), sphere (center + radius), or elbow (grid node + in/out
  directions). The whole picture is a flat append-only list of prims that is
  re-rendered every frame; "growing" a straight pipe just increments the
  `len` of its last cylinder prim, and a turn retracts that cylinder by
  `ELBOW_R` before inserting the torus.
- **`Pipe`** — a walker on the grid: position, direction, the prim index of
  the cylinder it is extending (`lastCyl`), color.
- **`Scene`** — one fully independent simulation + camera: occupancy grid,
  prim list, `NUM_PIPES` pipes, death/holding reset state, viewport rect and
  derived projection (`aspect`, `dFront`, `camZ`, grid extents `NX/NY/NZ`).
  Fullscreen creates one `Scene` per monitor (`EnumDisplayMonitors`);
  windowed and `/shot` modes use a single Scene covering the client area.
- **Simulation** (`stepPipe`, `spawnPipe`, `killPipe`, `sceneReset`,
  `sceneTick`) — each tick every live pipe moves one cell, preferring
  straight (`P_STRAIGHT`); a boxed-in pipe gets an end cap and is replaced;
  after `DEATHS_RESET` deaths the scene holds `HOLD_MS` then clears —
  replicating the original's abrupt reset.
- **Rendering** (`sceneDraw`, `drawAll`) — three display lists built once
  (cylinder, sphere, quarter-torus elbow); per frame: one clear, then each
  scene sets its own viewport + frustum and replays its prim list.
- **Win32 plumbing** — `parseCommandLine` turns the screensaver argument
  contract (`/s`, `/c`, `/p`, plus the custom `/shot`) into a
  `LaunchOptions`; `createGLWindow`/`destroyGLWindow` own the window and
  context; `createScenes`/`destroyScenes` own the scenes; `runShot` and
  `runInteractive` are the two main loops (the latter a fixed-timestep
  accumulator over `QueryPerformanceCounter`, vsync via `wglSwapIntervalEXT`
  when available); `wndProc` holds the input-to-exit logic. `WinMain` only
  wires these together.

### Geometry conventions (read before touching joint/drawing math)

- Directions are encoded 0–5 as +x,−x,+y,−y,+z,−z; **opposite is `d ^ 1`**.
  The `DV` table maps codes to vectors.
- Grid nodes are `CELL` apart; `nodeWorld()` centers the grid on the origin.
  The camera sits on +Z at `camZ` looking down −Z; pipes are axis-aligned to
  the view, like the original.
- The elbow display list is a quarter torus in the local XY plane: arc from
  `(ELBOW_R,0,0)` (tangent +Y) to `(0,ELBOW_R,0)` (tangent −X). Placement
  maps local +Y to the incoming travel direction `d1` and local −X to the
  outgoing `d2`; the arc center is `node − ELBOW_R·d1 + ELBOW_R·d2`, and the
  third basis vector is the cross product so the matrix stays a proper
  rotation. The incoming cylinder must be shortened by `ELBOW_R` or it pokes
  through the outside of the bend.
- Cylinders are unit-length along +Z in the display list and scaled by
  `glScalef(1,1,len)` — side normals have z = 0, so the non-uniform scale
  does not corrupt lighting (and `GL_NORMALIZE` is enabled regardless, for
  the uniformly scaled spheres).
- GL viewport origin is **bottom-left**; monitor rects from
  `EnumDisplayMonitors` are top-left in virtual-screen coordinates. The
  conversion lives in `monitorEnum` (`vy = winH − (yTop + h)`). Monitors can
  have negative virtual-screen coordinates; everything is offset by
  `vsX/vsY`.
- Scenes never overlap pixels, so a single shared depth buffer and no
  scissor test are sufficient. If you ever add overlapping viewports or
  per-scene clear colors, you will need `GL_SCISSOR_TEST`.

## Known traps (each of these caused a real bug or failure)

- **Argument parsing order**: `/shot` must be matched **before** `/s`,
  because `/s` is a prefix of `/shot`. Any new `/s…` argument has the same
  problem.
- **`MAX_MONITORS` is taken**: `ddeml.h` (included via `windows.h`) defines
  it. Ours is named `MAX_SCENES`. Check new macro names against Windows
  headers.
- **PowerShell + GUI apps**: launching `pipes.exe` returns immediately; use
  `Start-Process -Wait` when you need the exit/output (see Verifying above).
- **The sandboxed PowerShell tool may misparse compound commands** whose
  later arguments contain path-like strings (e.g. a commit message
  containing `/shot` after a `Remove-Item`). Split into separate calls if a
  safe command is unexpectedly blocked.
- Fullscreen exits on >4 px of real mouse movement — don't assume a long
  `/s` session will survive on a machine someone is using.

## Repository conventions

- Git identity for this repo is intentionally the GitHub noreply address
  (`mlvgog <mlvgog@users.noreply.github.com>`), set in local config. The
  repo must stay free of personal information: no real names, personal
  emails, or `C:\Users\<name>` paths in any committed file **or commit
  message**. Before pushing, scan history if in doubt
  (`git grep -iE '<patterns>' $(git rev-list --all)`).
- Every change goes on its own feature branch (`feature/<name>`, cut from
  the latest `main`) and reaches `main` only by merging its pull request,
  after review and with the CI test run green. Never push to `main`
  directly. Merge with rebase so the individual commits (the design log)
  survive.
- Commit style: imperative subject; a detailed body explaining what, why,
  the design decisions, and how it was verified — the history doubles as the
  project's design log. End with a `Co-Authored-By:` trailer naming the
  Claude model that did the work (`Claude <Model> <noreply@anthropic.com>`).
- Build artifacts (`*.exe`, `*.scr`, test BMP/PNGs) are gitignored;
  `preview.png` is the one committed image. If a change alters the look,
  regenerate `preview.png` from a ~140-step `/shot` render and mention it in
  the commit.
- README must keep: the AI-generation notice (with model name), build +
  `.scr` install instructions, and the missing-features list. When you
  implement something from that list, remove it from the list in the same
  commit.

## Missing features / future work (mirror of README)

- Teapot easter egg (needs Utah teapot Bezier patch data — do not hand-type
  it from memory; fetch real data).
- Configuration dialog (`/c` is a stub; options are compile-time constants).
- `/p <hwnd>` preview-pane rendering (currently exits cleanly).
- Textured pipes (the original's optional surface mode).
