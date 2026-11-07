#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CELL        7.0f
#define PIPE_R      1.0f
#define ELBOW_R     PIPE_R
#define SHORT_PIPE  (CELL - 2.0f * PIPE_R)

#define SLOWEST_STEP_MS  100.0
#define FASTEST_STEP_MS  11.0
#define MAX_SPEED        100
#define DEFAULT_SPEED    50
#define MAX_RESOLUTION   200

#define SETTINGS_KEY           "Software\\pipes-scr-remake"
#define SETTINGS_KEY_VARIABLE  "PIPES_SETTINGS_KEY"

#define NORMAL_PIPES_PER_SCENE  5
#define MULTIPLE_PIPES_FACTOR   1.5
#define MIN_PIPES_AT_ONCE       2
#define MAX_PIPES_AT_ONCE       4
#define MAX_WEIGHT_STRAIGHT     100
#define LONG_RUNNER_ONE_IN      20
#define BALL_JOINT_ONE_IN       3
#define CHASE_SCENE_ONE_IN      5
#define SCENE_TURN_DEGREES      9.73156f

#define DISSOLVE_MS                         1700
#define DISSOLVE_FRAME_MS                   16
#define DISSOLVE_SQUARES_ACROSS_SHORT_SIDE  32
#define MAX_DISSOLVE_SQUARES                16384

#define FOV_Y                  90.0
#define NEAR_PLANE             1.0
#define EYE_DISTANCE           75.0
#define NODES_ACROSS_LONG_SIDE 15
#define MAXX                   NODES_ACROSS_LONG_SIDE
#define MAXY                   NODES_ACROSS_LONG_SIDE
#define MAXZ                   NODES_ACROSS_LONG_SIDE

#define DEFAULT_SLICES 8
#define MIN_SLICES     4
#define MAX_SLICES     64

#define GLOBAL_AMBIENT  1.0f
#define LIGHT_AMBIENT   0.1f

#define MAX_PRIMS   (4 * MAXX * MAXY * MAXZ)
#define MAX_SCENES  8

#define DEFAULT_SHOT_STEPS 700
#define WINDOWED_WIDTH     1280
#define WINDOWED_HEIGHT    720
#define MOUSE_EXIT_SLOP    4

enum { PRIM_CYL, PRIM_SPH, PRIM_ELB };
enum { JOINT_ELBOW, JOINT_BALL, JOINT_MIXED, JOINT_CYCLE };
enum { PIPE_ACTIVE, PIPE_STUCK, PIPE_OUT_OF_NODES };
enum { SCENE_GROWING, SCENE_FINISHED, SCENE_DISSOLVING };
enum {
    EMERALD, JADE, OBSIDIAN, PEARL, RUBY, TURQUOISE, BRASS, BRONZE, CHROME,
    COPPER, GOLD, SILVER, BLACK_PLASTIC, CYAN_PLASTIC, GREEN_PLASTIC,
    RED_PLASTIC, WHITE_PLASTIC, YELLOW_PLASTIC, BLACK_RUBBER, CYAN_RUBBER,
    GREEN_RUBBER, RED_RUBBER, WHITE_RUBBER, YELLOW_RUBBER, NUM_TEA_MATERIALS
};

#define NO_DIRECTION (-1)

typedef struct {
    int   type;
    int   material;
    float a[3];
    int   d1, d2;
    float len;
    float r;
} Prim;

typedef struct {
    int   state;
    int   x, y, z;
    int   dir;
    int   weightStraight;
    int   followsLead;
    int   startsFarAway;
    int   retired;
    int   lastCyl;
    int   material;
} Pipe;

typedef struct {
    int    vx, vy, vw, vh;
    int    NX, NY, NZ;
    double aspect;
    unsigned char grid[MAXX][MAXY][MAXZ];
    Prim   prims[MAX_PRIMS];
    int    nPrims;
    Pipe   pipes[MAX_PIPES_AT_ONCE];
    int    nPipes;
    int    lead;
    int    pipesStarted;
    int    pipeBudget;
    int    pipesDrawn;
    int    chase;
    int    jointStyle;
    int    phase;
    float  rotation;
    float  nextRotation;
    ULONGLONG dissolveStart;
    int    squareSize;
    int    squaresAcross;
    int    squareCount;
    int    dissolveOrder[MAX_DISSOLVE_SQUARES];
    unsigned char squareCleared[MAX_DISSOLVE_SQUARES];
} Scene;

typedef enum {
    MODE_WINDOWED,
    MODE_FULLSCREEN,
    MODE_CONFIGURE,
    MODE_PREVIEW,
    MODE_SHOT
} LaunchMode;

typedef struct {
    LaunchMode mode;
    char       shotFile[MAX_PATH];
    int        shotSteps;
    int        hasShotSeed;
    unsigned   shotSeed;
    HWND       previewParent;
    HWND       ownerWindow;
} LaunchOptions;

static const int DV[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
};

static Scene *scenes[MAX_SCENES];
static int    nScenes;

static GLuint dlCyl, dlSph, dlElb;
static int    winW = WINDOWED_WIDTH, winH = WINDOWED_HEIGHT;
static int    vsX, vsY;
static int    fullscreen;
static int    previewing;
static HWND   previewParent;
static HWND   hwnd;
static HDC    hdc;
static HGLRC  hrc;
static POINT  mouseStart;
static int    haveMouseStart;

static int    multiplePipes = 0;
static int    jointStyleSetting = JOINT_ELBOW;
static int    slices = DEFAULT_SLICES;
static double stepMs = 33.0;

typedef struct {
    int multiplePipes;
    int jointStyle;
    int speed;
    int resolution;
} Settings;

static const char *const JOINT_NAMES[] = { "Elbow", "Ball", "Mixed", "Cycle" };

