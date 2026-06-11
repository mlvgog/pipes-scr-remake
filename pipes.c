/*
 * pipes.c — recreation of the classic Windows "3D Pipes" screensaver.
 *
 * Win32 + fixed-function OpenGL 1.1, single file, no dependencies beyond
 * opengl32/gdi32/user32 (same tech as the original sspipes.scr).
 *
 * Usage:
 *   pipes.exe              windowed (for development / casual viewing)
 *   pipes.exe /s           fullscreen screensaver mode (any input exits)
 *   pipes.exe /c           "configure" (no options; shows a message box)
 *   pipes.exe /p <hwnd>    preview mode (not supported; exits cleanly)
 *   pipes.exe /shot f.bmp [steps]   simulate N steps, render once, save BMP, exit
 *
 * Build:  gcc -O2 -o pipes.exe pipes.c -lopengl32 -lgdi32 -luser32 -mwindows
 */

#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Tunables — chosen to match the look of the original                  */

#define CELL        1.0f    /* grid spacing (world units)               */
#define PIPE_R      0.30f   /* pipe radius                              */
#define ELBOW_R     0.42f   /* bend radius of elbow joints              */
#define BALL_R      0.44f   /* radius of ball joints (the big spheres)  */
#define CAP_R       0.38f   /* radius of start/end caps                 */

#define NUM_PIPES   4       /* pipes growing simultaneously             */
#define STEP_MS     45.0    /* ms between growth steps                  */
#define P_STRAIGHT  0.55f   /* chance to keep going straight            */
#define P_BALL      0.25f   /* chance a turn uses a ball joint          */

#define DEATHS_RESET 12     /* pipes finished before the scene resets   */
#define HOLD_MS      1600   /* pause on the finished scene before clear */

#define FOV_Y       34.0    /* vertical field of view, degrees          */

#define GRID_NY     12
#define GRID_NZ     14
#define MAXX        64
#define MAXY        32
#define MAXZ        32

#define CYL_SLICES  20
#define SPH_SLICES  18
#define SPH_STACKS  12
#define ARC_SEGS    12
#define TUBE_SLICES 18

#define MAX_PRIMS   60000

/* ------------------------------------------------------------------ */

enum { PRIM_CYL, PRIM_SPH, PRIM_ELB };

typedef struct {
    int   type;
    float col[3];
    float a[3];     /* CYL: start point; SPH: center; ELB: node center */
    int   d1, d2;   /* CYL: direction; ELB: in-dir, out-dir            */
    float len;      /* CYL only                                        */
    float r;        /* SPH only                                        */
} Prim;

typedef struct {
    int   alive;
    int   x, y, z;
    int   dir;      /* direction of last move */
    int   hasDir;   /* 0 right after spawn    */
    int   lastCyl;  /* prim index of the cylinder being extended, or -1 */
    float col[3];
} Pipe;

/* dirs: 0:+x 1:-x 2:+y 3:-y 4:+z 5:-z  (opposite = d ^ 1) */
static const int DV[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
};

static Prim prims[MAX_PRIMS];
static int  nPrims;

static unsigned char grid[MAXX][MAXY][MAXZ];
static int NX, NY = GRID_NY, NZ = GRID_NZ;

static Pipe pipes[NUM_PIPES];
static int  deaths;
static int  holding;          /* scene finished, waiting to clear */
static ULONGLONG holdStart;

static GLuint dlCyl, dlSph, dlElb;
static double camZ;
static int    winW = 1280, winH = 720;
static int    fullscreen;
static HWND   hwnd;
static HDC    hdc;
static HGLRC  hrc;
static POINT  mouseStart;
static int    haveMouseStart;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

/* ------------------------------------------------------------------ */
/* Colors — random bright hues like the original                       */

