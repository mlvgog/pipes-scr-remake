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

typedef struct {
    int multiple;
    int joints;
} SavedSettings;

static SavedSettings useSettings(int multiple, int joints)
{
    SavedSettings saved;
    saved.multiple = multiplePipes;
    saved.joints = jointStyleSetting;
    multiplePipes = multiple;
    jointStyleSetting = joints;
    return saved;
}

static void restoreSettings(SavedSettings saved)
{
    multiplePipes = saved.multiple;
    jointStyleSetting = saved.joints;
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
    s->lead = -1;
    s->jointStyle = JOINT_ELBOW;
    s->pipeBudget = NORMAL_PIPES_PER_SCENE;
    s->phase = SCENE_GROWING;
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

static void occupyEverything(Scene *s)
{
    memset(s->grid, 1, sizeof s->grid);
}

static Pipe *pipeArrivingAt(Scene *s, int x, int y, int z, int dir, int weight)
{
    Pipe *p = &s->pipes[s->nPipes++];
    float from[3];
    memset(p, 0, sizeof *p);
    p->state = PIPE_ACTIVE;
    p->x = x; p->y = y; p->z = z;
    p->dir = dir;
    p->weightStraight = weight;
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

static int isCap(const Scene *s, int i)
{
    return s->prims[i].type == PRIM_SPH && s->prims[i].r == CAP_R;
}

static int pipesDrawnInPicture(const Scene *s)
{
    int i, caps = 0, pipesThatGrew = 0;
    for (i = 0; i < s->nPrims; i++) {
        if (!isCap(s, i)) continue;
        caps++;
        if (i + 1 < s->nPrims && s->prims[i + 1].type == PRIM_CYL && sameNode(s->prims[i].a, s->prims[i + 1].a))
            pipesThatGrew++;
    }
    return caps - pipesThatGrew;
}

static int runSceneUntilItFinishes(Scene *s, int *mostPipesAtOnce)
{
    int ticks = 0;
    if (mostPipesAtOnce) *mostPipesAtOnce = s->nPipes;
    while (s->phase == SCENE_GROWING && ticks < 100000) {
        sceneTick(s);
        if (mostPipesAtOnce && s->nPipes > *mostPipesAtOnce) *mostPipesAtOnce = s->nPipes;
        ticks++;
    }
    return ticks;
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
    SavedSettings saved = useSettings(1, JOINT_MIXED);
    Scene *s = sceneGrownFromSeed(seed);
    int sceneNumber, guard, i, d;
    memset(&report, 0, sizeof report);
    for (sceneNumber = 0; sceneNumber < scenesToRun; sceneNumber++) {
        for (guard = 0; guard < 5000; guard++) {
            retireOrRestartStuckPipes(s);
            if (s->nPipes == 0) break;
            for (i = 0; i < s->nPipes; i++) {
                Pipe before = s->pipes[i];
                int wasFree[6];
                int dx, dy, dz, moved = -1;
                if (before.state != PIPE_ACTIVE) { growPipe(s, &s->pipes[i]); continue; }
                for (d = 0; d < 6; d++) wasFree[d] = isFreeToward(s, &before, d);
                growPipe(s, &s->pipes[i]);
                if (s->pipes[i].state != PIPE_ACTIVE) continue;
                report.moves++;
                dx = s->pipes[i].x - before.x;
                dy = s->pipes[i].y - before.y;
                dz = s->pipes[i].z - before.z;
                if (abs(dx) + abs(dy) + abs(dz) != 1) { report.jumps++; continue; }
                for (d = 0; d < 6; d++)
                    if (DV[d][0] == dx && DV[d][1] == dy && DV[d][2] == dz) moved = d;
                if (!wasFree[moved]) report.movesIntoTakenNodes++;
                if (moved == (before.dir ^ 1)) report.reversals++;
                if (!inBounds(s, s->pipes[i].x, s->pipes[i].y, s->pipes[i].z)) report.stepsOutsideGrid++;
            }
        }
        startScene(s, 1);
    }
    free(s);
    restoreSettings(saved);
    return report;
}

static void a_new_scene_starts_its_pipe_with_a_cap_and_a_first_stretch_of_pipe(void)
{
    SavedSettings saved = useSettings(0, JOINT_ELBOW);
    Scene *s = sceneGrownFromSeed(1);
    const Pipe *p = &s->pipes[0];
    float now[3];
    expectNumber("pipes growing in a new single-pipe scene", 1, s->nPipes);
    expectNumber("pieces in a new scene", 2, s->nPrims);
    expect(isCap(s, 0), "the first piece to be a start cap");
    expect(s->prims[1].type == PRIM_CYL && sameNode(s->prims[0].a, s->prims[1].a),
           "the first stretch of pipe to leave from the cap");
    nodeWorld(s, p->x, p->y, p->z, now);
    expect(fabs(fabs(now[0] - s->prims[0].a[0]) + fabs(now[1] - s->prims[0].a[1]) +
                fabs(now[2] - s->prims[0].a[2]) - CELL) < 1e-4,
           "the pipe to be one node away from its cap after starting");
    expect(s->grid[p->x][p->y][p->z], "the pipe's node to be marked as taken");
    free(s);
    restoreSettings(saved);
}

static void a_pipe_only_ever_moves_one_node_at_a_time(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(11, 8);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("moves longer than one node", 0, r.jumps);
}

static void a_pipe_never_moves_into_a_node_that_is_already_taken(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(12, 8);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("moves into taken nodes", 0, r.movesIntoTakenNodes);
}

static void a_pipe_never_turns_straight_back_on_itself(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(13, 8);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("reversals", 0, r.reversals);
}

static void pipes_never_leave_the_grid(void)
{
    WalkReport r = walkPipesThroughSeveralScenes(14, 8);
    expect(r.moves > 500, "plenty of moves to be checked");
    expectNumber("steps outside the grid", 0, r.stepsOutsideGrid);
}

static void going_straight_lengthens_the_current_pipe_instead_of_adding_a_piece(void)
{
    Scene *s = emptyScene();
    Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0, MAX_WEIGHT_STRAIGHT);
    int piecesBefore = s->nPrims;
    growPipe(s, p);
    expectNumber("pieces after a straight step", piecesBefore, s->nPrims);
    expectNear("length of the straight pipe", 2.0 * CELL, s->prims[p->lastCyl].len);
    expectNumber("pipe x after moving along +x", 6, p->x);
    free(s);
}

static void a_long_runner_always_goes_straight_while_the_way_ahead_is_free(void)
{
    unsigned seed;
    int turned = 0;
    for (seed = 1; seed <= 300; seed++) {
        Scene *s = emptyScene();
        Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0, MAX_WEIGHT_STRAIGHT);
        srand(seed);
        if (chooseDirection(s, p) != 0) turned++;
        free(s);
    }
    expectNumber("turns taken by a pipe with the top straight weight", 0, turned);
}

