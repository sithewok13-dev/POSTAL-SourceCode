/* Linux harness for the touch controls' core (ios/IOSTouchCore.c).
   cc -std=c99 -Wall -Werror ios/IOSTouchCore.c ios/tests/touch_core_test.c -lm && ./a.out */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../IOSTouchCore.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int _ctFail = 0, _ctPass = 0;
#define CHECK(c) do { if (c) _ctPass++; else { _ctFail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* touches are only compared: any distinct pointers do */
static char T[10];
#define F(i) ((const void *)&T[i])

static const IOSTouchCore *K;
static double _now = 100.0;
static unsigned int _ulFrames = 0;

/* POSTAL's HUD at 480 game pixels tall, gameW wide: the 40 px strip, the text over its left 640 px, a gap 170..290 */
static IOSTouchHud Hud(double gameW, int bGap)
{
  IOSTouchHud h;
  memset(&h, 0, sizeof(h));
  h.afTopBar[0] = 0; h.afTopBar[1] = 0; h.afTopBar[2] = 1; h.afTopBar[3] = (float)(40.0 / 480.0);
  h.afTopText[0] = (float)(4.0 / gameW); h.afTopText[1] = 0; h.afTopText[2] = (float)(640.0 / gameW); h.afTopText[3] = h.afTopBar[3];
  if (bGap) {
    h.afTopGap[0] = (float)(170.0 / gameW); h.afTopGap[2] = (float)(290.0 / gameW);
    h.afTopGap[1] = 0; h.afTopGap[3] = h.afTopBar[3];
  }
  return h;
}

static IOSTouchHud _hud;
static int _bFull = 0; /* lay out for full-screen play (no status strip) */

static void Screen(double W, double H, double inL, double inT, double inR, double inB, int bGap)
{
  IOSTouchCore_Reset();
  _hud = Hud(480.0 * W / H, bGap);
  if (_bFull) {
    memset(_hud.afTopBar, 0, sizeof(_hud.afTopBar));
    memset(_hud.afTopText, 0, sizeof(_hud.afTopText));
    memset(_hud.afTopGap, 0, sizeof(_hud.afTopGap));
    _hud.bFullScreen = 1;
    snprintf(_hud.szStatus, sizeof(_hud.szStatus), "Population 38 Hostiles 30 Killed 0 (0%% / 87%%)");
    _hud.iHealth = 100;
    _hud.iAmmo = -1;
  }
  IOSTouchCore_SetHud(&_hud);
  IOSTouchCore_Layout(W, H, inL, inT, inR, inB);
  IOSTouchCore_SetMode(IOSTOUCH_GAMEPLAY, _now);
  K = IOSTouchCore_Get();
}
static void Phone14(void) { Screen(844, 390, 47, 0, 47, 21, 1); }

static void SetHud(void)
{
  if (IOSTouchCore_SetHud(&_hud)) IOSTouchCore_Layout(K->W, K->H, 47, 0, 47, 21);
}

static unsigned int Frame(double dt)
{
  _now += dt;
  _ulFrames++;
  return IOSTouchCore_Tick(_now, _ulFrames);
}
/* Runs frames for t seconds (1/60 each); returns every request asked meanwhile */
static unsigned int Frames(double t)
{
  unsigned int ul = 0;
  for (double d = 0; d < t - 1e-9; d += 1.0 / 60.0) ul |= Frame(1.0 / 60.0);
  return ul;
}
static void Down(int i, double x, double y) { IOSTouchCore_TouchBegan(F(i), x, y, _now); }
static void Move(int i, double x, double y) { IOSTouchCore_TouchMoved(F(i), x, y, _now); }
static void Up(int i, double x, double y) { IOSTouchCore_TouchEnded(F(i), x, y, _now, 0); }
static void Cancel(int i, double x, double y) { IOSTouchCore_TouchEnded(F(i), x, y, _now, 1); }
static double BX(int b) { return K->aButtons[b].x; }
static double BY(int b) { return K->aButtons[b].y; }
static IOSTouchInput Read(void)
{
  IOSTouchInput in;
  IOSTouchCore_ReadInput(&in);
  return in;
}
static void Tap(int b)
{
  Down(0, BX(b), BY(b));
  Frame(0.05);
  Up(0, BX(b), BY(b));
}

/* ------------------------------------------------------------ layout */
static int ButtonInside(int b, double x0, double y0, double x1, double y1)
{
  const IOSTouchButton *p = &K->aButtons[b];
  return p->x - p->hw >= x0 - 1e-6 && p->x + p->hw <= x1 + 1e-6 && p->y - p->r >= y0 - 1e-6 && p->y + p->r <= y1 + 1e-6;
}

static double Gap(int a, int b)
{
  const IOSTouchButton *p = &K->aButtons[a], *q = &K->aButtons[b];
  /* the gap between two buttons as boxes (pills) or circles */
  if (!p->bPill && !q->bPill) return hypot(p->x - q->x, p->y - q->y) - p->r - q->r;
  const double dx = fabs(p->x - q->x) - p->hw - q->hw, dy = fabs(p->y - q->y) - p->r - q->r;
  return fmax(dx, dy);
}

