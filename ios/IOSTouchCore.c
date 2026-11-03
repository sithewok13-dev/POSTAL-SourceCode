/* POSTAL for iOS: the touch controls' logic (plain C). See IOSTouchCore.h. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "IOSTouchCore.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

#define MAX_TOUCHES 10

/* The buttons as made (labels, kinds, sizes); the layout sets where */
static const IOSTouchButton _aButtonDefs[BTN_COUNT] = {
  [BTN_FIRE]        = { "FIRE",        KIND_HOLD,      IOSTOUCH_FIRE,          0, IOSTOUCH_FIRE_RADIUS,    0, 1 },
  [BTN_DUCK]        = { "DUCK",        KIND_HOLD,      IOSTOUCH_DUCK,          0, IOSTOUCH_DUCK_RADIUS,    0, 1 },
  [BTN_EXECUTE]     = { "EXECUTE",     KIND_HOLD,      IOSTOUCH_EXECUTE,       0, IOSTOUCH_EXECUTE_RADIUS, 0, 1 },
  [BTN_NEXTWPN]     = { "NEXT\nWPN",   KIND_WHEEL,     IOSTOUCH_NEXTWEAPON,    1, 12, 19, 0 },
  [BTN_PREVWPN]     = { "PREV\nWPN",   KIND_WHEEL,     IOSTOUCH_PREVWEAPON,    1, 12, 19, 0 },
  [BTN_SAVE]        = { "SAVE",        KIND_HOLDSAVE,  IOSTOUCH_REQ_QUICKSAVE, 1, 12, 25, 0 },
  [BTN_LOAD]        = { "LOAD",        KIND_HOLDSAVE,  IOSTOUCH_REQ_QUICKLOAD, 1, 12, 25, 0 },
  [BTN_MENU]        = { "MENU",        KIND_MENU,      IOSTOUCH_REQ_MENU,      1, 12, 25, 0 },
  [BTN_CHEAT]       = { "",            KIND_TAP,       IOSTOUCH_REQ_CHEAT,     0, 12, 12, 0 },
  [BTN_CAMERA]      = { "",            KIND_CAMERA,    0,                      0, 12, 12, 0 },
  [BTN_FOLLOWNEXT]  = { "",            KIND_TAP,       IOSTOUCH_REQ_FOLLOWNEXT, 0, 12, 12, 0 },
  [BTN_SLOWMO]      = { "",            KIND_TAP,       IOSTOUCH_REQ_SLOWMO,    1, 12, 15, 0 },
  [BTN_BACK]        = { "BACK",        KIND_TAP,       IOSTOUCH_REQ_BACK,      0, IOSTOUCH_TOPROW_RADIUS, 0, 0 },
  [BTN_TRAY_LEVEL]  = { "LEVEL",       KIND_TRAY,      IOSTOUCH_REQ_LEVEL,     0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_FOLLOW] = { "FOLLOW",      KIND_TRAY,      IOSTOUCH_REQ_FOLLOW,    0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_CHEAT]  = { "CHEAT",       KIND_TRAY,      IOSTOUCH_REQ_CHEAT,     0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_SLOW]   = { "SLOW",        KIND_TRAY,      IOSTOUCH_REQ_SLOWMO,    0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_CAMERA] = { "",            KIND_TRAY,      0,                      0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_FPS]    = { "FPS",         KIND_TRAY,      0,                      0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_KEYS]   = { "",            KIND_TRAY,      IOSTOUCH_REQ_KEYBOARD,  0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
};

/* ------------------------------------------------------------ state */
typedef enum {
  ROLE_NONE = 0,
  ROLE_STICK,
  ROLE_AIM,     /* the right aim stick (floating, where the thumb landed) */
  ROLE_BUTTON,
  ROLE_IGNORED, /* does nothing until it lifts */
  ROLE_DEADTAP, /* dead: a tap on the screen restarts the level (neither moves nor aims) */
  ROLE_WHEEL,   /* on the open wheel (or the NEXT/PREV press that opened it) */
} Role;

typedef struct {
  const void *touch;   /* NULL: free */
  Role role;
  int button;
  double x, y;         /* now */
  double xDown, yDown; /* where it landed */
  double tDown;        /* when (the touch's clock) */
  unsigned int uSeq;
  int bFired;          /* the hold is over (fired, slid off, or no tap any more) */
  int bSlid;           /* camera: moved too far for a tap */
  int trayButton;      /* MENU held open: the tray button under it, or -1 */
  /* aiming (ROLE_AIM, or a KIND_HOLD button): the centre it is measured from (where it landed), whether
     it is outside IOSTOUCH_AIM_DEAD now (it aims), whether it ever was (its place among the aiming
     fingers), its direction (unit, screen terms, up +); the aim stick: the push (0..1 of the radius)
     and whether it is pushed far enough to fire */
  double aimOX, aimOY;
  int bAimArmed, bAimEver;
  double aimX, aimY;
  double aimPush;
  int bAimFire;
  /* the wheel */
  int wheelSlot, bWheelOpener, bWheelHeld, bArmed;
  double wheelOX, wheelOY, wheelAim;
} Slot;

static IOSTouchCore C;
static int _bInit = 0;
static Slot _aSlots[MAX_TOUCHES];
static unsigned int _uSeq = 0;
static unsigned int _ulRequests = 0;
static double _tLastTick = 0.0;     /* the previous tick (holds are timed to it: every lift before it has come in) */
/* the game's input */
static unsigned int _ulSeen = 0;    /* held bits a read has seen since they were pressed */
static unsigned int _ulHeldLast = 0;
static unsigned char _aubPulse[8];  /* one-read presses waiting, per bit (taps quicker than a frame, NEXT/PREV taps) */
static unsigned int _ulPulseGap = 0;/* bits pulsed last read (a read off before the next) */
static int _iPick = 0;              /* the wheel's pick, waiting (1 + type) */

/* FPS */
static int _bFpsBase = 0;
static unsigned int _ulFpsFrames = 0;
static double _tFpsBase = 0.0;
/* levels */
static char **_ppszLevels = NULL;
static int _iLevelPick = -1;
/* the HUD rectangles the layout was made for (fractions) */
static float _afLaidBar[4], _afLaidText[4], _afLaidGap[4];
static int _bLaidFull = -1;
static double _inL, _inT, _inR, _inB;

static void Init(void)
{
  if (_bInit) return;
  _bInit = 1;
  memset(&C, 0, sizeof(C));
  memcpy(C.aButtons, _aButtonDefs, sizeof(C.aButtons));
  /* (a circle's half width is its radius: the tray's buttons are
     never sized by the layout, and the view sizes each button from hw x r) */
  for (int b = 0; b < BTN_COUNT; b++) {
    if (!C.aButtons[b].bPill) C.aButtons[b].hw = C.aButtons[b].r;
  }
  C.bShowCamera = 1;
  C.bShowCheat = 0;
  C.iFpsShown = -1;
  C.iMode = IOSTOUCH_HIDDEN;
  C.iWheelHot = -1;
  C.tMessage = -1e9;
}

const IOSTouchCore *IOSTouchCore_Get(void) { Init(); return &C; }
IOSTouchCore *IOSTouchCore_Settings(void) { Init(); return &C; }

/* ------------------------------------------------------------ geometry */
static double DistToSegment(double px, double py, double ax, double ay, double bx, double by)
{
  const double vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
  double t = len2 > 0 ? ((px - ax) * vx + (py - ay) * vy) / len2 : 0;
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return hypot(px - (ax + t * vx), py - (ay + t * vy));
}

/* How far (x,y) is outside the button's edge (negative: inside) */
static double ButtonDist(const IOSTouchButton *b, double x, double y)
{
  if (!b->bPill || b->hw <= b->r) return hypot(x - b->x, y - b->y) - b->r;
  const double d = b->hw - b->r;
  return DistToSegment(x, y, b->x - d, b->y, b->x + d, b->y) - b->r;
}

static IOSTouchRect FracRect(const float af[4], double W, double H)
{
  IOSTouchRect r = { 0, af[0] * W, af[1] * H, af[2] * W, af[3] * H };
  r.bValid = r.x1 > r.x0 && r.y1 > r.y0;
  return r;
}

static void SetPill(int b, double x, double y, double w, double h)
{
  C.aButtons[b].x = x;
  C.aButtons[b].y = y;
  C.aButtons[b].hw = w * 0.5;
  C.aButtons[b].r = h * 0.5;
}

static void SetCircle(int b, double x, double y, double r)
{
  C.aButtons[b].x = x;
  C.aButtons[b].y = y;
  C.aButtons[b].r = r;
  C.aButtons[b].hw = r;
}

static double ButtonW(int b) { return 2.0 * C.aButtons[b].hw; }

/* ------------------------------------------------------------ visibility */
static int LevelShows(void) { return C.hud.bCheatOn && C.ctLevels > 0; }
static int FollowShows(void) { return C.hud.bCheatOn; }

static int IsTray(int b) { return b >= BTN_TRAY_FIRST && b <= BTN_TRAY_LAST; }
static int IsTopRow(int b)
{
  return (b >= BTN_NEXTWPN && b <= BTN_MENU) || b == BTN_CHEAT || b == BTN_CAMERA || b == BTN_FOLLOWNEXT || b == BTN_SLOWMO;
}

int IOSTouchCore_IsShown(int b)
{
  Init();
  if (b < 0 || b >= BTN_COUNT) return 0;
  if (C.iMode == IOSTOUCH_MENU) return b == BTN_BACK; /* (the menus: BACK alone) */
  if (C.iMode != IOSTOUCH_GAMEPLAY || b == BTN_BACK) return 0;
  switch (b) {
  case BTN_CHEAT: return C.bShowCheat;
  case BTN_CAMERA: return C.bShowCamera;
  case BTN_FOLLOWNEXT: return C.hud.bFollowing;
  case BTN_SLOWMO: return C.hud.iSlowMo != 0;
  /* EXECUTE: only while it would do something -- or while a finger still holds it (the execution
     began and the victim died under it: it stays pressed until that finger lifts). Hidden, nothing
     finds it: a touch on its spot is the aim stick's. */
  case BTN_EXECUTE: return C.hud.bCanExecute || C.aButtonHeld[BTN_EXECUTE] > 0;
  case BTN_TRAY_LEVEL: return C.bTrayOpen && LevelShows();
  case BTN_TRAY_FOLLOW: return C.bTrayOpen && FollowShows();
  default: break;
  }
  if (b >= BTN_TRAY_FIRST && b <= BTN_TRAY_LAST) return C.bTrayOpen;
  return 1;
}

/* ------------------------------------------------------------ the tray's layout */
/* Its buttons (those that show) right-aligned under MENU, IOSTOUCH_TRAY_STEP
   apart, wrapping to a second row where one row would leave the screen; the
   version under it all */
static void PlaceTray(void)
{
  int aiShown[BTN_TRAY_LAST - BTN_TRAY_FIRST + 1], ct = 0;
  for (int b = BTN_TRAY_FIRST; b <= BTN_TRAY_LAST; b++) {
    if ((b == BTN_TRAY_LEVEL && !LevelShows()) || (b == BTN_TRAY_FOLLOW && !FollowShows())) {
      C.aButtons[b].x = C.aButtons[b].y = -1000; /* (hidden: nothing finds it) */
      continue;
    }
    aiShown[ct++] = b;
  }
  const double kx = fmin(C.aButtons[BTN_MENU].x, C.right - IOSTOUCH_TRAY_BACK);
  const double y0 = C.fTrayY;
  int perRow = (int)floor((kx - C.left - IOSTOUCH_TRAY_BACK) / IOSTOUCH_TRAY_STEP) + 1;
  if (perRow < 1) perRow = 1;
  C.ctTrayRows = ct > perRow ? 2 : 1;
  const int ctFirst = C.ctTrayRows == 2 ? (ct + 1) / 2 : ct; /* (two rows: as even as can be) */
  int ctFirstRow = ctFirst > perRow ? perRow : ctFirst;
  for (int row = 0; row < C.ctTrayRows; row++) {
    const int i0 = row ? ctFirstRow : 0, i1 = row ? ct : ctFirstRow;
    const double y = y0 + row * (2.0 * IOSTOUCH_TRAY_BACK);
    for (int i = i0; i < i1; i++) {
      C.aButtons[aiShown[i]].x = kx - (i1 - 1 - i) * IOSTOUCH_TRAY_STEP;
      C.aButtons[aiShown[i]].y = y;
    }
    IOSTouchRect *r = &C.arTrayBack[row];
    r->bValid = i1 > i0;
    r->x0 = kx - (i1 - 1 - i0) * IOSTOUCH_TRAY_STEP - IOSTOUCH_TRAY_BACK;
    r->x1 = kx + IOSTOUCH_TRAY_BACK;
    r->y0 = y - IOSTOUCH_TRAY_BACK;
    r->y1 = y + IOSTOUCH_TRAY_BACK;
  }
  if (C.ctTrayRows < 2) C.arTrayBack[1].bValid = 0;
  const double lastY1 = C.arTrayBack[C.ctTrayRows - 1].y1;
  /* the version: just below the tray */
  C.fVersionX = (C.arTrayBack[0].x0 + C.arTrayBack[0].x1) * 0.5;
  C.fVersionY = lastY1 + 4.0 + IOSTOUCH_VERSION_H * 0.5;
}

/* ------------------------------------------------------------ the layout */
/* The wheel round the middle of the safe area, 4 pt down, as big as fits (Sam's) */
static void WheelGeometry(void)
{
  const double top = fmax(_inT, 8.0), bottom = C.H - fmax(_inB, 8.0);
  C.fWheelCX = (C.left + C.right) * 0.5;
  C.fWheelCY = (top + bottom) * 0.5 + 4.0;
  C.fWheelR1 = fmin(IOSTOUCH_WHEEL_R1_MAX, (bottom - top) * 0.5 - IOSTOUCH_WHEEL_POP - 6.0);
  C.fWheelR0 = round(C.fWheelR1 * IOSTOUCH_WHEEL_R0_FRAC);
}

/* The game's own status strip (not full screen): pills in it, as tall as it less ~5 pt */
static void LayoutStrip(double W, double H)
{
  C.rStrip = FracRect(C.hud.afTopBar, W, H);
  if (!C.rStrip.bValid) C.rStrip = (IOSTouchRect){ 1, 0, 0, W, H * IOSTOUCH_STRIP_FRAC };
  const double stripH = C.rStrip.y1 - C.rStrip.y0;
  const double ph = fmin(fmax(stripH - 5.0, IOSTOUCH_PILL_MIN_H), IOSTOUCH_PILL_MAX_H);
  C.fPillH = ph;
  const double cy = fmax((C.rStrip.y0 + C.rStrip.y1) * 0.5, fmax(C.top, 1.0) + ph * 0.5);
  /* (a pill taller than the strip hangs below it) */
  const double row1Bottom = fmax(C.rStrip.y1, cy + ph * 0.5);
  C.fRow2Y = row1Bottom + IOSTOUCH_ROW_GAP + ph * 0.5;

  for (int b = BTN_NEXTWPN; b <= BTN_MENU; b++) C.aButtons[b].bPill = 1;
  C.aButtons[BTN_SLOWMO].bPill = 1;
  /* SAVE, LOAD, MENU at the right end */
  const double sw = IOSTOUCH_PILL_W, step = IOSTOUCH_PILL_W + IOSTOUCH_PILL_GAP;
  SetPill(BTN_MENU, C.right - 2.0 - sw * 0.5, cy, sw, ph);
  SetPill(BTN_LOAD, C.aButtons[BTN_MENU].x - step, cy, sw, ph);
  SetPill(BTN_SAVE, C.aButtons[BTN_LOAD].x - step, cy, sw, ph);

  /* NEXT WPN, PREV WPN: in the status text's gap if two fit (each as wide
     as fits, IOSTOUCH_WHEELPILL_MIN_W to _W, 6 pt apart), else left of SAVE */
  const IOSTouchRect rText = FracRect(C.hud.afTopText, W, H);
  const IOSTouchRect rGap = FracRect(C.hud.afTopGap, W, H);
  const double textEnd = rText.bValid ? rText.x1 : C.left;
  const double gapW = rGap.bValid ? rGap.x1 - rGap.x0 : 0.0;
  int bWheelInGap = 0;
  if (gapW >= 2.0 * IOSTOUCH_WHEELPILL_MIN_W + IOSTOUCH_WHEELPILL_GAP) {
    const double gw = fmin(IOSTOUCH_WHEELPILL_W, fmax(IOSTOUCH_WHEELPILL_MIN_W, floor((gapW - IOSTOUCH_WHEELPILL_GAP - 2.0) * 0.5 + 0.01)));
    const double half = (gw + IOSTOUCH_WHEELPILL_GAP) * 0.5, mid = (rGap.x0 + rGap.x1) * 0.5;
    if (mid - half - gw * 0.5 >= C.left && mid + half + gw * 0.5 <= C.aButtons[BTN_SAVE].x - sw * 0.5) {
      SetPill(BTN_NEXTWPN, mid - half, cy, gw, ph);
      SetPill(BTN_PREVWPN, mid + half, cy, gw, ph);
      bWheelInGap = 1;
    }
  }

  /* Leftwards from SAVE: the FPS readout while it shows (if the rest below
     still fits in the strip with it), (NEXT/PREV WPN if not in the gap), the
     cheat icon, the camera icon, the follow arrow, the slow-motion label --
     each in the strip while it stays right of the status text, the rest in a
     second row under it, leftwards from the right (the FPS readout first
     there, if it isn't in the strip). The slots stay put whether or not
     their icon shows, so nothing moves under a finger (only switching FPS
     lays them out again). */
  int aiChain[6], ctChain = 0;
  const double ww = IOSTOUCH_WHEELPILL_W;
  if (!bWheelInGap) {
    aiChain[ctChain++] = BTN_PREVWPN;
    aiChain[ctChain++] = BTN_NEXTWPN;
    SetPill(BTN_NEXTWPN, 0, 0, ww, ph);
    SetPill(BTN_PREVWPN, 0, 0, ww, ph);
  }
  aiChain[ctChain++] = BTN_CHEAT;
  aiChain[ctChain++] = BTN_CAMERA;
  const int ctMain = ctChain; /* (these must fit for the FPS readout to sit in the strip) */
  aiChain[ctChain++] = BTN_FOLLOWNEXT;
  aiChain[ctChain++] = BTN_SLOWMO;
  SetCircle(BTN_CHEAT, 0, 0, ph * 0.5);
  SetCircle(BTN_CAMERA, 0, 0, ph * 0.5);
  SetCircle(BTN_FOLLOWNEXT, 0, 0, IOSTOUCH_ARROW_W * 0.5);
  SetPill(BTN_SLOWMO, 0, 0, IOSTOUCH_SLOWMO_W, fmin(ph, 24.0));
  const double x0 = C.aButtons[BTN_SAVE].x - sw * 0.5 - IOSTOUCH_ROW_GAP;
  const double xMin = fmax(textEnd + IOSTOUCH_ROW_GAP, C.left);
  C.bFpsInStrip = 0;
  if (C.bShowFps) {
    double need = IOSTOUCH_FPS_W + IOSTOUCH_ROW_GAP;
    for (int k = 0; k < ctMain; k++) need += ButtonW(aiChain[k]) + IOSTOUCH_ROW_GAP;
    C.bFpsInStrip = x0 - need + IOSTOUCH_ROW_GAP >= xMin;
  }
  double xr = x0;
  if (C.bFpsInStrip) {
    C.rFps = (IOSTouchRect){ 1, xr - IOSTOUCH_FPS_W, cy - IOSTOUCH_FPS_H * 0.5, xr, cy + IOSTOUCH_FPS_H * 0.5 };
    xr -= IOSTOUCH_FPS_W + IOSTOUCH_ROW_GAP;
  }
  int i = 0;
  for (; i < ctChain; i++) {
    const int b = aiChain[i];
    const double w = ButtonW(b);
    if (xr - w < xMin) break;
    C.aButtons[b].x = xr - w * 0.5;
    C.aButtons[b].y = cy;
    xr -= w + IOSTOUCH_ROW_GAP;
  }
  /* the second row: the FPS readout under LOAD and MENU (if not in the strip), then what didn't fit */
  xr = C.right - 2.0;
  if (!C.bFpsInStrip) {
    C.rFps = (IOSTouchRect){ 1, xr - IOSTOUCH_FPS_W, C.fRow2Y - IOSTOUCH_FPS_H * 0.5, xr, C.fRow2Y + IOSTOUCH_FPS_H * 0.5 };
    xr -= IOSTOUCH_FPS_W + IOSTOUCH_ROW_GAP + 2.0;
  }
  for (; i < ctChain; i++) {
    const int b = aiChain[i];
    const double w = ButtonW(b);
    if (C.aButtons[b].r * 2.0 > ph) C.aButtons[b].r = ph * 0.5;
    C.aButtons[b].x = xr - w * 0.5;
    C.aButtons[b].y = C.fRow2Y;
    xr -= w + IOSTOUCH_ROW_GAP;
  }

  C.fTrayY = C.fRow2Y + C.fPillH * 0.5 + 6.0 + IOSTOUCH_TRAY_BACK;
  C.fMessageY = C.fRow2Y + 22.0;
}

/* Full screen (no status strip): the top row, every item a 44 pt round
   button (the house's 22 pt radius), in two groups packed from the corners
   with one even gap between neighbours (IOSTOUCH_TOPROW_GAP, less only where
   everything showing wouldn't fit, the same for all):
     left:  NEXT WPN, PREV WPN, then the cheat icon, the follow arrow and the
            slow-motion label while each shows (the cheat things together);
     right: MENU, LOAD, SAVE in the corner, then (leftwards) the FPS readout
            and the camera icon while each shows.
   Items close up when one hides. The game's status text is centred on the
   screen between the groups (IOSTouchCore_StatusRects), or in the row under
   them. Laid out again whenever what shows changes (IOSTouchCore_SetHud, the
   tray). */
static int TopRowShows(int b)
{
  switch (b) {
  case BTN_CHEAT: return C.bShowCheat;
  case BTN_CAMERA: return C.bShowCamera;
  case BTN_FOLLOWNEXT: return C.hud.bFollowing;
  case BTN_SLOWMO: return C.hud.iSlowMo != 0;
  default: return 1;
  }
}

#define TOPROW_FPS (-1) /* (the FPS readout, in the right group's list) */
static void PlaceTopRow(void)
{
  if (!C.hud.bFullScreen || C.W <= 0) return;
  const double R = IOSTOUCH_TOPROW_RADIUS, y = C.fTopRowY;
  int aiL[5], nL = 0, aiR[5], nR = 0;
  aiL[nL++] = BTN_NEXTWPN;
  aiL[nL++] = BTN_PREVWPN;
  const int aiOptL[3] = { BTN_CHEAT, BTN_FOLLOWNEXT, BTN_SLOWMO };
  for (int i = 0; i < 3; i++) {
    SetCircle(aiOptL[i], C.aButtons[BTN_PREVWPN].x, y, R); /* (hidden: under PREV WPN, found by nothing) */
    C.aButtons[aiOptL[i]].bPill = 0;
    if (TopRowShows(aiOptL[i])) aiL[nL++] = aiOptL[i];
  }
  aiR[nR++] = BTN_MENU;
  aiR[nR++] = BTN_LOAD;
  aiR[nR++] = BTN_SAVE;
  if (C.bShowFps) aiR[nR++] = TOPROW_FPS;
  SetCircle(BTN_CAMERA, C.right - 2.0 - R, y, R);
  if (C.bShowCamera) aiR[nR++] = BTN_CAMERA;
  double wSum = 0.0;
  for (int i = 0; i < nR; i++) wSum += aiR[i] == TOPROW_FPS ? IOSTOUCH_FPS_W : 2.0 * R;
  wSum += nL * 2.0 * R;
  /* nL - 1 + nR - 1 gaps inside the groups, and at least two between them */
  const double room = (C.right - 2.0) - (C.left + 2.0) - wSum;
  C.fTopRowGap = fmax(IOSTOUCH_TOPROW_GAP_MIN, fmin(IOSTOUCH_TOPROW_GAP, room / (nL + nR)));
  double x = C.left + 2.0;
  for (int i = 0; i < nL; i++) {
    SetCircle(aiL[i], x + R, y, R);
    x += 2.0 * R + C.fTopRowGap;
  }
  x = C.right - 2.0;
  C.rFps = (IOSTouchRect){ 1, x - IOSTOUCH_FPS_W, y - IOSTOUCH_FPS_H * 0.5, x, y + IOSTOUCH_FPS_H * 0.5 };
  for (int i = 0; i < nR; i++) {
    if (aiR[i] == TOPROW_FPS) {
      C.rFps = (IOSTouchRect){ 1, x - IOSTOUCH_FPS_W, y - IOSTOUCH_FPS_H * 0.5, x, y + IOSTOUCH_FPS_H * 0.5 };
      x -= IOSTOUCH_FPS_W + C.fTopRowGap;
    }
    else {
      SetCircle(aiR[i], x - R, y, R);
      x -= 2.0 * R + C.fTopRowGap;
    }
  }
}

static void LayoutTopRow(void)
{
  const double R = IOSTOUCH_TOPROW_RADIUS, y = fmax(_inT, 8.0) + 26.0;
  for (int b = BTN_NEXTWPN; b <= BTN_MENU; b++) C.aButtons[b].bPill = 0;
  C.fTopRowY = y;
  C.bFpsInStrip = 1; /* (in the top row) */
  PlaceTopRow();
  C.fPillH = 2.0 * R;
  C.rStrip = (IOSTouchRect){ 0, 0, 0, 0, 0 }; /* (none: a touch up there moves or aims as anywhere) */
  C.fRow2Y = y + R + IOSTOUCH_ROW_GAP + 12.0;
  C.fTrayY = C.aButtons[BTN_MENU].y + 52.0;
  /* the game's info: health bottom left, the weapon in hand bottom centre, "you died" in the middle */
  C.fHealthX = C.left + 4.0;
  C.fInfoY = C.bottom - 10.0;
  C.fWeaponX = (C.left + C.right) * 0.5;
  C.fDeadX = (C.left + C.right) * 0.5;
  C.fDeadY = (fmax(_inT, 8.0) + C.bottom) * 0.5 - 30.0;
  IOSTouchRect rTop, rRow2;
  IOSTouchCore_StatusRects(&rTop, &rRow2);
  C.fMessageY = rRow2.y1 + 14.0;
}

/* Where the status text may go, always centred on the screen's centre (it
   never moves sideways): in the top row, as wide either way as it stays
   clear of every top-row button and icon showing now (and the FPS readout);
   and a row under the top row, as wide as the screen allows either way */
void IOSTouchCore_StatusRects(IOSTouchRect *prTop, IOSTouchRect *prRow2)
{
  const double R = IOSTOUCH_TOPROW_RADIUS, y = C.aButtons[BTN_MENU].y, cx = C.W * 0.5;
  double half = fmin(cx - C.left, C.right - cx);
  const int ai[9] = { BTN_NEXTWPN, BTN_PREVWPN, BTN_SAVE, BTN_LOAD, BTN_MENU, BTN_CAMERA, BTN_CHEAT, BTN_FOLLOWNEXT, BTN_SLOWMO };
  for (int i = 0; i < 9; i++) {
    const IOSTouchButton *p = &C.aButtons[ai[i]];
    if (i >= 5 && !IOSTouchCore_IsShown(ai[i])) continue;
    const double x0 = p->x - p->hw, x1 = p->x + p->hw;
    half = fmin(half, (x1 <= cx ? cx - x1 : (x0 >= cx ? x0 - cx : 0.0)) - 8.0);
  }
  if (C.bShowFps) {
    const double x0 = C.rFps.x0, x1 = C.rFps.x1;
    half = fmin(half, (x1 <= cx ? cx - x1 : (x0 >= cx ? x0 - cx : 0.0)) - 8.0);
  }
  prTop->bValid = half > 0.0;
  prTop->x0 = cx - fmax(half, 0.0);
  prTop->x1 = cx + fmax(half, 0.0);
  prTop->y0 = y - R;
  prTop->y1 = y + R;
  const double half2 = fmin(cx - C.left, C.right - cx);
  prRow2->bValid = 1;
  prRow2->x0 = cx - half2;
  prRow2->x1 = cx + half2;
  prRow2->y0 = y + R + IOSTOUCH_ROW_GAP;
  prRow2->y1 = prRow2->y0 + IOSTOUCH_STATUS_ROW_H;
}

void IOSTouchCore_Layout(double W, double H, double inL, double inT, double inR, double inB)
{
  Init();
  C.W = W;
  C.H = H;
  _inL = inL; _inT = inT; _inR = inR; _inB = inB;
  memcpy(_afLaidBar, C.hud.afTopBar, sizeof(_afLaidBar));
  memcpy(_afLaidText, C.hud.afTopText, sizeof(_afLaidText));
  memcpy(_afLaidGap, C.hud.afTopGap, sizeof(_afLaidGap));
  _bLaidFull = C.hud.bFullScreen != 0;
  C.left = fmax(inL, 8.0);
  C.right = W - fmax(inR, 8.0);
  C.top = inT;
  C.bottom = H - fmax(inB, 8.0);

  /* Bottom right: FIRE, DUCK left of it, EXECUTE straight above, IOSTOUCH_CLUSTER_GAP edge to edge (house) */
  const double fx = C.right - IOSTOUCH_FIRE_IN_X, fy = C.bottom - IOSTOUCH_FIRE_IN_Y;
  SetCircle(BTN_FIRE, fx, fy, IOSTOUCH_FIRE_RADIUS);
  SetCircle(BTN_DUCK, fx - (IOSTOUCH_FIRE_RADIUS + IOSTOUCH_CLUSTER_GAP + IOSTOUCH_DUCK_RADIUS), fy, IOSTOUCH_DUCK_RADIUS);
  SetCircle(BTN_EXECUTE, fx, fy - (IOSTOUCH_FIRE_RADIUS + IOSTOUCH_CLUSTER_GAP + IOSTOUCH_EXECUTE_RADIUS), IOSTOUCH_EXECUTE_RADIUS);

  if (C.hud.bFullScreen) LayoutTopRow();
  else LayoutStrip(W, H);

  /* the menus' BACK: where NEXT WPN sits in full-screen play, whichever layout this is */
  SetCircle(BTN_BACK, C.left + 2.0 + IOSTOUCH_TOPROW_RADIUS, fmax(_inT, 8.0) + 26.0, IOSTOUCH_TOPROW_RADIUS);

  /* messages under the top row, in the middle */
  C.fMessageX = (C.left + C.right) * 0.5;

  PlaceTray();
  WheelGeometry();
}

int IOSTouchCore_SetHud(const IOSTouchHud *pHud)
{
  Init();
  const int bWasCheat = C.hud.bCheatOn, bWasFollowing = C.hud.bFollowing, iWasSlow = C.hud.iSlowMo;
  if (pHud) C.hud = *pHud;
  if (bWasFollowing != C.hud.bFollowing || (iWasSlow != 0) != (C.hud.iSlowMo != 0)) PlaceTopRow();
  if (bWasCheat != C.hud.bCheatOn && C.bTrayOpen) PlaceTray(); /* (LEVEL, FOLLOW come and go) */
  if (!pHud || C.W <= 0) return 0;
  return memcmp(_afLaidBar, C.hud.afTopBar, sizeof(_afLaidBar)) || memcmp(_afLaidText, C.hud.afTopText, sizeof(_afLaidText))
         || memcmp(_afLaidGap, C.hud.afTopGap, sizeof(_afLaidGap)) || _bLaidFull != (C.hud.bFullScreen != 0);
}

/* ------------------------------------------------------------ hit testing */
int IOSTouchCore_ButtonAt(double x, double y)
{
  Init();
  int best = -1;
  double bestDist = 0;
  /* The open tray on top: anywhere on a row's backing is
     on its nearest button */
  if (C.bTrayOpen) {
    for (int b = BTN_TRAY_FIRST; b <= BTN_TRAY_LAST; b++) {
      if (!IOSTouchCore_IsShown(b)) continue;
      const IOSTouchButton *p = &C.aButtons[b];
      if (fabs(y - p->y) > IOSTOUCH_TRAY_BACK || fabs(x - p->x) > IOSTOUCH_TRAY_BACK) continue;
      const double d = hypot(x - p->x, y - p->y);
      if (best < 0 || d < bestDist) {
        best = b;
        bestDist = d;
      }
    }
    if (best >= 0) return best;
  }
  for (int b = 0; b < BTN_COUNT; b++) {
    if (IsTray(b) || !IOSTouchCore_IsShown(b)) continue;
    const double d = ButtonDist(&C.aButtons[b], x, y);
    if (d <= IOSTOUCH_TOUCH_SLOP && (best < 0 || d < bestDist)) {
      best = b;
      bestDist = d;
    }
  }
  return best;
}

static int IsOnButton(double x, double y, int b)
{
  return ButtonDist(&C.aButtons[b], x, y) <= IOSTOUCH_TOUCH_SLOP;
}

static int LiftsOnButton(double x, double y, int b)
{
  return IsOnButton(x, y, b) || (IsTray(b) && IOSTouchCore_ButtonAt(x, y) == b);
}

static Slot *FindSlot(const void *touch)
{
  for (int i = 0; i < MAX_TOUCHES; i++) {
    if (_aSlots[i].touch == touch) return &_aSlots[i];
  }
  return NULL;
}

/* ------------------------------------------------------------ the tray */
static void SetTrayOpen(int bOpen)
{
  bOpen = bOpen != 0;
  if (C.bTrayOpen == bOpen) return;
  C.bTrayOpen = bOpen;
  if (bOpen) {
    PlaceTray();
    return;
  }
  for (int i = 0; i < MAX_TOUCHES; i++) {
    Slot *s = &_aSlots[i];
    if (!s->touch || s->role != ROLE_BUTTON) continue;
    if (IsTray(s->button)) {
      if (C.aButtonHeld[s->button] > 0) C.aButtonHeld[s->button]--;
      s->role = ROLE_IGNORED;
    }
    s->trayButton = -1;
  }
}

void IOSTouchCore_ShowMessage(const char *str, double fSeconds, double now)
{
  Init();
  snprintf(C.szMessage, sizeof(C.szMessage), "%s", str ? str : "");
  C.tMessage = now;
  C.fMessageSeconds = fSeconds > 0 ? fSeconds : IOSTOUCH_MESSAGE_TIME;
}

int IOSTouchCore_MessageShows(double now)
{
  return C.szMessage[0] && now >= C.tMessage && now - C.tMessage < C.fMessageSeconds;
}

static void UseTrayButton(int b, double now)
{
  switch (b) {
  case BTN_TRAY_SLOW:
    _ulRequests |= IOSTOUCH_REQ_SLOWMO; /* (off, 1/2, 1/4, off: the game; stays open) */
    return;
  case BTN_TRAY_FPS:
    C.bShowFps = !C.bShowFps;
    _bFpsBase = 0;
    C.iFpsShown = -1;
    if (C.W > 0) IOSTouchCore_Layout(C.W, C.H, _inL, _inT, _inR, _inB); /* (the readout may go in the strip) */
    break;
  case BTN_TRAY_KEYS:
    _ulRequests |= IOSTOUCH_REQ_KEYBOARD;
    break;
  case BTN_TRAY_CHEAT:
    /* Shows the cheat icon and switches the cheat on; tapped again, hides
       it and switches the cheat off (if on). The game plays the sound. */
    if (!C.bShowCheat) {
      C.bShowCheat = 1;
      if (!C.hud.bCheatOn) _ulRequests |= IOSTOUCH_REQ_CHEAT;
    }
    else {
      C.bShowCheat = 0;
      if (C.hud.bCheatOn) _ulRequests |= IOSTOUCH_REQ_CHEAT;
    }
    PlaceTopRow();
    return; /* (stays open: LEVEL and FOLLOW appear once the game has it on) */
  case BTN_TRAY_LEVEL:
    if (C.ctLevels <= 0) {
      IOSTouchCore_ShowMessage("No levels found", 0, now);
      break;
    }
    C.bLevelListOpen = 1;
    _iLevelPick = -1;
    _ulRequests |= IOSTOUCH_REQ_LEVEL;
    break;
  case BTN_TRAY_CAMERA:
    C.bShowCamera = !C.bShowCamera;
    PlaceTopRow();
    return;
  case BTN_TRAY_FOLLOW:
    _ulRequests |= IOSTOUCH_REQ_FOLLOW;
    break;
  default:
    return;
  }
  SetTrayOpen(0);
}

/* ------------------------------------------------------------ the wheel */
int IOSTouchCore_WheelSlices(void)
{
  Init();
  int n = C.wpn.ctWeapons;
  return n < 0 ? 0 : (n > IOSTOUCH_WPN_MAX ? IOSTOUCH_WPN_MAX : n);
}

double IOSTouchCore_WheelSliceWidth(void)
{
  const int n = IOSTouchCore_WheelSlices();
  return n > 0 ? fmin(IOSTOUCH_WHEEL_SLICE_MAX, IOSTOUCH_WHEEL_RING_DEG / n) : IOSTOUCH_WHEEL_SLICE_MAX;
}

/* The first slice's edge at the gap's left edge, then clockwise */
double IOSTouchCore_WheelSliceDeg(int i)
{
  const int n = IOSTouchCore_WheelSlices();
  const double w = IOSTouchCore_WheelSliceWidth(), gap = 360.0 - n * w;
  return 270.0 - gap * 0.5 - w * 0.5 - i * w;
}

int IOSTouchCore_WheelSliceState(int i)
{
  if (i < 0 || i >= IOSTouchCore_WheelSlices() || !C.wpn.abOwned[i]) return WSTATE_NOTFOUND;
  if (C.wpn.aiAmmo[i] == 0) return WSTATE_NOAMMO;
  return WSTATE_READY;
}

const char *IOSTouchCore_WheelSliceName(int i)
{
  if (i < 0 || i >= IOSTouchCore_WheelSlices() || !C.wpn.apszName[i]) return "";
  return C.wpn.apszName[i];
}

int IOSTouchCore_WheelSliceCount(int i)
{
  if (IOSTouchCore_WheelSliceState(i) == WSTATE_NOTFOUND || C.wpn.aiAmmo[i] < 0) return -1;
  return C.wpn.aiAmmo[i] > 999 ? 999 : C.wpn.aiAmmo[i];
}

static int WheelCanOpen(void) { return C.wpn.bValid && IOSTouchCore_WheelSlices() > 0; }

static int WheelCanPick(int i)
{
  return i >= 0 && i < IOSTouchCore_WheelSlices() && IOSTouchCore_WheelSliceState(i) == WSTATE_READY && i != C.wpn.iCurrent;
}

static double AngleDiff(double a, double b)
{
  double d = fmod(a - b, 360.0);
  if (d > 180.0) d -= 360.0;
  if (d < -180.0) d += 360.0;
  return d;
}

static int SliceForAngle(double deg, int cur)
{
  const int n = IOSTouchCore_WheelSlices();
  const double half = IOSTouchCore_WheelSliceWidth() * 0.5;
  if (cur >= 0 && cur < n && fabs(AngleDiff(deg, IOSTouchCore_WheelSliceDeg(cur))) <= half + IOSTOUCH_WHEEL_HYST) return cur;
  int best = IOSTOUCH_WHEEL_CANCEL;
  double bestDiff = 0;
  for (int i = 0; i < n; i++) {
    const double d = fabs(AngleDiff(deg, IOSTouchCore_WheelSliceDeg(i)));
    if (d <= half && (best < 0 || d < bestDiff)) {
      best = i;
      bestDiff = d;
    }
  }
  return best;
}

/* The slice under (x,y): on the ring by its angle; -1 off it */
static int SliceAt(double x, double y, int cur)
{
  const double dx = x - C.fWheelCX, dy = C.fWheelCY - y, r = sqrt(dx * dx + dy * dy);
  if (r < C.fWheelR0 - 4.0 || r > C.fWheelR1 + IOSTOUCH_WHEEL_POP + 14.0) return -1;
  return SliceForAngle(atan2(dy, dx) * 180.0 / M_PI, cur);
}

/* The opener's slide (Sam's IOSTouch_WheelAim) */
static void WheelAim(Slot *s, double x, double y)
{
  double dx = x - s->wheelOX, dy = y - s->wheelOY, r = sqrt(dx * dx + dy * dy);
  if (r > IOSTOUCH_WHEEL_LEASH) {
    const double k = (r - IOSTOUCH_WHEEL_LEASH) / r;
    s->wheelOX += dx * k;
    s->wheelOY += dy * k;
    dx = x - s->wheelOX;
    dy = y - s->wheelOY;
    r = IOSTOUCH_WHEEL_LEASH;
  }
  const double cx = x - C.fWheelCX, cy = C.fWheelCY - y;
  if (sqrt(cx * cx + cy * cy) <= C.fWheelR1 + IOSTOUCH_WHEEL_POP + 14.0) {
    s->bArmed = 1;
    s->wheelAim = atan2(cy, cx) * 180.0 / M_PI;
    s->wheelSlot = SliceAt(x, y, s->wheelSlot);
    return;
  }
  if (s->bArmed && hypot(x - s->xDown, y - s->yDown) < IOSTOUCH_WHEEL_DEAD) {
    s->wheelOX = s->xDown;
    s->wheelOY = s->yDown;
    s->wheelSlot = -1;
    return;
  }
  if (r < IOSTOUCH_WHEEL_DEAD) {
    s->wheelSlot = -1;
    return;
  }
  s->bArmed = 1;
  s->wheelAim = atan2(-dy, dx) * 180.0 / M_PI;
  s->wheelSlot = SliceForAngle(s->wheelAim, s->wheelSlot);
}

static int WheelPointedSlot(int *pbCancel, const Slot **ppSlide)
{
  const Slot *pSlide = NULL, *pFinger = NULL;
  for (int i = 0; i < MAX_TOUCHES; i++) {
    const Slot *s = &_aSlots[i];
    if (!s->touch || s->role != ROLE_WHEEL) continue;
    if (s->bWheelOpener && s->bWheelHeld) continue;
    if (s->bWheelOpener && !C.bWheelTapMode) pSlide = s;
    else if (!pFinger || s->uSeq > pFinger->uSeq) pFinger = s;
  }
  int hot = -1, bCancel = 0;
  if (pSlide && pSlide->bArmed) {
    hot = pSlide->wheelSlot;
    bCancel = hot < 0;
  }
  else if (pFinger) {
    hot = pFinger->wheelSlot;
    bCancel = hot < 0;
    pSlide = NULL;
  }
  else {
    pSlide = NULL;
  }
  if (pbCancel) *pbCancel = C.bWheelOpen && bCancel;
  if (ppSlide) *ppSlide = C.bWheelOpen ? pSlide : NULL;
  return C.bWheelOpen ? hot : -1;
}

int IOSTouchCore_WheelPointed(int *pbCancel) { Init(); return WheelPointedSlot(pbCancel, NULL); }

int IOSTouchCore_WheelNeedle(double *pfDeg)
{
  const Slot *s = NULL;
  WheelPointedSlot(NULL, &s);
  if (!s || !s->bArmed) return 0;
  *pfDeg = s->wheelAim;
  return 1;
}

static void WheelUpdateHot(void)
{
  const int hot = WheelPointedSlot(NULL, NULL);
  if (hot != C.iWheelHot) {
    if (hot != -1) C.ctWheelTicks++;
    C.iWheelHot = hot;
  }
}

static void StopStick(void)
{
  C.bStickActive = 0;
  C.fStickX = C.fStickY = 0.0;
}

static void WheelOpen(Slot *s, int bHeld)
{
  SetTrayOpen(0);
  for (int i = 0; i < MAX_TOUCHES; i++) {
    Slot *o = &_aSlots[i];
    if (!o->touch) continue;
    if (o->role == ROLE_BUTTON && C.aButtonHeld[o->button] > 0) C.aButtonHeld[o->button]--;
    if (o->role == ROLE_STICK) StopStick();
    if (o != s) o->role = ROLE_IGNORED;
  }
  C.fSaveRing = C.fLoadRing = C.fMenuRing = 0.0f;
  C.afWheelRing[0] = C.afWheelRing[1] = 0.0f;
  /* a NEXT/PREV tap not read yet would undo the pick */
  memset(_aubPulse, 0, sizeof(_aubPulse));
  C.bWheelOpen = 1;
  C.bWheelTapMode = bHeld != 0;
  C.iWheelButton = s->button;
  s->role = ROLE_WHEEL;
  s->bWheelOpener = 1;
  s->bWheelHeld = bHeld != 0;
  s->bArmed = 0;
  s->wheelSlot = -1;
  s->wheelOX = s->xDown;
  s->wheelOY = s->yDown;
  C.iWheelHot = -1;
  WheelUpdateHot();
}

static void WheelClose(int i)
{
  if (C.bWheelOpen && WheelCanPick(i)) _iPick = 1 + C.wpn.aiType[i];
  for (int t = 0; t < MAX_TOUCHES; t++) {
    if (_aSlots[t].touch && _aSlots[t].role == ROLE_WHEEL) _aSlots[t].role = ROLE_IGNORED;
  }
  C.bWheelOpen = 0;
  C.bWheelTapMode = 0;
  C.iWheelHot = -1;
}

static int IsWheelPress(const Slot *s)
{
  return s->touch && s->role == ROLE_BUTTON && C.aButtons[s->button].kind == KIND_WHEEL;
}

static int WheelPressSlid(Slot *s, double x, double y)
{
  if (!IsWheelPress(s) || s->bFired || hypot(x - s->xDown, y - s->yDown) < IOSTOUCH_WHEEL_OPEN_SLIDE) return 0;
  if (!WheelCanOpen()) {
    s->bFired = 1;
    C.afWheelRing[s->button == BTN_PREVWPN] = 0.0f;
    return 0;
  }
  WheelOpen(s, 0);
  return 1;
}

void IOSTouchCore_SetWeapons(const IOSTouchWeapons *pw)
{
  Init();
  const int ctWas = C.wpn.ctWeapons;
  if (pw) C.wpn = *pw;
  else memset(&C.wpn, 0, sizeof(C.wpn));
  if (C.wpn.ctWeapons < 0) C.wpn.ctWeapons = 0;
  if (C.wpn.ctWeapons > IOSTOUCH_WPN_MAX) C.wpn.ctWeapons = IOSTOUCH_WPN_MAX;
  /* The slices are laid out from ctWeapons every time (IOSTouchCore_WheelSliceWidth/Deg: evenly round
     the ring, the cancel gap at the bottom). The game can't take a pick, or the slices changed under
     the open wheel (the cheat adds or drops the Deathwad: a slice now means another weapon): it
     closes, picking nothing. */
  if (C.bWheelOpen && (!WheelCanOpen() || C.wpn.ctWeapons != ctWas)) WheelClose(-1);
}

void IOSTouchCore_WheelMiddle(const char **ppszTitle, int *piCount, const char **ppszHint)
{
  int bCancel = 0;
  const int hot = IOSTouchCore_WheelPointed(&bCancel);
  int i = hot >= 0 ? hot : C.wpn.iCurrent;
  *ppszTitle = "";
  *piCount = -1;
  if (hot == IOSTOUCH_WHEEL_CANCEL) {
    *ppszHint = "lift to cancel";
    return;
  }
  if (i >= 0 && i < IOSTouchCore_WheelSlices()) {
    const int st = IOSTouchCore_WheelSliceState(i);
    *ppszTitle = (st == WSTATE_NOTFOUND && hot >= 0) ? "?" : (C.wpn.apszName[i] ? C.wpn.apszName[i] : "");
    *piCount = IOSTouchCore_WheelSliceCount(i);
    if (hot >= 0) {
      *ppszHint = st == WSTATE_NOTFOUND ? "not found yet" : (st == WSTATE_NOAMMO ? "no ammo" : "lift to select");
      return;
    }
  }
  *ppszHint = C.bWheelTapMode ? "tap a weapon" : "slide toward a weapon";
}

void IOSTouchCore_TintIcon(const unsigned char *pubSrc, int ctTexels, int iState, unsigned char *pubDst)
{
  if (iState == WSTATE_READY) {
    memcpy(pubDst, pubSrc, (size_t)ctTexels * 4);
    return;
  }
  const float k = (iState == WSTATE_NOAMMO) ? 0.80f : 0.40f;
  const float a = (iState == WSTATE_NOAMMO) ? 0.70f : 0.45f;
  for (int i = 0; i < ctTexels; i++) {
    const unsigned char *p = pubSrc + i * 4;
    const float lum = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
    float v = lum * k + 0.5f, al = p[3] * a + 0.5f;
    if (v > 255.0f) v = 255.0f;
    if (al > 255.0f) al = 255.0f;
    pubDst[i * 4 + 0] = pubDst[i * 4 + 1] = pubDst[i * 4 + 2] = (unsigned char)v;
    pubDst[i * 4 + 3] = (unsigned char)al;
  }
}

/* ------------------------------------------------------------ the camera */
/* A camera press moved IOSTOUCH_CAMERA_SLIDE from where it landed: no photo
   (a lift that never did, however long it was held, is one photo) */
static void CameraMoved(Slot *s, double x, double y)
{
  if (hypot(x - s->xDown, y - s->yDown) >= IOSTOUCH_CAMERA_SLIDE) s->bSlid = 1;
}

/* ------------------------------------------------------------ the stick and aiming */
static void SetStick(double x, double y)
{
  double dx = (x - C.fStickOX) / IOSTOUCH_STICK_RADIUS, dy = (y - C.fStickOY) / IOSTOUCH_STICK_RADIUS;
  const double len = sqrt(dx * dx + dy * dy);
  if (len > 1.0) {
    dx /= len;
    dy /= len;
  }
  C.fStickX = dx;
  C.fStickY = dy;
}

/* An aiming finger is at (x,y): it aims the way it is from where it landed
   (the aim stick's centre; FIRE's too), once outside IOSTOUCH_AIM_DEAD --
   inside it, it doesn't aim (the dude keeps facing the way he did). Held
   still, the aim stays. The aim stick fires while pushed to
   IOSTOUCH_AIM_FIRE_ON of its radius, until back under IOSTOUCH_AIM_FIRE_OFF. */
static void AimMoved(Slot *s, double x, double y)
{
  const double dx = x - s->aimOX, dy = y - s->aimOY, r = hypot(dx, dy);
  s->aimPush = fmin(r / IOSTOUCH_AIM_RADIUS, 1.0);
  if (r < IOSTOUCH_AIM_DEAD) {
    s->bAimArmed = 0;
  }
  else {
    if (!s->bAimEver) {
      s->bAimEver = 1;
      s->uSeq = ++_uSeq; /* (the newest finger to aim wins) */
    }
    s->bAimArmed = 1;
    s->aimX = dx / r;
    s->aimY = -dy / r;
  }
  if (s->role == ROLE_AIM) {
    if (!s->bAimFire && s->aimPush >= IOSTOUCH_AIM_FIRE_ON) {
      s->bAimFire = 1;
      C.ctAimFireTicks++;
    }
    else if (s->bAimFire && s->aimPush < IOSTOUCH_AIM_FIRE_OFF) {
      s->bAimFire = 0;
    }
  }
}

/* A finger that has aimed and is still down (in its dead zone too: it keeps the
   aim until it lifts, rather than handing it back to an older finger) */
static int AimsNow(const Slot *s)
{
  if (!s->touch || !s->bAimEver) return 0;
  return s->role == ROLE_AIM || (s->role == ROLE_BUTTON && C.aButtons[s->button].bAimWhileHeld);
}

/* The aiming finger that wins (the newest to aim), for HeldBits: only its stick fires */
static const Slot *_pAimWinner;

/* The newest aiming finger's direction (none while it's in its dead zone); the aim
   stick to draw (the newest one down) */
static void UpdateAim(void)
{
  const Slot *best = NULL, *stick = NULL;
  for (int i = 0; i < MAX_TOUCHES; i++) {
    const Slot *s = &_aSlots[i];
    if (AimsNow(s) && (!best || s->uSeq > best->uSeq)) best = s;
    if (s->touch && s->role == ROLE_AIM && (!stick || s->uSeq > stick->uSeq)) stick = s;
  }
  C.bAimStick = stick != NULL;
  if (stick) {
    C.fAimStickOX = stick->aimOX;
    C.fAimStickOY = stick->aimOY;
    double kx = (stick->x - stick->aimOX) / IOSTOUCH_AIM_RADIUS, ky = (stick->y - stick->aimOY) / IOSTOUCH_AIM_RADIUS;
    const double len = sqrt(kx * kx + ky * ky);
    if (len > 1.0) { /* (the knob stays on the edge; the direction still follows the thumb) */
      kx /= len;
      ky /= len;
    }
    C.fAimStickX = kx;
    C.fAimStickY = ky;
    C.bAimFiring = stick == best && stick->bAimFire; /* (only the winner fires: see HeldBits) */
  }
  else {
    C.bAimFiring = 0;
  }
  _pAimWinner = best;
  if (!best || !best->bAimArmed) {
    C.bAim = 0;
    return;
  }
  C.bAim = 1;
  C.fAimX = best->aimX;
  C.fAimY = best->aimY;
}

/* ------------------------------------------------------------ touches */
static void DropTouches(void)
{
  memset(_aSlots, 0, sizeof(_aSlots));
  memset(C.aButtonHeld, 0, sizeof(C.aButtonHeld));
  StopStick();
}

void IOSTouchCore_TouchBegan(const void *touch, double x, double y, double t)
{
  Init();
  Slot *s = FindSlot(NULL);
  if (!s || !touch || FindSlot(touch)) return;
  memset(s, 0, sizeof(*s));
  s->touch = touch;
  s->x = s->xDown = s->aimOX = x;
  s->y = s->yDown = s->aimOY = y;
  s->tDown = t;
  s->uSeq = ++_uSeq;
  s->trayButton = -1;
  s->wheelSlot = -1;
  s->button = -1;

  if (C.iMode == IOSTOUCH_MENU && IOSTouchCore_ButtonAt(x, y) == BTN_BACK) {
    s->role = ROLE_BUTTON; /* (the menus: only BACK; the view passes every other touch on to SDL) */
    s->button = BTN_BACK;
    C.aButtonHeld[BTN_BACK]++;
    return;
  }
  if (C.iMode != IOSTOUCH_GAMEPLAY) {
    s->role = ROLE_IGNORED;
    return;
  }
  /* The level list takes the touches on it (the view's); one anywhere else closes it */
  if (C.bLevelListOpen) {
    IOSTouchCore_CloseLevelList();
    s->role = ROLE_IGNORED;
    return;
  }
  /* The open wheel takes every touch */
  if (C.bWheelOpen) {
    s->role = ROLE_WHEEL;
    s->wheelSlot = SliceAt(x, y, -1);
    WheelUpdateHot();
    return;
  }
  s->button = IOSTouchCore_ButtonAt(x, y);
  /* An open tray closes at a touch anywhere else (on MENU: only that) */
  if (C.bTrayOpen && !IsTray(s->button)) {
    SetTrayOpen(0);
    if (s->button == BTN_MENU || C.hud.bDead) { /* (dead: closing the tray must not also restart) */
      s->role = ROLE_IGNORED;
      return;
    }
  }
  /* Dead: a tap anywhere but the top row, the tray and FIRE (which the game
     also takes) restarts the level */
  if (C.hud.bDead && s->button != BTN_FIRE && !IsTray(s->button) && !IsTopRow(s->button)) {
    /* a near miss on the top row (or the status text in it) does nothing */
    if (C.hud.bFullScreen && s->button < 0 && y < C.fTopRowY + IOSTOUCH_TOPROW_RADIUS + 6.0) {
      s->role = ROLE_IGNORED;
      return;
    }
    s->role = ROLE_DEADTAP;
    s->button = -1;
    return;
  }
  if (s->button >= 0) {
    if (C.aButtons[s->button].kind == KIND_WHEEL) {
      /* a second finger on the same wheel button is ignored */
      for (int k = 0; k < MAX_TOUCHES; k++) {
        const Slot *o = &_aSlots[k];
        if (o != s && IsWheelPress(o) && o->button == s->button) {
          s->role = ROLE_IGNORED;
          return;
        }
      }
    }
    s->role = ROLE_BUTTON;
    C.aButtonHeld[s->button]++;
    if (C.aButtons[s->button].kind == KIND_HOLD) _ulSeen &= ~C.aButtons[s->button].ulAction;
    return;
  }
  if (y < C.rStrip.y1) {
    s->role = ROLE_IGNORED; /* (the status strip off the pills: neither moves nor aims) */
    return;
  }
  if (x < C.W * IOSTOUCH_STICK_ZONE && !C.bStickActive) {
    s->role = ROLE_STICK;
    C.bStickActive = 1;
    C.fStickOX = x;
    C.fStickOY = y;
    C.fStickX = C.fStickY = 0.0;
    return;
  }
  s->role = ROLE_AIM; /* the aim stick, centred here */
  UpdateAim();
}

void IOSTouchCore_TouchMoved(const void *touch, double x, double y, double t)
{
  (void)t;
  Init();
  Slot *s = touch ? FindSlot(touch) : NULL;
  if (!s) return;
  WheelPressSlid(s, x, y);
  switch (s->role) {
  case ROLE_WHEEL:
    if (s->bWheelOpener && s->bWheelHeld) {
      if (hypot(x - s->xDown, y - s->yDown) >= IOSTOUCH_WHEEL_DEAD) {
        C.bWheelTapMode = 0;
        s->bWheelHeld = 0;
        WheelAim(s, x, y);
      }
    }
    else if (s->bWheelOpener && !C.bWheelTapMode) {
      WheelAim(s, x, y);
    }
    else {
      s->wheelSlot = SliceAt(x, y, s->wheelSlot);
    }
    WheelUpdateHot();
    break;
  case ROLE_STICK:
    SetStick(x, y);
    break;
  case ROLE_DEADTAP:
    if (hypot(x - s->xDown, y - s->yDown) >= IOSTOUCH_TAP_SLIDE) s->bSlid = 1;
    break;
  case ROLE_AIM:
    AimMoved(s, x, y);
    break;
  case ROLE_BUTTON: {
    if (s->button == BTN_BACK && hypot(x - s->xDown, y - s->yDown) >= IOSTOUCH_TAP_SLIDE) s->bSlid = 1; /* (slid: no BACK) */
    const int b = s->button;
    const int kind = C.aButtons[b].kind;
    if (C.aButtons[b].bAimWhileHeld) AimMoved(s, x, y);
    if (b == BTN_MENU && s->bFired && C.bTrayOpen) {
      const int tb = IOSTouchCore_ButtonAt(x, y);
      s->trayButton = IsTray(tb) ? tb : -1;
    }
    /* SAVE, LOAD, MENU: a finger that leaves the button is done, even if it comes back */
    if ((kind == KIND_HOLDSAVE || kind == KIND_MENU) && !s->bFired && !IsOnButton(x, y, b)) s->bFired = 1;
    if (kind == KIND_CAMERA) CameraMoved(s, x, y);
    break;
  }
  default:
    break;
  }
  s->x = x;
  s->y = y;
  UpdateAim();
}

/* A quick press lifted before a read saw it held: one read's press */
static void QueuePulse(unsigned int ul)
{
  for (int i = 0; i < 8; i++) {
    if ((ul & (1u << i)) && _aubPulse[i] < 255) _aubPulse[i]++;
  }
}

void IOSTouchCore_TouchEnded(const void *touch, double x, double y, double t, int bCancelled)
{
  Init();
  Slot *s = touch ? FindSlot(touch) : NULL;
  if (!s) return;
  if (!bCancelled) {
    s->x = x;
    s->y = y;
  }
  const double held = t - s->tDown;

  if (s->role == ROLE_WHEEL && C.bWheelOpen) {
    if (bCancelled) {
      if (s->bWheelOpener) C.bWheelTapMode = 1;
    }
    else if (s->bWheelOpener && s->bWheelHeld) {
      /* opened by holding still: stays open, to tap */
    }
    else if (s->bWheelOpener && !C.bWheelTapMode) {
      WheelAim(s, x, y);
      if (!s->bArmed) C.bWheelTapMode = 1;
      else WheelClose(s->wheelSlot);
    }
    else {
      const int slice = SliceAt(x, y, s->wheelSlot);
      int bSliding = 0;
      for (int k = 0; k < MAX_TOUCHES; k++) {
        const Slot *o = &_aSlots[k];
        if (o != s && o->touch && o->role == ROLE_WHEEL && o->bWheelOpener && o->bArmed && !C.bWheelTapMode) bSliding = 1;
      }
      if (!(slice == -1 && bSliding) && !(slice >= 0 && IOSTouchCore_WheelSliceState(slice) != WSTATE_READY)) WheelClose(slice);
    }
  }
  else if (IsWheelPress(s)) {
    if (C.aButtonHeld[s->button] > 0) C.aButtonHeld[s->button]--;
    C.afWheelRing[s->button == BTN_PREVWPN] = 0.0f;
    if (bCancelled || s->bFired) {
    }
    else if (hypot(x - s->xDown, y - s->yDown) >= IOSTOUCH_WHEEL_OPEN_SLIDE) {
      if (WheelCanOpen()) { /* a flick with no move between: a slide, then its lift */
        WheelOpen(s, 0);
        WheelAim(s, x, y);
        if (s->bArmed) WheelClose(s->wheelSlot);
        else C.bWheelTapMode = 1;
      }
    }
    else if (held < IOSTOUCH_WHEEL_HOLD) {
      QueuePulse(C.aButtons[s->button].ulAction);
    }
    else if (WheelCanOpen()) {
      WheelOpen(s, 1); /* held long enough, lifted before a frame saw it */
    }
  }
  else if (s->role == ROLE_BUTTON) {
    const int b = s->button;
    const IOSTouchButton *p = &C.aButtons[b];
    if (C.aButtonHeld[b] > 0) C.aButtonHeld[b]--;
    const int bOn = !bCancelled && LiftsOnButton(x, y, b);
    switch (p->kind) {
    case KIND_HOLD:
      if (!bCancelled && C.aButtonHeld[b] == 0 && !(_ulSeen & p->ulAction)) QueuePulse(p->ulAction);
      break;
    case KIND_HOLDSAVE:
      /* a tap: its screen; held long enough (by the touch's clock) but lifted
         before a frame saw it: the quick save / load after all */
      if (bOn && !s->bFired) {
        if (held < IOSTOUCH_QUICKSAVE_HOLD) _ulRequests |= (b == BTN_SAVE) ? IOSTOUCH_REQ_SAVESCREEN : IOSTOUCH_REQ_LOADSCREEN;
        else if (!(_ulRequests & (IOSTOUCH_REQ_QUICKSAVE | IOSTOUCH_REQ_QUICKLOAD))) _ulRequests |= p->ulAction;
      }
      if (b == BTN_SAVE) C.fSaveRing = 0.0f;
      else C.fLoadRing = 0.0f;
      break;
    case KIND_MENU:
      if (bOn && !s->bFired) {
        if (held < IOSTOUCH_MENU_HOLD) _ulRequests |= IOSTOUCH_REQ_MENU;
        else SetTrayOpen(1);
      }
      else if (!bCancelled && s->bFired && C.bTrayOpen) {
        const int tb = IOSTouchCore_ButtonAt(x, y);
        if (IsTray(tb)) UseTrayButton(tb, t);
      }
      C.fMenuRing = 0.0f;
      break;
    case KIND_TAP:
      if (bOn && !(b == BTN_BACK && s->bSlid)) _ulRequests |= p->ulAction;
      break;
    case KIND_CAMERA:
      if (bOn && !s->bSlid) _ulRequests |= IOSTOUCH_REQ_PHOTO; /* (a screenshot, however long it was held) */
      break;
    case KIND_TRAY:
      if (bOn) UseTrayButton(b, t);
      break;
    default:
      break;
    }
  }
  else if (s->role == ROLE_STICK) {
    StopStick();
  }
  else if (s->role == ROLE_DEADTAP) {
    if (C.hud.bDead && !bCancelled && !s->bSlid && hypot(x - s->xDown, y - s->yDown) < IOSTOUCH_TAP_SLIDE) _ulRequests |= IOSTOUCH_REQ_RESTART;
  }
  memset(s, 0, sizeof(*s));
  if (C.bWheelOpen) WheelUpdateHot();
  UpdateAim();
}

/* ------------------------------------------------------------ once a frame */
static unsigned int HeldBits(void)
{
  unsigned int ul = 0;
  /* the aim stick pushed full -- only the one that has the aim, so fire, aim and what's drawn agree */
  if (_pAimWinner && _pAimWinner->touch && _pAimWinner->role == ROLE_AIM && _pAimWinner->bAimFire) ul |= IOSTOUCH_FIRE;
  for (int b = 0; b < BTN_COUNT; b++) {
    if (C.aButtonHeld[b] && C.aButtons[b].kind == KIND_HOLD) ul |= C.aButtons[b].ulAction;
  }
  return ul;
}

static void TickHolds(double now)
{
  /* Holds go off only once the finger is known to have stayed down long
     enough: timed to the previous tick, by which every lift made before it
     has come in (Sam's IOSTouch_TickHolds) */
  const double known = _tLastTick > 0.0 ? _tLastTick : now;
  _tLastTick = now;


  /* NEXT/PREV WPN held still: the wheel opens to tap */
  float afWheel[2] = { 0.0f, 0.0f };
  for (int i = 0; i < MAX_TOUCHES && !C.bWheelOpen; i++) {
    Slot *s = &_aSlots[i];
    if (!IsWheelPress(s) || s->bFired) continue;
    if (known - s->tDown >= IOSTOUCH_WHEEL_HOLD) {
      if (WheelCanOpen()) {
        WheelOpen(s, 1);
        afWheel[0] = afWheel[1] = 0.0f;
        break;
      }
      s->bFired = 1;
      continue;
    }
    if (WheelCanOpen()) {
      float *pf = &afWheel[s->button == BTN_PREVWPN];
      *pf = fmaxf(*pf, (float)fmin(fmax(0.0, (now - s->tDown) / IOSTOUCH_WHEEL_HOLD), 1.0));
    }
  }
  C.afWheelRing[0] = afWheel[0];
  C.afWheelRing[1] = afWheel[1];

  float fSave = 0, fLoad = 0, fMenu = 0;
  int iDone = -1;
  for (int i = 0; i < MAX_TOUCHES; i++) {
    Slot *s = &_aSlots[i];
    if (!s->touch || s->bFired || s->role != ROLE_BUTTON) continue;
    const int b = s->button;
    if (b == BTN_SAVE || b == BTN_LOAD) {
      if (!IsOnButton(s->x, s->y, b)) {
        s->bFired = 1;
        continue;
      }
      if (known - s->tDown >= IOSTOUCH_QUICKSAVE_HOLD) {
        if (iDone != BTN_SAVE) iDone = b; /* both done together: SAVE wins */
      }
      else {
        const float f = (float)fmin(fmax(0.0, (now - s->tDown) / IOSTOUCH_QUICKSAVE_HOLD), 1.0);
        if (b == BTN_SAVE) fSave = fmaxf(fSave, f);
        else fLoad = fmaxf(fLoad, f);
      }
    }
    else if (b == BTN_MENU) {
      if (!IsOnButton(s->x, s->y, b)) {
        s->bFired = 1;
        continue;
      }
      if (known - s->tDown >= IOSTOUCH_MENU_HOLD) {
        s->bFired = 1;
        SetTrayOpen(1);
      }
      else {
        fMenu = (float)fmin(fmax(0.0, (now - s->tDown) / IOSTOUCH_MENU_HOLD), 1.0);
      }
    }
  }
  if (iDone >= 0) {
    /* one save or load, once: every hold on SAVE and LOAD ends */
    for (int j = 0; j < MAX_TOUCHES; j++) {
      Slot *o = &_aSlots[j];
      if (o->touch && o->role == ROLE_BUTTON && (o->button == BTN_SAVE || o->button == BTN_LOAD)) o->bFired = 1;
    }
    fSave = fLoad = 0;
    if (!(_ulRequests & (IOSTOUCH_REQ_QUICKSAVE | IOSTOUCH_REQ_QUICKLOAD))) _ulRequests |= C.aButtons[iDone].ulAction;
  }
  C.fSaveRing = fSave;
  C.fLoadRing = fLoad;
  C.fMenuRing = fMenu;

}

static void CountFrames(unsigned int ulFrames, double now)
{
  if (!C.bShowFps) return;
  if (!_bFpsBase || ulFrames < _ulFpsFrames) {
    _bFpsBase = 1;
    _ulFpsFrames = ulFrames;
    _tFpsBase = now;
  }
  else if (now - _tFpsBase >= IOSTOUCH_FPS_PERIOD) {
    C.iFpsShown = (int)lround((double)(ulFrames - _ulFpsFrames) / (now - _tFpsBase));
    _ulFpsFrames = ulFrames;
    _tFpsBase = now;
  }
}

unsigned int IOSTouchCore_Tick(double now, unsigned int ulFramesDrawn)
{
  Init();
  if (C.iMode != IOSTOUCH_HIDDEN) TickHolds(now);
  UpdateAim(); /* (a hold may have opened the wheel, which lets go of the aiming fingers) */
  CountFrames(ulFramesDrawn, now);
  const unsigned int ul = _ulRequests;
  _ulRequests = 0;
  return ul;
}

void IOSTouchCore_ReadInput(IOSTouchInput *p)
{
  Init();
  memset(p, 0, sizeof(*p));
  if (C.iMode != IOSTOUCH_GAMEPLAY) {
    memset(_aubPulse, 0, sizeof(_aubPulse));
    _iPick = 0;
    return;
  }
  /* the stick: nothing in the dead zone, then up to full speed at
     IOSTOUCH_STICK_RUN, where it runs */
  if (C.bStickActive) {
    const double len = sqrt(C.fStickX * C.fStickX + C.fStickY * C.fStickY);
    if (len > IOSTOUCH_STICK_DEADZONE) {
      double k = (len - IOSTOUCH_STICK_DEADZONE) / (IOSTOUCH_STICK_RUN - IOSTOUCH_STICK_DEADZONE);
      if (k > 1.0) k = 1.0;
      p->fMoveX = (float)(C.fStickX / len * k);
      p->fMoveY = (float)(-C.fStickY / len * k);
      if (len >= IOSTOUCH_STICK_RUN) p->ulButtons |= IOSTOUCH_RUN;
    }
  }
  p->bAim = C.bAim;
  p->fAimX = (float)C.fAimX;
  p->fAimY = (float)C.fAimY;
  const unsigned int ulHeld = HeldBits();
  _ulSeen |= ulHeld;
  _ulHeldLast = ulHeld;
  p->ulButtons |= ulHeld;
  /* one-read presses: never two reads in a row for the same bit, and not
     NEXT/PREV in the read the wheel's pick goes in */
  unsigned int ulGap = 0;
  for (int i = 0; i < 8; i++) {
    const unsigned int ul = 1u << i;
    if (_ulPulseGap & ul) continue;
    if (!_aubPulse[i]) continue;
    if ((ul & (IOSTOUCH_NEXTWEAPON | IOSTOUCH_PREVWEAPON)) && _iPick) continue;
    _aubPulse[i]--;
    p->ulButtons |= ul;
    ulGap |= ul;
  }
  _ulPulseGap = ulGap;
  if (_iPick) {
    p->iSelectWeapon = _iPick;
    _iPick = 0;
  }
}

/* ------------------------------------------------------------ modes */
static void ResetState(void)
{
  SetTrayOpen(0);
  DropTouches();
  if (C.bWheelOpen) {
    C.bWheelOpen = C.bWheelTapMode = 0;
    C.iWheelHot = -1;
  }
  C.afWheelRing[0] = C.afWheelRing[1] = 0.0f;
  C.fSaveRing = C.fLoadRing = C.fMenuRing = 0.0f;
  C.bLevelListOpen = 0;
  _ulRequests = 0;
  _tLastTick = 0.0;
  _bFpsBase = 0;
  memset(_aubPulse, 0, sizeof(_aubPulse));
  _ulPulseGap = _ulSeen = _ulHeldLast = 0;
  _iPick = 0;
  UpdateAim();
}

void IOSTouchCore_SetMode(int iMode, double now)
{
  Init();
  (void)now;
  if (iMode == C.iMode) return;
  C.iMode = iMode;
  ResetState();
  if (C.bTrayOpen) PlaceTray();
}

int IOSTouchCore_TakesTouchAt(double x, double y)
{
  Init();
  if (C.iMode == IOSTOUCH_GAMEPLAY) return 1;
  return C.iMode == IOSTOUCH_MENU && IOSTouchCore_ButtonAt(x, y) == BTN_BACK;
}

int IOSTouchCore_HoldsGame(void)
{
  Init();
  return C.iMode == IOSTOUCH_GAMEPLAY && (C.bWheelOpen || C.bLevelListOpen);
}

void IOSTouchCore_Reset(void)
{
  for (int i = 0; i < C.ctLevels && _ppszLevels; i++) free(_ppszLevels[i]);
  free(_ppszLevels);
  _ppszLevels = NULL;
  _bInit = 0;
  Init();
  memset(_aSlots, 0, sizeof(_aSlots));
  _uSeq = 0;
  _ulRequests = 0;
  _tLastTick = 0;
  _ulSeen = _ulHeldLast = _ulPulseGap = 0;
  memset(_aubPulse, 0, sizeof(_aubPulse));
  _iPick = 0;
  _bFpsBase = 0;
  _iLevelPick = -1;
  memset(_afLaidBar, 0, sizeof(_afLaidBar));
  memset(_afLaidText, 0, sizeof(_afLaidText));
  memset(_afLaidGap, 0, sizeof(_afLaidGap));
  _bLaidFull = -1;
}

/* ------------------------------------------------------------ levels */
void IOSTouchCore_SetLevels(const char *const *ppszNames, int ctLevels)
{
  Init();
  for (int i = 0; i < C.ctLevels && _ppszLevels; i++) free(_ppszLevels[i]);
  free(_ppszLevels);
  _ppszLevels = NULL;
  C.ctLevels = 0;
  if (!ppszNames || ctLevels <= 0) return;
  _ppszLevels = (char **)calloc((size_t)ctLevels, sizeof(char *));
  if (!_ppszLevels) return;
  for (int i = 0; i < ctLevels; i++) {
    const char *s = ppszNames[i] ? ppszNames[i] : "";
    const size_t n = strlen(s) + 1;
    _ppszLevels[i] = (char *)malloc(n);
    if (_ppszLevels[i]) memcpy(_ppszLevels[i], s, n);
  }
  C.ctLevels = ctLevels;
  if (C.bTrayOpen) PlaceTray();
}

const char *IOSTouchCore_LevelName(int i)
{
  if (i < 0 || i >= C.ctLevels || !_ppszLevels || !_ppszLevels[i]) return "";
  return _ppszLevels[i];
}

void IOSTouchCore_PickLevel(int i)
{
  if (!C.bLevelListOpen || i < 0 || i >= C.ctLevels) return;
  _iLevelPick = i;
  C.ctLevelTicks++;
  C.bLevelListOpen = 0;
}

void IOSTouchCore_CloseLevelList(void) { C.bLevelListOpen = 0; }

int IOSTouchCore_TakeLevelPick(void)
{
  Init();
  const int i = _iLevelPick;
  _iLevelPick = -1;
  return i;
}

/* ------------------------------------------------------------ looks */
int IOSTouchCore_IsLit(int b)
{
  if (b < 0 || b >= BTN_COUNT) return 0;
  if (C.aButtonHeld[b] || (C.bWheelOpen && b == C.iWheelButton)) return 1;
  for (int t = 0; t < MAX_TOUCHES; t++) {
    const Slot *s = &_aSlots[t];
    if (s->touch && s->role == ROLE_BUTTON && s->button == BTN_MENU && s->bFired && s->trayButton == b) return 1;
  }
  return 0;
}

int IOSTouchCore_IsOn(int b)
{
  switch (b) {
  case BTN_TRAY_FPS: return C.bShowFps;
  case BTN_TRAY_SLOW: return C.hud.iSlowMo != 0;
  case BTN_TRAY_CAMERA: return C.bShowCamera;
  case BTN_TRAY_CHEAT: return C.hud.bCheatOn;
  case BTN_TRAY_FOLLOW: return C.hud.bFollowing;
  case BTN_CHEAT: return C.hud.bCheatOn;
  default: return 0;
  }
}

const char *IOSTouchCore_Label(int b, char *buf, int size)
{
  if (b == BTN_TRAY_SLOW) {
    snprintf(buf, (size_t)size, "%s", C.hud.iSlowMo >= 4 ? "SLOW \xC2\xBC" : (C.hud.iSlowMo ? "SLOW \xC2\xBD" : "SLOW")); /* 1/4, 1/2 */
    return buf;
  }
  if (b == BTN_SLOWMO) {
    snprintf(buf, (size_t)size, "%s", C.hud.iSlowMo >= 4 ? "\xC2\xBC\xC3\x97" : "\xC2\xBD\xC3\x97"); /* 1/4x, 1/2x */
    return buf;
  }
  snprintf(buf, (size_t)size, "%s", (b >= 0 && b < BTN_COUNT) ? C.aButtons[b].label : "");
  return buf;
}