static void a_pipe_turns_whenever_the_way_ahead_is_blocked(void)
{
    unsigned seed;
    int wentStraight = 0, gotStuck = 0;
    for (seed = 1; seed <= 300; seed++) {
        Scene *s = emptyScene();
        Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0, MAX_WEIGHT_STRAIGHT);
        int d;
        occupy(s, 6, 5, 5);
        srand(seed);
        d = chooseDirection(s, p);
        if (d == 0) wentStraight++;
        if (d == NO_DIRECTION) gotStuck++;
        free(s);
    }
    expectNumber("times it went straight into the blocked node", 0, wentStraight);
    expectNumber("times it gave up although turns were free", 0, gotStuck);
}

static void a_short_runner_goes_straight_in_proportion_to_its_weight(void)
{
    unsigned seed;
    int straight = 0;
    const int trials = 6000;
    for (seed = 1; seed <= (unsigned)trials; seed++) {
        Scene *s = emptyScene();
        Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0, 2);
        srand(seed);
        if (chooseDirection(s, p) == 0) straight++;
        free(s);
    }
    expectBetween("share of straight steps with weight 2 against 4 free turns",
                  0.30, 0.37, (double)straight / trials);
}

static void about_one_pipe_in_twenty_is_a_long_runner(void)
{
    int i, longRunners = 0, lowestShort = 99, highestShort = 0, lowestLong = 999, highestLong = 0;
    const int pipes = 40000;
    srand(77);
    for (i = 0; i < pipes; i++) {
        Pipe p;
        preparePipe(&p);
        if (p.weightStraight >= MAX_WEIGHT_STRAIGHT / 4) {
            longRunners++;
            if (p.weightStraight < lowestLong) lowestLong = p.weightStraight;
            if (p.weightStraight > highestLong) highestLong = p.weightStraight;
        } else {
            if (p.weightStraight < lowestShort) lowestShort = p.weightStraight;
            if (p.weightStraight > highestShort) highestShort = p.weightStraight;
        }
    }
    expectBetween("share of long runners", 0.04, 0.06, (double)longRunners / pipes);
    expectNumber("smallest everyday straight weight", 1, lowestShort);
    expectNumber("largest everyday straight weight", 4, highestShort);
    expectNumber("smallest long-runner weight", MAX_WEIGHT_STRAIGHT / 4, lowestLong);
    expectNumber("largest long-runner weight", MAX_WEIGHT_STRAIGHT, highestLong);
}

