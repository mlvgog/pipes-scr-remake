#define WinMain pipesProgramEntry
#include "../pipes.c"
#undef WinMain

#include <stdarg.h>

#define OUTPUT_DIR "test-output"
#define PROGRAM_TIMEOUT_MS 60000
#define WINDOW_TIMEOUT_MS 15000
#define DIALOG_SETTLE_MS 500

static int  testsPassed;
static int  testsFailed;
static int  currentTestFailed;
static char failureNotes[8192];
static char pipesExe[MAX_PATH] = "pipes.exe";

static void addNote(const char *format, ...)
{
    size_t used = strlen(failureNotes);
    va_list args;
    if (used + 16 >= sizeof failureNotes) return;
    strcat(failureNotes, "          ");
    used = strlen(failureNotes);
    va_start(args, format);
    vsnprintf(failureNotes + used, sizeof failureNotes - used - 2, format, args);
    va_end(args);
    strcat(failureNotes, "\n");
}

static void expect(int condition, const char *whatShouldBeTrue)
{
    if (condition) return;
    currentTestFailed = 1;
    addNote("expected %s", whatShouldBeTrue);
}

static void expectNumber(const char *what, long long expected, long long actual)
{
    if (expected == actual) return;
    currentTestFailed = 1;
    addNote("%s: expected %lld, got %lld", what, expected, actual);
}

static void expectBetween(const char *what, double low, double high, double actual)
{
    if (actual >= low && actual <= high) return;
    currentTestFailed = 1;
    addNote("%s: expected between %g and %g, got %g", what, low, high, actual);
}

static void expectNear(const char *what, double expected, double actual)
{
    if (fabs(expected - actual) < 1e-4) return;
    currentTestFailed = 1;
    addNote("%s: expected %g, got %g", what, expected, actual);
}

static void expectText(const char *what, const char *expected, const char *actual)
{
    if (strcmp(expected, actual) == 0) return;
    currentTestFailed = 1;
    addNote("%s: expected \"%s\", got \"%s\"", what, expected, actual);
}

static void heading(const char *title)
{
    printf("\n%s\n", title);
}

static void runTest(const char *name, void (*test)(void))
{
    char sentence[256];
    size_t i;
    snprintf(sentence, sizeof sentence, "%s", name);
    for (i = 0; sentence[i]; i++)
        if (sentence[i] == '_') sentence[i] = ' ';
    currentTestFailed = 0;
    failureNotes[0] = 0;
    test();
    if (currentTestFailed) {
        testsFailed++;
        printf("  FAIL  %s\n%s", sentence, failureNotes);
    } else {
        testsPassed++;
        printf("  PASS  %s\n", sentence);
    }
    fflush(stdout);
}

#define run(test) runTest(#test, test)

static LaunchOptions parsed(const char *commandLine)
{
    LaunchOptions options;
    parseCommandLine(commandLine, &options);
    return options;
}

static Scene *emptyScene(void)
{
    Scene *s = (Scene *)calloc(1, sizeof(Scene));
    if (!s) {
        fprintf(stderr, "out of memory while building a test scene\n");
        exit(2);
    }
    s->vw = 1280;
    s->vh = 720;
    s->NY = GRID_NY;
    s->NZ = GRID_NZ;
    sceneGeometry(s);
    return s;
}

static Scene *sceneGrownFromSeed(unsigned seed)
{
    srand(seed);
    return sceneCreate(0, 0, 1280, 720);
}

static void occupy(Scene *s, int x, int y, int z)
{
    if (inBounds(s, x, y, z)) s->grid[x][y][z] = 1;
}

static void occupyNeighboursExcept(Scene *s, int x, int y, int z, int openDir)
{
    int d;
    for (d = 0; d < 6; d++)
        if (d != openDir) occupy(s, x + DV[d][0], y + DV[d][1], z + DV[d][2]);
}

static Pipe *pipeArrivingAt(Scene *s, int x, int y, int z, int dir)
{
    Pipe *p = &s->pipes[0];
    float from[3];
    int i;
    for (i = 0; i < NUM_PIPES; i++) s->pipes[i].alive = 0;
    memset(p, 0, sizeof *p);
    p->alive = 1;
    p->x = x; p->y = y; p->z = z;
    p->dir = dir;
    p->hasDir = 1;
    p->col[0] = 1.0f;
    occupy(s, x, y, z);
    occupy(s, x - DV[dir][0], y - DV[dir][1], z - DV[dir][2]);
    nodeWorld(s, x - DV[dir][0], y - DV[dir][1], z - DV[dir][2], from);
    p->lastCyl = addCyl(s, from, dir, CELL, p->col);
    return p;
}

static int sameNode(const float *a, const float *b)
{
    return fabs(a[0] - b[0]) < 1e-4 && fabs(a[1] - b[1]) < 1e-4 && fabs(a[2] - b[2]) < 1e-4;
}

typedef struct {
    int moves;
    int jumps;
    int movesIntoTakenNodes;
    int reversals;
    int stepsOutsideGrid;
} WalkReport;

