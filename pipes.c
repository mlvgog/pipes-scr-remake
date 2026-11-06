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

#define CELL        1.0f
#define PIPE_R      0.30f
#define ELBOW_R     0.42f
#define BALL_R      0.44f
#define CAP_R       0.38f

#define NUM_PIPES   4
#define STEP_MS     45.0
#define P_STRAIGHT  0.55f
#define P_BALL      0.25f

#define DEATHS_RESET 12
#define HOLD_MS      1600

#define FOV_Y       34.0

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
#define MAX_SCENES 8

enum { PRIM_CYL, PRIM_SPH, PRIM_ELB };

typedef struct {
    int   type;
    float col[3];
    float a[3];
    int   d1, d2;
    float len;
    float r;
} Prim;

typedef struct {
    int   alive;
    int   x, y, z;
    int   dir;
    int   hasDir;
    int   lastCyl;
    float col[3];
} Pipe;

typedef struct {
    int    vx, vy, vw, vh;
    int    NX, NY, NZ;
    double aspect, dFront, camZ;
    unsigned char grid[MAXX][MAXY][MAXZ];
    Prim   prims[MAX_PRIMS];
    int    nPrims;
    Pipe   pipes[NUM_PIPES];
    int    deaths;
    int    holding;
    ULONGLONG holdStart;
} Scene;

static const int DV[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
};

static Scene *scenes[MAX_SCENES];
static int    nScenes;

static GLuint dlCyl, dlSph, dlElb;
static int    winW = 1280, winH = 720;
static int    vsX, vsY;
static int    fullscreen;
static HWND   hwnd;
static HDC    hdc;
static HGLRC  hrc;
static POINT  mouseStart;
static int    haveMouseStart;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

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

static void nodeWorld(const Scene *s, int x, int y, int z, float *w)
{
    w[0] = (x - (s->NX - 1) * 0.5f) * CELL;
    w[1] = (y - (s->NY - 1) * 0.5f) * CELL;
    w[2] = (z - (s->NZ - 1) * 0.5f) * CELL;
}

static void scheduleReset(Scene *s)
{
    if (!s->holding) { s->holding = 1; s->holdStart = GetTickCount64(); }
}

static int addPrim(Scene *s, int type, const float *col)
{
    Prim *p;
    if (s->nPrims >= MAX_PRIMS) { scheduleReset(s); return -1; }
    p = &s->prims[s->nPrims];
    memset(p, 0, sizeof *p);
    p->type = type;
    p->col[0] = col[0]; p->col[1] = col[1]; p->col[2] = col[2];
    return s->nPrims++;
}

static int addCyl(Scene *s, const float *start, int dir, float len, const float *col)
{
    int i = addPrim(s, PRIM_CYL, col);
    if (i < 0) return -1;
    memcpy(s->prims[i].a, start, 3 * sizeof(float));
    s->prims[i].d1 = dir;
    s->prims[i].len = len;
    return i;
}

static void addSph(Scene *s, const float *c, float r, const float *col)
{
    int i = addPrim(s, PRIM_SPH, col);
    if (i < 0) return;
    memcpy(s->prims[i].a, c, 3 * sizeof(float));
    s->prims[i].r = r;
}

static void addElb(Scene *s, int x, int y, int z, int d1, int d2, const float *col)
{
    int i = addPrim(s, PRIM_ELB, col);
    if (i < 0) return;
    nodeWorld(s, x, y, z, s->prims[i].a);
    s->prims[i].d1 = d1;
    s->prims[i].d2 = d2;
}

static int inBounds(const Scene *s, int x, int y, int z)
{
    return x >= 0 && x < s->NX && y >= 0 && y < s->NY && z >= 0 && z < s->NZ;
}

static int spawnPipe(Scene *s, Pipe *p)
{
    int tries;
    for (tries = 0; tries < 400; tries++) {
        int x = rand() % s->NX, y = rand() % s->NY, z = rand() % s->NZ;
        int d, free_nbr = 0;
        if (s->grid[x][y][z]) continue;
        for (d = 0; d < 6; d++) {
            int nx = x + DV[d][0], ny = y + DV[d][1], nz = z + DV[d][2];
            if (inBounds(s, nx, ny, nz) && !s->grid[nx][ny][nz]) { free_nbr = 1; break; }
        }
        if (!free_nbr) continue;
        memset(p, 0, sizeof *p);
        p->alive = 1;
        p->x = x; p->y = y; p->z = z;
        p->hasDir = 0;
        p->lastCyl = -1;
        randomColor(p->col);
        s->grid[x][y][z] = 1;
        {
            float w[3];
            nodeWorld(s, x, y, z, w);
            addSph(s, w, CAP_R, p->col);
        }
        return 1;
    }
    return 0;
}

static void killPipe(Scene *s, Pipe *p)
{
    float w[3];
    nodeWorld(s, p->x, p->y, p->z, w);
    addSph(s, w, CAP_R, p->col);
    p->alive = 0;
    s->deaths++;
    if (s->deaths >= DEATHS_RESET) { scheduleReset(s); return; }
    if (!spawnPipe(s, p)) scheduleReset(s);
}