static void the_same_seed_always_grows_the_same_pipes(void)
{
    SavedSettings saved = useSettings(1, JOINT_MIXED);
    Scene *a = sceneGrownFromSeed(42);
    Scene *b;
    int i;
    for (i = 0; i < 300; i++) sceneTick(a);
    b = sceneGrownFromSeed(42);
    for (i = 0; i < 300; i++) sceneTick(b);
    expectNumber("pieces in the second scene", a->nPrims, b->nPrims);
    expect(memcmp(a->prims, b->prims, sizeof(Prim) * (size_t)a->nPrims) == 0, "every piece to match");
    free(a);
    free(b);
    restoreSettings(saved);
}

typedef enum { TURN_MADE_A_BALL, TURN_MADE_AN_ELBOW, TURN_MADE_SOMETHING_ELSE } TurnResult;

static TurnResult turnUpwardsWithSeed(int jointStyle, unsigned seed)
{
    Scene *s = emptyScene();
    Pipe *p;
    int incoming, first;
    float node[3], expectedStart[3];
    TurnResult result = TURN_MADE_SOMETHING_ELSE;
    s->jointStyle = jointStyle;
    p = pipeArrivingAt(s, 5, 5, 5, 0, 1);
    incoming = p->lastCyl;
    occupyNeighboursExcept(s, 5, 5, 5, 2);
    nodeWorld(s, 5, 5, 5, node);
    first = s->nPrims;
    srand(seed);
    growPipe(s, p);
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

static int countTurns(int jointStyle, TurnResult wanted, int trials)
{
    int seed, count = 0;
    for (seed = 1; seed <= trials; seed++)
        if (turnUpwardsWithSeed(jointStyle, (unsigned)seed) == wanted) count++;
    return count;
}

static void with_elbow_joints_every_turn_is_an_elbow_that_pulls_the_incoming_pipe_back(void)
{
    expectNumber("well-formed elbows out of 200 turns", 200, countTurns(JOINT_ELBOW, TURN_MADE_AN_ELBOW, 200));
}

static void with_ball_joints_every_turn_is_a_big_sphere_with_the_new_pipe_leaving_its_centre(void)
{
    expectNumber("well-formed ball joints out of 200 turns", 200, countTurns(JOINT_BALL, TURN_MADE_A_BALL, 200));
}

static void with_mixed_joints_about_one_turn_in_three_is_a_ball(void)
{
    const int trials = 3000;
    int balls = countTurns(JOINT_MIXED, TURN_MADE_A_BALL, trials);
    int elbows = countTurns(JOINT_MIXED, TURN_MADE_AN_ELBOW, trials);
    expectNumber("turns that made something other than a ball or an elbow", trials, balls + elbows);
    expectBetween("share of turns with a ball joint", 0.30, 0.37, (double)balls / trials);
}

static void with_cycling_joints_each_scene_moves_on_from_elbows_to_balls_to_mixed(void)
{
    SavedSettings saved = useSettings(0, JOINT_CYCLE);
    Scene *s = sceneGrownFromSeed(3);
    expectNumber("joints in the first scene (0 elbow, 1 ball, 2 mixed)", JOINT_ELBOW, s->jointStyle);
    startScene(s, 1);
    expectNumber("joints in the second scene", JOINT_BALL, s->jointStyle);
    startScene(s, 1);
    expectNumber("joints in the third scene", JOINT_MIXED, s->jointStyle);
    startScene(s, 1);
    expectNumber("joints in the fourth scene", JOINT_ELBOW, s->jointStyle);
    free(s);
    restoreSettings(saved);
}

static void a_fixed_joint_style_stays_the_same_in_every_scene(void)
{
    SavedSettings saved = useSettings(0, JOINT_BALL);
    Scene *s = sceneGrownFromSeed(3);
    int i;
    for (i = 0; i < 4; i++) {
        expectNumber("joints in every scene (1 = ball)", JOINT_BALL, s->jointStyle);
        startScene(s, 1);
    }
    free(s);
    restoreSettings(saved);
}

static void single_pipe_mode_grows_one_pipe_at_a_time_and_five_pipes_per_scene(void)
{
    SavedSettings saved = useSettings(0, JOINT_ELBOW);
    unsigned seed;
    int mostAtOnce = 0, fivePipeScenes = 0;
    for (seed = 1; seed <= 20; seed++) {
        Scene *s = sceneGrownFromSeed(seed);
        int most;
        runSceneUntilItFinishes(s, &most);
        if (most > mostAtOnce) mostAtOnce = most;
        if (pipesDrawnInPicture(s) == 5) fivePipeScenes++;
        free(s);
    }
    expectNumber("most pipes growing at once", 1, mostAtOnce);
    expectNumber("scenes out of 20 that drew exactly five pipes", 20, fivePipeScenes);
    restoreSettings(saved);
}

static void multiple_pipe_mode_starts_two_to_four_pipes_together(void)
{
    SavedSettings saved = useSettings(1, JOINT_ELBOW);
    unsigned seed;
    int seen[MAX_PIPES_AT_ONCE + 1];
    int outside = 0;
    memset(seen, 0, sizeof seen);
    for (seed = 1; seed <= 300; seed++) {
        Scene *s = sceneGrownFromSeed(seed);
        if (s->nPipes < MIN_PIPES_AT_ONCE || s->nPipes > MAX_PIPES_AT_ONCE) outside++;
        else seen[s->nPipes]++;
        free(s);
    }
    expectNumber("scenes starting with fewer than two or more than four pipes", 0, outside);
    expect(seen[2] && seen[3] && seen[4], "scenes starting with two, three and four pipes all to occur");
    restoreSettings(saved);
}

static void multiple_pipe_mode_draws_seven_pipes_per_scene(void)
{
    SavedSettings saved = useSettings(1, JOINT_ELBOW);
    unsigned seed;
    int tooMany = 0, sevenPipeScenes = 0;
    for (seed = 1; seed <= 20; seed++) {
        Scene *s = sceneGrownFromSeed(seed);
        int started;
        runSceneUntilItFinishes(s, NULL);
        started = pipesDrawnInPicture(s);
        if (started > 7) tooMany++;
        if (started == 7) sevenPipeScenes++;
        free(s);
    }
    expectNumber("scenes with more than seven pipes", 0, tooMany);
    expectNumber("scenes out of 20 that drew exactly seven pipes", 20, sevenPipeScenes);
    restoreSettings(saved);
}

static void a_stuck_pipe_gets_an_end_cap_and_a_replacement_while_the_budget_lasts(void)
{
    Scene *s = emptyScene();
    Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0, 1);
    float node[3];
    int capIndex;
    s->pipesStarted = 1;
    occupyNeighboursExcept(s, 5, 5, 5, -1);
    capIndex = s->nPrims;
    nodeWorld(s, 5, 5, 5, node);
    srand(3);
    growPipe(s, p);
    expect(isCap(s, capIndex) && sameNode(s->prims[capIndex].a, node),
           "an end cap where the pipe got stuck");
    expectNumber("pipe state after getting stuck (1 = stuck)", PIPE_STUCK, p->state);
    retireOrRestartStuckPipes(s);
    expectNumber("pipes still in the scene", 1, s->nPipes);
    expectNumber("state of the replacement (0 = growing)", PIPE_ACTIVE, s->pipes[0].state);
    expect(isCap(s, capIndex + 1), "the replacement to begin with its own start cap");
    expectNumber("pipes started so far", 2, s->pipesStarted);
    free(s);
}