static int clampInt(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static Settings defaultSettings(void)
{
    Settings s;
    s.multiplePipes = 0;
    s.jointStyle = JOINT_ELBOW;
    s.speed = DEFAULT_SPEED;
    s.resolution = 0;
    return s;
}

static int slicesForResolution(int resolution)
{
    int level = (int)(clampInt(resolution, 0, MAX_RESOLUTION) / 100.0 * 4.0 / 2.0001);
    return (level + 2) * 4;
}

static double stepMsForSpeed(int speed)
{
    double position = clampInt(speed, 0, MAX_SPEED) / (double)MAX_SPEED;
    return SLOWEST_STEP_MS * pow(FASTEST_STEP_MS / SLOWEST_STEP_MS, position);
}

static void applySettings(const Settings *s)
{
    multiplePipes = s->multiplePipes != 0;
    jointStyleSetting = clampInt(s->jointStyle, JOINT_ELBOW, JOINT_CYCLE);
    slices = slicesForResolution(s->resolution);
    stepMs = stepMsForSpeed(s->speed);
}

static void settingsKeyPath(char *path, DWORD size)
{
    DWORD n = GetEnvironmentVariableA(SETTINGS_KEY_VARIABLE, path, size);
    if (n == 0 || n >= size) snprintf(path, size, "%s", SETTINGS_KEY);
}

static int readNumber(HKEY key, const char *name, int fallback)
{
    DWORD value = 0, size = sizeof value, type = 0;
    if (RegQueryValueExA(key, name, NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return fallback;
    return (int)value;
}

static Settings loadSettings(void)
{
    Settings s = defaultSettings();
    char path[256];
    HKEY key;
    int joint;
    settingsKeyPath(path, sizeof path);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return s;
    s.multiplePipes = readNumber(key, "MultiplePipes", s.multiplePipes) != 0;
    joint = readNumber(key, "JointType", s.jointStyle);
    s.jointStyle = (joint >= JOINT_ELBOW && joint <= JOINT_CYCLE) ? joint : JOINT_ELBOW;
    s.speed = clampInt(readNumber(key, "Speed", s.speed), 0, MAX_SPEED);
    s.resolution = clampInt(readNumber(key, "Resolution", s.resolution), 0, MAX_RESOLUTION);
    RegCloseKey(key);
    return s;
}

static int writeNumber(HKEY key, const char *name, int number)
{
    DWORD value = (DWORD)number;
    return RegSetValueExA(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof value) == ERROR_SUCCESS;
}

static int saveSettings(const Settings *s)
{
    char path[256];
    HKEY key;
    int ok;
    settingsKeyPath(path, sizeof path);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS)
        return 0;
    ok = writeNumber(key, "MultiplePipes", s->multiplePipes) &
         writeNumber(key, "JointType", s->jointStyle) &
         writeNumber(key, "Speed", s->speed) &
         writeNumber(key, "Resolution", s->resolution);
    RegCloseKey(key);
    return ok;
}

static const float TEA_MATERIALS[NUM_TEA_MATERIALS][10] = {
    { .0215f, .1745f, .0215f, .07568f, .61424f, .07568f, .633f, .727811f, .633f, .6f },
    { .135f, .2225f, .1575f, .54f, .89f, .63f, .316228f, .316228f, .316228f, .1f },
    { .05375f, .05f, .06625f, .18275f, .17f, .22525f, .332741f, .328634f, .346435f, .3f },
    { .25f, .20725f, .20725f, 1.0f, .829f, .829f, .296648f, .296648f, .296648f, .088f },
    { .1745f, .01175f, .01175f, .61424f, .04136f, .04136f, .727811f, .626959f, .626959f, .6f },
    { .1f, .18725f, .1745f, .396f, .74151f, .69102f, .297254f, .30829f, .306678f, .1f },
    { .329412f, .223529f, .027451f, .780392f, .568627f, .113725f, .992157f, .941176f, .807843f, .21794872f },
    { .2125f, .1275f, .054f, .714f, .4284f, .18144f, .393548f, .271906f, .166721f, .2f },
    { .25f, .25f, .25f, .4f, .4f, .4f, .774597f, .774597f, .774597f, .6f },
    { .19125f, .0735f, .0225f, .7038f, .27048f, .0828f, .256777f, .137622f, .086014f, .1f },
    { .24725f, .1995f, .0745f, .75164f, .60648f, .22648f, .628281f, .555802f, .366065f, .4f },
    { .19225f, .19225f, .19225f, .50754f, .50754f, .50754f, .508273f, .508273f, .508273f, .4f },
    { 0.0f, 0.0f, 0.0f, .01f, .01f, .01f, .5f, .5f, .5f, .25f },
    { 0.0f, .1f, .06f, 0.0f, .50980392f, .50980392f, .50196078f, .50196078f, .50196078f, .25f },
    { 0.0f, 0.0f, 0.0f, .1f, .35f, .1f, .45f, .55f, .45f, .25f },
    { .05f, 0.0f, 0.0f, .5f, 0.0f, 0.0f, .7f, .6f, .6f, .25f },
    { 0.0f, 0.0f, 0.0f, .55f, .55f, .55f, .7f, .7f, .7f, .25f },
    { 0.0f, 0.0f, 0.0f, .5f, .5f, 0.0f, .6f, .6f, .5f, .25f },
    { .02f, .02f, .02f, .01f, .01f, .01f, .4f, .4f, .4f, .078125f },
    { 0.0f, .05f, .05f, .4f, .5f, .5f, .04f, .7f, .7f, .078125f },
    { 0.0f, .05f, 0.0f, .4f, .5f, .4f, .04f, .7f, .04f, .078125f },
    { .05f, 0.0f, 0.0f, .5f, .4f, .4f, .7f, .04f, .04f, .078125f },
    { .05f, .05f, .05f, .5f, .5f, .5f, .7f, .7f, .7f, .078125f },
    { .05f, .05f, 0.0f, .5f, .5f, .4f, .7f, .7f, .04f, .078125f }
};

static const int PIPE_MATERIALS[] = {
    EMERALD, JADE, PEARL, RUBY, TURQUOISE, BRASS, BRONZE, COPPER, GOLD,
    SILVER, CYAN_PLASTIC, WHITE_PLASTIC, YELLOW_PLASTIC, CYAN_RUBBER,
    GREEN_RUBBER, WHITE_RUBBER
};

#define NUM_PIPE_MATERIALS ((int)(sizeof PIPE_MATERIALS / sizeof PIPE_MATERIALS[0]))

static void applyMaterial(int material)
{
    const float *m = TEA_MATERIALS[material];
    float ambient[4], diffuse[4], specular[4];
    int k;
    for (k = 0; k < 3; k++) {
        ambient[k] = m[k];
        diffuse[k] = m[3 + k];
        specular[k] = m[6 + k];
    }
    ambient[3] = diffuse[3] = specular[3] = 1.0f;
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, m[9] * 128.0f);
}

static float ballRadius(void)
{
    return (float)(sqrt(2.0) * PIPE_R / cos(M_PI / slices));
}

static void buildCylinder(void)
{
    int i;
    dlCyl = glGenLists(1);
    glNewList(dlCyl, GL_COMPILE);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= slices; i++) {
        float a = (float)(2.0 * M_PI * i / slices);
        float c = cosf(a), s = sinf(a);
        glNormal3f(c, s, 0.0f);
        glVertex3f(PIPE_R * c, PIPE_R * s, 1.0f);
        glVertex3f(PIPE_R * c, PIPE_R * s, 0.0f);
    }
    glEnd();
    glEndList();
}

static void sphereVertex(float latitude, float longitude)
{
    float x = cosf(latitude) * cosf(longitude);
    float y = cosf(latitude) * sinf(longitude);
    float z = sinf(latitude);
    glNormal3f(x, y, z);
    glVertex3f(x, y, z);
}

static void buildSphere(void)
{
    int i, j, stacks = slices;
    dlSph = glGenLists(1);
    glNewList(dlSph, GL_COMPILE);
    for (i = 0; i < stacks; i++) {
        float t0 = (float)(M_PI * (-0.5 + (double)i / stacks));
        float t1 = (float)(M_PI * (-0.5 + (double)(i + 1) / stacks));
        glBegin(GL_QUAD_STRIP);
        for (j = 0; j <= slices; j++) {
            float a = (float)(2.0 * M_PI * j / slices);
            sphereVertex(t1, a);
            sphereVertex(t0, a);
        }
        glEnd();
    }
    glEndList();
}