static void randomColor(float *c)
{
    float h = frand() * 6.0f;
    float s = 0.85f + frand() * 0.15f;
    int   i = (int)h % 6;
    float f = h - (float)((int)h);
    float p = 1.0f - s, q = 1.0f - s * f, t = 1.0f - s * (1.0f - f);
    switch (i) {
    case 0: c[0] = 1; c[1] = t; c[2] = p; break;
    case 1: c[0] = q; c[1] = 1; c[2] = p; break;
    case 2: c[0] = p; c[1] = 1; c[2] = t; break;
    case 3: c[0] = p; c[1] = q; c[2] = 1; break;
    case 4: c[0] = t; c[1] = p; c[2] = 1; break;
    default:c[0] = 1; c[1] = p; c[2] = q; break;
    }
}

/* ------------------------------------------------------------------ */
/* Geometry display lists                                              */

static void buildCylinder(void)
{
    int i;
    dlCyl = glGenLists(1);
    glNewList(dlCyl, GL_COMPILE);
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= CYL_SLICES; i++) {
        float a = (float)(2.0 * M_PI * i / CYL_SLICES);
        float c = cosf(a), s = sinf(a);
        glNormal3f(c, s, 0.0f);
        glVertex3f(PIPE_R * c, PIPE_R * s, 0.0f);
        glVertex3f(PIPE_R * c, PIPE_R * s, 1.0f);
    }
    glEnd();
    glEndList();
}

static void buildSphere(void)
{
    int i, j;
    dlSph = glGenLists(1);
    glNewList(dlSph, GL_COMPILE);
    for (i = 0; i < SPH_STACKS; i++) {
        float t0 = (float)(M_PI * (-0.5 + (double)i / SPH_STACKS));
        float t1 = (float)(M_PI * (-0.5 + (double)(i + 1) / SPH_STACKS));
        glBegin(GL_QUAD_STRIP);
        for (j = 0; j <= SPH_SLICES; j++) {
            float a = (float)(2.0 * M_PI * j / SPH_SLICES);
            float c = cosf(a), s = sinf(a);
            glNormal3f(cosf(t0) * c, cosf(t0) * s, sinf(t0));
            glVertex3f(cosf(t0) * c, cosf(t0) * s, sinf(t0));
            glNormal3f(cosf(t1) * c, cosf(t1) * s, sinf(t1));
            glVertex3f(cosf(t1) * c, cosf(t1) * s, sinf(t1));
        }
        glEnd();
    }
    glEndList();
}

/*
 * Quarter torus in the local XY plane: ring path sweeps from angle 0 to
 * 90 degrees with sweep radius ELBOW_R and tube radius PIPE_R.  The arc
 * starts at (ELBOW_R,0,0) with tangent +Y and ends at (0,ELBOW_R,0) with
 * tangent -X; the placement matrix maps local +Y to the incoming travel
 * direction and local -X to the outgoing one.
 */