static void once_the_budget_is_spent_a_stuck_pipe_retires_and_the_scene_finishes(void)
{
    Scene *s = emptyScene();
    Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0, 1);
    s->pipesStarted = NORMAL_PIPES_PER_SCENE;
    p->state = PIPE_STUCK;
    retireOrRestartStuckPipes(s);
    expectNumber("pipes left", 0, s->nPipes);
    sceneTick(s);
    expectNumber("scene phase (1 = finished)", SCENE_FINISHED, s->phase);
    free(s);
}

static void a_pipe_that_cannot_find_a_free_node_ends_the_scene_early(void)
{
    Scene *s = emptyScene();
    Pipe *p = pipeArrivingAt(s, 5, 5, 5, 0, 1);
    s->pipesStarted = 1;
    p->state = PIPE_STUCK;
    occupyEverything(s);
    retireOrRestartStuckPipes(s);
    expectNumber("budget after failing to find a free node", 2, s->pipeBudget);
    expectNumber("pipe state (2 = out of nodes)", PIPE_OUT_OF_NODES, s->pipes[0].state);
    sceneTick(s);
    expectNumber("pipe state one step later (1 = stuck)", PIPE_STUCK, s->pipes[0].state);
    sceneTick(s);
    expectNumber("pipes left", 0, s->nPipes);
    expectNumber("scene phase (1 = finished)", SCENE_FINISHED, s->phase);
    free(s);
}