static void buildElbow(void)
{
    int i, j, stacks = slices / 2 < 2 ? 2 : slices / 2;
    dlElb = glGenLists(1);
    glNewList(dlElb, GL_COMPILE);
    for (i = 0; i < stacks; i++) {
        float t0 = (float)(M_PI * 0.5 * i / stacks);
        float t1 = (float)(M_PI * 0.5 * (i + 1) / stacks);
        glBegin(GL_QUAD_STRIP);
        for (j = 0; j <= slices; j++) {
            float p = (float)(2.0 * M_PI * j / slices);
            float cp = cosf(p), sp = sinf(p);
            float t, ct, st;
            int k;
            for (k = 0; k < 2; k++) {
                t = k ? t1 : t0;
                ct = cosf(t); st = sinf(t);
                glNormal3f(cp * ct, cp * st, sp);
                glVertex3f((ELBOW_R + PIPE_R * cp) * ct,
                           (ELBOW_R + PIPE_R * cp) * st,
                           PIPE_R * sp);
            }
        }
        glEnd();
    }
    glEndList();
}

static void nodeWorld(const Scene *s, int x, int y, int z, float *w)
{
    w[0] = (x - (s->NX - 1) * 0.5f) * CELL;
    w[1] = (y - (s->NY - 1) * 0.5f) * CELL;
    w[2] = (z - (s->NZ - 1) * 0.5f) * CELL;
}

static int randomBelow(int n)
{
    return rand() % n;
}

static int randomBetween(int low, int high)
{
    return low + rand() % (high - low + 1);
}

static void finishScene(Scene *s)
{
    if (s->phase == SCENE_GROWING) s->phase = SCENE_FINISHED;
}

static int addPrim(Scene *s, int type, int material)
{
    Prim *p;
    if (s->nPrims >= MAX_PRIMS) { finishScene(s); return -1; }
    p = &s->prims[s->nPrims];
    memset(p, 0, sizeof *p);
    p->type = type;
    p->material = material;
    return s->nPrims++;
}

static int addCyl(Scene *s, const float *start, int dir, float len, int material)
{
    int i = addPrim(s, PRIM_CYL, material);
    if (i < 0) return -1;
    memcpy(s->prims[i].a, start, 3 * sizeof(float));
    s->prims[i].d1 = dir;
    s->prims[i].len = len;
    return i;
}

static void addSph(Scene *s, const float *c, float r, int material)
{
    int i = addPrim(s, PRIM_SPH, material);
    if (i < 0) return;
    memcpy(s->prims[i].a, c, 3 * sizeof(float));
    s->prims[i].r = r;
}

static void addElb(Scene *s, int x, int y, int z, int d1, int d2, int material)
{
    int i = addPrim(s, PRIM_ELB, material);
    if (i < 0) return;
    nodeWorld(s, x, y, z, s->prims[i].a);
    s->prims[i].d1 = d1;
    s->prims[i].d2 = d2;
}

static int inBounds(const Scene *s, int x, int y, int z)
{
    return x >= 0 && x < s->NX && y >= 0 && y < s->NY && z >= 0 && z < s->NZ;
}

static int isFree(const Scene *s, int x, int y, int z)
{
    return inBounds(s, x, y, z) && !s->grid[x][y][z];
}

static int isFreeToward(const Scene *s, const Pipe *p, int d)
{
    return isFree(s, p->x + DV[d][0], p->y + DV[d][1], p->z + DV[d][2]);
}

static int chooseWeightedDirection(const Scene *s, const Pipe *p)
{
    int turns[6], nTurns = 0, weight = p->weightStraight, d, choice;
    if (!isFreeToward(s, p, p->dir)) weight = 0;
    else if (weight >= MAX_WEIGHT_STRAIGHT) return p->dir;
    for (d = 0; d < 6; d++)
        if (d != p->dir && isFreeToward(s, p, d)) turns[nTurns++] = d;
    if (weight + nTurns == 0) return NO_DIRECTION;
    choice = randomBelow(weight + nTurns);
    return choice < weight ? p->dir : turns[choice - weight];
}

static int chooseDirectionTowards(const Scene *s, const Pipe *p, const Pipe *target)
{
    int delta[3], options[6], nOptions = 0, d;
    delta[0] = target->x - p->x;
    delta[1] = target->y - p->y;
    delta[2] = target->z - p->z;
    if (!delta[0] && !delta[1] && !delta[2]) return chooseWeightedDirection(s, p);
    for (d = 0; d < 6; d++) {
        int along = delta[d / 2];
        int towards = (d % 2 == 0) ? along > 0 : along < 0;
        if (towards && isFreeToward(s, p, d)) options[nOptions++] = d;
    }
    if (nOptions == 0)
        for (d = 0; d < 6; d++)
            if (isFreeToward(s, p, d)) options[nOptions++] = d;
    if (nOptions == 0) return NO_DIRECTION;
    return options[randomBelow(nOptions)];
}

static int chooseDirection(Scene *s, Pipe *p)
{
    const Pipe *lead = s->lead >= 0 ? &s->pipes[s->lead] : NULL;
    int d;
    if (p->followsLead && lead && lead != p) d = chooseDirectionTowards(s, p, lead);
    else d = chooseWeightedDirection(s, p);
    if (d != NO_DIRECTION) s->grid[p->x + DV[d][0]][p->y + DV[d][1]][p->z + DV[d][2]] = 1;
    return d;
}

static int nextJointStyle(int style)
{
    if (style == JOINT_MIXED) return JOINT_ELBOW;
    if (style == JOINT_ELBOW) return JOINT_BALL;
    return JOINT_MIXED;
}

static int turnGetsBallJoint(const Scene *s)
{
    if (s->jointStyle == JOINT_ELBOW) return 0;
    if (s->jointStyle == JOINT_BALL) return 1;
    return randomBelow(BALL_JOINT_ONE_IN) == 0;
}

static int findRandomFreeNode(const Scene *s, int *pos)
{
    int tries, limit = 2 * s->NX * s->NY * s->NZ, x, y, z;
    for (tries = 0; tries < limit; tries++) {
        x = randomBelow(s->NX);
        y = randomBelow(s->NY);
        z = randomBelow(s->NZ);
        if (!s->grid[x][y][z]) { pos[0] = x; pos[1] = y; pos[2] = z; return 1; }
    }
    for (x = 0; x < s->NX; x++)
        for (y = 0; y < s->NY; y++)
            for (z = 0; z < s->NZ; z++)
                if (!s->grid[x][y][z]) { pos[0] = x; pos[1] = y; pos[2] = z; return 1; }
    return 0;
}