static WalkReport walkPipesThroughSeveralScenes(unsigned seed, int scenesToRun)
{
    WalkReport report;
    Scene *s = sceneGrownFromSeed(seed);
    int sceneNumber, guard, i, d;
    memset(&report, 0, sizeof report);
    for (sceneNumber = 0; sceneNumber < scenesToRun; sceneNumber++) {
        for (guard = 0; guard < 5000 && !s->holding; guard++) {
            for (i = 0; i < NUM_PIPES && !s->holding; i++) {
                Pipe before = s->pipes[i];
                int wasFree[6];
                int deathsBefore = s->deaths;
                int dx, dy, dz, moved = -1;
                if (!before.alive) continue;
                for (d = 0; d < 6; d++) {
                    int nx = before.x + DV[d][0], ny = before.y + DV[d][1], nz = before.z + DV[d][2];
                    wasFree[d] = inBounds(s, nx, ny, nz) && !s->grid[nx][ny][nz];
                }
                stepPipe(s, &s->pipes[i]);
                if (s->deaths != deathsBefore) continue;
                report.moves++;
                dx = s->pipes[i].x - before.x;
                dy = s->pipes[i].y - before.y;
                dz = s->pipes[i].z - before.z;
                if (abs(dx) + abs(dy) + abs(dz) != 1) { report.jumps++; continue; }
                for (d = 0; d < 6; d++)
                    if (DV[d][0] == dx && DV[d][1] == dy && DV[d][2] == dz) moved = d;
                if (!wasFree[moved]) report.movesIntoTakenNodes++;
                if (before.hasDir && moved == (before.dir ^ 1)) report.reversals++;
                if (!inBounds(s, s->pipes[i].x, s->pipes[i].y, s->pipes[i].z)) report.stepsOutsideGrid++;
            }
        }
        sceneReset(s);
    }
    free(s);
    return report;
}

static void no_arguments_open_the_windowed_viewer(void)
{
    expectNumber("mode for an empty command line", MODE_WINDOWED, parsed("").mode);
    expectNumber("mode for a missing command line", MODE_WINDOWED, parsed(NULL).mode);
}

static void slash_s_starts_the_fullscreen_screensaver_in_any_spelling(void)
{
    expectNumber("mode for /s", MODE_FULLSCREEN, parsed("/s").mode);
    expectNumber("mode for /S", MODE_FULLSCREEN, parsed("/S").mode);
    expectNumber("mode for -s", MODE_FULLSCREEN, parsed("-s").mode);
    expectNumber("mode for leading spaces before /s", MODE_FULLSCREEN, parsed("   /s").mode);
}

static void slash_shot_is_never_mistaken_for_slash_s(void)
{
    expectNumber("mode for /shot", MODE_SHOT, parsed("/shot").mode);
    expectNumber("mode for /SHOT", MODE_SHOT, parsed("/SHOT x.bmp").mode);
}

static void slash_shot_reads_the_file_name_step_count_and_seed(void)
{
    LaunchOptions o = parsed("/shot out.bmp 140 7");
    expectText("file name", "out.bmp", o.shotFile);
    expectNumber("step count", 140, o.shotSteps);
    expect(o.hasShotSeed, "a seed to be recorded");
    expectNumber("seed", 7, o.shotSeed);
}

static void slash_shot_uses_sensible_defaults_when_details_are_missing(void)
{
    LaunchOptions bare = parsed("/shot");
    LaunchOptions fileOnly = parsed("/shot picture.bmp");
    LaunchOptions badSteps = parsed("/shot picture.bmp nonsense");
    expectText("default file name", "pipes_shot.bmp", bare.shotFile);
    expectNumber("default step count", DEFAULT_SHOT_STEPS, bare.shotSteps);
    expect(!bare.hasShotSeed, "no seed when none is given");
    expectText("file name when only a file is given", "picture.bmp", fileOnly.shotFile);
    expectNumber("step count when only a file is given", DEFAULT_SHOT_STEPS, fileOnly.shotSteps);
    expectNumber("step count when the steps are not a number", DEFAULT_SHOT_STEPS, badSteps.shotSteps);
}

static void unknown_or_run_together_switches_open_the_windowed_viewer(void)
{
    expectNumber("mode for /settings", MODE_WINDOWED, parsed("/settings").mode);
    expectNumber("mode for /shotout.bmp", MODE_WINDOWED, parsed("/shotout.bmp").mode);
    expectNumber("mode for /x", MODE_WINDOWED, parsed("/x").mode);
}

static void slash_c_asks_for_the_configuration_screen(void)
{
    expectNumber("mode for /c", MODE_CONFIGURE, parsed("/c").mode);
    expectNumber("mode for /c:1234", MODE_CONFIGURE, parsed("/c:1234").mode);
}

static void slash_p_asks_for_the_preview_pane(void)
{
    expectNumber("mode for /p 1234", MODE_PREVIEW, parsed("/p 1234").mode);
}

static void a_new_scene_starts_every_pipe_on_its_own_free_node_with_a_cap(void)
{
    Scene *s = sceneGrownFromSeed(1);
    int i, j;
    expectNumber("pieces in a brand new scene", NUM_PIPES, s->nPrims);
    for (i = 0; i < NUM_PIPES; i++) {
        float w[3];
        const Pipe *p = &s->pipes[i];
        expect(p->alive, "every pipe to be growing");
        expect(s->grid[p->x][p->y][p->z], "every pipe's node to be marked as taken");
        nodeWorld(s, p->x, p->y, p->z, w);
        expect(s->prims[i].type == PRIM_SPH && s->prims[i].r == CAP_R, "each pipe to begin with a start cap");
        expect(sameNode(s->prims[i].a, w), "each start cap to sit on its pipe's node");
        for (j = 0; j < i; j++)
            expect(p->x != s->pipes[j].x || p->y != s->pipes[j].y || p->z != s->pipes[j].z,
                   "no two pipes to start on the same node");
    }
    free(s);
}