static void a_pipe_that_starts_boxed_in_is_finished_at_once(void)
{
    Scene *s = emptyScene();
    Pipe p;
    occupyEverything(s);
    s->grid[5][5][5] = 0;
    memset(&p, 0, sizeof p);
    p.weightStraight = 1;
    srand(1);
    expect(startPipe(s, &p), "the start itself to succeed");
    expectNumber("pipe state (1 = stuck)", PIPE_STUCK, p.state);
    expectNumber("pieces drawn for it", 1, s->nPrims);
    expectNumber("pipe x", 5, p.x);
    expectNumber("pipe y", 5, p.y);
    expectNumber("pipe z", 5, p.z);
    free(s);
}

static void about_one_multiple_pipe_scene_in_five_becomes_a_chase(void)
{
    SavedSettings saved = useSettings(1, JOINT_ELBOW);
    unsigned seed;
    int chases = 0;
    const int scenesTried = 1000;
    for (seed = 1; seed <= (unsigned)scenesTried; seed++) {
        Scene *s = sceneGrownFromSeed(seed);
        if (s->chase) chases++;
        free(s);
    }
    expectBetween("share of new scenes that are chases", 0.16, 0.24, (double)chases / scenesTried);
    restoreSettings(saved);
}

static void single_pipe_scenes_never_chase(void)
{
    SavedSettings saved = useSettings(0, JOINT_ELBOW);
    unsigned seed;
    int chases = 0;
    for (seed = 1; seed <= 300; seed++) {
        Scene *s = sceneGrownFromSeed(seed);
        if (s->chase) chases++;
        free(s);
    }
    expectNumber("chasing single-pipe scenes", 0, chases);
    restoreSettings(saved);
}

static void once_a_chase_has_started_every_later_scene_is_a_chase_like_the_original(void)
{
    SavedSettings saved = useSettings(1, JOINT_ELBOW);
    Scene *s = sceneGrownFromSeed(4);
    int i, followers = 0;
    s->chase = 1;
    startScene(s, 1);
    expect(s->chase, "the next scene to chase as well");
    expectNumber("lead pipe", 0, s->lead);
    for (i = 1; i < s->nPipes; i++) if (s->pipes[i].followsLead) followers++;
    expectNumber("pipes following the lead", s->nPipes - 1, followers);
    expect(s->pipes[0].startsFarAway, "the lead to restart far away from where it gets stuck");
    free(s);
    restoreSettings(saved);
}

static void in_a_chase_the_other_pipes_head_towards_the_lead(void)
{
    unsigned seed;
    int towards = 0;
    for (seed = 1; seed <= 200; seed++) {
        Scene *s = emptyScene();
        Pipe *lead = pipeArrivingAt(s, 10, 5, 5, 2, 1);
        Pipe *follower = pipeArrivingAt(s, 4, 5, 5, 2, 1);
        s->lead = 0;
        follower->followsLead = 1;
        (void)lead;
        srand(seed);
        if (chooseDirection(s, follower) == 0) towards++;
        free(s);
    }
    expectNumber("steps out of 200 that moved towards the lead", 200, towards);
}

static void a_chasing_pipe_that_cannot_head_for_the_lead_takes_any_free_way(void)
{
    Scene *s = emptyScene();
    Pipe *follower;
    pipeArrivingAt(s, 10, 5, 5, 2, 1);
    follower = pipeArrivingAt(s, 4, 5, 5, 2, 1);
    s->lead = 0;
    follower->followsLead = 1;
    occupyNeighboursExcept(s, 4, 5, 5, 5);
    srand(1);
    expectNumber("direction taken (5 = -z, the only free way)", 5, chooseDirection(s, follower));
    free(s);
}

static void the_far_node_search_finds_exactly_the_free_nodes_at_each_distance(void)
{
    Scene *s = emptyScene();
    int centre[3] = { 3, 3, 3 }, pos[3], r, x, y, z, wrong = 0;
    for (r = 0; r <= 3; r++) {
        int expected = 0;
        for (x = 0; x < s->NX; x++)
            for (y = 0; y < s->NY; y++)
                for (z = 0; z < s->NZ; z++) {
                    int d = abs(x - 3);
                    if (abs(y - 3) > d) d = abs(y - 3);
                    if (abs(z - 3) > d) d = abs(z - 3);
                    if (d == r) expected++;
                }
        if (freeNodesAtDistance(s, centre, r, -1, pos) != expected) wrong++;
    }
    expectNumber("distances where the count of free nodes was wrong", 0, wrong);
    free(s);
}