static int freeNodesAtDistance(const Scene *s, const int *centre, int r, int wanted, int *pos)
{
    int x, y, z, count = 0;
    int x0 = centre[0] - r < 0 ? 0 : centre[0] - r, x1 = centre[0] + r >= s->NX ? s->NX - 1 : centre[0] + r;
    int y0 = centre[1] - r < 0 ? 0 : centre[1] - r, y1 = centre[1] + r >= s->NY ? s->NY - 1 : centre[1] + r;
    for (x = x0; x <= x1; x++)
        for (y = y0; y <= y1; y++) {
            int onSide = abs(x - centre[0]) == r || abs(y - centre[1]) == r;
            int zStep = onSide ? 1 : 2 * r;
            for (z = centre[2] - r; z <= centre[2] + r; z += zStep > 0 ? zStep : 1) {
                if (!isFree(s, x, y, z)) continue;
                if (count == wanted) { pos[0] = x; pos[1] = y; pos[2] = z; }
                count++;
            }
        }
    return count;
}

static int findFreeNodeFarFrom(const Scene *s, const Pipe *p, int *pos)
{
    int corner[3], r, found, reach = (s->NX > s->NY ? s->NX : s->NY) / 3;
    corner[0] = p->x >= s->NX / 2 ? 0 : s->NX - 1;
    corner[1] = p->y >= s->NY / 2 ? 0 : s->NY - 1;
    corner[2] = p->z >= s->NZ / 2 ? 0 : s->NZ - 1;
    for (r = 0; r <= reach; r++) {
        found = freeNodesAtDistance(s, corner, r, -1, pos);
        if (found) {
            freeNodesAtDistance(s, corner, r, randomBelow(found), pos);
            return 1;
        }
    }
    return findRandomFreeNode(s, pos);
}

static void pointAlong(const float *from, int d, float distance, float *out)
{
    out[0] = from[0] + DV[d][0] * distance;
    out[1] = from[1] + DV[d][1] * distance;
    out[2] = from[2] + DV[d][2] * distance;
}

static void movePipe(Pipe *p, int d)
{
    p->dir = d;
    p->x += DV[d][0];
    p->y += DV[d][1];
    p->z += DV[d][2];
}

static int startPipe(Scene *s, Pipe *p)
{
    int pos[3], found, d;
    float w[3], start[3];
    found = p->startsFarAway ? findFreeNodeFarFrom(s, p, pos) : findRandomFreeNode(s, pos);
    if (!found) { p->state = PIPE_OUT_OF_NODES; return 0; }
    p->x = pos[0]; p->y = pos[1]; p->z = pos[2];
    s->grid[p->x][p->y][p->z] = 1;
    s->pipesDrawn++;
    p->lastCyl = -1;
    p->material = PIPE_MATERIALS[randomBelow(NUM_PIPE_MATERIALS)];
    p->dir = randomBelow(6);
    d = chooseDirection(s, p);
    nodeWorld(s, p->x, p->y, p->z, w);
    addSph(s, w, ballRadius(), p->material);
    if (d == NO_DIRECTION) { p->state = PIPE_STUCK; return 1; }
    p->state = PIPE_ACTIVE;
    pointAlong(w, d, PIPE_R, start);
    p->lastCyl = addCyl(s, start, d, SHORT_PIPE, p->material);
    movePipe(p, d);
    return 1;
}

static void growPipe(Scene *s, Pipe *p)
{
    int d;
    float w[3], start[3];

    if (p->state == PIPE_OUT_OF_NODES) { p->state = PIPE_STUCK; return; }
    if (p->state != PIPE_ACTIVE) return;

    d = chooseDirection(s, p);
    nodeWorld(s, p->x, p->y, p->z, w);
    if (d == NO_DIRECTION) {
        addSph(s, w, ballRadius(), p->material);
        p->state = PIPE_STUCK;
        return;
    }

    if (d == p->dir && p->lastCyl >= 0) {
        s->prims[p->lastCyl].len += CELL;
    } else if (d == p->dir) {
        pointAlong(w, d, -PIPE_R, start);
        p->lastCyl = addCyl(s, start, d, CELL, p->material);
    } else {
        if (turnGetsBallJoint(s)) addSph(s, w, ballRadius(), p->material);
        else addElb(s, p->x, p->y, p->z, p->dir, d, p->material);
        pointAlong(w, d, PIPE_R, start);
        p->lastCyl = addCyl(s, start, d, SHORT_PIPE, p->material);
    }
    movePipe(p, d);
}

static void preparePipe(Pipe *p)
{
    memset(p, 0, sizeof *p);
    p->lastCyl = -1;
    if (randomBelow(LONG_RUNNER_ONE_IN) == 0)
        p->weightStraight = randomBetween(MAX_WEIGHT_STRAIGHT / 4, MAX_WEIGHT_STRAIGHT);
    else
        p->weightStraight = randomBetween(1, 4);
}

static void startScene(Scene *s, int afterDissolve)
{
    int i, startingPipes = 1;

    memset(s->grid, 0, sizeof s->grid);
    s->nPrims = 0;
    s->nPipes = 0;
    s->lead = -1;
    s->phase = SCENE_GROWING;
    s->jointStyle = jointStyleSetting == JOINT_CYCLE ? nextJointStyle(s->jointStyle) : jointStyleSetting;

    s->pipeBudget = NORMAL_PIPES_PER_SCENE;
    if (multiplePipes) {
        s->pipeBudget = (int)(s->pipeBudget * MULTIPLE_PIPES_FACTOR);
        startingPipes = randomBetween(MIN_PIPES_AT_ONCE, MAX_PIPES_AT_ONCE);
        if (startingPipes > s->pipeBudget) startingPipes = s->pipeBudget;
        if (randomBelow(CHASE_SCENE_ONE_IN) == 0) s->chase = 1;
    }

    s->rotation = s->nextRotation;
    if (afterDissolve) {
        s->nextRotation += SCENE_TURN_DEGREES;
        if (s->nextRotation >= 360.0f) s->nextRotation -= 360.0f;
    }

    s->pipesStarted = 0;
    s->pipesDrawn = 0;
    for (i = 0; i < startingPipes; i++) {
        Pipe *p = &s->pipes[s->nPipes++];
        preparePipe(p);
        if (s->chase) {
            if (i == 0) s->lead = 0;
            else p->followsLead = 1;
        }
        startPipe(s, p);
        if (i == 0 && s->chase) p->startsFarAway = 1;
        s->pipesStarted++;
    }
}

static void retireOrRestartStuckPipes(Scene *s)
{
    int i, kept = 0, newLead = -1, leadRetired = 0;

    for (i = 0; i < s->nPipes; i++) {
        Pipe *p = &s->pipes[i];
        p->retired = 0;
        if (p->state != PIPE_STUCK) continue;
        s->pipesStarted++;
        if (s->pipesStarted > s->pipeBudget) {
            p->retired = 1;
            if (i == s->lead) leadRetired = 1;
        } else if (!startPipe(s, p)) {
            s->pipeBudget = s->pipesStarted;
        }
    }

    for (i = 0; i < s->nPipes; i++) {
        if (s->pipes[i].retired) continue;
        if (i == s->lead) newLead = kept;
        s->pipes[kept++] = s->pipes[i];
    }
    s->nPipes = kept;
    s->lead = newLead;

    if (leadRetired && s->nPipes > 0) {
        s->lead = randomBelow(s->nPipes);
        s->pipes[s->lead].startsFarAway = 1;
        s->pipes[s->lead].followsLead = 0;
    }
}