static void CheckLayoutFits(const char *name, double W, double H, double inL, double inT, double inR, double inB)
{
  int bad = 0;
  /* the top strip's things are inside the safe area's sides (top inset is 0 in landscape) */
  _hud.bCheatOn = 1;
  _hud.bFollowing = 1;
  _hud.iSlowMo = 2;
  IOSTouchCore_SetHud(&_hud);
  IOSTouchCore_SetLevels((const char *const[]){ "Hwy", "Mine" }, 2);
  IOSTouchCore_Settings()->bShowCheat = 1;
  IOSTouchCore_Settings()->bShowCamera = 1;
  IOSTouchCore_Settings()->bShowFps = 1;
  IOSTouchCore_Layout(W, H, inL, inT, inR, inB);
  /* open the tray */
  Down(0, BX(BTN_MENU), BY(BTN_MENU));
  Frames(0.6);
  Up(0, BX(BTN_MENU), BY(BTN_MENU));
  CHECK(K->bTrayOpen);
  for (int b = 0; b < BTN_COUNT; b++) {
    if (!IOSTouchCore_IsShown(b)) continue;
    if (!ButtonInside(b, inL, inT, W - inR, H - inB)) {
      printf("  %s: button %d (%s) at %.1f,%.1f outside the safe area\n", name, b, K->aButtons[b].label, BX(b), BY(b));
      bad = 1;
    }
  }
  /* no two shown buttons overlap (the tray's are on top of the rest and may
     lie over them; they are checked among themselves) */
  for (int a = 0; a < BTN_COUNT; a++) {
    for (int b = a + 1; b < BTN_COUNT; b++) {
      if (!IOSTouchCore_IsShown(a) || !IOSTouchCore_IsShown(b)) continue;
      const int ta = a >= BTN_TRAY_FIRST, tb = b >= BTN_TRAY_FIRST;
      if (ta != tb) continue;
      if (Gap(a, b) < 0.0) {
        printf("  %s: buttons %d and %d overlap\n", name, a, b);
        bad = 1;
      }
    }
  }
  /* every button is drawn as big as it is (a circle as wide as tall: the tray's were 0 wide, build 0.1.12) */
  for (int b = 0; b < BTN_COUNT; b++) {
    const IOSTouchButton *p = &K->aButtons[b];
    if (p->r <= 0 || p->hw < p->r - 1e-9 || (!p->bPill && fabs(p->hw - p->r) > 1e-9)) {
      printf("  %s: button %d drawn %.1f x %.1f\n", name, b, 2 * p->hw, 2 * p->r);
      bad = 1;
    }
  }
  /* full screen: the status text's band and row clear of every button (but the tray, on top) and on screen */
  if (_bFull) {
    IOSTouchRect rt, r2;
    IOSTouchCore_StatusRects(&rt, &r2);
    const IOSTouchRect *apr[2] = { &rt, &r2 };
    for (int k = 0; k < 2; k++) {
      const IOSTouchRect *r = apr[k];
      if (!r->bValid) continue;
      if (r->x0 < inL || r->x1 > W - inR || r->y0 < inT) { printf("  %s: status rect %d off the safe area\n", name, k); bad = 1; }
      for (int b = 0; b < BTN_TRAY_FIRST; b++) {
        if (!IOSTouchCore_IsShown(b)) continue;
        const IOSTouchButton *p = &K->aButtons[b];
        if (p->x + p->hw > r->x0 && p->x - p->hw < r->x1 && p->y + p->r > r->y0 && p->y - p->r < r->y1) {
          printf("  %s: status rect %d over button %d\n", name, k, b);
          bad = 1;
        }
      }
    }
    if (K->fInfoY > H - inB || K->fHealthX < inL) { printf("  %s: info off the safe area\n", name); bad = 1; }
  }
  /* the FPS readout clear of the buttons outside the tray (which hides it) */
  for (int b = 0; b < BTN_TRAY_FIRST; b++) {
    if (!IOSTouchCore_IsShown(b)) continue;
    const IOSTouchButton *p = &K->aButtons[b];
    if (p->x + p->hw > K->rFps.x0 && p->x - p->hw < K->rFps.x1 && p->y + p->r > K->rFps.y0 && p->y - p->r < K->rFps.y1) {
      printf("  %s: FPS over button %d\n", name, b);
      bad = 1;
    }
  }
  /* the FPS readout and the version label too */
  if (K->rFps.x0 < inL || K->rFps.x1 > W - inR) { printf("  %s: FPS outside\n", name); bad = 1; }
  if (K->fVersionY + IOSTOUCH_VERSION_H * 0.5 > H - inB) { printf("  %s: version outside\n", name); bad = 1; }
  /* the wheel */
  if (K->fWheelCY - K->fWheelR1 - IOSTOUCH_WHEEL_POP < inT || K->fWheelCY + K->fWheelR1 + IOSTOUCH_WHEEL_POP > H - inB) {
    printf("  %s: wheel outside\n", name);
    bad = 1;
  }
  CHECK(!bad);
  if (!bad) printf("  layout %s: ok (FIRE %.0f,%.0f  NEXT %.0f  SAVE %.0f  camera %.0f,%.0f  tray rows %d)\n", name, BX(BTN_FIRE),
                   BY(BTN_FIRE), BX(BTN_NEXTWPN), BX(BTN_SAVE), BX(BTN_CAMERA), BY(BTN_CAMERA), K->ctTrayRows);
}

static void OpenTrayAt(void);