static void a_pipe_only_ever_moves_one_node_at_a_time(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(11, 6);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("moves longer than one node", 0, r.jumps);
}

static void a_pipe_never_moves_into_a_node_that_is_already_taken(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(12, 6);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("moves into taken nodes", 0, r.movesIntoTakenNodes);
}

static void a_pipe_never_turns_straight_back_on_itself(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(13, 6);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("reversals", 0, r.reversals);
}

static void pipes_never_leave_the_grid(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(14, 6);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("steps outside the grid", 0, r.stepsOutsideGrid);
}

static void going_straight_lengthens_the_current_pipe_instead_of_adding_a_piece(void)
{
    Scene *s = emptyScene();
    Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0);
    int piecesBefore;
    occupyNeighboursExcept(s, 5, 5, 5, 0);
    piecesBefore = s->nPrims;
    stepPipe(s, p);
    expectNumber("pieces after a straight step", piecesBefore, s->nPrims);
    expectNear("length of the straight pipe", 2.0 * CELL, s->prims[p->lastCyl].len);
    expectNumber("pipe x after moving along +x", 6, p->x);
    free(s);
}

typedef enum { TURN_MADE_A_BALL, TURN_MADE_AN_ELBOW, TURN_MADE_SOMETHING_ELSE } TurnResult;

static TurnResult turnUpwardsWithSeed(unsigned seed)
{
    Scene *s = emptyScene();
    Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0);
    int incoming = p->lastCyl;
    int first;
    float node[3], expectedStart[3];
    TurnResult result = TURN_MADE_SOMETHING_ELSE;
    occupyNeighboursExcept(s, 5, 5, 5, 2);
    nodeWorld(s, 5, 5, 5, node);
    first = s->nPrims;
    srand(seed);
    stepPipe(s, p);
    if (s->nPrims == first + 2 && s->prims[first].type == PRIM_SPH) {
        const Prim *ball = &s->prims[first], *next = &s->prims[first + 1];
        if (ball->r == BALL_R && sameNode(ball->a, node) && next->type == PRIM_CYL &&
            next->d1 == 2 && sameNode(next->a, node) && fabs(next->len - CELL) < 1e-4 &&
            fabs(s->prims[incoming].len - CELL) < 1e-4)
            result = TURN_MADE_A_BALL;
    } else if (s->nPrims == first + 2 && s->prims[first].type == PRIM_ELB) {
        const Prim *elbow = &s->prims[first], *next = &s->prims[first + 1];
        expectedStart[0] = node[0];
        expectedStart[1] = node[1] + ELBOW_R;
        expectedStart[2] = node[2];
        if (elbow->d1 == 0 && elbow->d2 == 2 && sameNode(elbow->a, node) &&
            next->type == PRIM_CYL && next->d1 == 2 && sameNode(next->a, expectedStart) &&
            fabs(next->len - (CELL - ELBOW_R)) < 1e-4 &&
            fabs(s->prims[incoming].len - (CELL - ELBOW_R)) < 1e-4)
            result = TURN_MADE_AN_ELBOW;
    }
    free(s);
    return result;
}

static void a_ball_joint_is_a_big_sphere_with_the_new_pipe_starting_at_its_centre(void)
{
    unsigned seed;
    int balls = 0, misshapen = 0;
    for (seed = 1; seed <= 200; seed++) {
        TurnResult r = turnUpwardsWithSeed(seed);
        if (r == TURN_MADE_A_BALL) balls++;
        if (r == TURN_MADE_SOMETHING_ELSE) misshapen++;
    }
    expect(balls > 0, "at least one turn to make a ball joint");
    expectNumber("turns that made something other than a ball or an elbow", 0, misshapen);
}

static void an_elbow_pulls_the_incoming_pipe_back_by_the_bend_radius(void)
{
    unsigned seed;
    int elbows = 0, misshapen = 0;
    for (seed = 1; seed <= 200; seed++) {
        TurnResult r = turnUpwardsWithSeed(seed);
        if (r == TURN_MADE_AN_ELBOW) elbows++;
        if (r == TURN_MADE_SOMETHING_ELSE) misshapen++;
    }
    expect(elbows > 0, "at least one turn to make an elbow");
    expectNumber("turns that made something other than a ball or an elbow", 0, misshapen);
}

static void about_one_turn_in_four_gets_a_ball_joint(void)
{
    unsigned seed;
    int balls = 0;
    const int turns = 2000;
    for (seed = 1; seed <= (unsigned)turns; seed++)
        if (turnUpwardsWithSeed(seed) == TURN_MADE_A_BALL) balls++;
    expectBetween("share of turns with a ball joint", 0.20, 0.30, (double)balls / turns);
}

static void a_pipe_with_nowhere_to_go_gets_an_end_cap_and_a_replacement(void)
{
    Scene *s = emptyScene();
    Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0);
    float node[3];
    int capIndex;
    occupyNeighboursExcept(s, 5, 5, 5, -1);
    capIndex = s->nPrims;
    nodeWorld(s, 5, 5, 5, node);
    srand(3);
    stepPipe(s, p);
    expect(s->prims[capIndex].type == PRIM_SPH && s->prims[capIndex].r == CAP_R,
           "the stuck pipe to get an end cap");
    expect(sameNode(s->prims[capIndex].a, node), "the end cap to sit where the pipe got stuck");
    expectNumber("finished pipes", 1, s->deaths);
    expect(p->alive, "a replacement pipe to start growing");
    expect(p->x != 5 || p->y != 5 || p->z != 5, "the replacement to start somewhere else");
    expect(s->prims[capIndex + 1].type == PRIM_SPH, "the replacement to begin with a start cap");
    free(s);
}

