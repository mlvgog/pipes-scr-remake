# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repository.

## What this project is

A faithful recreation of the classic Windows "3D Pipes" screensaver
(`sspipes.scr`, Windows NT 4 – XP). The entire program is **one C file**,
[pipes.c](pipes.c), using Win32 + fixed-function OpenGL 1.1 — deliberately the
same technology as the original. Keep it that way: no build system beyond the
gcc command (plus a second one for the test suite), no external libraries
beyond `opengl32`/`gdi32`/`user32`/`advapi32` (the registry, for settings), no C++ — unless the user explicitly asks
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
gcc -O2 -Wall -Wextra -o pipes.exe pipes.c -lopengl32 -lgdi32 -luser32 -ladvapi32 -mwindows
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
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -o pipes.exe pipes.c -lopengl32 -lgdi32 -luser32 -ladvapi32 -mwindows
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -o test_pipes.exe tests/test_pipes.c -lopengl32 -lgdi32 -luser32 -ladvapi32
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
size does not depend on the desktop size. (On a desktop smaller than that
with a hardware OpenGL driver, the off-screen part of the read-back is
undefined; GDI generic, Mesa and Wine render to memory and are fine.)

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

- **Settings** — `Settings` (multiple pipes, joint type, speed 0–100,
  resolution 0–200) is loaded from `HKCU\Software\pipes-scr-remake`
  (DWORD values `MultiplePipes`, `JointType`, `Speed`, `Resolution`;
  missing or nonsense values fall back to the original's defaults) and
  `applySettings` turns it into the globals the program uses
  (`multiplePipes`, `jointStyleSetting`, `slices` via the original's
  8/12/16/20 mapping, `stepMs` on a log scale 100 → 11 ms). The
  environment variable `PIPES_SETTINGS_KEY` points the program at another
  key; the test suite uses it so a developer's own settings are never
  touched. The "3D Pipes Setup" dialog is built at run time from an
  in-memory `DLGTEMPLATE` (`buildSettingsDialog`, standard user32
  controls only — no `.rc` file, no comctl32) and run with
  `DialogBoxIndirectParamA`; `/c[:hwnd]` makes the given window its owner,
  and running the `.scr` with no arguments opens it too (that is what
  Explorer's *Configure* does), while `pipes.exe` with no arguments stays
  the windowed viewer.
- **Tunables** — every visual/behavioral knob is a `#define` at the top
  (pipe/elbow/ball radii, `STEP_MS`, pipes per scene, straight weights,
  ball/chase odds, dissolve timing, per-scene turn, grid extents,
  tessellation). Tune look-and-feel here, not in the drawing code. The
  user-facing options come from Settings (below).
- **Materials** — `TEA_MATERIALS` is the classic OpenGL "tea" material
  table (24 entries: ambient, diffuse, specular RGB + shininess/128);
  `PIPE_MATERIALS` lists the 16 the original picks pipes from.
  `applyMaterial` sets them with `glMaterialfv` (no `GL_COLOR_MATERIAL`).
- **`Prim`** — one drawn primitive with a material: cylinder (start point +
  axis direction + length), sphere (center + radius), or elbow (grid node +
  in/out directions). The whole picture is a flat append-only list of prims
  that is re-rendered every frame; "growing" a straight pipe just increments
  the `len` of its last cylinder prim by `CELL`.
- **`Pipe`** — a walker on the grid: `state` (`PIPE_ACTIVE`, `PIPE_STUCK`,
  `PIPE_OUT_OF_NODES`), position, direction, its own `weightStraight`,
  chase roles (`followsLead`, `startsFarAway`), the prim index of the
  cylinder it is extending (`lastCyl`), color.
- **`Scene`** — one fully independent simulation + camera: occupancy grid,
  prim list, up to `MAX_PIPES_AT_ONCE` pipes, the scene's pipe budget and
  count (`pipeBudget`, `pipesStarted`), chase state (`chase`, `lead`), the
  current `jointStyle`, `phase` (`SCENE_GROWING` → `SCENE_FINISHED` →
  `SCENE_DISSOLVING`), the dissolve plan, the per-scene `rotation`, viewport
  rect and derived projection (`aspect`, `dFront`, `camZ`, grid extents
  `NX/NY/NZ`). Fullscreen creates one `Scene` per monitor
  (`EnumDisplayMonitors`); windowed and `/shot` modes use a single Scene
  covering the client area.
- **Simulation** — the original's rules. `startScene` sets the budget
  (5 pipes; ×1.5 = 7 with `multiplePipes`, starting 2–4 at once), rolls a
  chase (1 in 5, sticky like the original), advances a cycling joint style
  and sets the rotation (the original's timing: the first two scenes share
  0°, then +9.73156° each). `sceneTick` retires or restarts stuck pipes
  (`retireOrRestartStuckPipes`; a restart counts against the budget), then
  `growPipe`s each one: `chooseDirection` is a weighted draw over
  `weightStraight` + free turns (weight 100 = always straight), or for a
  chase follower a step towards the lead. With no pipes left the scene is
  `SCENE_FINISHED` for one tick, then `beginDissolve` plans a random order
  of squares (`planDissolve`, min(w,h)/32 px, alternating linear probe
  like the original) that `drawDissolve` paints black over `DISSOLVE_MS`,
  then the next scene starts.
- **Rendering** (`sceneDraw`, `drawDissolve`, `drawAll`) — three display
  lists built once (cylinder, sphere, quarter-torus elbow); per frame: one
  clear, then each scene sets its own viewport + frustum, applies its
  rotation, replays its prim list, and paints its eaten dissolve squares as
  black quads in a viewport-sized ortho projection.
- **Win32 plumbing** — `parseCommandLine` turns the screensaver argument
  contract (`/s`, `/c`, `/p`, plus the custom `/shot`) into a
  `LaunchOptions`; `createGLWindow`/`destroyGLWindow` own the window and
  context; `createScenes`/`destroyScenes` own the scenes; `runShot` and
  `runInteractive` are the two main loops (the latter a fixed-timestep
  accumulator over `QueryPerformanceCounter`, vsync via `wglSwapIntervalEXT`
  when available); `wndProc` holds the input-to-exit logic. `WinMain` only
  wires these together. `/p <hwnd>` (and `/p:<hwnd>`) sets `previewing`
  and creates the window as a disabled `WS_CHILD` filling the given
  window's client area, so clicks reach the settings dialog; it ignores
  input and ends when the parent destroys it (`WM_DESTROY`). A handle that
  is missing or not a window exits at once with code 0.

### Geometry conventions (read before touching joint/drawing math)

- Directions are encoded 0–5 as +x,−x,+y,−y,+z,−z; **opposite is `d ^ 1`**.
  The `DV` table maps codes to vectors.
- The original's units: nodes are `CELL` = 7 apart, pipes have radius
  `PIPE_R` = 1. `gridForViewport` gives 15 nodes along the longer screen
  side, `(int)(15/aspect)` along the shorter and 15 deep; `nodeWorld()`
  centers the grid on the origin. The camera is at z = `EYE_DISTANCE` (75)
  looking down −Z with a 90° vertical field of view (`glFrustum` with
  near 1 and `farPlane`), then turned by the scene's rotation about Y.
  A window resize regrids and starts a fresh scene (`sceneResize`), as the
  original did.
- A pipe piece runs from one radius past a node to one radius before the
  next: the first stretch after a start or a joint is `SHORT_PIPE`
  (7 − 2 = 5) long, and each straight step adds `CELL`. Joints and caps sit
  on the node and fill the gap: balls/caps have radius `ballRadius()` =
  √2·R/cos(π/slices) (just enough to hide the corners of a faceted pipe end).
- The elbow display list is a quarter torus in the local XY plane: arc from
  `(ELBOW_R,0,0)` (tangent +Y) to `(0,ELBOW_R,0)` (tangent −X), with
  `ELBOW_R` = `PIPE_R` (the original's tight elbow). Placement
  (`elbowMatrix`) maps local +Y to the incoming travel direction `d1` and
  local −X to the outgoing `d2`; the arc center is
  `node − ELBOW_R·d1 + ELBOW_R·d2`, so the arc starts exactly where the
  incoming pipe ends and finishes where the outgoing one begins, and the
  third basis vector is the cross product so the matrix stays a proper
  rotation.
- Tessellation is `slices` (8 by default, like the original): cylinders and
  elbow rings have `slices` sides, spheres `slices`×`slices`, elbows
  `slices/2` segments along the bend. With an even side count every ring
  has vertices on the axis directions, so cylinder and elbow facets line up.
- Back-face culling is on, so every display list must wind its quads
  counter-clockwise seen from outside (the cylinder and sphere strips emit
  the far vertex of each pair first for that reason).
- Lighting is the original's: global ambient 1.0, one directional light at
  (90, 90, 150) in eye space with ambient 0.1, diffuse 1 and the default
  white specular.
- Cylinders are unit-length along +Z in the display list and scaled by
  `glScalef(1,1,len)` — side normals have z = 0, so the non-uniform scale
  does not corrupt lighting (and `GL_NORMALIZE` is enabled regardless, for
  the uniformly scaled unit spheres).
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
- Keep [WORKLOG.md](WORKLOG.md) current at every step (roadmap status,
  the next step, a dated log entry, and any research facts the next steps
  depend on) and commit it with the work, pushing work in progress to the
  feature branch promptly, so the work can always be resumed from the
  repository alone.
- Commit style: imperative subject; a detailed body explaining what, why,
  the design decisions, and how it was verified — the history doubles as the
  project's design log. End with a `Co-Authored-By:` trailer naming the
  Claude model that did the work (`Claude <Model> <noreply@anthropic.com>`).
- Build artifacts (`*.exe`, `*.scr`, test BMP/PNGs) are gitignored;
  `preview.png` is the one committed image. If a change alters the look,
  regenerate `preview.png` from a ~140-step `/shot` render and mention it in
  the commit. The committed one is `/shot preview.bmp 140 7` with Multiple
  pipes and Mixed joints saved (point `PIPES_SETTINGS_KEY` at a scratch key
  holding `MultiplePipes`=1 and `JointType`=2, then delete it).
- README must keep: the AI-generation notice (with model name), build +
  `.scr` install instructions, and the missing-features list. When you
  implement something from that list, remove it from the list in the same
  commit.

## Missing features / future work (mirror of README)

- Teapot easter egg (needs Utah teapot Bezier patch data — do not hand-type
  it from memory; fetch real data).
- Textured pipes (the original's optional surface mode).