static void TestLayout(void)
{
  printf("layout\n");
  Screen(568, 320, 0, 0, 0, 0, 1);
  /* iPhone SE: the gap (170..290 of 852 game px) is 80 pt: two 36 pt pills fit in it */
  CHECK(fabs(K->aButtons[BTN_NEXTWPN].hw - 18) < 1e-9 && BY(BTN_NEXTWPN) == BY(BTN_SAVE));
  CHECK(BX(BTN_NEXTWPN) - 18 >= 170 * 568 / 852.0 && BX(BTN_PREVWPN) + 18 <= 290 * 568 / 852.0
        && fabs(Gap(BTN_NEXTWPN, BTN_PREVWPN) - 6) < 1e-9);
  CheckLayoutFits("iPhone SE 568x320", 568, 320, 0, 0, 0, 0);
  Screen(844, 390, 47, 0, 47, 21, 1);
  CheckLayoutFits("iPhone 14 844x390", 844, 390, 47, 0, 47, 21);
  Screen(1180, 820, 0, 24, 0, 20, 1);
  CheckLayoutFits("iPad 1180x820", 1180, 820, 0, 24, 0, 20);
  Screen(844, 390, 47, 0, 47, 21, 0);
  CheckLayoutFits("iPhone 14, no gap", 844, 390, 47, 0, 47, 21);
  /* a made-up narrow screen: the tray wraps to a second row */
  Screen(340, 300, 0, 0, 0, 0, 1);
  CheckLayoutFits("narrow 340x300", 340, 300, 0, 0, 0, 0);
  CHECK(K->ctTrayRows == 2);

  /* full-screen play: the house top row */
  _bFull = 1;
  Screen(568, 320, 0, 0, 0, 0, 1);
  CheckLayoutFits("full screen iPhone SE", 568, 320, 0, 0, 0, 0);
  Screen(844, 390, 47, 0, 47, 21, 1);
  CheckLayoutFits("full screen iPhone 14", 844, 390, 47, 0, 47, 21);
  Screen(1180, 820, 0, 24, 0, 20, 1);
  CheckLayoutFits("full screen iPad", 1180, 820, 0, 24, 0, 20);
  Screen(568, 320, 0, 0, 0, 0, 1);
  {
    IOSTouchRect rt, r2;
    IOSTouchCore_StatusRects(&rt, &r2);
    /* (iPhone SE, the camera icon alone: centred on the screen, as wide as it stays clear of it) */
    CHECK(rt.bValid && fabs((rt.x0 + rt.x1) * 0.5 - 284) < 1e-9
          && (fabs(rt.x1 - (BX(BTN_CAMERA) - 22 - 8)) < 1e-9 || fabs(rt.x0 - (BX(BTN_PREVWPN) + 22 + 8)) < 1e-9));
    CHECK(fabs((r2.x0 + r2.x1) * 0.5 - 284) < 1e-9 && r2.x0 == 8 && r2.x1 == 560);
  }
  Screen(844, 390, 47, 0, 47, 21, 1);
  CHECK(!K->aButtons[BTN_SAVE].bPill && K->aButtons[BTN_SAVE].r == 22 && K->aButtons[BTN_NEXTWPN].r == 22);
  /* NEXT, PREV from the left corner; MENU, LOAD, SAVE, the camera from the right; 22 pt apart edge to edge */
  CHECK(BX(BTN_NEXTWPN) == 47 + 24 && BX(BTN_PREVWPN) == 47 + 90 && BY(BTN_NEXTWPN) == 34);
  CHECK(BX(BTN_MENU) == 797 - 24 && BX(BTN_LOAD) == 797 - 90 && BX(BTN_SAVE) == 797 - 156 && BY(BTN_SAVE) == 34);
  CHECK(BX(BTN_CAMERA) == 797 - 222 && K->aButtons[BTN_CAMERA].r == 22 && !K->aButtons[BTN_CAMERA].bPill && BY(BTN_CAMERA) == 34);
  {
    IOSTouchRect rt, r2;
    IOSTouchCore_StatusRects(&rt, &r2);
    CHECK(rt.bValid && rt.x0 >= BX(BTN_PREVWPN) + 22 && rt.x1 <= BX(BTN_CAMERA) - 22 && r2.y0 >= 34 + 22);
    CHECK(rt.x1 - rt.x0 > 240 && fabs((rt.x0 + rt.x1) * 0.5 - 422) < 1e-9); /* (the camera icon alone) */
    /* the cheat icon showing: on the left, 22 pt right of PREV WPN; the text space stays centred */
    IOSTouchCore_Settings()->bShowCheat = 1;
    IOSTouchCore_Layout(844, 390, 47, 0, 47, 21);
    CHECK(BX(BTN_CHEAT) == BX(BTN_PREVWPN) + 66 && BY(BTN_CHEAT) == 34 && K->aButtons[BTN_CHEAT].r == 22);
    IOSTouchRect rt2, rt3;
    IOSTouchCore_StatusRects(&rt2, &r2);
    CHECK(rt2.x0 >= BX(BTN_CHEAT) + 22 + 8 - 1e-9 && fabs((rt2.x0 + rt2.x1) * 0.5 - 422) < 1e-9);
    IOSTouchCore_Settings()->bShowFps = 1;
    IOSTouchCore_Layout(844, 390, 47, 0, 47, 21);
    IOSTouchCore_StatusRects(&rt3, &r2);
    CHECK(rt3.x1 <= rt2.x1 && fabs((rt3.x0 + rt3.x1) * 0.5 - 422) < 1e-9 && rt3.x1 <= K->rFps.x0 - 8 + 1e-9);
    IOSTouchCore_Settings()->bShowFps = 0;
    IOSTouchCore_Settings()->bShowCheat = 0;
    IOSTouchCore_Layout(844, 390, 47, 0, 47, 21);
  }
  /* a touch up top moves or aims (no strip to keep clear) */
  Down(1, 500, 30);
  Move(1, 500, 60);
  CHECK(Read().bAim);
  Up(1, 500, 60);
  /* alive: a tap on the screen asks no restart */
  Down(2, 500, 250); Up(2, 500, 250);
  CHECK(Frame(0.016) == 0);
  /* dead: FIRE still presses FIRE (the game restarts the level on it), and asks no RESTART */
  _hud.bDead = 1;
  SetHud();
  Down(2, BX(BTN_FIRE), BY(BTN_FIRE));
  CHECK(Read().ulButtons & IOSTOUCH_FIRE);
  Up(2, BX(BTN_FIRE), BY(BTN_FIRE));
  CHECK(Frame(0.016) == 0);
  /* dead: a tap anywhere else (the aim area, the stick area, DUCK), however long: RESTART; no aim, no stick */
  {
    const double ax[3] = { 500, 150, BX(BTN_DUCK) }, ay[3] = { 250, 250, BY(BTN_DUCK) };
    for (int k = 0; k < 3; k++) {
      Down(2, ax[k], ay[k]);
      Move(2, ax[k] + 6, ay[k] - 5);
      IOSTouchInput in = Read();
      CHECK(!in.bAim && !K->bStickActive && in.fMoveX == 0 && in.fMoveY == 0 && !(in.ulButtons & IOSTOUCH_DUCK));
      Frames(k == 1 ? 1.0 : 0.1);
      Up(2, ax[k] + 6, ay[k] - 5);
      CHECK(Frame(0.016) == IOSTOUCH_REQ_RESTART);
    }
  }
  /* dead: a drag, a cancelled touch: nothing; the top row still works (SAVE: its screen) */
  Down(2, 500, 250); Move(2, 530, 250); Up(2, 500, 250);
  Down(3, 500, 250); Cancel(3, 500, 250);
  CHECK(Frame(0.016) == 0);
  Down(2, BX(BTN_SAVE), BY(BTN_SAVE)); Frames(0.1); Up(2, BX(BTN_SAVE), BY(BTN_SAVE));
  CHECK(Frame(0.016) == IOSTOUCH_REQ_SAVESCREEN);
  _hud.bDead = 0;
  SetHud();
  /* switching out of full screen lays the strip out again */
  _hud = Hud(1038, 1);
  CHECK(IOSTouchCore_SetHud(&_hud) == 1);
  _bFull = 0;

  /* iPhone 14 as in rig/mock.py */
  Phone14();
  CHECK(fabs(BX(BTN_FIRE) - 737) < 0.5 && fabs(BY(BTN_FIRE) - 307) < 0.5);
  /* DUCK left of FIRE, EXECUTE straight above, 30 pt edge to edge (house IOSTOUCH_CLUSTER_GAP) */
  CHECK(fabs(BY(BTN_DUCK) - BY(BTN_FIRE)) < 1e-9 && fabs(Gap(BTN_FIRE, BTN_DUCK) - 30.0) < 1e-9 && BX(BTN_DUCK) < BX(BTN_FIRE));
  CHECK(fabs(BX(BTN_EXECUTE) - BX(BTN_FIRE)) < 1e-9 && fabs(Gap(BTN_FIRE, BTN_EXECUTE) - 30.0) < 1e-9 && BY(BTN_EXECUTE) < BY(BTN_FIRE));
  CHECK(fabs(BX(BTN_DUCK) - 636) < 0.5 && fabs(BY(BTN_EXECUTE) - 204) < 0.5);
  CHECK(fabs(BX(BTN_MENU) - 770) < 0.5 && fabs(BX(BTN_LOAD) - 715) < 0.5 && fabs(BX(BTN_SAVE) - 660) < 0.5);
  CHECK(fabs(K->fPillH - 27.5) < 0.6 && fabs(BY(BTN_MENU) - 16.25) < 0.5);
  CHECK(fabs(BX(BTN_PREVWPN) - BX(BTN_NEXTWPN) - 44) < 0.01 && fabs(K->aButtons[BTN_NEXTWPN].hw - 19) < 1e-9);
  /* in the gap (170..290 game px of 1038) */
  CHECK(BX(BTN_NEXTWPN) - 19 >= 170 * 844 / 1038.0 && BX(BTN_PREVWPN) + 19 <= 290 * 844 / 1038.0);
  /* cheat and camera right of the text, in the strip */
  CHECK(BX(BTN_CHEAT) - K->aButtons[BTN_CHEAT].r >= 640 * 844 / 1038.0 && fabs(BY(BTN_CAMERA) - BY(BTN_SAVE)) < 0.01);
  /* the FPS readout: in the second row while the status text is long; left of SAVE in the strip where there is room */
  CHECK(fabs((K->rFps.y0 + K->rFps.y1) * 0.5 - K->fRow2Y) < 1e-9);
  IOSTouchCore_Settings()->bShowFps = 0;
  OpenTrayAt();
  Tap(BTN_TRAY_FPS);
  CHECK(K->bShowFps && !K->bFpsInStrip && K->rFps.y0 > BY(BTN_SAVE));
  _hud.afTopText[2] = (float)(400.0 / 1038.0); /* a shorter status text */
  SetHud();
  CHECK(K->bFpsInStrip && fabs(K->rFps.x1 - (BX(BTN_SAVE) - 25 - IOSTOUCH_ROW_GAP)) < 1e-9
        && fabs((K->rFps.y0 + K->rFps.y1) * 0.5 - BY(BTN_SAVE)) < 1e-9 && BX(BTN_CHEAT) < K->rFps.x0
        && BX(BTN_CAMERA) - K->aButtons[BTN_CAMERA].r >= 400 * 844 / 1038.0);
  /* no gap: NEXT/PREV left of SAVE */
  Screen(844, 390, 47, 0, 47, 21, 0);
  CHECK(BX(BTN_PREVWPN) < BX(BTN_SAVE) && BX(BTN_PREVWPN) > BX(BTN_NEXTWPN));
  CHECK(BX(BTN_NEXTWPN) - 19 >= 640 * 844 / 1038.0 || BY(BTN_NEXTWPN) > BY(BTN_SAVE));
}