static void the_scene_pauses_after_twelve_finished_pipes_and_then_starts_over(void)
{
    Scene *s = sceneGrownFromSeed(5);
    int piecesWhenFinished;
    s->deaths = DEATHS_RESET - 1;
    killPipe(s, &s->pipes[0]);
    expect(s->holding, "the scene to hold once the twelfth pipe finishes");
    piecesWhenFinished = s->nPrims;
    sceneTick(s);
    expect(s->holding, "the scene to keep holding during the pause");
    expectNumber("pieces during the pause", piecesWhenFinished, s->nPrims);
    s->holdStart -= HOLD_MS;
    sceneTick(s);
    expect(!s->holding, "the scene to start over after the pause");
    expectNumber("finished pipes after starting over", 0, s->deaths);
    expectNumber("pieces after starting over", NUM_PIPES, s->nPrims);
    free(s);
}

static void a_full_picture_starts_over_instead_of_overflowing(void)
{
    Scene *s = sceneGrownFromSeed(6);
    float colour[3] = { 1, 1, 1 };
    s->nPrims = MAX_PRIMS;
    expectNumber("index given to a piece that does not fit", -1, addPrim(s, PRIM_SPH, colour));
    expect(s->holding, "the scene to hold and start over");
    free(s);
}

static void the_same_seed_always_grows_the_same_pipes(void)
{
    Scene *a = sceneGrownFromSeed(42);
    Scene *b;
    int i;
    for (i = 0; i < 300; i++) sceneTick(a);
    b = sceneGrownFromSeed(42);
    for (i = 0; i < 300; i++) sceneTick(b);
    expectNumber("pieces in the second scene", a->nPrims, b->nPrims);
    expect(memcmp(a->prims, b->prims, sizeof(Prim) * (size_t)a->nPrims) == 0,
           "every piece to match");
    free(a);
    free(b);
}

static void a_wider_screen_gets_more_columns_of_pipes(void)
{
    Scene *normal = sceneCreate(0, 0, 1280, 1024);
    Scene *wide = sceneCreate(0, 0, 2560, 1080);
    expect(wide->NX > normal->NX, "a 21:9 screen to have more columns than a 5:4 one");
    expectNumber("rows", GRID_NY, wide->NY);
    expectNumber("layers", GRID_NZ, wide->NZ);
    free(normal);
    free(wide);
}

static void the_number_of_columns_stays_within_limits(void)
{
    Scene *tall = sceneCreate(0, 0, 100, 1000);
    Scene *veryWide = sceneCreate(0, 0, 10000, 100);
    expectNumber("columns on a very tall screen", 8, tall->NX);
    expectNumber("columns on a very wide screen", MAXX, veryWide->NX);
    free(tall);
    free(veryWide);
}

static void resizing_the_window_keeps_the_pipes_already_drawn(void)
{
    Scene *s = sceneGrownFromSeed(8);
    int columns, pieces, i;
    for (i = 0; i < 50; i++) sceneTick(s);
    columns = s->NX;
    pieces = s->nPrims;
    s->vw = 800;
    s->vh = 800;
    sceneGeometry(s);
    expectNumber("columns after resizing", columns, s->NX);
    expectNumber("pieces after resizing", pieces, s->nPrims);
    expectNear("aspect ratio after resizing", 1.0, s->aspect);
    free(s);
}

static void enumerateMonitor(int left, int top, int right, int bottom)
{
    RECT r;
    r.left = left; r.top = top; r.right = right; r.bottom = bottom;
    monitorEnum(NULL, NULL, &r, 0);
}

static void useVirtualScreen(int x, int y, int height)
{
    destroyScenes();
    vsX = x;
    vsY = y;
    winH = height;
}

static void restoreScreenDefaults(void)
{
    destroyScenes();
    vsX = 0;
    vsY = 0;
    winW = WINDOWED_WIDTH;
    winH = WINDOWED_HEIGHT;
}

static void a_single_monitor_gets_one_scene_covering_all_of_it(void)
{
    useVirtualScreen(0, 0, 1080);
    enumerateMonitor(0, 0, 1920, 1080);
    expectNumber("scenes", 1, nScenes);
    expectNumber("viewport left", 0, scenes[0]->vx);
    expectNumber("viewport bottom", 0, scenes[0]->vy);
    expectNumber("viewport width", 1920, scenes[0]->vw);
    expectNumber("viewport height", 1080, scenes[0]->vh);
    restoreScreenDefaults();
}

static void a_monitor_left_of_the_main_one_lands_on_the_left_of_the_window(void)
{
    useVirtualScreen(-1280, 0, 1224);
    enumerateMonitor(0, 0, 1920, 1080);
    enumerateMonitor(-1280, 200, 0, 1224);
    expectNumber("scenes", 2, nScenes);
    expectNumber("main monitor viewport left", 1280, scenes[0]->vx);
    expectNumber("main monitor viewport bottom", 144, scenes[0]->vy);
    expectNumber("left monitor viewport left", 0, scenes[1]->vx);
    expectNumber("left monitor viewport bottom", 0, scenes[1]->vy);
    restoreScreenDefaults();
}