static void the_lead_restarts_in_the_far_corner_from_where_it_got_stuck(void)
{
    Scene *s = emptyScene();
    Pipe stuckNearOrigin;
    int pos[3];
    memset(&stuckNearOrigin, 0, sizeof stuckNearOrigin);
    stuckNearOrigin.x = 1; stuckNearOrigin.y = 1; stuckNearOrigin.z = 1;
    srand(1);
    expect(findFreeNodeFarFrom(s, &stuckNearOrigin, pos), "a far node to be found");
    expectNumber("far x", s->NX - 1, pos[0]);
    expectNumber("far y", s->NY - 1, pos[1]);
    expectNumber("far z", s->NZ - 1, pos[2]);
    occupy(s, s->NX - 1, s->NY - 1, s->NZ - 1);
    expect(findFreeNodeFarFrom(s, &stuckNearOrigin, pos), "a node next to the taken corner to be found");
    expect(abs(pos[0] - (s->NX - 1)) <= 1 && abs(pos[1] - (s->NY - 1)) <= 1 && abs(pos[2] - (s->NZ - 1)) <= 1,
           "the node to be right next to the far corner");
    free(s);
}

static void when_the_lead_retires_another_pipe_takes_over_the_lead(void)
{
    Scene *s = emptyScene();
    Pipe *lead, *follower;
    lead = pipeArrivingAt(s, 2, 2, 2, 0, 1);
    follower = pipeArrivingAt(s, 8, 8, 8, 0, 1);
    s->chase = 1;
    s->lead = 0;
    follower->followsLead = 1;
    s->pipesStarted = s->pipeBudget;
    lead->state = PIPE_STUCK;
    srand(1);
    retireOrRestartStuckPipes(s);
    expectNumber("pipes left", 1, s->nPipes);
    expectNumber("new lead", 0, s->lead);
    expect(!s->pipes[0].followsLead, "the new lead to stop following");
    expect(s->pipes[0].startsFarAway, "the new lead to restart far away");
    free(s);
}

static void a_full_picture_finishes_the_scene_instead_of_overflowing(void)
{
    Scene *s = sceneGrownFromSeed(6);
    float colour[3] = { 1, 1, 1 };
    s->nPrims = MAX_PRIMS;
    expectNumber("index given to a piece that does not fit", -1, addPrim(s, PRIM_SPH, colour));
    expectNumber("scene phase (1 = finished)", SCENE_FINISHED, s->phase);
    free(s);
}

static void a_finished_picture_stays_for_one_step_before_it_starts_to_dissolve(void)
{
    SavedSettings saved = useSettings(0, JOINT_ELBOW);
    Scene *s = sceneGrownFromSeed(5);
    int pieces;
    runSceneUntilItFinishes(s, NULL);
    pieces = s->nPrims;
    expectNumber("phase when the last pipe retires (1 = finished)", SCENE_FINISHED, s->phase);
    sceneTick(s);
    expectNumber("phase one step later (2 = dissolving)", SCENE_DISSOLVING, s->phase);
    expectNumber("pieces still shown while dissolving", pieces, s->nPrims);
    expectNumber("squares eaten at the very start", 0, squaresEaten(s, s->dissolveStart));
    free(s);
    restoreSettings(saved);
}

static void the_dissolve_eats_every_square_exactly_once(void)
{
    Scene *s = emptyScene();
    int *timesEaten, i, wrong = 0;
    srand(8);
    planDissolve(s);
    timesEaten = (int *)calloc((size_t)s->squareCount, sizeof(int));
    for (i = 0; i < s->squareCount; i++) timesEaten[s->dissolveOrder[i]]++;
    for (i = 0; i < s->squareCount; i++) if (timesEaten[i] != 1) wrong++;
    expectNumber("squares eaten other than exactly once", 0, wrong);
    expect(s->dissolveOrder[0] != 0 || s->dissolveOrder[1] != 1, "the order to be shuffled");
    free(timesEaten);
    free(s);
}

static void dissolve_squares_are_a_thirty_second_of_the_shorter_side(void)
{
    Scene *s = emptyScene();
    planDissolve(s);
    expectNumber("square size on a 1280x720 screen", 22, s->squareSize);
    expectNumber("squares across a 1280x720 screen", 59, s->squaresAcross);
    expectNumber("squares on a 1280x720 screen", 59 * 33, s->squareCount);
    s->vw = 1920; s->vh = 1080;
    planDissolve(s);
    expectNumber("square size on a 1920x1080 screen", 33, s->squareSize);
    s->vw = 20; s->vh = 10;
    planDissolve(s);
    expectNumber("square size on a tiny screen", 1, s->squareSize);
    s->vw = 30000; s->vh = 40;
    planDissolve(s);
    expect(s->squareCount <= MAX_DISSOLVE_SQUARES, "a very wide screen to use bigger squares rather than overflow");
    free(s);
}