/* ------------------------------------------------------------ the full-screen top row, every combination */
static double EdgeL(int b) { return b < 0 ? K->rFps.x0 : BX(b) - K->aButtons[b].hw; }
static double EdgeR(int b) { return b < 0 ? K->rFps.x1 : BX(b) + K->aButtons[b].hw; }

static void TopRowCombos(const char *name, double W, double H, double inL, double inR, double inB, double gWant)
{
  _bFull = 1;
  Screen(W, H, inL, 0, inR, inB, 0);
  int bad = 0;
  double gMin = 1e9;
  for (int m = 0; m < 32; m++) {
    const int bCheat = m & 1, bCam = (m >> 1) & 1, bFollow = (m >> 2) & 1, bSlow = (m >> 3) & 1, bFps = (m >> 4) & 1;
    IOSTouchCore_Settings()->bShowCheat = bCheat;
    IOSTouchCore_Settings()->bShowCamera = bCam;
    IOSTouchCore_Settings()->bShowFps = bFps;
    _hud.bFollowing = bFollow;
    _hud.iSlowMo = bSlow ? 4 : 0;
    _hud.bCheatOn = bCheat;
    IOSTouchCore_SetHud(&_hud);
    IOSTouchCore_Layout(W, H, inL, 0, inR, inB);
    const double g = K->fTopRowGap;
    gMin = fmin(gMin, g);
    /* the groups as they should be, in order from their corners (-1: the FPS readout) */
    int aL[5], nL = 0, aR[5], nR = 0;
    aL[nL++] = BTN_NEXTWPN; aL[nL++] = BTN_PREVWPN;
    if (bCheat) aL[nL++] = BTN_CHEAT;
    if (bFollow) aL[nL++] = BTN_FOLLOWNEXT;
    if (bSlow) aL[nL++] = BTN_SLOWMO;
    aR[nR++] = BTN_MENU; aR[nR++] = BTN_LOAD; aR[nR++] = BTN_SAVE;
    if (bFps) aR[nR++] = -1;
    if (bCam) aR[nR++] = BTN_CAMERA;
    if (fabs(EdgeL(aL[0]) - (inL > 8 ? inL : 8) - 2) > 1e-9 || fabs(EdgeR(aR[0]) - (W - (inR > 8 ? inR : 8)) + 2) > 1e-9) bad = 1;
    for (int i = 1; i < nL; i++) if (fabs(EdgeL(aL[i]) - EdgeR(aL[i - 1]) - g) > 1e-9) bad = 1;
    for (int i = 1; i < nR; i++) if (fabs(EdgeL(aR[i - 1]) - EdgeR(aR[i]) - g) > 1e-9) bad = 1;
    if (EdgeL(aR[nR - 1]) - EdgeR(aL[nL - 1]) < g - 1e-9) bad = 1;          /* (the groups never meet) */
    for (int i = 0; i < nL; i++) if (aL[i] >= 0 && (K->aButtons[aL[i]].r != 22 || BY(aL[i]) != BY(BTN_MENU) || !IOSTouchCore_IsShown(aL[i]))) bad = 1;
    for (int i = 0; i < nR; i++) if (aR[i] >= 0 && (K->aButtons[aR[i]].r != 22 || BY(aR[i]) != BY(BTN_MENU) || !IOSTouchCore_IsShown(aR[i]))) bad = 1;
    if (!bCheat && IOSTouchCore_IsShown(BTN_CHEAT)) bad = 1;
    if (!bCam && IOSTouchCore_IsShown(BTN_CAMERA)) bad = 1;
    /* the status text's space: centred on the screen, clear of all of them */
    IOSTouchRect rt, r2;
    IOSTouchCore_StatusRects(&rt, &r2);
    if (fabs((r2.x0 + r2.x1) * 0.5 - W * 0.5) > 1e-9) bad = 1;
    if (rt.bValid) {
      if (fabs((rt.x0 + rt.x1) * 0.5 - W * 0.5) > 1e-9) bad = 1;
      for (int i = 0; i < nL; i++) if (EdgeR(aL[i]) > rt.x0 - 8 + 1e-9 && EdgeL(aL[i]) < rt.x1) bad = 1;
      for (int i = 0; i < nR; i++) if (EdgeL(aR[i]) < rt.x1 + 8 - 1e-9 && EdgeR(aR[i]) > rt.x0) bad = 1;
    }
    if (bad) {
      printf("  %s: top row wrong with cheat %d camera %d follow %d slow %d fps %d (gap %.1f)\n", name, bCheat, bCam, bFollow, bSlow, bFps, g);
      break;
    }
    if (gWant > 0 && fabs(g - gWant) > 1e-9) {
      printf("  %s: gap %.1f, not %.0f\n", name, g, gWant);
      bad = 1;
      break;
    }
  }
  CHECK(!bad && gMin >= IOSTOUCH_TOPROW_GAP_MIN);
  if (!bad) printf("  top row %s: all 32 combinations ok (gap %.0f..22)\n", name, gMin);
  IOSTouchCore_Settings()->bShowCheat = IOSTouchCore_Settings()->bShowFps = 0;
  IOSTouchCore_Settings()->bShowCamera = 1;
  _bFull = 0;
}