static void planDissolve(Scene *s)
{
    int shorter = s->vw < s->vh ? s->vw : s->vh;
    int size = shorter / DISSOLVE_SQUARES_ACROSS_SHORT_SIDE;
    int across, down, n, i;
    if (size < 1) size = 1;
    for (;;) {
        across = (s->vw + size - 1) / size;
        down = (s->vh + size - 1) / size;
        n = across * down;
        if (n <= MAX_DISSOLVE_SQUARES) break;
        size++;
    }
    if (n < 1) n = 1;
    s->squareSize = size;
    s->squaresAcross = across;
    s->squareCount = n;
    memset(s->squareCleared, 0, (size_t)n);
    for (i = 0; i < n; i++) {
        int k = randomBelow(n), up = k, down2 = k, searchUp = 0;
        while (s->squareCleared[k]) {
            if (searchUp) { if (up < n - 1) up++; k = up; }
            else          { if (down2 > 0) down2--; k = down2; }
            searchUp = !searchUp;
        }
        s->squareCleared[k] = 1;
        s->dissolveOrder[i] = k;
    }
}

static void beginDissolve(Scene *s)
{
    planDissolve(s);
    s->phase = SCENE_DISSOLVING;
    s->dissolveStart = GetTickCount64();
}

static int squaresEaten(const Scene *s, ULONGLONG now)
{
    ULONGLONG elapsed = now - s->dissolveStart;
    if (s->phase != SCENE_DISSOLVING) return 0;
    if (elapsed >= DISSOLVE_MS) return s->squareCount;
    return (int)((ULONGLONG)s->squareCount * elapsed / DISSOLVE_MS);
}

static void sceneTick(Scene *s)
{
    int i;
    if (s->phase == SCENE_DISSOLVING) {
        if (GetTickCount64() - s->dissolveStart >= DISSOLVE_MS) startScene(s, 1);
        return;
    }
    if (s->phase == SCENE_FINISHED) {
        beginDissolve(s);
        return;
    }
    retireOrRestartStuckPipes(s);
    if (s->nPipes == 0) {
        finishScene(s);
        return;
    }
    for (i = 0; i < s->nPipes && s->phase == SCENE_GROWING; i++)
        growPipe(s, &s->pipes[i]);
}

static void tickAll(void)
{
    int i;
    for (i = 0; i < nScenes; i++) sceneTick(scenes[i]);
}

static int anySceneDissolving(void)
{
    int i;
    for (i = 0; i < nScenes; i++) if (scenes[i]->phase == SCENE_DISSOLVING) return 1;
    return 0;
}

static DWORD restBeforeNextFrame(double msSinceLastStep, int dissolving)
{
    double rest = stepMs - msSinceLastStep;
    if (dissolving && rest > DISSOLVE_FRAME_MS) rest = DISSOLVE_FRAME_MS;
    return rest < 1.0 ? 0 : (DWORD)rest;
}

static int anySceneRunning(void)
{
    int i;
    for (i = 0; i < nScenes; i++) if (scenes[i]->phase == SCENE_GROWING) return 1;
    return 0;
}

static void gridForViewport(int width, int height, int *nx, int *ny, int *nz)
{
    double aspect = height > 0 ? (double)width / height : 1.0;
    if (width >= height) {
        *nx = NODES_ACROSS_LONG_SIDE;
        *ny = (int)(NODES_ACROSS_LONG_SIDE / aspect);
    } else {
        *ny = NODES_ACROSS_LONG_SIDE;
        *nx = (int)(NODES_ACROSS_LONG_SIDE * aspect);
    }
    if (*nx < 1) *nx = 1;
    if (*ny < 1) *ny = 1;
    *nz = NODES_ACROSS_LONG_SIDE;
}

static void sceneGeometry(Scene *s)
{
    s->aspect = (s->vh > 0) ? (double)s->vw / (double)s->vh : 1.0;
    gridForViewport(s->vw, s->vh, &s->NX, &s->NY, &s->NZ);
}

static double farPlane(const Scene *s)
{
    int longest = s->NX > s->NY ? s->NX : s->NY;
    double depth = (double)longest / NODES_ACROSS_LONG_SIDE * (NODES_ACROSS_LONG_SIDE + 1) * CELL;
    return EYE_DISTANCE + 2.0 * depth;
}

static Scene *sceneCreate(int vx, int vy, int vw, int vh)
{
    Scene *s = (Scene *)calloc(1, sizeof(Scene));
    if (!s) return NULL;
    s->vx = vx; s->vy = vy; s->vw = vw; s->vh = vh;
    sceneGeometry(s);
    s->jointStyle = JOINT_MIXED;
    startScene(s, 0);
    return s;
}

static void sceneResize(Scene *s, int vw, int vh)
{
    if (vw <= 0 || vh <= 0 || (vw == s->vw && vh == s->vh)) return;
    s->vw = vw;
    s->vh = vh;
    sceneGeometry(s);
    startScene(s, 0);
}

static BOOL CALLBACK monitorEnum(HMONITOR hm, HDC hdcMon, LPRECT rc, LPARAM lp)
{
    int x, yTop, w, h;
    (void)hm; (void)hdcMon; (void)lp;
    if (nScenes >= MAX_SCENES) return FALSE;
    x = rc->left - vsX;
    yTop = rc->top - vsY;
    w = rc->right - rc->left;
    h = rc->bottom - rc->top;
    scenes[nScenes] = sceneCreate(x, winH - (yTop + h), w, h);
    if (scenes[nScenes]) nScenes++;
    return TRUE;
}

static int createScenes(void)
{
    if (fullscreen)
        EnumDisplayMonitors(NULL, NULL, monitorEnum, 0);
    if (nScenes == 0) {
        scenes[0] = sceneCreate(0, 0, winW, winH);
        if (!scenes[0]) return 0;
        nScenes = 1;
    }
    return 1;
}

static void destroyScenes(void)
{
    int i;
    for (i = 0; i < nScenes; i++) {
        free(scenes[i]);
        scenes[i] = NULL;
    }
    nScenes = 0;
}

static void drawCylRot(int dir)
{
    switch (dir) {
    case 0: glRotatef( 90.0f, 0, 1, 0); break;
    case 1: glRotatef(-90.0f, 0, 1, 0); break;
    case 2: glRotatef(-90.0f, 1, 0, 0); break;
    case 3: glRotatef( 90.0f, 1, 0, 0); break;
    case 4: break;
    case 5: glRotatef(180.0f, 0, 1, 0); break;
    }
}