static void stepPipe(Scene *s, Pipe *p)
{
    int cand[6], nc = 0, d;
    float w[3];

    for (d = 0; d < 6; d++) {
        int nx, ny, nz;
        if (p->hasDir && d == (p->dir ^ 1)) continue;
        nx = p->x + DV[d][0]; ny = p->y + DV[d][1]; nz = p->z + DV[d][2];
        if (inBounds(s, nx, ny, nz) && !s->grid[nx][ny][nz]) cand[nc++] = d;
    }
    if (nc == 0) { killPipe(s, p); return; }

    d = -1;
    if (p->hasDir && frand() < P_STRAIGHT) {
        int i;
        for (i = 0; i < nc; i++) if (cand[i] == p->dir) { d = p->dir; break; }
    }
    if (d < 0) d = cand[rand() % nc];

    nodeWorld(s, p->x, p->y, p->z, w);

    if (!p->hasDir) {
        p->lastCyl = addCyl(s, w, d, CELL, p->col);
    } else if (d == p->dir) {
        if (p->lastCyl >= 0) s->prims[p->lastCyl].len += CELL;
        else p->lastCyl = addCyl(s, w, d, CELL, p->col);
    } else if (frand() < P_BALL) {

        addSph(s, w, BALL_R, p->col);
        p->lastCyl = addCyl(s, w, d, CELL, p->col);
    } else {

        float st[3];
        if (p->lastCyl >= 0) s->prims[p->lastCyl].len -= ELBOW_R;
        addElb(s, p->x, p->y, p->z, p->dir, d, p->col);
        st[0] = w[0] + DV[d][0] * ELBOW_R;
        st[1] = w[1] + DV[d][1] * ELBOW_R;
        st[2] = w[2] + DV[d][2] * ELBOW_R;
        p->lastCyl = addCyl(s, st, d, CELL - ELBOW_R, p->col);
    }

    p->dir = d;
    p->hasDir = 1;
    p->x += DV[d][0]; p->y += DV[d][1]; p->z += DV[d][2];
    s->grid[p->x][p->y][p->z] = 1;
}

static void sceneReset(Scene *s)
{
    int i;
    memset(s->grid, 0, sizeof s->grid);
    s->nPrims = 0;
    s->deaths = 0;
    s->holding = 0;
    for (i = 0; i < NUM_PIPES; i++)
        if (!spawnPipe(s, &s->pipes[i])) s->pipes[i].alive = 0;
}

static void sceneTick(Scene *s)
{
    int i;
    if (s->holding) {
        if (GetTickCount64() - s->holdStart >= HOLD_MS) sceneReset(s);
        return;
    }
    for (i = 0; i < NUM_PIPES; i++) {
        if (s->holding) break;
        if (s->pipes[i].alive) stepPipe(s, &s->pipes[i]);
    }
}

static void tickAll(void)
{
    int i;
    for (i = 0; i < nScenes; i++) sceneTick(scenes[i]);
}

static void sceneGeometry(Scene *s)
{
    double tanHalf = tan(FOV_Y * 0.5 * M_PI / 180.0);
    s->aspect = (s->vh > 0) ? (double)s->vw / (double)s->vh : 1.0;
    s->dFront = (s->NY * CELL * 0.5 * 0.94) / tanHalf;
    s->camZ = s->NZ * CELL * 0.5 + s->dFront;
    if (s->NX == 0) {

        double hx = tanHalf * s->aspect * (s->dFront + s->NZ * CELL * 0.5);
        int nx = (int)(hx * 2.0 / CELL) + 1;
        if (nx > MAXX) nx = MAXX;
        if (nx < 8) nx = 8;
        s->NX = nx;
    }
}

static Scene *sceneCreate(int vx, int vy, int vw, int vh)
{
    Scene *s = (Scene *)calloc(1, sizeof(Scene));
    if (!s) return NULL;
    s->vx = vx; s->vy = vy; s->vw = vw; s->vh = vh;
    s->NY = GRID_NY;
    s->NZ = GRID_NZ;
    sceneGeometry(s);
    sceneReset(s);
    return s;
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

static void sceneDraw(const Scene *s)
{
    double znear = s->dFront * 0.45;
    double zfar = s->camZ + s->NZ * CELL + 5.0;
    double tanHalf = tan(FOV_Y * 0.5 * M_PI / 180.0);
    double t = tanHalf * znear;
    int i;

    glViewport(s->vx, s->vy, s->vw, s->vh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-t * s->aspect, t * s->aspect, -t, t, znear, zfar);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, (float)-s->camZ);

    for (i = 0; i < s->nPrims; i++) {
        const Prim *p = &s->prims[i];
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

static void drawAll(void)
{
    int i;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (i = 0; i < nScenes; i++) sceneDraw(scenes[i]);
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

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        winW = LOWORD(lp); winH = HIWORD(lp);
        if (!fullscreen && nScenes == 1) {
            scenes[0]->vw = winW;
            scenes[0]->vh = winH;
            sceneGeometry(scenes[0]);
        }
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
            return 0;
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

        vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        winW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        winH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        hwnd = CreateWindowEx(WS_EX_TOPMOST, wc.lpszClassName, "3D Pipes",
                              WS_POPUP | WS_VISIBLE, vsX, vsY, winW, winH,
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
            (PFNWGLSWAPINTERVALEXT)(void *)wglGetProcAddress("wglSwapIntervalEXT");
        if (swapInterval) swapInterval(1);
    }

    initGL();
    {
        RECT cr;
        GetClientRect(hwnd, &cr);
        winW = cr.right; winH = cr.bottom;
    }

    if (fullscreen)
        EnumDisplayMonitors(NULL, NULL, monitorEnum, 0);
    if (nScenes == 0) {
        scenes[0] = sceneCreate(0, 0, winW, winH);
        if (!scenes[0]) return 1;
        nScenes = 1;
    }

    if (shotMode) {
        int i, j;
        for (i = 0; i < shotSteps; i++) {
            int anyRunning = 0;
            for (j = 0; j < nScenes; j++) if (!scenes[j]->holding) anyRunning = 1;
            if (!anyRunning) break;
            tickAll();
        }
        drawAll();
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
            tickAll();
            acc -= STEP_MS;
        }

        drawAll();
        SwapBuffers(hdc);
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hrc);
    return 0;
}