static void TestTopRow(void)
{
  printf("full-screen top row\n");
  TopRowCombos("iPhone SE", 568, 320, 0, 0, 0, 0);
  TopRowCombos("iPhone 14", 844, 390, 47, 47, 21, 22);
  TopRowCombos("iPhone 16 Pro Max", 956, 440, 62, 62, 21, 22);
}

/* ------------------------------------------------------------ stick, aim */
static void TestStick(void)
{
  printf("stick and aim\n");
  Phone14();
  CHECK(!K->bStickActive);
  Down(1, 150, 250);
  CHECK(K->bStickActive && K->fStickOX == 150 && K->fStickOY == 250);
  Move(1, 150, 250 - 21); /* 35% up: walk (run starts at 60%) */
  IOSTouchInput in = Read();
  CHECK(in.fMoveY > 0.3f && in.fMoveY < 0.6f && fabsf(in.fMoveX) < 1e-4f && !(in.ulButtons & IOSTOUCH_RUN));
  Move(1, 150 + 80, 250); /* past the edge, right: run */
  in = Read();
  CHECK(fabsf(in.fMoveX - 1.0f) < 1e-4f && (in.ulButtons & IOSTOUCH_RUN));
  Move(1, 150, 250 + 52); /* 87% down: run */
  in = Read();
  CHECK(in.fMoveY < -0.99f && (in.ulButtons & IOSTOUCH_RUN));
  Move(1, 153, 252); /* dead zone */
  in = Read();
  CHECK(in.fMoveX == 0 && in.fMoveY == 0);
  Up(1, 153, 252);
  CHECK(!K->bStickActive);

  /* aiming: a drag right of the stick zone, not on a button */
  Down(2, 500, 200);
  Move(2, 505, 200); /* first 8 pt: nothing */
  in = Read();
  CHECK(!in.bAim);
  Move(2, 530, 170);
  in = Read();
  CHECK(in.bAim && fabsf(in.fAimX * in.fAimX + in.fAimY * in.fAimY - 1.0f) < 1e-4f);
  CHECK(fabsf(in.fAimX - 0.7071f) < 1e-3f && fabsf(in.fAimY - 0.7071f) < 1e-3f); /* up-right (up +) */
  Frames(0.5); /* held still: stays */
  in = Read();
  CHECK(in.bAim && fabsf(in.fAimX - 0.7071f) < 1e-3f);
  Move(2, 530 - 200, 170); /* turned back left: the leash answers at once */
  in = Read();
  CHECK(in.fAimX < -0.95f);
  Up(2, 330, 170);
  in = Read();
  CHECK(!in.bAim);

  /* FIRE: held fires, a drag on it aims */
  Down(3, BX(BTN_FIRE), BY(BTN_FIRE));
  in = Read();
  CHECK(in.ulButtons & IOSTOUCH_FIRE);
  Move(3, BX(BTN_FIRE), BY(BTN_FIRE) - 20);
  in = Read();
  CHECK((in.ulButtons & IOSTOUCH_FIRE) && in.bAim && in.fAimY > 0.99f);
  Up(3, BX(BTN_FIRE), BY(BTN_FIRE) - 20);
  in = Read();
  CHECK(!(in.ulButtons & IOSTOUCH_FIRE));
  /* a FIRE tap quicker than a read: one read's press */
  Tap(BTN_FIRE);
  in = Read();
  CHECK(in.ulButtons & IOSTOUCH_FIRE);
  in = Read();
  CHECK(!(in.ulButtons & IOSTOUCH_FIRE));
  /* DUCK, EXECUTE */
  Down(4, BX(BTN_DUCK) + 30, BY(BTN_DUCK)); /* within the slop */
  Down(5, BX(BTN_EXECUTE), BY(BTN_EXECUTE));
  in = Read();
  CHECK((in.ulButtons & IOSTOUCH_DUCK) && (in.ulButtons & IOSTOUCH_EXECUTE));
  Up(4, 0, 0); Up(5, 0, 0);
  /* a second finger on the left while the stick is down aims */
  Down(1, 100, 300);
  Down(2, 200, 300);
  Move(2, 200, 330);
  in = Read();
  CHECK(K->bStickActive && in.bAim && in.fAimY < -0.99f);
  Up(1, 0, 0); Up(2, 0, 0);
  /* a touch on the strip but off the pills does nothing */
  Down(1, 100, 10);
  Move(1, 160, 60);
  in = Read();
  CHECK(!K->bStickActive && !in.bAim);
  Up(1, 0, 0);
}

/* ------------------------------------------------------------ SAVE, LOAD, MENU */
static void TestSaveLoad(void)
{
  printf("save, load, menu\n");
  for (int k = 0; k < 2; k++) {
    const int b = k ? BTN_LOAD : BTN_SAVE;
    const unsigned int screen = k ? IOSTOUCH_REQ_LOADSCREEN : IOSTOUCH_REQ_SAVESCREEN;
    const unsigned int quick = k ? IOSTOUCH_REQ_QUICKLOAD : IOSTOUCH_REQ_QUICKSAVE;
    Phone14();
    /* tap */
    Down(0, BX(b), BY(b));
    unsigned int ul = Frames(0.1);
    Up(0, BX(b), BY(b));
    ul |= Frame(1.0 / 60);
    CHECK(ul == screen);
    /* held: once, with the finger down */
    Down(0, BX(b), BY(b));
    ul = Frames(0.2);
    CHECK(ul == 0 && (k ? K->fLoadRing : K->fSaveRing) > 0.5f);
    ul = Frames(0.2);
    CHECK(ul == quick);
    ul = Frames(0.5);
    Up(0, BX(b), BY(b));
    ul |= Frames(0.1);
    CHECK(ul == 0);
    /* slid off: nothing, even back on */
    Down(0, BX(b), BY(b));
    Frames(0.1);
    Move(0, BX(b), BY(b) + 40);
    Move(0, BX(b), BY(b));
    ul = Frames(0.5);
    Up(0, BX(b), BY(b));
    ul |= Frames(0.1);
    CHECK(ul == 0);
  }
  /* both held: one, SAVE */
  Phone14();
  Down(0, BX(BTN_LOAD), BY(BTN_LOAD));
  Down(1, BX(BTN_SAVE), BY(BTN_SAVE));
  unsigned int ul = Frames(0.5);
  CHECK(ul == IOSTOUCH_REQ_QUICKSAVE);
  Up(0, 0, 0); Up(1, 0, 0);

  /* MENU tap / hold */
  Phone14();
  Down(0, BX(BTN_MENU), BY(BTN_MENU));
  ul = Frames(0.2);
  Up(0, BX(BTN_MENU), BY(BTN_MENU));
  ul |= Frame(0.016);
  CHECK(ul == IOSTOUCH_REQ_MENU && !K->bTrayOpen);
  Down(0, BX(BTN_MENU), BY(BTN_MENU));
  ul = Frames(0.6);
  CHECK(K->bTrayOpen);
  Up(0, BX(BTN_MENU), BY(BTN_MENU));
  ul |= Frame(0.016);
  CHECK(ul == 0 && K->bTrayOpen);
  /* a touch elsewhere closes it */
  Down(0, 400, 200);
  CHECK(!K->bTrayOpen);
  Up(0, 400, 200);
  /* MENU slid off before the hold: neither */
  Down(0, BX(BTN_MENU), BY(BTN_MENU));
  Move(0, BX(BTN_MENU) - 60, BY(BTN_MENU) + 60);
  ul = Frames(0.6);
  Up(0, BX(BTN_MENU) - 60, BY(BTN_MENU) + 60);
  ul |= Frame(0.016);
  CHECK(ul == 0 && !K->bTrayOpen);
  /* hold MENU and slide onto a tray button: it acts */
  Down(0, BX(BTN_MENU), BY(BTN_MENU));
  Frames(0.6);
  Move(0, BX(BTN_TRAY_FPS), BY(BTN_TRAY_FPS));
  CHECK(IOSTouchCore_IsLit(BTN_TRAY_FPS));
  Up(0, BX(BTN_TRAY_FPS), BY(BTN_TRAY_FPS));
  CHECK(K->bShowFps && !K->bTrayOpen);
}