static void elbowMatrix(const Prim *p, float *m)
{
    const int *v1 = DV[p->d1], *v2 = DV[p->d2];
    float X[3], Y[3], Z[3], C[3];
    int k;
    for (k = 0; k < 3; k++) {
        Y[k] = (float)v1[k];
        X[k] = (float)-v2[k];
        C[k] = p->a[k] - ELBOW_R * v1[k] + ELBOW_R * v2[k];
    }
    Z[0] = X[1] * Y[2] - X[2] * Y[1];
    Z[1] = X[2] * Y[0] - X[0] * Y[2];
    Z[2] = X[0] * Y[1] - X[1] * Y[0];
    m[0] = X[0]; m[1] = X[1]; m[2]  = X[2]; m[3]  = 0;
    m[4] = Y[0]; m[5] = Y[1]; m[6]  = Y[2]; m[7]  = 0;
    m[8] = Z[0]; m[9] = Z[1]; m[10] = Z[2]; m[11] = 0;
    m[12] = C[0]; m[13] = C[1]; m[14] = C[2]; m[15] = 1;
}

static void sceneDraw(const Scene *s)
{
    double t = NEAR_PLANE * tan(FOV_Y * 0.5 * M_PI / 180.0);
    int i, material = -1;

    glViewport(s->vx, s->vy, s->vw, s->vh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-t * s->aspect, t * s->aspect, -t, t, NEAR_PLANE, farPlane(s));
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, (float)-EYE_DISTANCE);
    glRotatef(s->rotation, 0.0f, 1.0f, 0.0f);

    for (i = 0; i < s->nPrims; i++) {
        const Prim *p = &s->prims[i];
        if (p->material != material) {
            material = p->material;
            applyMaterial(material);
        }
        glPushMatrix();
        switch (p->type) {
        case PRIM_CYL:
            glTranslatef(p->a[0], p->a[1], p->a[2]);
            drawCylRot(p->d1);
            glScalef(1.0f, 1.0f, p->len);
            glCallList(dlCyl);
            break;
        case PRIM_SPH:
            glTranslatef(p->a[0], p->a[1], p->a[2]);
            glScalef(p->r, p->r, p->r);
            glCallList(dlSph);
            break;
        case PRIM_ELB: {
            float m[16];
            elbowMatrix(p, m);
            glMultMatrixf(m);
            glCallList(dlElb);
            break;
        }
        }
        glPopMatrix();
    }
}

static void drawDissolve(const Scene *s, int eaten)
{
    int i;
    if (eaten <= 0) return;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, s->vw, 0.0, s->vh, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3f(0.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
    for (i = 0; i < eaten; i++) {
        int k = s->dissolveOrder[i];
        int x0 = (k % s->squaresAcross) * s->squareSize;
        int y0 = (k / s->squaresAcross) * s->squareSize;
        int x1 = x0 + s->squareSize, y1 = y0 + s->squareSize;
        glVertex2i(x0, y0);
        glVertex2i(x1, y0);
        glVertex2i(x1, y1);
        glVertex2i(x0, y1);
    }
    glEnd();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

static void drawAll(void)
{
    int i;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (i = 0; i < nScenes; i++) {
        sceneDraw(scenes[i]);
        drawDissolve(scenes[i], squaresEaten(scenes[i], GetTickCount64()));
    }
}

static void initGL(void)
{
    const float lightPosition[4] = { 90.0f, 90.0f, 150.0f, 0.0f };
    const float lightAmbient[4]  = { LIGHT_AMBIENT, LIGHT_AMBIENT, LIGHT_AMBIENT, 1.0f };
    const float lightDiffuse[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float sceneAmbient[4]  = { GLOBAL_AMBIENT, GLOBAL_AMBIENT, GLOBAL_AMBIENT, 1.0f };

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, sceneAmbient);
    glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);

    buildCylinder();
    buildSphere();
    buildElbow();
}

static int bmpRowStride(int width)
{
    return (width * 3 + 3) & ~3;
}

static int saveBMP(const char *path, int w, int h)
{
    int stride = bmpRowStride(w);
    unsigned char *buf = (unsigned char *)malloc((size_t)stride * h);
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    FILE *f;
    int x, y;

    if (!buf) return 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf);
    for (y = 0; y < h; y++) {
        unsigned char *row = buf + (size_t)y * stride;
        for (x = 0; x < w; x++) {
            unsigned char t = row[x * 3];
            row[x * 3] = row[x * 3 + 2];
            row[x * 3 + 2] = t;
        }
    }
    memset(&bfh, 0, sizeof bfh);
    memset(&bih, 0, sizeof bih);
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof bfh + sizeof bih;
    bfh.bfSize = bfh.bfOffBits + stride * h;
    bih.biSize = sizeof bih;
    bih.biWidth = w;
    bih.biHeight = h;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    fwrite(&bfh, sizeof bfh, 1, f);
    fwrite(&bih, sizeof bih, 1, f);
    fwrite(buf, (size_t)stride * h, 1, f);
    fclose(f);
    free(buf);
    return 1;
}