static void buildElbow(void)
{
    int i, j;
    dlElb = glGenLists(1);
    glNewList(dlElb, GL_COMPILE);
    for (i = 0; i < ARC_SEGS; i++) {
        float t0 = (float)(M_PI * 0.5 * i / ARC_SEGS);
        float t1 = (float)(M_PI * 0.5 * (i + 1) / ARC_SEGS);
        glBegin(GL_QUAD_STRIP);
        for (j = 0; j <= TUBE_SLICES; j++) {
            float p = (float)(2.0 * M_PI * j / TUBE_SLICES);
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

/* ------------------------------------------------------------------ */
/* World <-> grid                                                      */

static void nodeWorld(int x, int y, int z, float *w)
{
    w[0] = (x - (NX - 1) * 0.5f) * CELL;
    w[1] = (y - (NY - 1) * 0.5f) * CELL;
    w[2] = (z - (NZ - 1) * 0.5f) * CELL;
}

/* ------------------------------------------------------------------ */
/* Primitive list                                                      */

static int addPrim(int type, const float *col)
{
    Prim *p;
    if (nPrims >= MAX_PRIMS) { holding = 1; holdStart = GetTickCount64(); return -1; }
    p = &prims[nPrims];
    memset(p, 0, sizeof *p);
    p->type = type;
    p->col[0] = col[0]; p->col[1] = col[1]; p->col[2] = col[2];
    return nPrims++;
}

static int addCyl(const float *start, int dir, float len, const float *col)
{
    int i = addPrim(PRIM_CYL, col);
    if (i < 0) return -1;
    memcpy(prims[i].a, start, 3 * sizeof(float));
    prims[i].d1 = dir;
    prims[i].len = len;
    return i;
}

static void addSph(const float *c, float r, const float *col)
{
    int i = addPrim(PRIM_SPH, col);
    if (i < 0) return;
    memcpy(prims[i].a, c, 3 * sizeof(float));
    prims[i].r = r;
}

static void addElb(int x, int y, int z, int d1, int d2, const float *col)
{
    int i = addPrim(PRIM_ELB, col);
    if (i < 0) return;
    nodeWorld(x, y, z, prims[i].a);
    prims[i].d1 = d1;
    prims[i].d2 = d2;
}

/* ------------------------------------------------------------------ */
/* Pipe simulation                                                     */

static int inBounds(int x, int y, int z)
{
    return x >= 0 && x < NX && y >= 0 && y < NY && z >= 0 && z < NZ;
}

static void scheduleReset(void)
{
    if (!holding) { holding = 1; holdStart = GetTickCount64(); }
}

static int spawnPipe(Pipe *p)
{
    int tries;
    for (tries = 0; tries < 400; tries++) {
        int x = rand() % NX, y = rand() % NY, z = rand() % NZ;
        int d, free_nbr = 0;
        if (grid[x][y][z]) continue;
        for (d = 0; d < 6; d++) {
            int nx = x + DV[d][0], ny = y + DV[d][1], nz = z + DV[d][2];
            if (inBounds(nx, ny, nz) && !grid[nx][ny][nz]) { free_nbr = 1; break; }
        }
        if (!free_nbr) continue;
        memset(p, 0, sizeof *p);
        p->alive = 1;
        p->x = x; p->y = y; p->z = z;
        p->hasDir = 0;
        p->lastCyl = -1;
        randomColor(p->col);
        grid[x][y][z] = 1;
        {
            float w[3];
            nodeWorld(x, y, z, w);
            addSph(w, CAP_R, p->col);   /* start cap */
        }
        return 1;
    }
    return 0;
}

static void killPipe(Pipe *p)
{
    float w[3];
    nodeWorld(p->x, p->y, p->z, w);
    addSph(w, CAP_R, p->col);           /* end cap */
    p->alive = 0;
    deaths++;
    if (deaths >= DEATHS_RESET) { scheduleReset(); return; }
    if (!spawnPipe(p)) scheduleReset();
}

static void stepPipe(Pipe *p)
{
    int cand[6], nc = 0, d;
    float w[3];

    for (d = 0; d < 6; d++) {
        int nx, ny, nz;
        if (p->hasDir && d == (p->dir ^ 1)) continue;
        nx = p->x + DV[d][0]; ny = p->y + DV[d][1]; nz = p->z + DV[d][2];
        if (inBounds(nx, ny, nz) && !grid[nx][ny][nz]) cand[nc++] = d;
    }
    if (nc == 0) { killPipe(p); return; }

    d = -1;
    if (p->hasDir && frand() < P_STRAIGHT) {
        int i;
        for (i = 0; i < nc; i++) if (cand[i] == p->dir) { d = p->dir; break; }
    }
    if (d < 0) d = cand[rand() % nc];

    nodeWorld(p->x, p->y, p->z, w);

    if (!p->hasDir) {
        p->lastCyl = addCyl(w, d, CELL, p->col);
    } else if (d == p->dir) {
        if (p->lastCyl >= 0) prims[p->lastCyl].len += CELL;
        else p->lastCyl = addCyl(w, d, CELL, p->col);
    } else if (frand() < P_BALL) {
        /* ball joint: cylinders meet at the node, big sphere covers it */
        addSph(w, BALL_R, p->col);
        p->lastCyl = addCyl(w, d, CELL, p->col);
    } else {
        /* elbow joint: retract the incoming cylinder, insert quarter torus */
        float s[3];
        if (p->lastCyl >= 0) prims[p->lastCyl].len -= ELBOW_R;
        addElb(p->x, p->y, p->z, p->dir, d, p->col);
        s[0] = w[0] + DV[d][0] * ELBOW_R;
        s[1] = w[1] + DV[d][1] * ELBOW_R;
        s[2] = w[2] + DV[d][2] * ELBOW_R;
        p->lastCyl = addCyl(s, d, CELL - ELBOW_R, p->col);
    }

    p->dir = d;
    p->hasDir = 1;
    p->x += DV[d][0]; p->y += DV[d][1]; p->z += DV[d][2];
    grid[p->x][p->y][p->z] = 1;
}

static void resetScene(void)
{
    int i;
    memset(grid, 0, sizeof grid);
    nPrims = 0;
    deaths = 0;
    holding = 0;
    for (i = 0; i < NUM_PIPES; i++)
        if (!spawnPipe(&pipes[i])) pipes[i].alive = 0;
}

static void tick(void)
{
    int i;
    if (holding) {
        if (GetTickCount64() - holdStart >= HOLD_MS) resetScene();
        return;
    }
    for (i = 0; i < NUM_PIPES; i++) {
        if (holding) break;
        if (pipes[i].alive) stepPipe(&pipes[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */

static void setProjection(int w, int h)
{
    double aspect = (h > 0) ? (double)w / (double)h : 1.0;
    double tanHalf = tan(FOV_Y * 0.5 * M_PI / 180.0);
    double dFront = (NY * CELL * 0.5 * 0.94) / tanHalf;
    double znear, zfar, t;

    camZ = NZ * CELL * 0.5 + dFront;
    znear = dFront * 0.45;
    zfar = camZ + NZ * CELL + 5.0;

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    t = tanHalf * znear;
    glFrustum(-t * aspect, t * aspect, -t, t, znear, zfar);
    glMatrixMode(GL_MODELVIEW);

    /* size the grid X extent so pipes fill the width at mid-depth */
    {
        double hx = tanHalf * aspect * (dFront + NZ * CELL * 0.5);
        int nx = (int)(hx * 2.0 / CELL) + 1;
        if (nx > MAXX) nx = MAXX;
        if (nx < 8) nx = 8;
        if (NX == 0) NX = nx;   /* fixed after first call */
    }
}

static void drawCylRot(int dir)
{
    switch (dir) {
    case 0: glRotatef( 90.0f, 0, 1, 0); break;  /* +x */
    case 1: glRotatef(-90.0f, 0, 1, 0); break;  /* -x */
    case 2: glRotatef(-90.0f, 1, 0, 0); break;  /* +y */
    case 3: glRotatef( 90.0f, 1, 0, 0); break;  /* -y */
    case 4: break;                               /* +z */
    case 5: glRotatef(180.0f, 0, 1, 0); break;  /* -z */
    }
}

static void drawScene(void)
{
    int i;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, (float)-camZ);

    for (i = 0; i < nPrims; i++) {
        Prim *p = &prims[i];
        glColor3fv(p->col);
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
            const int *v1 = DV[p->d1], *v2 = DV[p->d2];
            float X[3], Y[3], Z[3], C[3], m[16];
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
            glMultMatrixf(m);
            glCallList(dlElb);
            break;
        }
        }
        glPopMatrix();
    }
}

static void initGL(void)
{
    const float lightDir[4]  = { -0.4f, 0.7f, 1.0f, 0.0f };
    const float lightDif[4]  = { 0.95f, 0.95f, 0.95f, 1.0f };
    const float lightAmb[4]  = { 0.18f, 0.18f, 0.18f, 1.0f };
    const float lightSpc[4]  = { 0.9f, 0.9f, 0.9f, 1.0f };
    const float matSpec[4]   = { 0.85f, 0.85f, 0.85f, 1.0f };

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  lightDif);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  lightAmb);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpc);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 60.0f);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    buildCylinder();
    buildSphere();
    buildElbow();
}