/* ------------------------------------------------------------ the tray */
static void OpenTray(void)
{
  Down(9, BX(BTN_MENU), BY(BTN_MENU));
  Frames(0.6);
  Up(9, BX(BTN_MENU), BY(BTN_MENU));
}

static void OpenTrayAt(void) { OpenTray(); }

static void TestTray(void)
{
  printf("tray\n");
  Phone14();
  OpenTray();
  CHECK(K->bTrayOpen);
  CHECK(IOSTouchCore_IsShown(BTN_TRAY_FPS) && IOSTouchCore_IsShown(BTN_TRAY_KEYS) && IOSTouchCore_IsShown(BTN_TRAY_CHEAT)
        && IOSTouchCore_IsShown(BTN_TRAY_SLOW) && IOSTouchCore_IsShown(BTN_TRAY_CAMERA));
  IOSTouchCore_SetLevels((const char *const[]){ "Hwy", "Mine", "Ctrl" }, 3);
  CHECK(!IOSTouchCore_IsShown(BTN_TRAY_LEVEL) && !IOSTouchCore_IsShown(BTN_TRAY_FOLLOW));
  /* the order, left to right, 50 apart */
  /* CHEAT, SLOW, CAMERA, FPS, keyboard (under MENU), 50 apart */
  CHECK(fabs(BX(BTN_TRAY_SLOW) - BX(BTN_TRAY_CHEAT) - 50) < 1e-6 && fabs(BX(BTN_TRAY_CAMERA) - BX(BTN_TRAY_SLOW) - 50) < 1e-6
        && fabs(BX(BTN_TRAY_FPS) - BX(BTN_TRAY_CAMERA) - 50) < 1e-6 && fabs(BX(BTN_TRAY_KEYS) - BX(BTN_TRAY_FPS) - 50) < 1e-6);
  CHECK(fabs(BX(BTN_TRAY_KEYS) - BX(BTN_MENU)) < 1e-6);
  const double xCheat = BX(BTN_TRAY_CHEAT);
  /* SLOW: asks for the next speed, stays open; says the speed, gold while on */
  char buf[32];
  Tap(BTN_TRAY_SLOW);
  CHECK(Frame(0.016) == IOSTOUCH_REQ_SLOWMO && K->bTrayOpen && !IOSTouchCore_IsOn(BTN_TRAY_SLOW));
  CHECK(strcmp(IOSTouchCore_Label(BTN_TRAY_SLOW, buf, sizeof(buf)), "SLOW") == 0);
  _hud.iSlowMo = 2;
  SetHud();
  CHECK(IOSTouchCore_IsOn(BTN_TRAY_SLOW) && strcmp(IOSTouchCore_Label(BTN_TRAY_SLOW, buf, sizeof(buf)), "SLOW \xC2\xBD") == 0);
  _hud.iSlowMo = 4;
  SetHud();
  CHECK(strcmp(IOSTouchCore_Label(BTN_TRAY_SLOW, buf, sizeof(buf)), "SLOW \xC2\xBC") == 0);
  _hud.iSlowMo = 0;
  SetHud();
  /* CHEAT: shows the icon and asks the cheat on */
  Tap(BTN_TRAY_CHEAT);
  CHECK(K->bShowCheat && (Frame(0.016) & IOSTOUCH_REQ_CHEAT));
  CHECK(!IOSTouchCore_IsOn(BTN_TRAY_CHEAT)); /* (gold only once the game has it on) */
  _hud.bCheatOn = 1;
  SetHud();
  CHECK(IOSTouchCore_IsOn(BTN_TRAY_CHEAT));
  CHECK(IOSTouchCore_IsShown(BTN_TRAY_LEVEL) && IOSTouchCore_IsShown(BTN_TRAY_FOLLOW));
  /* LEVEL and FOLLOW come in at the left end: the rest stay put */
  CHECK(fabs(BX(BTN_TRAY_FOLLOW) - BX(BTN_TRAY_LEVEL) - 50) < 1e-6 && fabs(BX(BTN_TRAY_CHEAT) - BX(BTN_TRAY_FOLLOW) - 50) < 1e-6
        && BX(BTN_TRAY_CHEAT) == xCheat && fabs(BX(BTN_TRAY_KEYS) - BX(BTN_MENU)) < 1e-6);
  CHECK(IOSTouchCore_IsShown(BTN_CHEAT));
  /* the cheat icon toggles */
  Down(0, 400, 200); Up(0, 400, 200); /* (closes the tray) */
  Tap(BTN_CHEAT);
  CHECK(Frame(0.016) == IOSTOUCH_REQ_CHEAT);
  /* LEVEL opens the list; the game holds still; a pick comes once */
  OpenTray();
  Tap(BTN_TRAY_LEVEL);
  CHECK(K->bLevelListOpen && !K->bTrayOpen && IOSTouchCore_HoldsGame());
  CHECK((Frame(0.016) & IOSTOUCH_REQ_LEVEL) && strcmp(IOSTouchCore_LevelName(1), "Mine") == 0);
  IOSTouchCore_PickLevel(2);
  CHECK(!K->bLevelListOpen && IOSTouchCore_TakeLevelPick() == 2 && IOSTouchCore_TakeLevelPick() == -1);
  OpenTray();
  Tap(BTN_TRAY_LEVEL);
  Down(0, 20, 300); /* a touch off the list closes it */
  CHECK(!K->bLevelListOpen && IOSTouchCore_TakeLevelPick() == -1);
  Up(0, 20, 300);
  /* FOLLOW, keyboard: ask, close */
  OpenTray();
  Tap(BTN_TRAY_FOLLOW);
  CHECK(!K->bTrayOpen && Frame(0.016) == IOSTOUCH_REQ_FOLLOW);
  OpenTray();
  Tap(BTN_TRAY_KEYS);
  CHECK(Frame(0.016) == IOSTOUCH_REQ_KEYBOARD);
  /* cheat off: LEVEL and FOLLOW go */
  _hud.bCheatOn = 0;
  SetHud();
  OpenTray();
  CHECK(!IOSTouchCore_IsShown(BTN_TRAY_LEVEL) && !IOSTouchCore_IsShown(BTN_TRAY_FOLLOW));
  /* CHEAT tapped again hides the icon (the cheat is off: no request) */
  Tap(BTN_TRAY_CHEAT);
  CHECK(!K->bShowCheat && Frame(0.016) == 0);
  /* CAMERA: tap hides / shows the icon (held: nothing more) */
  CHECK(K->bShowCamera);
  Tap(BTN_TRAY_CAMERA);
  CHECK(!K->bShowCamera && !IOSTouchCore_IsShown(BTN_CAMERA) && K->bTrayOpen);
  Tap(BTN_TRAY_CAMERA);
  CHECK(K->bShowCamera);
  Down(0, BX(BTN_TRAY_CAMERA), BY(BTN_TRAY_CAMERA));
  Frames(0.8);
  Up(0, BX(BTN_TRAY_CAMERA), BY(BTN_TRAY_CAMERA));
  CHECK(!K->bShowCamera && K->bTrayOpen && Frame(0.016) == 0);
}