static void a_monitor_above_the_main_one_lands_at_the_top_of_the_window(void)
{
    useVirtualScreen(0, -1080, 2160);
    enumerateMonitor(0, 0, 1920, 1080);
    enumerateMonitor(0, -1080, 1920, 0);
    expectNumber("main monitor viewport bottom", 0, scenes[0]->vy);
    expectNumber("upper monitor viewport bottom", 1080, scenes[1]->vy);
    restoreScreenDefaults();
}

static void no_more_than_eight_monitors_get_a_scene(void)
{
    int i;
    BOOL keepGoing = TRUE;
    useVirtualScreen(0, 0, 100);
    for (i = 0; i < 10; i++) {
        RECT r;
        r.left = i * 100; r.top = 0; r.right = i * 100 + 100; r.bottom = 100;
        keepGoing = monitorEnum(NULL, NULL, &r, 0);
    }
    expectNumber("scenes", MAX_SCENES, nScenes);
    expect(!keepGoing, "monitor enumeration to stop once all scenes are used");
    restoreScreenDefaults();
}

static unsigned readU32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned readU16(const unsigned char *p) { return p[0] | p[1] << 8; }

typedef struct {
    int    loaded;
    long   fileSize;
    unsigned declaredSize, pixelOffset, headerSize, planes, bitsPerPixel;
    int    width, height;
    unsigned char *pixels;
} Bitmap;

static Bitmap loadBitmap(const char *path)
{
    Bitmap b;
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    memset(&b, 0, sizeof b);
    if (!f) return b;
    fseek(f, 0, SEEK_END);
    b.fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (b.fileSize < 54) { fclose(f); return b; }
    data = (unsigned char *)malloc((size_t)b.fileSize);
    if (fread(data, 1, (size_t)b.fileSize, f) != (size_t)b.fileSize) { fclose(f); free(data); return b; }
    fclose(f);
    if (data[0] != 'B' || data[1] != 'M') { free(data); return b; }
    b.loaded = 1;
    b.declaredSize = readU32(data + 2);
    b.pixelOffset = readU32(data + 10);
    b.headerSize = readU32(data + 14);
    b.width = (int)readU32(data + 18);
    b.height = (int)readU32(data + 22);
    b.planes = readU16(data + 26);
    b.bitsPerPixel = readU16(data + 28);
    b.pixels = data;
    return b;
}

static void freeBitmap(Bitmap *b)
{
    free(b->pixels);
    b->pixels = NULL;
}

static double shareOfLitPixels(const Bitmap *b)
{
    int stride = bmpRowStride(b->width), x, y;
    long lit = 0;
    for (y = 0; y < b->height; y++) {
        const unsigned char *row = b->pixels + b->pixelOffset + (size_t)y * stride;
        for (x = 0; x < b->width; x++)
            if (row[x * 3] > 16 || row[x * 3 + 1] > 16 || row[x * 3 + 2] > 16) lit++;
    }
    return (double)lit / ((double)b->width * b->height);
}

static int filesAreIdentical(const char *a, const char *b)
{
    Bitmap x = loadBitmap(a), y = loadBitmap(b);
    int same = x.loaded && y.loaded && x.fileSize == y.fileSize &&
               memcmp(x.pixels, y.pixels, (size_t)x.fileSize) == 0;
    freeBitmap(&x);
    freeBitmap(&y);
    return same;
}

static void bitmap_rows_are_padded_to_whole_groups_of_four_bytes(void)
{
    expectNumber("bytes per row for 1 pixel", 4, bmpRowStride(1));
    expectNumber("bytes per row for 2 pixels", 8, bmpRowStride(2));
    expectNumber("bytes per row for 4 pixels", 12, bmpRowStride(4));
    expectNumber("bytes per row for 333 pixels", 1000, bmpRowStride(333));
    expectNumber("bytes per row for 1280 pixels", 3840, bmpRowStride(1280));
}