static void the_dissolve_takes_about_one_point_seven_seconds(void)
{
    Scene *s = emptyScene();
    planDissolve(s);
    s->phase = SCENE_DISSOLVING;
    s->dissolveStart = 100000;
    expectNumber("squares eaten at the start", 0, squaresEaten(s, 100000));
    expectBetween("squares eaten half way", s->squareCount / 2 - 1, s->squareCount / 2 + 1,
                  squaresEaten(s, 100000 + DISSOLVE_MS / 2));
    expectNumber("squares eaten at the end", s->squareCount, squaresEaten(s, 100000 + DISSOLVE_MS));
    free(s);
}

static void after_the_dissolve_a_fresh_scene_begins(void)
{
    SavedSettings saved = useSettings(0, JOINT_ELBOW);
    Scene *s = sceneGrownFromSeed(5);
    runSceneUntilItFinishes(s, NULL);
    sceneTick(s);
    sceneTick(s);
    expectNumber("phase before the dissolve time is up (2 = dissolving)", SCENE_DISSOLVING, s->phase);
    s->dissolveStart -= DISSOLVE_MS;
    sceneTick(s);
    expectNumber("phase after the dissolve (0 = growing)", SCENE_GROWING, s->phase);
    expectNumber("pieces in the fresh scene", 2, s->nPrims);
    free(s);
    restoreSettings(saved);
}

static void each_new_scene_is_turned_a_little_further_than_the_last(void)
{
    Scene *s = sceneGrownFromSeed(2);
    expectNear("turn of the first scene", 0.0, s->rotation);
    startScene(s, 1);
    expectNear("turn of the second scene (the original only starts turning from the third)", 0.0, s->rotation);
    startScene(s, 1);
    expectNear("turn of the third scene", SCENE_TURN_DEGREES, s->rotation);
    startScene(s, 1);
    expectNear("turn of the fourth scene", 2.0 * SCENE_TURN_DEGREES, s->rotation);
    s->nextRotation = 355.0f;
    startScene(s, 1);
    expectNear("turn just before passing a full circle", 355.0, s->rotation);
    startScene(s, 1);
    expectNear("turn after passing a full circle", 355.0 + SCENE_TURN_DEGREES - 360.0, s->rotation);
    free(s);
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

static int openTestPicture(int width, int height)
{
    winW = width;
    winH = height;
    fullscreen = 0;
    if (createGLWindow(GetModuleHandle(NULL), MODE_SHOT)) {
        initGL();
        return 1;
    }
    expect(0, "an OpenGL window to open");
    destroyGLWindow();
    drainQuitMessage();
    restoreScreenDefaults();
    return 0;
}

static void closeTestPicture(void)
{
    destroyGLWindow();
    drainQuitMessage();
    restoreScreenDefaults();
}

static double litShareOfFrame(int width, int height)
{
    unsigned char *pixels = (unsigned char *)malloc((size_t)width * height * 3);
    long lit = 0, i;
    if (!pixels) return -1.0;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    for (i = 0; i < (long)width * height; i++)
        if (pixels[i * 3] > 16 || pixels[i * 3 + 1] > 16 || pixels[i * 3 + 2] > 16) lit++;
    free(pixels);
    return (double)lit / ((double)width * height);
}

static double litShareWithSquaresEaten(Scene *s, int eaten)
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sceneDraw(s);
    drawDissolve(s, eaten);
    glFinish();
    return litShareOfFrame(winW, winH);
}