/* ------------------------------------------------------------ the wheel */
static IOSTouchWeapons Weapons(void)
{
  static const char *names[] = { "Machine Gun", "Shotgun", "Spray Cannon", "Grenades", "Missiles", "Heatseekers",
                                 "Molotovs", "Napalm", "Flamer", "Mines", "Deathwad" };
  IOSTouchWeapons w;
  memset(&w, 0, sizeof(w));
  w.bValid = 1;
  w.ctWeapons = 11;
  for (int i = 0; i < 11; i++) {
    w.aiType[i] = 10 + i;
    w.apszName[i] = names[i];
    w.abOwned[i] = i < 8;
    w.aiAmmo[i] = (i == 3) ? 0 : 20 + i;
  }
  w.aiAmmo[0] = -1;
  w.iCurrent = 0;
  return w;
}

/* A point dist out from (x,y) at deg (counter-clockwise from right, screen up +) */
static double PX(double x, double deg, double dist) { return x + dist * cos(deg * M_PI / 180.0); }
static double PY(double y, double deg, double dist) { return y - dist * sin(deg * M_PI / 180.0); }

static void TestWheel(void)
{
  printf("weapon wheel\n");
  Phone14();
  IOSTouchWeapons w = Weapons();
  IOSTouchCore_SetWeapons(&w);
  const double nx = BX(BTN_NEXTWPN), ny = BY(BTN_NEXTWPN);
  /* quick tap: one NEXT */
  Down(0, nx, ny);
  Frames(0.1);
  Up(0, nx, ny);
  IOSTouchInput in = Read();
  CHECK((in.ulButtons & IOSTOUCH_NEXTWEAPON) && !K->bWheelOpen);
  in = Read();
  CHECK(!(in.ulButtons & IOSTOUCH_NEXTWEAPON));
  /* PREV tap */
  Tap(BTN_PREVWPN);
  in = Read();
  CHECK(in.ulButtons & IOSTOUCH_PREVWEAPON);
  /* slid 10 pt: opens at once; the game holds still */
  Down(0, nx, ny);
  Move(0, nx + 6, ny + 6);
  CHECK(!K->bWheelOpen);
  Move(0, nx + 8, ny + 8);
  CHECK(K->bWheelOpen && IOSTouchCore_HoldsGame());
  /* slide toward slice 2 (Spray Cannon) and lift: picked */
  const double d2 = IOSTouchCore_WheelSliceDeg(2);
  Move(0, PX(nx, d2, 40), PY(ny, d2, 40));
  int bCancel = 0;
  CHECK(IOSTouchCore_WheelPointed(&bCancel) == 2 && !bCancel && K->ctWheelTicks > 0);
  const char *title, *hint;
  int count;
  IOSTouchCore_WheelMiddle(&title, &count, &hint);
  CHECK(strcmp(title, "Spray Cannon") == 0 && count == 22 && strcmp(hint, "lift to select") == 0);
  CHECK(strcmp(IOSTouchCore_WheelSliceName(10), "Deathwad") == 0 && strcmp(IOSTouchCore_WheelSliceName(11), "") == 0);
  Up(0, PX(nx, d2, 40), PY(ny, d2, 40));
  CHECK(!K->bWheelOpen && !IOSTouchCore_HoldsGame());
  in = Read();
  CHECK(in.iSelectWeapon == 1 + 12 && !(in.ulButtons & (IOSTOUCH_NEXTWEAPON | IOSTOUCH_PREVWEAPON)));
  in = Read();
  CHECK(in.iSelectWeapon == 0);
  /* the gap at the bottom cancels */
  Down(0, nx, ny);
  Move(0, nx, ny + 12);
  Move(0, nx, ny + 50);
  CHECK(IOSTouchCore_WheelPointed(&bCancel) == IOSTOUCH_WHEEL_CANCEL && bCancel);
  IOSTouchCore_WheelMiddle(&title, &count, &hint);
  CHECK(strcmp(hint, "lift to cancel") == 0);
  Up(0, nx, ny + 50);
  in = Read();
  CHECK(!K->bWheelOpen && in.iSelectWeapon == 0);
  /* slide back into the dead zone and lift: open to tap; tap a slice */
  Down(0, nx, ny);
  Move(0, nx + 11, ny); /* (open, still in the 14 pt dead zone) */
  Up(0, nx + 11, ny);
  CHECK(K->bWheelOpen && K->bWheelTapMode);
  const double d5 = IOSTouchCore_WheelSliceDeg(5);
  const double rr = (K->fWheelR0 + K->fWheelR1) * 0.5;
  Down(1, PX(K->fWheelCX, d5, rr), PY(K->fWheelCY, d5, rr));
  Up(1, PX(K->fWheelCX, d5, rr), PY(K->fWheelCY, d5, rr));
  in = Read();
  CHECK(!K->bWheelOpen && in.iSelectWeapon == 1 + 15);
  /* hold 0.3 s: open to tap; a no-ammo slice stays open, not found too, then elsewhere closes */
  Down(0, nx, ny);
  Frames(0.2);
  CHECK(!K->bWheelOpen && K->afWheelRing[0] > 0.3f);
  Frames(0.2);
  CHECK(K->bWheelOpen && K->bWheelTapMode);
  Up(0, nx, ny);
  CHECK(K->bWheelOpen);
  const double d3 = IOSTouchCore_WheelSliceDeg(3); /* grenades: no ammo */
  Down(1, PX(K->fWheelCX, d3, rr), PY(K->fWheelCY, d3, rr));
  IOSTouchCore_WheelMiddle(&title, &count, &hint);
  CHECK(strcmp(hint, "no ammo") == 0 && count == 0);
  Up(1, PX(K->fWheelCX, d3, rr), PY(K->fWheelCY, d3, rr));
  CHECK(K->bWheelOpen);
  CHECK(IOSTouchCore_WheelSliceState(9) == WSTATE_NOTFOUND && IOSTouchCore_WheelSliceCount(9) == -1);
  Down(1, K->fWheelCX, K->fWheelCY); /* the middle */
  Up(1, K->fWheelCX, K->fWheelCY);
  in = Read();
  CHECK(!K->bWheelOpen && in.iSelectWeapon == 0);
  /* slices: 11 of 29 degrees, the gap at the bottom */
  CHECK(fabs(IOSTouchCore_WheelSliceWidth() - 29.0) < 1e-9);
  w.ctWeapons = 16;
  IOSTouchCore_SetWeapons(&w);
  CHECK(fabs(IOSTouchCore_WheelSliceWidth() - 322.0 / 16) < 1e-9);
  /* the wheel closes, picking nothing, when the dude can't change weapon */
  Down(0, nx, ny);
  Move(0, nx + 20, ny);
  CHECK(K->bWheelOpen);
  w.bValid = 0;
  IOSTouchCore_SetWeapons(&w);
  CHECK(!K->bWheelOpen);
  Up(0, nx + 20, ny);
  in = Read();
  CHECK(in.iSelectWeapon == 0);
  /* the opener taken away by iOS: open to tap */
  w.bValid = 1;
  IOSTouchCore_SetWeapons(&w);
  Down(0, nx, ny);
  Move(0, nx + 20, ny);
  Cancel(0, nx + 20, ny);
  CHECK(K->bWheelOpen && K->bWheelTapMode);
  Down(1, K->fWheelCX, K->fWheelCY); Up(1, K->fWheelCX, K->fWheelCY);
  CHECK(!K->bWheelOpen);
  /* a mode change closes it */
  w.bValid = 1;
  IOSTouchCore_SetWeapons(&w);
  Down(0, nx, ny);
  Move(0, nx + 20, ny);
  IOSTouchCore_SetMode(IOSTOUCH_HIDDEN, _now);
  CHECK(!K->bWheelOpen);
}