static const char *skipBlanks(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *readWord(const char *s, char *word, size_t size)
{
    size_t n = 0;
    s = skipBlanks(s);
    while (*s && *s != ' ' && *s != '\t') {
        if (n + 1 < size) word[n++] = *s;
        s++;
    }
    word[n] = 0;
    return s;
}

static int switchIs(const char *arg, const char *name)
{
    size_t n = strlen(name);
    char next;
    if (_strnicmp(arg, name, n) != 0) return 0;
    next = arg[n];
    return next == 0 || next == ' ' || next == '\t' || next == ':';
}

static void parseShotArguments(const char *rest, LaunchOptions *out)
{
    char word[MAX_PATH];
    rest = readWord(rest, word, sizeof word);
    if (word[0]) snprintf(out->shotFile, sizeof out->shotFile, "%s", word);
    rest = readWord(rest, word, sizeof word);
    if (word[0]) {
        out->shotSteps = atoi(word);
        if (out->shotSteps <= 0) out->shotSteps = DEFAULT_SHOT_STEPS;
    }
    readWord(rest, word, sizeof word);
    if (word[0]) {
        out->shotSeed = (unsigned)strtoul(word, NULL, 10);
        out->hasShotSeed = 1;
    }
}

static HWND windowNumberAfter(const char *a)
{
    char word[32];
    readWord(*a == ':' ? a + 1 : a, word, sizeof word);
    return (HWND)(UINT_PTR)_strtoui64(word, NULL, 10);
}

static void parseCommandLine(const char *cmdLine, int runningAsScrFile, LaunchOptions *out)
{
    const char *a = skipBlanks(cmdLine ? cmdLine : "");
    memset(out, 0, sizeof *out);
    out->mode = MODE_WINDOWED;
    out->shotSteps = DEFAULT_SHOT_STEPS;
    strcpy(out->shotFile, "pipes_shot.bmp");

    if (switchIs(a, "/shot")) {
        out->mode = MODE_SHOT;
        parseShotArguments(a + 5, out);
    } else if (switchIs(a, "/s") || switchIs(a, "-s")) {
        out->mode = MODE_FULLSCREEN;
    } else if (switchIs(a, "/c")) {
        out->mode = MODE_CONFIGURE;
        out->ownerWindow = windowNumberAfter(a + 2);
    } else if (switchIs(a, "/p")) {
        out->mode = MODE_PREVIEW;
        out->previewParent = windowNumberAfter(a + 2);
    } else if (!*a && runningAsScrFile) {
        out->mode = MODE_CONFIGURE;
    }
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        winW = LOWORD(lp); winH = HIWORD(lp);
        if (!fullscreen && nScenes == 1) sceneResize(scenes[0], winW, winH);
        return 0;
    case WM_KEYDOWN:
        if (previewing) return 0;
        if (fullscreen || wp == VK_ESCAPE) DestroyWindow(h);
        return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        if (fullscreen) DestroyWindow(h);
        return 0;
    case WM_MOUSEMOVE:
        if (fullscreen) {
            POINT pt;
            GetCursorPos(&pt);
            if (!haveMouseStart) { mouseStart = pt; haveMouseStart = 1; }
            else if (abs(pt.x - mouseStart.x) > MOUSE_EXIT_SLOP ||
                     abs(pt.y - mouseStart.y) > MOUSE_EXIT_SLOP)
                DestroyWindow(h);
        }
        return 0;
    case WM_SETCURSOR:
        if (fullscreen) { SetCursor(NULL); return TRUE; }
        break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXT)(int);

static const char *const WINDOW_CLASS = "PipesWnd";
static const char *const WINDOW_TITLE = "3D Pipes";

static void registerWindowClass(HINSTANCE hInst)
{
    WNDCLASS wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClass(&wc);
}

static HWND createAppWindow(HINSTANCE hInst, LaunchMode mode)
{
    if (mode == MODE_FULLSCREEN) {
        vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        winW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        winH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return CreateWindowEx(WS_EX_TOPMOST, WINDOW_CLASS, WINDOW_TITLE,
                              WS_POPUP | WS_VISIBLE, vsX, vsY, winW, winH,
                              NULL, NULL, hInst, NULL);
    }
    if (mode == MODE_PREVIEW) {
        RECT parentArea;
        GetClientRect(previewParent, &parentArea);
        winW = parentArea.right;
        winH = parentArea.bottom;
        return CreateWindowEx(0, WINDOW_CLASS, WINDOW_TITLE,
                              WS_CHILD | WS_VISIBLE | WS_DISABLED, 0, 0, winW, winH,
                              previewParent, NULL, hInst, NULL);
    }
    if (mode == MODE_SHOT) {
        return CreateWindowEx(WS_EX_TOOLWINDOW, WINDOW_CLASS, WINDOW_TITLE,
                              WS_POPUP | WS_VISIBLE, 0, 0, winW, winH,
                              NULL, NULL, hInst, NULL);
    }
    {
        RECT r;
        r.left = 0; r.top = 0; r.right = winW; r.bottom = winH;
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        return CreateWindow(WINDOW_CLASS, WINDOW_TITLE,
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            NULL, NULL, hInst, NULL);
    }
}

static int attachGLContext(void)
{
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
    hdc = GetDC(hwnd);
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) return 0;
    hrc = wglCreateContext(hdc);
    if (!hrc || !wglMakeCurrent(hdc, hrc)) return 0;
    {
        PFNWGLSWAPINTERVALEXT swapInterval =
            (PFNWGLSWAPINTERVALEXT)(void *)wglGetProcAddress("wglSwapIntervalEXT");
        if (swapInterval) swapInterval(1);
    }
    return 1;
}

static int createGLWindow(HINSTANCE hInst, LaunchMode mode)
{
    RECT cr;
    registerWindowClass(hInst);
    hwnd = createAppWindow(hInst, mode);
    if (!hwnd) return 0;
    if (!attachGLContext()) return 0;
    GetClientRect(hwnd, &cr);
    winW = cr.right; winH = cr.bottom;
    return 1;
}

static void destroyGLWindow(void)
{
    wglMakeCurrent(NULL, NULL);
    if (hrc) wglDeleteContext(hrc);
    if (hwnd && IsWindow(hwnd)) {
        if (hdc) ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
    }
    hrc = NULL;
    hdc = NULL;
    hwnd = NULL;
}

static int runShot(const LaunchOptions *options)
{
    int i, saved;
    for (i = 0; i < options->shotSteps && anySceneRunning(); i++)
        tickAll();
    drawAll();
    glFinish();
    saved = saveBMP(options->shotFile, winW, winH);
    SwapBuffers(hdc);
    return saved ? 0 : 1;
}

static int runInteractive(void)
{
    LARGE_INTEGER freq, last, now;
    double acc = 0.0;
    MSG msg;

    GetCursorPos(&mouseStart);
    haveMouseStart = 1;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    for (;;) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        QueryPerformanceCounter(&now);
        acc += (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart;
        last = now;
        if (acc > 500.0) acc = 500.0;
        while (acc >= stepMs) {
            tickAll();
            acc -= stepMs;
        }

        drawAll();
        SwapBuffers(hdc);
        {
            DWORD rest = restBeforeNextFrame(acc, anySceneDissolving());
            if (rest) MsgWaitForMultipleObjects(0, NULL, FALSE, rest, QS_ALLINPUT);
        }
    }
}

enum { ID_SINGLE = 101, ID_MULTIPLE, ID_JOINT_TYPE, ID_SPEED, ID_RESOLUTION };

typedef struct {
    DWORD storage[1024];
    int   used;
    int   items;
} DialogTemplate;

static void templateWord(DialogTemplate *t, WORD w)
{
    ((WORD *)t->storage)[t->used++] = w;
}

static void templateLong(DialogTemplate *t, DWORD d)
{
    templateWord(t, LOWORD(d));
    templateWord(t, HIWORD(d));
}

static void templateText(DialogTemplate *t, const char *text)
{
    while (*text) templateWord(t, (WORD)(unsigned char)*text++);
    templateWord(t, 0);
}

static void templateAlign(DialogTemplate *t)
{
    if (t->used & 1) templateWord(t, 0);
}