/* ------------------------------------------------------------------ */
/* BMP screenshot (for the /shot self-test mode)                       */

static int saveBMP(const char *path, int w, int h)
{
    int stride = (w * 3 + 3) & ~3;
    unsigned char *buf = (unsigned char *)malloc((size_t)stride * h);
    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    FILE *f;
    int x, y;

    if (!buf) return 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf);
    /* RGB -> BGR in place */
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

/* ------------------------------------------------------------------ */
/* Win32 plumbing                                                      */

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        winW = LOWORD(lp); winH = HIWORD(lp);
        if (hrc) setProjection(winW, winH);
        return 0;
    case WM_KEYDOWN:
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
            else if (abs(pt.x - mouseStart.x) > 4 || abs(pt.y - mouseStart.y) > 4)
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

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    WNDCLASS wc;
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
    MSG msg;
    int running = 1;
    int shotMode = 0, shotSteps = 700;
    char shotFile[MAX_PATH] = "pipes_shot.bmp";
    LARGE_INTEGER freq, last, now;
    double acc = 0.0;

    (void)hPrev; (void)nShow;

    /* ---- arguments ---- */
    {
        char *a = cmdLine;
        while (*a == ' ') a++;
        if (_strnicmp(a, "/shot", 5) == 0) {
            shotMode = 1;
            a += 5;
            while (*a == ' ') a++;
            if (*a) {
                char *sp = strchr(a, ' ');
                if (sp) {
                    *sp = 0;
                    shotSteps = atoi(sp + 1);
                    if (shotSteps <= 0) shotSteps = 700;
                }
                strncpy(shotFile, a, MAX_PATH - 1);
            }
        } else if (_strnicmp(a, "/s", 2) == 0 || _strnicmp(a, "-s", 2) == 0) {
            fullscreen = 1;
        } else if (_strnicmp(a, "/c", 2) == 0) {
            MessageBoxA(NULL, "3D Pipes has no options to configure.",
                        "3D Pipes", MB_OK | MB_ICONINFORMATION);
            return 0;
        } else if (_strnicmp(a, "/p", 2) == 0) {
            return 0;   /* preview-in-dialog not supported */
        }
    }

    srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());

    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "PipesWnd";
    RegisterClass(&wc);

    if (fullscreen) {
        winW = GetSystemMetrics(SM_CXSCREEN);
        winH = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowEx(WS_EX_TOPMOST, wc.lpszClassName, "3D Pipes",
                              WS_POPUP | WS_VISIBLE, 0, 0, winW, winH,
                              NULL, NULL, hInst, NULL);
    } else {
        RECT r = { 0, 0, winW, winH };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindow(wc.lpszClassName, "3D Pipes",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            NULL, NULL, hInst, NULL);
    }
    if (!hwnd) return 1;

    hdc = GetDC(hwnd);
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) return 1;
    hrc = wglCreateContext(hdc);
    if (!hrc || !wglMakeCurrent(hdc, hrc)) return 1;

    {
        PFNWGLSWAPINTERVALEXT swapInterval =
            (PFNWGLSWAPINTERVALEXT)wglGetProcAddress("wglSwapIntervalEXT");
        if (swapInterval) swapInterval(1);
    }

    initGL();
    {
        RECT cr;
        GetClientRect(hwnd, &cr);
        winW = cr.right; winH = cr.bottom;
    }
    NX = 0;
    setProjection(winW, winH);
    resetScene();

    if (shotMode) {
        int i;
        for (i = 0; i < shotSteps; i++) {
            if (holding) break;     /* scene finished early */
            tick();
        }
        drawScene();
        glFinish();
        saveBMP(shotFile, winW, winH);
        SwapBuffers(hdc);
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(hrc);
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return 0;
    }

    GetCursorPos(&mouseStart);
    haveMouseStart = 1;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        QueryPerformanceCounter(&now);
        acc += (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart;
        last = now;
        if (acc > 500.0) acc = 500.0;
        while (acc >= STEP_MS) {
            tick();
            acc -= STEP_MS;
        }

        drawScene();
        SwapBuffers(hdc);
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hrc);
    return 0;
}