/* ------------------------------------------------------------ the camera */
static void TestCamera(void)
{
  printf("camera\n");
  Phone14();
  const double cx = BX(BTN_CAMERA), cy = BY(BTN_CAMERA);
  CHECK(IOSTouchCore_IsShown(BTN_CAMERA));
  /* tap: a photo */
  Down(0, cx, cy);
  unsigned int ul = Frames(0.1);
  Up(0, cx, cy);
  ul |= Frame(0.016);
  CHECK(ul == IOSTOUCH_REQ_PHOTO);
  /* a long press: still one photo (as it lifts), nothing while held; the game never holds still for it */
  Down(0, cx, cy);
  ul = Frames(2.0);
  CHECK(ul == 0 && !IOSTouchCore_HoldsGame() && IOSTouchCore_IsShown(BTN_FIRE));
  Move(0, cx + 4, cy + 3); /* (a little wobble is still a tap) */
  Up(0, cx + 4, cy + 3);
  ul = Frame(0.016) | Frames(0.5);
  CHECK(ul == IOSTOUCH_REQ_PHOTO);
  /* slid 10 pt and back: nothing */
  Down(0, cx, cy);
  Move(0, cx + 12, cy);
  Move(0, cx, cy);
  Up(0, cx, cy);
  CHECK(Frames(0.1) == 0);
  /* iOS took the touch away: nothing */
  Down(0, cx, cy);
  Cancel(0, cx, cy);
  CHECK(Frames(0.1) == 0);
  /* slides (any way) do nothing: no burst, no zoom-out, no slow motion */
  const double adx[4] = { 15, -15, 0, 0 }, ady[4] = { 0, 0, 40, -20 };
  for (int k = 0; k < 4; k++) {
    Down(0, cx, cy);
    Move(0, cx + adx[k], cy + ady[k]);
    ul = Frames(0.8);
    Up(0, cx + adx[k], cy + ady[k]);
    ul |= Frame(0.016);
    CHECK(ul == 0 && !IOSTouchCore_HoldsGame());
  }
  /* the slow-motion label shows while on; a tap on it asks for the next speed too */
  char buf[16];
  CHECK(!IOSTouchCore_IsShown(BTN_SLOWMO));
  _hud.iSlowMo = 2;
  SetHud();
  CHECK(IOSTouchCore_IsShown(BTN_SLOWMO) && strcmp(IOSTouchCore_Label(BTN_SLOWMO, buf, sizeof(buf)), "\xC2\xBD\xC3\x97") == 0);
  Tap(BTN_SLOWMO);
  CHECK(Frame(0.016) == IOSTOUCH_REQ_SLOWMO);
  /* the follow arrow while following */
  CHECK(!IOSTouchCore_IsShown(BTN_FOLLOWNEXT));
  _hud.bFollowing = 1;
  SetHud();
  CHECK(IOSTouchCore_IsShown(BTN_FOLLOWNEXT));
  Tap(BTN_FOLLOWNEXT);
  CHECK(Frame(0.016) == IOSTOUCH_REQ_FOLLOWNEXT);
  /* a camera tap on a mode change's far side asks nothing more */
  Down(0, cx, cy);
  IOSTouchCore_SetMode(IOSTOUCH_HIDDEN, _now);
  Up(0, cx, cy);
  CHECK(Frames(0.1) == 0);
  /* the camera hidden: nothing there */
  Phone14();
  IOSTouchCore_Settings()->bShowCamera = 0;
  CHECK(IOSTouchCore_ButtonAt(cx, cy) != BTN_CAMERA);
}

int main(void)
{
  TestLayout();
  TestTopRow();
  TestStick();
  TestSaveLoad();
  TestTray();
  TestWheel();
  TestCamera();
  printf("%d passed, %d failed\n", _ctPass, _ctFail);
  return _ctFail ? 1 : 0;
}