static void drainQuitMessage(void)
{
    MSG m;
    while (PeekMessage(&m, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
}

static void removeOldOutputs(void)
{
    WIN32_FIND_DATAA found;
    char path[sizeof OUTPUT_DIR + MAX_PATH + 1];
    HANDLE h = FindFirstFileA(OUTPUT_DIR "\\*.bmp", &found);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        snprintf(path, sizeof path, OUTPUT_DIR "\\%s", found.cFileName);
        DeleteFileA(path);
    } while (FindNextFileA(h, &found));
    FindClose(h);
}

static void a_screenshot_of_an_odd_sized_window_is_a_valid_bitmap(void)
{
    const char *path = OUTPUT_DIR "\\odd-size.bmp";
    Bitmap b;
    int i;
    winW = 333;
    winH = 201;
    fullscreen = 0;
    srand(9);
    if (!createGLWindow(GetModuleHandle(NULL), MODE_SHOT)) {
        expect(0, "an OpenGL window to open");
        destroyGLWindow();
        drainQuitMessage();
        restoreScreenDefaults();
        return;
    }
    initGL();
    createScenes();
    for (i = 0; i < 140; i++) tickAll();
    drawAll();
    glFinish();
    expect(saveBMP(path, winW, winH), "the screenshot to be written");
    destroyGLWindow();
    drainQuitMessage();
    restoreScreenDefaults();
    b = loadBitmap(path);
    expect(b.loaded, "the screenshot to be a readable bitmap");
    expectNumber("width", 333, b.width);
    expectNumber("height", 201, b.height);
    expectNumber("bits per pixel", 24, b.bitsPerPixel);
    expectNumber("colour planes", 1, b.planes);
    expectNumber("info header size", 40, b.headerSize);
    expectNumber("pixel data offset", 54, b.pixelOffset);
    expectNumber("declared file size", 54 + 1000 * 201, b.declaredSize);
    expectNumber("actual file size", 54 + 1000 * 201, b.fileSize);
    freeBitmap(&b);
}

typedef struct {
    PROCESS_INFORMATION info;
    int started;
} Program;

static Program startPipes(const char *arguments)
{
    Program p;
    STARTUPINFOA si;
    char commandLine[1024];
    memset(&p, 0, sizeof p);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    snprintf(commandLine, sizeof commandLine, "\"%s\" %s", pipesExe, arguments);
    p.started = CreateProcessA(NULL, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &p.info) != 0;
    return p;
}

static int finishesWithin(Program *p, DWORD ms, DWORD *exitCode)
{
    DWORD code = STILL_ACTIVE;
    if (!p->started) return 0;
    if (WaitForSingleObject(p->info.hProcess, ms) != WAIT_OBJECT_0) return 0;
    GetExitCodeProcess(p->info.hProcess, &code);
    if (exitCode) *exitCode = code;
    return 1;
}

static int stillRunningAfter(Program *p, DWORD ms)
{
    return p->started && WaitForSingleObject(p->info.hProcess, ms) == WAIT_TIMEOUT;
}

static void stopPipes(Program *p)
{
    if (!p->started) return;
    if (WaitForSingleObject(p->info.hProcess, 0) == WAIT_TIMEOUT) {
        TerminateProcess(p->info.hProcess, 99);
        WaitForSingleObject(p->info.hProcess, 5000);
    }
    CloseHandle(p->info.hThread);
    CloseHandle(p->info.hProcess);
    p->started = 0;
}

typedef struct {
    DWORD processId;
    const char *className;
    HWND found;
} WindowSearch;

static BOOL CALLBACK matchProcessWindow(HWND h, LPARAM lp)
{
    WindowSearch *search = (WindowSearch *)lp;
    DWORD owner = 0;
    char className[128];
    GetWindowThreadProcessId(h, &owner);
    if (owner != search->processId || !IsWindowVisible(h)) return TRUE;
    GetClassNameA(h, className, sizeof className);
    if (strcmp(className, search->className) != 0) return TRUE;
    search->found = h;
    return FALSE;
}

static HWND waitForWindow(Program *p, const char *className)
{
    DWORD start = GetTickCount();
    WindowSearch search;
    if (!p->started) return NULL;
    search.processId = p->info.dwProcessId;
    search.className = className;
    while (GetTickCount() - start < WINDOW_TIMEOUT_MS) {
        search.found = NULL;
        EnumWindows(matchProcessWindow, (LPARAM)&search);
        if (search.found) return search.found;
        if (WaitForSingleObject(p->info.hProcess, 50) == WAIT_OBJECT_0) return NULL;
    }
    return NULL;
}

typedef struct {
    const char *text;
    int found;
} TextSearch;

static BOOL CALLBACK matchChildText(HWND h, LPARAM lp)
{
    TextSearch *search = (TextSearch *)lp;
    char text[256];
    GetWindowTextA(h, text, sizeof text);
    if (strcmp(text, search->text) == 0) { search->found = 1; return FALSE; }
    return TRUE;
}

static int windowShowsText(HWND h, const char *text)
{
    TextSearch search;
    search.text = text;
    search.found = 0;
    EnumChildWindows(h, matchChildText, (LPARAM)&search);
    return search.found;
}

static void clickDialogButton(HWND dialog, int buttonId)
{
    HWND button = GetDlgItem(dialog, buttonId);
    Sleep(DIALOG_SETTLE_MS);
    PostMessage(dialog, WM_COMMAND, MAKEWPARAM(buttonId, BN_CLICKED), (LPARAM)button);
}

static void pressKey(HWND h, WPARAM key)
{
    PostMessage(h, WM_KEYDOWN, key, 0);
}

static void runShotProgram(const char *arguments, DWORD *exitCode, int *finished)
{
    Program p = startPipes(arguments);
    *finished = finishesWithin(&p, PROGRAM_TIMEOUT_MS, exitCode);
    stopPipes(&p);
}

static void screenshot_mode_saves_a_1280_by_720_picture_full_of_pipes(void)
{
    DWORD code = 1;
    int finished;
    Bitmap b;
    runShotProgram("/shot " OUTPUT_DIR "\\shot-seed-7.bmp 140 7", &code, &finished);
    expect(finished, "the program to finish on its own");
    expectNumber("exit code", 0, code);
    b = loadBitmap(OUTPUT_DIR "\\shot-seed-7.bmp");
    expect(b.loaded, "a readable bitmap to be saved");
    if (!b.loaded) return;
    expectNumber("width", 1280, b.width);
    expectNumber("height", 720, b.height);
    expectNumber("bits per pixel", 24, b.bitsPerPixel);
    expectBetween("share of the picture covered by pipes", 0.10, 0.95, shareOfLitPixels(&b));
    freeBitmap(&b);
}

static void the_same_seed_always_draws_the_same_picture(void)
{
    DWORD code = 1;
    int finished;
    runShotProgram("/shot " OUTPUT_DIR "\\shot-seed-7-again.bmp 140 7", &code, &finished);
    expect(finished && code == 0, "the second run to succeed");
    expect(filesAreIdentical(OUTPUT_DIR "\\shot-seed-7.bmp", OUTPUT_DIR "\\shot-seed-7-again.bmp"),
           "both pictures to be byte for byte identical");
}

static void different_seeds_draw_different_pictures(void)
{
    DWORD code = 1;
    int finished;
    runShotProgram("/shot " OUTPUT_DIR "\\shot-seed-8.bmp 140 8", &code, &finished);
    expect(finished && code == 0, "the run with seed 8 to succeed");
    expect(!filesAreIdentical(OUTPUT_DIR "\\shot-seed-7.bmp", OUTPUT_DIR "\\shot-seed-8.bmp"),
           "the pictures to differ");
}

static void screenshot_mode_stops_early_once_the_scene_has_finished(void)
{
    DWORD code = 1;
    int finished;
    Bitmap b;
    runShotProgram("/shot " OUTPUT_DIR "\\shot-finished.bmp 100000000 3", &code, &finished);
    expect(finished, "a huge step count to finish quickly because the scene completes");
    expectNumber("exit code", 0, code);
    b = loadBitmap(OUTPUT_DIR "\\shot-finished.bmp");
    expect(b.loaded && shareOfLitPixels(&b) > 0.10, "the finished scene to be saved");
    freeBitmap(&b);
}

static void screenshot_mode_reports_failure_when_the_picture_cannot_be_saved(void)
{
    DWORD code = 0;
    int finished;
    runShotProgram("/shot " OUTPUT_DIR "\\no-such-folder\\shot.bmp 10 1", &code, &finished);
    expect(finished, "the program to finish on its own");
    expectNumber("exit code", 1, code);
}

static void the_windowed_viewer_opens_a_window_titled_3d_pipes(void)
{
    Program p = startPipes("");
    HWND w = waitForWindow(&p, WINDOW_CLASS);
    char title[64] = "";
    expect(w != NULL, "a pipes window to appear");
    if (w) {
        GetWindowTextA(w, title, sizeof title);
        expectText("window title", "3D Pipes", title);
        expect((GetWindowLong(w, GWL_STYLE) & WS_CAPTION) == WS_CAPTION, "an ordinary window with a title bar");
    }
    stopPipes(&p);
}

static void the_windowed_viewer_ignores_ordinary_keys_and_clicks_but_closes_on_escape(void)
{
    Program p = startPipes("");
    HWND w = waitForWindow(&p, WINDOW_CLASS);
    DWORD code = 1;
    expect(w != NULL, "a pipes window to appear");
    if (w) {
        pressKey(w, 'A');
        PostMessage(w, WM_LBUTTONDOWN, 0, 0);
        expect(stillRunningAfter(&p, 1000), "the viewer to keep running after a key and a click");
        pressKey(w, VK_ESCAPE);
        expect(finishesWithin(&p, 10000, &code), "Escape to close the viewer");
        expectNumber("exit code", 0, code);
    }
    stopPipes(&p);
}

static void the_screensaver_covers_every_monitor_and_stays_on_top(void)
{
    Program p = startPipes("/s");
    HWND w = waitForWindow(&p, WINDOW_CLASS);
    RECT r;
    expect(w != NULL, "a screensaver window to appear");
    if (w) {
        GetWindowRect(w, &r);
        expectNumber("left edge", GetSystemMetrics(SM_XVIRTUALSCREEN), r.left);
        expectNumber("top edge", GetSystemMetrics(SM_YVIRTUALSCREEN), r.top);
        expectNumber("width", GetSystemMetrics(SM_CXVIRTUALSCREEN), r.right - r.left);
        expectNumber("height", GetSystemMetrics(SM_CYVIRTUALSCREEN), r.bottom - r.top);
        expect(GetWindowLong(w, GWL_EXSTYLE) & WS_EX_TOPMOST, "the window to stay on top");
        expect(!(GetWindowLong(w, GWL_STYLE) & WS_CAPTION), "no title bar");
    }
    stopPipes(&p);
}

static void the_screensaver_closes_when_any_key_is_pressed(void)
{
    Program p = startPipes("/s");
    HWND w = waitForWindow(&p, WINDOW_CLASS);
    DWORD code = 1;
    expect(w != NULL, "a screensaver window to appear");
    if (w) {
        pressKey(w, 'A');
        expect(finishesWithin(&p, 10000, &code), "a key press to close the screensaver");
        expectNumber("exit code", 0, code);
    }
    stopPipes(&p);
}

static void the_screensaver_closes_when_a_mouse_button_is_pressed(void)
{
    Program p = startPipes("/s");
    HWND w = waitForWindow(&p, WINDOW_CLASS);
    expect(w != NULL, "a screensaver window to appear");
    if (w) {
        PostMessage(w, WM_RBUTTONDOWN, 0, 0);
        expect(finishesWithin(&p, 10000, NULL), "a mouse click to close the screensaver");
    }
    stopPipes(&p);
}

static void the_screensaver_ignores_a_tiny_mouse_twitch_but_closes_on_a_real_move(void)
{
    Program p;
    HWND w;
    SetCursorPos(200, 200);
    p = startPipes("/s");
    w = waitForWindow(&p, WINDOW_CLASS);
    expect(w != NULL, "a screensaver window to appear");
    if (w) {
        POINT where;
        Sleep(500);
        SetCursorPos(202, 203);
        GetCursorPos(&where);
        expect(where.x == 202 && where.y == 203,
               "the test machine to let the mouse cursor be moved (otherwise this test cannot run)");
        PostMessage(w, WM_MOUSEMOVE, 0, MAKELPARAM(202, 203));
        expect(stillRunningAfter(&p, 1000), "a move of a few pixels to be ignored");
        SetCursorPos(260, 260);
        PostMessage(w, WM_MOUSEMOVE, 0, MAKELPARAM(260, 260));
        expect(finishesWithin(&p, 10000, NULL), "a real mouse move to close the screensaver");
    }
    stopPipes(&p);
}

static void the_configure_option_explains_that_there_is_nothing_to_set_up(void)
{
    Program p = startPipes("/c");
    HWND box = waitForWindow(&p, "#32770");
    DWORD code = 1;
    expect(box != NULL, "a message box to appear");
    if (box) {
        expect(windowShowsText(box, "3D Pipes has no options to configure."), "the message to explain there are no options");
        clickDialogButton(box, IDOK);
        expect(finishesWithin(&p, 10000, &code), "pressing OK to end the program");
        expectNumber("exit code", 0, code);
    }
    stopPipes(&p);
}

static void the_preview_option_exits_straight_away(void)
{
    Program p = startPipes("/p 12345");
    DWORD code = 1;
    expect(finishesWithin(&p, 10000, &code), "the program to end by itself");
    expectNumber("exit code", 0, code);
    stopPipes(&p);
}

int main(int argc, char **argv)
{
    if (argc > 1) snprintf(pipesExe, sizeof pipesExe, "%s", argv[1]);
    CreateDirectoryA(OUTPUT_DIR, NULL);
    removeOldOutputs();
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("3D Pipes test suite (program under test: %s)\n", pipesExe);

    heading("Command line");
    run(no_arguments_open_the_windowed_viewer);
    run(slash_s_starts_the_fullscreen_screensaver_in_any_spelling);
    run(slash_shot_is_never_mistaken_for_slash_s);
    run(slash_shot_reads_the_file_name_step_count_and_seed);
    run(slash_shot_uses_sensible_defaults_when_details_are_missing);
    run(unknown_or_run_together_switches_open_the_windowed_viewer);
    run(slash_c_asks_for_the_configuration_screen);
    run(slash_p_asks_for_the_preview_pane);

    heading("Growing pipes");
    run(a_new_scene_starts_every_pipe_on_its_own_free_node_with_a_cap);
    run(a_pipe_only_ever_moves_one_node_at_a_time);
    run(a_pipe_never_moves_into_a_node_that_is_already_taken);
    run(a_pipe_never_turns_straight_back_on_itself);
    run(pipes_never_leave_the_grid);
    run(going_straight_lengthens_the_current_pipe_instead_of_adding_a_piece);
    run(the_same_seed_always_grows_the_same_pipes);

    heading("Joints");
    run(a_ball_joint_is_a_big_sphere_with_the_new_pipe_starting_at_its_centre);
    run(an_elbow_pulls_the_incoming_pipe_back_by_the_bend_radius);
    run(about_one_turn_in_four_gets_a_ball_joint);

    heading("Scene lifecycle");
    run(a_pipe_with_nowhere_to_go_gets_an_end_cap_and_a_replacement);
    run(the_scene_pauses_after_twelve_finished_pipes_and_then_starts_over);
    run(a_full_picture_starts_over_instead_of_overflowing);

    heading("Screen layout");
    run(a_wider_screen_gets_more_columns_of_pipes);
    run(the_number_of_columns_stays_within_limits);
    run(resizing_the_window_keeps_the_pipes_already_drawn);
    run(a_single_monitor_gets_one_scene_covering_all_of_it);
    run(a_monitor_left_of_the_main_one_lands_on_the_left_of_the_window);
    run(a_monitor_above_the_main_one_lands_at_the_top_of_the_window);
    run(no_more_than_eight_monitors_get_a_scene);

    heading("Screenshots");
    run(bitmap_rows_are_padded_to_whole_groups_of_four_bytes);
    run(a_screenshot_of_an_odd_sized_window_is_a_valid_bitmap);
    run(screenshot_mode_saves_a_1280_by_720_picture_full_of_pipes);
    run(the_same_seed_always_draws_the_same_picture);
    run(different_seeds_draw_different_pictures);
    run(screenshot_mode_stops_early_once_the_scene_has_finished);
    run(screenshot_mode_reports_failure_when_the_picture_cannot_be_saved);

    heading("Running the program");
    run(the_windowed_viewer_opens_a_window_titled_3d_pipes);
    run(the_windowed_viewer_ignores_ordinary_keys_and_clicks_but_closes_on_escape);
    run(the_screensaver_covers_every_monitor_and_stays_on_top);
    run(the_screensaver_closes_when_any_key_is_pressed);
    run(the_screensaver_closes_when_a_mouse_button_is_pressed);
    run(the_screensaver_ignores_a_tiny_mouse_twitch_but_closes_on_a_real_move);
    run(the_configure_option_explains_that_there_is_nothing_to_set_up);
    run(the_preview_option_exits_straight_away);

    printf("\n%d passed, %d failed\n", testsPassed, testsFailed);
    return testsFailed == 0 ? 0 : 1;
}
