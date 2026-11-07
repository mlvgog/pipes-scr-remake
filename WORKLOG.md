# Work log

A running record of the work on this repository, so it can be picked up
from here at any time. Update it at every step and commit it with the work.
The project rules (single C file, the test suite, the feature-branch and
pull-request workflow, commit style) are in [CLAUDE.md](CLAUDE.md).

## Goal

Implement the README's missing features (preview pane, settings dialog,
textured pipes, teapot), first fixing the core behaviour so it matches the
original Windows XP `sspipes.scr`, which the owner runs every day and will
compare against closely. Each item is its own feature branch and pull
request. The PR is reviewed, the test suite must pass in CI, and the PR is
merged with **rebase**.

## Roadmap

| # | Branch | Status |
|---|---|---|
| 1 | `feature/test-suite`: test suite, Windows CI, testable `pipes.c` | **merged** ([#1](https://github.com/mlvgog/pipes-scr-remake/pull/1)) |
| 2 | `feature/original-scene-rules`: the original's simulation | **merged** ([#2](https://github.com/mlvgog/pipes-scr-remake/pull/2)) |
| 3 | `feature/original-look`: camera, proportions, materials, lighting, speed | **in progress** |
| 4 | `feature/preview-pane`: `/p <hwnd>` | to do |
| 5 | `feature/settings-dialog`: XP "3D Pipes Setup" dialog + registry | to do |
| 6 | `feature/textured-pipes`: Solid/Textured + "Choose Texture..." | to do |
| 7 | `feature/teapot`: Utah teapot easter egg | to do |
| 8 | `feature/release`: a really good first release | to do |

## Next step

PR 3 (`feature/original-look`) is implemented: 76 tests pass under Wine,
and the renders look like the XP saver. Next: commit, push, open the PR,
send the owner renders, then CI, review and rebase-merge. After that, cut
`feature/preview-pane` from the new `main`.

Housekeeping: the git proxy in cloud sessions refuses to delete remote
branches, so the merged `feature/test-suite` and
`feature/original-scene-rules` are still on GitHub (harmless; delete them in
the GitHub UI).

## The release (PR 8)

The owner considers releases a big thing and wants a really good one:

- A tag-triggered GitHub Actions workflow on `windows-latest` builds
  `pipes.exe` and `pipes.scr` with `-O2`.
- The full test suite must pass before anything is published.
- The release attaches the binaries, a zip and SHA-256 checksums.
- Hand-written release notes include:
  - what it is, with the AI-generation notice;
  - a screenshot or render;
  - install steps for the `.scr`;
  - the settings;
  - what matches the original and what's still missing;
  - the test summary.
- Add a CHANGELOG and a version string in the program (e.g. shown in the
  settings dialog).
- Cut it once PRs 2–7 are merged, after the owner has seen renders.

## How to build and test in a Linux cloud session

The personal environment `pipes-scr-remake` installs the toolchain. By hand:
`apt-get install -y mingw-w64 wine64 xvfb mesa-utils libgl1-mesa-dri imagemagick`.
The build and test commands are in CLAUDE.md under "Tests". Look at renders
with `convert out.bmp out.png`.

## The original's behaviour (the spec for PRs 2–7)

Sources:
- The NT 4.0 SDK sample this screensaver comes from (`MSTOOLS\SAMPLES\OPENGL\SCRSAVE`),
  as documented by the C# port
  [RussDev7/SspipesSharp](https://github.com/RussDev7/SspipesSharp). That
  port is GPL-3.0, so use it **only as a behavioural reference** (rules,
  constants, probabilities) and never copy its code.
- [pipes-tv](https://github.com/TheWombRaider/pipes-tv), which has
  measurements from footage.
- The owner's answers.

### Scene rules (PR 2, done)
- **Straight weights:** each pipe gets a straight weight once. 1 in 20 pipes
  get 25–100, the rest 1–4. Each step is a random draw over
  `weight + free turns`, and weight 100 always goes straight while it can.
- **Start:** a random free node (fall back to a scan) with a random initial
  heading, then a start cap plus the first stretch. A start with no free way
  is stuck at once; the original draws a teapot there (PR 7).
- **Budget:** Single = 1 pipe at a time, 5 per scene. Multiple = 2–4 start
  together, 7 per scene (5 × 1.5). Textured scenes use 3 instead of 5
  (PR 6). A stuck pipe restarts while the budget lasts; a restart that finds
  no free node shrinks the budget.
- **Chase:** Multiple only, 1 in 5 per scene, and sticky: the original never
  turns it off. Followers step towards the lead. The lead restarts near the
  corner opposite where it got stuck. If the lead retires, a random pipe
  takes over.
- **Joints:**
  - Elbow and Ball apply to every turn.
  - Mixed = 1 in 3 ball, plus 1 in 1000 teapot (PR 7).
  - Cycle starts at Mixed and advances every scene (including the first):
    Mixed→Elbow→Ball→Mixed. So the first scene is Elbow.
- **Scene end:** when no pipes are left, the finished picture stays for one
  step, then dissolves:
  - Squares of min(w,h)/32 px are eaten in random order, with an
    alternating down/up linear probe on collisions.
  - It takes about 1.7 s (measured).
  - The next scene's rotation = the stored value, which then grows by
    9.73156° (wrapping at 360). So scenes 1 and 2 are at 0° and scene 3 at
    9.73°.
  - A resize does a full clear and reset with no dissolve (PR 3, with the
    grid).

### Look (PR 3)
- **Grid:** 15 nodes on the longer screen axis, `(int)(15/aspect)` on the
  shorter, 15 deep, nodes 7 units apart, pipe radius 1.
- **Camera:** 90° vertical field of view, near 1,
  far `75 + 2·max(NX,NY)/15·16·7`, eye at z −75, then rotate about Y by the
  scene rotation.
- **Lengths:** straight segment 7 units. The short segment at a start or
  after a joint is 5 units (`7 − 2·radius`), from one radius past the node
  to one radius before the next node.
- **Elbow:** sweep radius = pipe radius (a tight elbow), with slices/2
  stacks along the bend.
- **Balls and caps:** solid radius `√2·R/cos(π/slices)`; textured `√2·R`.
- **Tessellation:** 8 sides by default. The resolution slider (0–200) maps
  to `(int(T/100·4/2.0001) + 2)·4` = 8, 12, 16 or 20 sides.
- **Lighting:**
  - Global ambient 1.0 (0.6 when textured).
  - Light0: ambient 0.1, diffuse 1, default specular, directional
    (90, 90, 150, 0).
  - Back-face culling on.
  - Material set per pipe with `glMaterial` (ambient/diffuse/specular/
    shininess×128).
- **Materials:** the classic OpenGL "tea" material table (24 entries, as in
  SGI's teapots demo), restricted to the 16 "good" indices
  0,1,3,4,5,6,7,9,10,11,13,16,17,19,20,22. One random material per pipe.
  Textured pipes use four near-white materials instead (PR 6).
- **Speed:** about 30 segments per second per pipe (measured from footage)
  at the default speed.

### Preview pane (PR 4)
`/p <hwnd>` and `/p:<hwnd>` give a `WS_CHILD` window sized to the parent's
client area. It ignores input-to-exit and quits when the parent goes away.

### Settings dialog (PR 5): the XP "3D Pipes Setup"
- Controls: Single/Multiple; a Joint Type drop-down (Elbow, Ball, Mixed,
  Cycle); Surface Style Solid/Textured with "Choose Texture..."; a speed
  slider (Slow–Fast); a resolution slider (Min–Max); OK/Cancel.
- XP has no Flex "Pipe Style"; only pre-XP versions do.
- Defaults: Single, Elbow, Solid, lowest resolution (8 sides).
- Storage: DWORDs in `HKCU\Software\pipes-scr-remake` (advapi32). Build the
  dialog from an in-memory `DLGTEMPLATE` (user32 only).
- `/c:<hwnd>` is the owner. Running the `.scr` with no arguments opens the
  dialog; `pipes.exe` with no arguments stays windowed.

### Textured pipes (PR 6)
- `GL_MODULATE`, `GL_REPEAT`, `GL_NEAREST`, global ambient 0.6.
- s runs along a segment (a short segment starts at 2/7), t around the pipe.
  Repeats = `max(1, round(viewport/800))`.
- The texture seam turns 90° about `old × new` at every turn.
- A textured ball joint is a swept ball patch.
- The original's built-in `stripe.bmp` can't be redistributed, so generate
  one in code. The owner remembers textured mode as "kinda as is with
  bright coloured pipes", so keep the texture subtle and show renders.
- "Choose Texture..." uses `GetOpenFileNameW` (comdlg32). Load the BMP with
  `LoadImageW` + `GetDIBits` and resample it to a power of two.

### Teapot (PR 7)
- Data: freeglut `src/fg_teapot_data.h` (MIT). Embed the 10 patches and
  control points and add a `THIRD_PARTY_NOTICES.md`.
- Draw with GL 1.1 evaluators (`glMap2f`/`glEvalMesh2`) and
  `GL_AUTO_NORMAL`, mirrored as in the GLUT teapot. Use front face CW and
  `GL_NORMALIZE` while drawing it.
- Size 2.5 × pipe radius, upright in its own frame (not aligned to the
  pipe), in the pipe's material.
- It appears on 1 in 1000 joints in Mixed (and in Cycle's Mixed phase), and
  wherever a pipe starts fully boxed in (any joint style).

## Log

- **2026-11-06**
  - Planned the seven PRs with the owner.
  - Set up mingw + Wine + Xvfb and the `pipes-scr-remake` cloud environment.
- **2026-11-06, PR 1:**
  - Removed source comments, made `pipes.c` testable, added `/shot` seeds.
  - Added the 43-test suite and Windows CI.
  - Reviewed it and fixed the findings.
  - Windows-only fixes: gcc format-truncation, and the OK button of an
    OK-only message box has ID `IDCANCEL`, so tests click buttons by their
    label.
  - Merged #1 by rebase.
- **2026-11-07, PR 2:**
  - Opened PR #2.
  - Review fixes: the far-corner search only visits the shell at each
    distance (a brute-force test checks it); clearer cap helpers in the
    tests.
  - The owner asked for a really good release, added as PR 8.
  - Implemented the original's scene rules (above).
  - Tests grew to 68, all passing under Wine.
  - The default render is now one pipe of elbows, as in the original's
    default settings.
- **2026-11-07, PR 2:** CI green and merged #2 by rebase.
- **2026-11-07, PR 3:**
  - Implemented the original's look:
    - 15-node grid, 7-unit spacing, radius 1.
    - 90° camera at 75 units; far plane from the grid.
    - Tight elbows, 5-unit short pieces, `√2/cos(π/8)` balls, 8 sides.
    - The 16 tea materials, the original's light, back-face culling.
    - 33 ms steps; resize starts a fresh scene.
  - preview.png is a 140-step render with Multiple + Mixed switched on.
  - 76 tests pass.
  - Opened #3 and sent the owner renders (also in the gitignored
    `renders/` folder).
  - Review fixes:
    - MAX_PRIMS is now sized from the 15³ grid (~7× less memory per scene).
    - The camera test reads the real projection and modelview matrices
      after `sceneDraw`.
    - The far plane is checked to cover the turned grid.
  - Left for PR 5:
    - Regenerate preview.png from a plain `/shot` once settings are saved.
    - The resolution slider must keep the side count even (8/12/16/20) so
      cylinder and elbow facets line up; add a test for that.