static void templateBegin(DialogTemplate *t, const char *title, short width, short height)
{
    memset(t, 0, sizeof *t);
    templateLong(t, DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    templateLong(t, 0);
    templateWord(t, 0);
    templateWord(t, 0);
    templateWord(t, 0);
    templateWord(t, (WORD)width);
    templateWord(t, (WORD)height);
    templateWord(t, 0);
    templateWord(t, 0);
    templateText(t, title);
    templateWord(t, 8);
    templateText(t, "MS Shell Dlg");
}

static void templateControl(DialogTemplate *t, WORD controlClass, const char *text, WORD id,
                            DWORD style, short x, short y, short width, short height)
{
    templateAlign(t);
    templateLong(t, style | WS_CHILD | WS_VISIBLE);
    templateLong(t, 0);
    templateWord(t, (WORD)x);
    templateWord(t, (WORD)y);
    templateWord(t, (WORD)width);
    templateWord(t, (WORD)height);
    templateWord(t, id);
    templateWord(t, 0xFFFF);
    templateWord(t, controlClass);
    templateText(t, text);
    templateWord(t, 0);
    t->items++;
    ((WORD *)t->storage)[4] = (WORD)t->items;
}

#define BUTTON_CLASS    0x0080
#define STATIC_CLASS    0x0082
#define SCROLLBAR_CLASS 0x0084
#define COMBOBOX_CLASS  0x0085

static void buildSettingsDialog(DialogTemplate *t)
{
    templateBegin(t, "3D Pipes Setup", 212, 150);
    templateControl(t, BUTTON_CLASS, "Pipes", 0xFFFF, BS_GROUPBOX, 7, 7, 95, 42);
    templateControl(t, BUTTON_CLASS, "&Single", ID_SINGLE, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 15, 20, 80, 10);
    templateControl(t, BUTTON_CLASS, "&Multiple", ID_MULTIPLE, BS_AUTORADIOBUTTON, 15, 33, 80, 10);
    templateControl(t, BUTTON_CLASS, "Joint Type", 0xFFFF, BS_GROUPBOX, 110, 7, 95, 42);
    templateControl(t, COMBOBOX_CLASS, "", ID_JOINT_TYPE, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 118, 22, 79, 70);
    templateControl(t, BUTTON_CLASS, "Speed", 0xFFFF, BS_GROUPBOX, 7, 54, 198, 30);
    templateControl(t, STATIC_CLASS, "Slow", 0xFFFF, SS_LEFT, 13, 67, 22, 8);
    templateControl(t, SCROLLBAR_CLASS, "", ID_SPEED, SBS_HORZ | WS_TABSTOP | WS_GROUP, 37, 66, 136, 10);
    templateControl(t, STATIC_CLASS, "Fast", 0xFFFF, SS_RIGHT, 177, 67, 22, 8);
    templateControl(t, BUTTON_CLASS, "Resolution", 0xFFFF, BS_GROUPBOX, 7, 88, 198, 30);
    templateControl(t, STATIC_CLASS, "Min", 0xFFFF, SS_LEFT, 13, 101, 22, 8);
    templateControl(t, SCROLLBAR_CLASS, "", ID_RESOLUTION, SBS_HORZ | WS_TABSTOP | WS_GROUP, 37, 100, 136, 10);
    templateControl(t, STATIC_CLASS, "Max", 0xFFFF, SS_RIGHT, 177, 101, 22, 8);
    templateControl(t, BUTTON_CLASS, "OK", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 99, 128, 50, 14);
    templateControl(t, BUTTON_CLASS, "Cancel", IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, 155, 128, 50, 14);
}

static void setSlider(HWND bar, int highest, int position)
{
    SetScrollRange(bar, SB_CTL, 0, highest, FALSE);
    SetScrollPos(bar, SB_CTL, clampInt(position, 0, highest), TRUE);
}

static void moveSlider(HWND bar, WPARAM request)
{
    int low, high, pos = GetScrollPos(bar, SB_CTL), line, page;
    GetScrollRange(bar, SB_CTL, &low, &high);
    line = (high - low) / 50 > 0 ? (high - low) / 50 : 1;
    page = (high - low) / 10 > 0 ? (high - low) / 10 : 1;
    switch (LOWORD(request)) {
    case SB_LINELEFT:      pos -= line; break;
    case SB_LINERIGHT:     pos += line; break;
    case SB_PAGELEFT:      pos -= page; break;
    case SB_PAGERIGHT:     pos += page; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = HIWORD(request); break;
    case SB_LEFT:          pos = low; break;
    case SB_RIGHT:         pos = high; break;
    default: return;
    }
    SetScrollPos(bar, SB_CTL, clampInt(pos, low, high), TRUE);
}

static void showSettingsInDialog(HWND dlg, const Settings *s)
{
    int i;
    CheckRadioButton(dlg, ID_SINGLE, ID_MULTIPLE, s->multiplePipes ? ID_MULTIPLE : ID_SINGLE);
    for (i = JOINT_ELBOW; i <= JOINT_CYCLE; i++)
        SendDlgItemMessageA(dlg, ID_JOINT_TYPE, CB_ADDSTRING, 0, (LPARAM)JOINT_NAMES[i]);
    SendDlgItemMessageA(dlg, ID_JOINT_TYPE, CB_SETCURSEL, (WPARAM)s->jointStyle, 0);
    setSlider(GetDlgItem(dlg, ID_SPEED), MAX_SPEED, s->speed);
    setSlider(GetDlgItem(dlg, ID_RESOLUTION), MAX_RESOLUTION, s->resolution);
}

static void readSettingsFromDialog(HWND dlg, Settings *s)
{
    LRESULT joint = SendDlgItemMessageA(dlg, ID_JOINT_TYPE, CB_GETCURSEL, 0, 0);
    s->multiplePipes = IsDlgButtonChecked(dlg, ID_MULTIPLE) == BST_CHECKED;
    s->jointStyle = (joint >= JOINT_ELBOW && joint <= JOINT_CYCLE) ? (int)joint : JOINT_ELBOW;
    s->speed = GetScrollPos(GetDlgItem(dlg, ID_SPEED), SB_CTL);
    s->resolution = GetScrollPos(GetDlgItem(dlg, ID_RESOLUTION), SB_CTL);
}

static INT_PTR CALLBACK settingsDialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrA(dlg, DWLP_USER, lp);
        showSettingsInDialog(dlg, (const Settings *)lp);
        return TRUE;
    case WM_HSCROLL:
        moveSlider((HWND)lp, wp);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            readSettingsFromDialog(dlg, (Settings *)GetWindowLongPtrA(dlg, DWLP_USER));
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int runSettingsDialog(HWND owner, Settings *s)
{
    DialogTemplate t;
    buildSettingsDialog(&t);
    if (owner && !IsWindow(owner)) owner = NULL;
    return DialogBoxIndirectParamA(GetModuleHandle(NULL), (LPCDLGTEMPLATEA)t.storage, owner,
                                   settingsDialogProc, (LPARAM)s) == IDOK;
}

static int runningAsScrFile(void)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    return n > 4 && n < sizeof path && _stricmp(path + n - 4, ".scr") == 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    LaunchOptions options;
    Settings settings;
    int result;

    (void)hPrev; (void)nShow;

    parseCommandLine(cmdLine, runningAsScrFile(), &options);
    settings = loadSettings();
    if (options.mode == MODE_CONFIGURE) {
        if (runSettingsDialog(options.ownerWindow, &settings)) saveSettings(&settings);
        return 0;
    }
    applySettings(&settings);
    if (options.mode == MODE_PREVIEW) {
        if (!options.previewParent || !IsWindow(options.previewParent)) return 0;
        previewing = 1;
        previewParent = options.previewParent;
    }

    fullscreen = options.mode == MODE_FULLSCREEN;
    srand(options.hasShotSeed ? options.shotSeed
                              : (unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());

    if (!createGLWindow(hInst, options.mode)) {
        destroyGLWindow();
        return previewing ? 0 : 1;
    }
    initGL();
    if (!createScenes()) {
        destroyGLWindow();
        return 1;
    }

    result = options.mode == MODE_SHOT ? runShot(&options) : runInteractive();

    destroyGLWindow();
    destroyScenes();
    return result;
}