static void a_screenshot_of_an_odd_sized_window_is_a_valid_bitmap(void)
{
    const char *path = OUTPUT_DIR "\\odd-size.bmp";
    Bitmap b;
    int i;
    srand(9);
    if (!openTestPicture(333, 201)) return;
    createScenes();
    for (i = 0; i < 140; i++) tickAll();
    drawAll();
    glFinish();
    expect(saveBMP(path, winW, winH), "the screenshot to be written");
    closeTestPicture();
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

static void black_squares_eat_the_finished_picture_until_nothing_is_left(void)
{
    SavedSettings saved = useSettings(1, JOINT_MIXED);
    Scene *s;
    double before, halfway, after;
    srand(21);
    if (!openTestPicture(400, 300)) { restoreSettings(saved); return; }
    createScenes();
    s = scenes[0];
    runSceneUntilItFinishes(s, NULL);
    sceneTick(s);
    before = litShareWithSquaresEaten(s, 0);
    halfway = litShareWithSquaresEaten(s, s->squareCount / 2);
    after = litShareWithSquaresEaten(s, s->squareCount);
    closeTestPicture();
    expect(before > 0.05, "the finished picture to show plenty of pipes");
    if (before > 0.0)
        expectBetween("share of the picture left half way through", 0.35, 0.65, halfway / before);
    expectBetween("share of the picture left at the end", 0.0, 0.0, after);
    restoreSettings(saved);
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

typedef struct {
    const char *label;
    HWND found;
} ButtonSearch;

static BOOL CALLBACK matchButtonLabel(HWND h, LPARAM lp)
{
    ButtonSearch *search = (ButtonSearch *)lp;
    char className[32], text[64];
    GetClassNameA(h, className, sizeof className);
    GetWindowTextA(h, text, sizeof text);
    if (_stricmp(className, "Button") == 0 && strcmp(text, search->label) == 0) {
        search->found = h;
        return FALSE;
    }
    return TRUE;
}

static int clickButtonLabelled(HWND dialog, const char *label)
{
    ButtonSearch search;
    search.label = label;
    search.found = NULL;
    EnumChildWindows(dialog, matchButtonLabel, (LPARAM)&search);
    if (!search.found) return 0;
    Sleep(DIALOG_SETTLE_MS);
    PostMessage(dialog, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(search.found), BN_CLICKED), (LPARAM)search.found);
    return 1;
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
        expect(clickButtonLabelled(box, "OK"), "the message box to have an OK button");
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
    run(a_new_scene_starts_its_pipe_with_a_cap_and_a_first_stretch_of_pipe);
    run(a_pipe_only_ever_moves_one_node_at_a_time);
    run(a_pipe_never_moves_into_a_node_that_is_already_taken);
    run(a_pipe_never_turns_straight_back_on_itself);
    run(pipes_never_leave_the_grid);
    run(going_straight_lengthens_the_current_pipe_instead_of_adding_a_piece);
    run(a_long_runner_always_goes_straight_while_the_way_ahead_is_free);
    run(a_pipe_turns_whenever_the_way_ahead_is_blocked);
    run(a_short_runner_goes_straight_in_proportion_to_its_weight);
    run(about_one_pipe_in_twenty_is_a_long_runner);
    run(the_same_seed_always_grows_the_same_pipes);

    heading("Joints");
    run(with_elbow_joints_every_turn_is_an_elbow_that_pulls_the_incoming_pipe_back);
    run(with_ball_joints_every_turn_is_a_big_sphere_with_the_new_pipe_leaving_its_centre);
    run(with_mixed_joints_about_one_turn_in_three_is_a_ball);
    run(with_cycling_joints_each_scene_moves_on_from_elbows_to_balls_to_mixed);
    run(a_fixed_joint_style_stays_the_same_in_every_scene);

    heading("How many pipes");
    run(single_pipe_mode_grows_one_pipe_at_a_time_and_five_pipes_per_scene);
    run(multiple_pipe_mode_starts_two_to_four_pipes_together);
    run(multiple_pipe_mode_draws_seven_pipes_per_scene);
    run(a_stuck_pipe_gets_an_end_cap_and_a_replacement_while_the_budget_lasts);
    run(once_the_budget_is_spent_a_stuck_pipe_retires_and_the_scene_finishes);
    run(a_pipe_that_cannot_find_a_free_node_ends_the_scene_early);
    run(a_pipe_that_starts_boxed_in_is_finished_at_once);
    run(a_full_picture_finishes_the_scene_instead_of_overflowing);

    heading("Chase scenes");
    run(about_one_multiple_pipe_scene_in_five_becomes_a_chase);
    run(single_pipe_scenes_never_chase);
    run(once_a_chase_has_started_every_later_scene_is_a_chase_like_the_original);
    run(in_a_chase_the_other_pipes_head_towards_the_lead);
    run(a_chasing_pipe_that_cannot_head_for_the_lead_takes_any_free_way);
    run(the_far_node_search_finds_exactly_the_free_nodes_at_each_distance);
    run(the_lead_restarts_in_the_far_corner_from_where_it_got_stuck);
    run(when_the_lead_retires_another_pipe_takes_over_the_lead);

    heading("End of a scene");
    run(a_finished_picture_stays_for_one_step_before_it_starts_to_dissolve);
    run(the_dissolve_eats_every_square_exactly_once);
    run(dissolve_squares_are_a_thirty_second_of_the_shorter_side);
    run(the_dissolve_takes_about_one_point_seven_seconds);
    run(after_the_dissolve_a_fresh_scene_begins);
    run(each_new_scene_is_turned_a_little_further_than_the_last);
    run(black_squares_eat_the_finished_picture_until_nothing_is_left);

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
