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

const float g_afIOSTouchGyroSens[IOSTOUCH_GYRO_NUM_SENS] = { 1.0f, 1.5f, 2.0f, 3.0f };

/* The buttons as made (labels, kinds, sizes); the layout sets where */
static const IOSTouchButton _aButtonDefs[BTN_COUNT] = {
  [BTN_FIRE]        = { "FIRE",        KIND_HOLD,      IOSTOUCH_FIRE,          0, IOSTOUCH_FIRE_RADIUS,    0, 1 },
  [BTN_DUCK]        = { "DUCK",        KIND_HOLD,      IOSTOUCH_DUCK,          0, IOSTOUCH_DUCK_RADIUS,    0, 1 },
  [BTN_EXECUTE]     = { "EXEC-\nUTE",  KIND_HOLD,      IOSTOUCH_EXECUTE,       0, IOSTOUCH_EXECUTE_RADIUS, 0, 1 },
  [BTN_NEXTWPN]     = { "NEXT\nWPN",   KIND_WHEEL,     IOSTOUCH_NEXTWEAPON,    1, 12, 19, 0 },
  [BTN_PREVWPN]     = { "PREV\nWPN",   KIND_WHEEL,     IOSTOUCH_PREVWEAPON,    1, 12, 19, 0 },
  [BTN_SAVE]        = { "SAVE",        KIND_HOLDSAVE,  IOSTOUCH_REQ_QUICKSAVE, 1, 12, 25, 0 },
  [BTN_LOAD]        = { "LOAD",        KIND_HOLDSAVE,  IOSTOUCH_REQ_QUICKLOAD, 1, 12, 25, 0 },
  [BTN_MENU]        = { "MENU",        KIND_MENU,      IOSTOUCH_REQ_MENU,      1, 12, 25, 0 },
  [BTN_CHEAT]       = { "",            KIND_TAP,       IOSTOUCH_REQ_CHEAT,     0, 12, 12, 0 },
  [BTN_CAMERA]      = { "",            KIND_CAMERA,    0,                      0, 12, 12, 0 },
  [BTN_FOLLOWNEXT]  = { "",            KIND_TAP,       IOSTOUCH_REQ_FOLLOWNEXT, 0, 12, 12, 0 },
  [BTN_SLOWMO]      = { "",            KIND_TAP,       IOSTOUCH_REQ_SLOWSPEED, 1, 12, 15, 0 },
  [BTN_FREEZECLOSE] = { "",            KIND_CLOSE,     IOSTOUCH_REQ_UNFREEZE,  0, IOSTOUCH_FREEZE_CLOSE_R, 0, 0 },
  [BTN_TRAY_SENS]   = { "SENS",        KIND_TRAY,      0,                      0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_GYRO]   = { "GYRO",        KIND_TRAY,      0,                      0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_FPS]    = { "FPS",         KIND_TRAY,      0,                      0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_KEYS]   = { "",            KIND_TRAY,      IOSTOUCH_REQ_KEYBOARD,  0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_CHEAT]  = { "CHEAT",       KIND_TRAY,      IOSTOUCH_REQ_CHEAT,     0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_LEVEL]  = { "LEVEL",       KIND_TRAY,      IOSTOUCH_REQ_LEVEL,     0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_AUTOPLAY] = { "AUTO-\nPLAY", KIND_TRAY,    IOSTOUCH_REQ_AUTOPLAY,  0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_CAMERA] = { "",            KIND_TRAY,      0,                      0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_TRAY_FOLLOW] = { "FOLLOW",      KIND_TRAY,      IOSTOUCH_REQ_FOLLOW,    0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_PHOTO_CLEAN] = { "CLEAN",       KIND_PHOTOKIND, IOSTOUCH_PHOTO_CLEAN,   0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_PHOTO_HUD]   = { "HUD",         KIND_PHOTOKIND, IOSTOUCH_PHOTO_HUD,     0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
  [BTN_PHOTO_HUDBTNS] = { "HUD+\nBTNS", KIND_PHOTOKIND, IOSTOUCH_PHOTO_HUDBTNS, 0, IOSTOUCH_TRAY_RADIUS, 0, 0 },
};

/* ------------------------------------------------------------ state */
typedef enum {
  ROLE_NONE = 0,
  ROLE_STICK,
  ROLE_AIM,
  ROLE_BUTTON,
  ROLE_IGNORED, /* does nothing until it lifts */
  ROLE_WHEEL,   /* on the open wheel (or the NEXT/PREV press that opened it) */
  ROLE_PAN,     /* freeze: drags the view */
  ROLE_AUTO,    /* autoplay: a tap (of a double tap) or a long press */
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
  int bSlid;           /* camera, autoplay: moved too far for a tap or a hold */
  int trayButton;      /* MENU held open: the tray button under it, or -1 */
  /* aiming (ROLE_AIM, or a KIND_HOLD button): the point it is measured from, armed past IOSTOUCH_AIM_START */
  double aimOX, aimOY;
  int bAimArmed;
  double aimX, aimY;
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
static double _fPanX = 0, _fPanY = 0, _fTurn = 0;
/* aiming: the gyro's turn of the aim, in degrees, while a thumb aims */
static double _fAimGyroDeg = 0.0;

/* the burst */
static const void *_pBurstTouch = NULL;
static double _tBurstNext = 0.0;
/* autoplay's double tap: the first tap's lift (time, place; -1: none) */
static double _tAutoTapLift = -1.0, _fAutoTapX = 0, _fAutoTapY = 0;
/* FPS */
static int _bFpsBase = 0;
static unsigned int _ulFpsFrames = 0;
static double _tFpsBase = 0.0;
/* gyro */
static int _bGyroWasOk = 0, _iGyroLastSide = 0;
static double _tGyroLastFrame = 0.0, _tGyroTurned = -1e9;
/* levels */
static char **_ppszLevels = NULL;
static int _iLevelPick = -1;
/* the HUD rectangles the layout was made for (fractions) */
static float _afLaidBar[4], _afLaidText[4], _afLaidGap[4];
static double _inL, _inT, _inR, _inB;

static void Init(void)
{
  if (_bInit) return;
  _bInit = 1;
  memset(&C, 0, sizeof(C));
  memcpy(C.aButtons, _aButtonDefs, sizeof(C.aButtons));
  C.iGyroMode = IOSTOUCH_GYRO_OFF;
  C.iGyroSens = IOSTOUCH_GYRO_DEFAULT_SENS;
  C.bShowCamera = 1;
  C.bShowCheat = 0;
  C.iPhotoKinds = IOSTOUCH_PHOTO_CLEAN;
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
static int FollowShows(void) { return C.hud.bCheatOn || C.iMode == IOSTOUCH_AUTOPLAY; }

static int IsTray(int b) { return b >= BTN_TRAY_FIRST && b <= BTN_PHOTO_LAST; }

int IOSTouchCore_IsShown(int b)
{
  Init();
  if (b < 0 || b >= BTN_COUNT || C.iMode != IOSTOUCH_GAMEPLAY) return 0;
  if (C.bFreeze) return b == BTN_CAMERA || b == BTN_FREEZECLOSE;
  switch (b) {
  case BTN_FREEZECLOSE: return 0;
  case BTN_CHEAT: return C.bShowCheat;
  case BTN_CAMERA: return C.bShowCamera;
  case BTN_FOLLOWNEXT: return C.hud.bFollowing;
  case BTN_SLOWMO: return C.hud.iSlowMo != 0;
  case BTN_TRAY_LEVEL: return C.bTrayOpen && LevelShows();
  case BTN_TRAY_FOLLOW: return C.bTrayOpen && FollowShows();
  default: break;
  }
  if (b >= BTN_TRAY_FIRST && b <= BTN_TRAY_LAST) return C.bTrayOpen;
  if (b >= BTN_PHOTO_FIRST && b <= BTN_PHOTO_LAST) return C.bTrayOpen && C.bPhotoRowOpen;
  return 1;
}

/* ------------------------------------------------------------ the tray's layout */
/* Its buttons (those that show) right-aligned under MENU, IOSTOUCH_TRAY_STEP
   apart, wrapping to a second row where one row would leave the screen; the
   PHOTO options a row under the tray's CAMERA; the version under it all */
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
  const double y0 = C.fRow2Y + C.fPillH * 0.5 + 6.0 + IOSTOUCH_TRAY_BACK;
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
  /* PHOTO's ticks: under the lowest row, centred on the tray's CAMERA, kept on screen */
  {
    const double py = lastY1 + IOSTOUCH_TRAY_BACK;
    double px = C.aButtons[BTN_TRAY_CAMERA].x;
    px = fmax(px, C.left + IOSTOUCH_TRAY_STEP + IOSTOUCH_TRAY_BACK);
    px = fmin(px, C.right - IOSTOUCH_TRAY_STEP - IOSTOUCH_TRAY_BACK);
    for (int b = BTN_PHOTO_FIRST; b <= BTN_PHOTO_LAST; b++) {
      C.aButtons[b].x = px + (b - BTN_PHOTO_HUD) * IOSTOUCH_TRAY_STEP;
      C.aButtons[b].y = py;
    }
    C.rPhotoBack = (IOSTouchRect){ 1, px - IOSTOUCH_TRAY_STEP - IOSTOUCH_TRAY_BACK, py - IOSTOUCH_TRAY_BACK,
                                   px + IOSTOUCH_TRAY_STEP + IOSTOUCH_TRAY_BACK, py + IOSTOUCH_TRAY_BACK };
  }
  /* the version: just below the tray (below PHOTO's row while it is open) */
  C.fVersionX = (C.arTrayBack[0].x0 + C.arTrayBack[0].x1) * 0.5;
  C.fVersionY = (C.bPhotoRowOpen ? C.rPhotoBack.y1 : lastY1) + 4.0 + IOSTOUCH_VERSION_H * 0.5;
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

void IOSTouchCore_Layout(double W, double H, double inL, double inT, double inR, double inB)
{
  Init();
  C.W = W;
  C.H = H;
  _inL = inL; _inT = inT; _inR = inR; _inB = inB;
  memcpy(_afLaidBar, C.hud.afTopBar, sizeof(_afLaidBar));
  memcpy(_afLaidText, C.hud.afTopText, sizeof(_afLaidText));
  memcpy(_afLaidGap, C.hud.afTopGap, sizeof(_afLaidGap));
  C.left = fmax(inL, 8.0);
  C.right = W - fmax(inR, 8.0);
  C.top = inT;
  C.bottom = H - fmax(inB, 8.0);

  /* Bottom right: FIRE, DUCK left of it, EXECUTE straight above (mock v4) */
  const double fx = C.right - IOSTOUCH_FIRE_IN_X, fy = C.bottom - IOSTOUCH_FIRE_IN_Y;
  SetCircle(BTN_FIRE, fx, fy, IOSTOUCH_FIRE_RADIUS);
  SetCircle(BTN_DUCK, fx - IOSTOUCH_CLUSTER_STEP, fy, IOSTOUCH_DUCK_RADIUS);
  SetCircle(BTN_EXECUTE, fx, fy - IOSTOUCH_CLUSTER_STEP, IOSTOUCH_EXECUTE_RADIUS);

  /* The game's status strip: the pills in it, as tall as it less ~5 pt */
  C.rStrip = FracRect(C.hud.afTopBar, W, H);
  if (!C.rStrip.bValid) C.rStrip = (IOSTouchRect){ 1, 0, 0, W, H * IOSTOUCH_STRIP_FRAC };
  const double stripH = C.rStrip.y1 - C.rStrip.y0;
  const double ph = fmin(fmax(stripH - 5.0, IOSTOUCH_PILL_MIN_H), IOSTOUCH_PILL_MAX_H);
  C.fPillH = ph;
  const double cy = fmax((C.rStrip.y0 + C.rStrip.y1) * 0.5, fmax(C.top, 1.0) + ph * 0.5);
  /* (a pill taller than the strip hangs below it) */
  const double row1Bottom = fmax(C.rStrip.y1, cy + ph * 0.5);
  C.fRow2Y = row1Bottom + IOSTOUCH_ROW_GAP + ph * 0.5;

  /* SAVE, LOAD, MENU at the right end */
  const double sw = IOSTOUCH_PILL_W, step = IOSTOUCH_PILL_W + IOSTOUCH_PILL_GAP;
  SetPill(BTN_MENU, C.right - 2.0 - sw * 0.5, cy, sw, ph);
  SetPill(BTN_LOAD, C.aButtons[BTN_MENU].x - step, cy, sw, ph);
  SetPill(BTN_SAVE, C.aButtons[BTN_LOAD].x - step, cy, sw, ph);

  /* NEXT WPN, PREV WPN: in the status text's gap if two fit, else left of SAVE */
  const IOSTouchRect rText = FracRect(C.hud.afTopText, W, H);
  const IOSTouchRect rGap = FracRect(C.hud.afTopGap, W, H);
  const double textEnd = rText.bValid ? rText.x1 : C.left;
  const double ww = IOSTOUCH_WHEELPILL_W;
  int bWheelInGap = 0;
  if (rGap.x1 - rGap.x0 >= 2.0 * ww + 6.0) {
    const double mid = (rGap.x0 + rGap.x1) * 0.5;
    if (mid - IOSTOUCH_WHEELPILL_STEP * 0.5 - ww * 0.5 >= C.left && mid + IOSTOUCH_WHEELPILL_STEP * 0.5 + ww * 0.5 <= C.aButtons[BTN_SAVE].x - sw * 0.5) {
      SetPill(BTN_NEXTWPN, mid - IOSTOUCH_WHEELPILL_STEP * 0.5, cy, ww, ph);
      SetPill(BTN_PREVWPN, mid + IOSTOUCH_WHEELPILL_STEP * 0.5, cy, ww, ph);
      bWheelInGap = 1;
    }
  }

  /* Leftwards from SAVE: (NEXT/PREV WPN if not in the gap), the cheat icon,
     the camera icon, the follow arrow, the slow-motion label -- each in the
     strip while it stays right of the status text, the rest in a second row
     under it, leftwards from the FPS readout at the right. The slots stay
     put whether or not their icon shows, so nothing moves under a finger. */
  int aiChain[6], ctChain = 0;
  if (!bWheelInGap) {
    aiChain[ctChain++] = BTN_PREVWPN;
    aiChain[ctChain++] = BTN_NEXTWPN;
    SetPill(BTN_NEXTWPN, 0, 0, ww, ph);
    SetPill(BTN_PREVWPN, 0, 0, ww, ph);
  }
  aiChain[ctChain++] = BTN_CHEAT;
  aiChain[ctChain++] = BTN_CAMERA;
  aiChain[ctChain++] = BTN_FOLLOWNEXT;
  aiChain[ctChain++] = BTN_SLOWMO;
  SetCircle(BTN_CHEAT, 0, 0, ph * 0.5);
  SetCircle(BTN_CAMERA, 0, 0, ph * 0.5);
  SetCircle(BTN_FOLLOWNEXT, 0, 0, IOSTOUCH_ARROW_W * 0.5);
  SetPill(BTN_SLOWMO, 0, 0, IOSTOUCH_SLOWMO_W, fmin(ph, 24.0));
  double xr = C.aButtons[BTN_SAVE].x - sw * 0.5 - IOSTOUCH_ROW_GAP;
  int i = 0;
  for (; i < ctChain; i++) {
    const int b = aiChain[i];
    const double w = ButtonW(b);
    if (xr - w < textEnd + IOSTOUCH_ROW_GAP || xr - w < C.left) break;
    C.aButtons[b].x = xr - w * 0.5;
    C.aButtons[b].y = cy;
    xr -= w + IOSTOUCH_ROW_GAP;
  }
  /* the second row: the FPS readout under LOAD and MENU, then what didn't fit */
  C.rFps = (IOSTouchRect){ 1, C.right - 2.0 - IOSTOUCH_FPS_W, C.fRow2Y - IOSTOUCH_FPS_H * 0.5,
                           C.right - 2.0, C.fRow2Y + IOSTOUCH_FPS_H * 0.5 };
  xr = C.rFps.x0 - IOSTOUCH_ROW_GAP - 2.0;
  for (; i < ctChain; i++) {
    const int b = aiChain[i];
    const double w = ButtonW(b);
    if (C.aButtons[b].r * 2.0 > ph) C.aButtons[b].r = ph * 0.5;
    C.aButtons[b].x = xr - w * 0.5;
    C.aButtons[b].y = C.fRow2Y;
    xr -= w + IOSTOUCH_ROW_GAP;
  }

  /* freeze's X where MENU is */
  SetCircle(BTN_FREEZECLOSE, C.right - IOSTOUCH_FREEZE_CLOSE_R - 4.0, fmax(cy, IOSTOUCH_FREEZE_CLOSE_R + 4.0), IOSTOUCH_FREEZE_CLOSE_R);

  /* messages under the strip, in the middle */
  C.fMessageX = (C.left + C.right) * 0.5;
  C.fMessageY = C.fRow2Y + 22.0;

  PlaceTray();
  WheelGeometry();
}

int IOSTouchCore_SetHud(const IOSTouchHud *pHud)
{
  Init();
  const int bWasCheat = C.hud.bCheatOn;
  if (pHud) C.hud = *pHud;
  if (bWasCheat != C.hud.bCheatOn && C.bTrayOpen) PlaceTray(); /* (LEVEL, FOLLOW come and go) */
  if (!pHud || C.W <= 0) return 0;
  return memcmp(_afLaidBar, C.hud.afTopBar, sizeof(_afLaidBar)) || memcmp(_afLaidText, C.hud.afTopText, sizeof(_afLaidText))
         || memcmp(_afLaidGap, C.hud.afTopGap, sizeof(_afLaidGap));
}

/* ------------------------------------------------------------ hit testing */
int IOSTouchCore_ButtonAt(double x, double y)
{
  Init();
  int best = -1;
  double bestDist = 0;
  /* The open tray, and PHOTO's row, on top: anywhere on a row's backing is
     on its nearest button */
  if (C.bTrayOpen) {
    for (int b = BTN_TRAY_FIRST; b <= BTN_PHOTO_LAST; b++) {
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
  C.fTrayCamRing = 0.0f;
  if (bOpen) {
    PlaceTray();
    return;
  }
  C.bPhotoRowOpen = 0;
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
  case BTN_TRAY_SENS:
    if (C.bGyroAvailable && C.iGyroMode != IOSTOUCH_GYRO_OFF) C.iGyroSens = (C.iGyroSens + 1) % IOSTOUCH_GYRO_NUM_SENS;
    return; /* (stays open) */
  case BTN_TRAY_GYRO:
    if (C.bGyroAvailable) C.iGyroMode = (C.iGyroMode + 1) % IOSTOUCH_GYRO_NUM_MODES;
    return;
  case BTN_TRAY_FPS:
    C.bShowFps = !C.bShowFps;
    _bFpsBase = 0;
    C.iFpsShown = -1;
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
  case BTN_TRAY_AUTOPLAY:
    _ulRequests |= IOSTOUCH_REQ_AUTOPLAY;
    break;
  case BTN_TRAY_CAMERA:
    C.bShowCamera = !C.bShowCamera;
    return;
  case BTN_TRAY_FOLLOW:
    _ulRequests |= IOSTOUCH_REQ_FOLLOW;
    break;
  case BTN_PHOTO_CLEAN: case BTN_PHOTO_HUD: case BTN_PHOTO_HUDBTNS: {
    const int k = (int)C.aButtons[b].ulAction;
    if ((C.iPhotoKinds & k) && (C.iPhotoKinds & ~k) == 0) {
      IOSTouchCore_ShowMessage("At least one kind of photo stays ticked", 0, now);
      return;
    }
    C.iPhotoKinds ^= k;
    return;
  }
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
  C.fSaveRing = C.fLoadRing = C.fMenuRing = C.fCameraRing = 0.0f;
  C.afWheelRing[0] = C.afWheelRing[1] = 0.0f;
  /* a NEXT/PREV tap not read yet would undo the pick */
  memset(_aubPulse, 0, sizeof(_aubPulse));
  _fTurn = 0;
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
  if (pw) C.wpn = *pw;
  else memset(&C.wpn, 0, sizeof(C.wpn));
  if (C.bWheelOpen && !WheelCanOpen()) WheelClose(-1);
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
static void StartBurst(const void *touch, double now)
{
  _pBurstTouch = touch;
  C.bBursting = 1;
  C.ctBurstShots = 1;
  _tBurstNext = now + IOSTOUCH_BURST_GAP;
  _ulRequests |= IOSTOUCH_REQ_BURST;
}

static void EndBurst(void)
{
  _pBurstTouch = NULL;
  C.bBursting = 0;
}

static void EndFreeze(void)
{
  if (!C.bFreeze) return;
  C.bFreeze = 0;
  _ulRequests |= IOSTOUCH_REQ_UNFREEZE;
}

static void StartFreeze(void)
{
  SetTrayOpen(0);
  for (int i = 0; i < MAX_TOUCHES; i++) {
    Slot *o = &_aSlots[i];
    if (!o->touch) continue;
    if (o->role == ROLE_BUTTON && C.aButtonHeld[o->button] > 0) C.aButtonHeld[o->button]--;
    if (o->role == ROLE_STICK) StopStick();
    o->role = ROLE_IGNORED;
  }
  C.fSaveRing = C.fLoadRing = C.fMenuRing = C.fCameraRing = 0.0f;
  C.bFreeze = 1;
  C.ctCameraTaps++;
  _fPanX = _fPanY = 0;
  _ulRequests |= IOSTOUCH_REQ_FREEZE;
}

/* A camera press moved to (x,y): sideways a burst; mostly down far enough a
   zoom-out shot; mostly up far enough slow motion on/off. Any of these, or
   any move of IOSTOUCH_CAMERA_SLIDE, ends its tap and hold. */
static void CameraMoved(Slot *s, double x, double y, double t)
{
  if (s->bFired) return;
  const double dx = x - s->xDown, dy = y - s->yDown;
  const int bVertical = fabs(dy) >= fabs(dx) && dy != 0.0;
  const double upFar = fmin(IOSTOUCH_SLOWMO_SWIPE, fmax(s->yDown - IOSTOUCH_SLOWMO_EDGE, IOSTOUCH_SLOWMO_MIN));
  if (hypot(dx, dy) >= IOSTOUCH_CAMERA_SLIDE) s->bSlid = 1;
  if (bVertical && dy > 0 && dy >= IOSTOUCH_ZOOMOUT_SWIPE) {
    s->bFired = 1;
    _ulRequests |= IOSTOUCH_REQ_ZOOMOUT;
  }
  else if (bVertical && dy < 0 && -dy >= upFar) {
    s->bFired = 1;
    C.ctCameraTaps++;
    _ulRequests |= IOSTOUCH_REQ_SLOWMO;
  }
  else if (!bVertical && hypot(dx, dy) >= IOSTOUCH_CAMERA_SLIDE) {
    s->bFired = 1;
    if (!_pBurstTouch) StartBurst(s->touch, t);
  }
  if (s->bFired) C.fCameraRing = 0.0f;
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

/* An aiming finger moved: the direction of its drag from where it landed
   (its first IOSTOUCH_AIM_START say nothing); the point it is measured from
   trails IOSTOUCH_AIM_LEASH behind, so turning back answers at once */
static void AimMoved(Slot *s, double x, double y)
{
  double dx = x - s->aimOX, dy = y - s->aimOY, r = hypot(dx, dy);
  if (!s->bAimArmed && r < IOSTOUCH_AIM_START) return;
  if (!s->bAimArmed) {
    s->bAimArmed = 1;
    s->uSeq = ++_uSeq; /* (the newest aim wins) */
  }
  if (r > IOSTOUCH_AIM_LEASH) {
    const double k = (r - IOSTOUCH_AIM_LEASH) / r;
    s->aimOX += dx * k;
    s->aimOY += dy * k;
    dx = x - s->aimOX;
    dy = y - s->aimOY;
    r = IOSTOUCH_AIM_LEASH;
  }
  if (r > 1e-6) {
    s->aimX = dx / r;
    s->aimY = -dy / r;
  }
}

static int AimsNow(const Slot *s)
{
  if (!s->touch || !s->bAimArmed) return 0;
  return s->role == ROLE_AIM || (s->role == ROLE_BUTTON && C.aButtons[s->button].bAimWhileHeld);
}

/* The newest aiming finger's direction, turned by tilt */
static void UpdateAim(void)
{
  const Slot *best = NULL;
  for (int i = 0; i < MAX_TOUCHES; i++) {
    if (AimsNow(&_aSlots[i]) && (!best || _aSlots[i].uSeq > best->uSeq)) best = &_aSlots[i];
  }
  if (!best) {
    C.bAim = 0;
    _fAimGyroDeg = 0.0;
    return;
  }
  const double a = _fAimGyroDeg * M_PI / 180.0;
  C.bAim = 1;
  C.fAimX = best->aimX * cos(a) - best->aimY * sin(a);
  C.fAimY = best->aimX * sin(a) + best->aimY * cos(a);
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

  if (C.iMode == IOSTOUCH_AUTOPLAY) {
    s->role = ROLE_AUTO;
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
  /* Freeze: its X, the camera (a photo), or a drag of the view */
  if (C.bFreeze) {
    s->button = IOSTouchCore_ButtonAt(x, y);
    s->role = s->button >= 0 ? ROLE_BUTTON : ROLE_PAN;
    if (s->button >= 0) C.aButtonHeld[s->button]++;
    return;
  }
  s->button = IOSTouchCore_ButtonAt(x, y);
  /* An open tray closes at a touch anywhere else (on MENU: only that) */
  if (C.bTrayOpen && !IsTray(s->button)) {
    SetTrayOpen(0);
    if (s->button == BTN_MENU) {
      s->role = ROLE_IGNORED;
      return;
    }
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
  s->role = ROLE_AIM;
}

void IOSTouchCore_TouchMoved(const void *touch, double x, double y, double t)
{
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
  case ROLE_AIM:
    AimMoved(s, x, y);
    break;
  case ROLE_PAN:
    _fPanX += x - s->x;
    _fPanY += y - s->y;
    break;
  case ROLE_AUTO:
    if (hypot(x - s->xDown, y - s->yDown) >= IOSTOUCH_AUTO_STILL) s->bSlid = 1;
    break;
  case ROLE_BUTTON: {
    const int b = s->button;
    const int kind = C.aButtons[b].kind;
    if (C.aButtons[b].bAimWhileHeld) AimMoved(s, x, y);
    if (b == BTN_MENU && s->bFired && C.bTrayOpen) {
      const int tb = IOSTouchCore_ButtonAt(x, y);
      s->trayButton = IsTray(tb) ? tb : -1;
    }
    /* SAVE, LOAD, MENU: a finger that leaves the button is done, even if it comes back */
    if ((kind == KIND_HOLDSAVE || kind == KIND_MENU) && !s->bFired && !IsOnButton(x, y, b)) s->bFired = 1;
    if (kind == KIND_CAMERA && !C.bFreeze) CameraMoved(s, x, y, t);
    if (kind == KIND_CAMERA && C.bFreeze && hypot(x - s->xDown, y - s->yDown) >= IOSTOUCH_CAMERA_SLIDE) s->bSlid = 1;
    if (b == BTN_TRAY_CAMERA && !IsOnButton(x, y, b)) {
      s->bFired = 1;
      C.fTrayCamRing = 0.0f;
    }
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
  if (s->touch == _pBurstTouch) EndBurst();
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
    case KIND_CLOSE:
      if (bOn) {
        if (p->kind == KIND_CLOSE) EndFreeze();
        else _ulRequests |= p->ulAction;
      }
      break;
    case KIND_CAMERA:
      if (bOn && !s->bFired && !s->bSlid) {
        if (C.bFreeze || held < IOSTOUCH_CAMERA_HOLD) _ulRequests |= IOSTOUCH_REQ_PHOTO;
        else StartFreeze();
      }
      C.fCameraRing = 0.0f;
      break;
    case KIND_TRAY:
    case KIND_PHOTOKIND:
      if (bOn && !(b == BTN_TRAY_CAMERA && s->bFired)) UseTrayButton(b, t);
      if (b == BTN_TRAY_CAMERA) C.fTrayCamRing = 0.0f;
      break;
    default:
      break;
    }
  }
  else if (s->role == ROLE_STICK) {
    StopStick();
  }
  else if (s->role == ROLE_AUTO) {
    if (!bCancelled && !s->bFired && !s->bSlid && held < IOSTOUCH_AUTO_TAP) {
      if (_tAutoTapLift >= 0 && s->tDown - _tAutoTapLift <= IOSTOUCH_AUTO_DOUBLE
          && hypot(x - _fAutoTapX, y - _fAutoTapY) <= IOSTOUCH_AUTO_NEAR) {
        _ulRequests |= IOSTOUCH_REQ_AUTONEXT;
        _tAutoTapLift = -1.0;
      }
      else {
        _tAutoTapLift = t;
        _fAutoTapX = x;
        _fAutoTapY = y;
      }
    }
    C.fAutoRing = 0.0f;
  }
  memset(s, 0, sizeof(*s));
  if (C.bWheelOpen) WheelUpdateHot();
  UpdateAim();
}

/* ------------------------------------------------------------ once a frame */
static unsigned int HeldBits(void)
{
  unsigned int ul = 0;
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

  /* autoplay's long press */
  if (C.iMode == IOSTOUCH_AUTOPLAY) {
    float ring = 0.0f;
    for (int i = 0; i < MAX_TOUCHES; i++) {
      Slot *s = &_aSlots[i];
      if (!s->touch || s->role != ROLE_AUTO || s->bFired || s->bSlid) continue;
      if (known - s->tDown >= IOSTOUCH_AUTO_HOLD) {
        s->bFired = 1;
        _ulRequests |= IOSTOUCH_REQ_AUTOSTOP;
        _tAutoTapLift = -1.0;
      }
      else {
        ring = fmaxf(ring, (float)fmax(0.0, (now - s->tDown) / IOSTOUCH_AUTO_HOLD));
      }
    }
    C.fAutoRing = ring > 1.0f ? 1.0f : ring;
    return;
  }

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

  float fSave = 0, fLoad = 0, fMenu = 0, fCam = 0, fTrayCam = 0;
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
    else if (b == BTN_CAMERA && !C.bFreeze && !s->bSlid) {
      if (known - s->tDown >= IOSTOUCH_CAMERA_HOLD) {
        s->bFired = 1;
        StartFreeze();
        break; /* (every touch is let go of) */
      }
      fCam = (float)fmin(fmax(0.0, (now - s->tDown) / IOSTOUCH_CAMERA_HOLD), 1.0);
    }
    else if (b == BTN_TRAY_CAMERA) {
      if (known - s->tDown >= IOSTOUCH_MENU_HOLD) {
        s->bFired = 1;
        C.bPhotoRowOpen = !C.bPhotoRowOpen;
        PlaceTray();
      }
      else {
        fTrayCam = (float)fmin(fmax(0.0, (now - s->tDown) / IOSTOUCH_MENU_HOLD), 1.0);
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
  C.fCameraRing = fCam;
  C.fTrayCamRing = fTrayCam;

  /* the burst: a photo every IOSTOUCH_BURST_GAP while its finger stays down */
  if (_pBurstTouch) {
    if (C.ctBurstShots >= IOSTOUCH_BURST_MAX) {
      EndBurst();
    }
    else if (now >= _tBurstNext) {
      C.ctBurstShots++;
      _tBurstNext += IOSTOUCH_BURST_GAP;
      if (_tBurstNext < now) _tBurstNext = now + IOSTOUCH_BURST_GAP;
      _ulRequests |= IOSTOUCH_REQ_BURST;
    }
  }
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
    _fPanX = _fPanY = _fTurn = 0;
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
  p->fPanX = (float)_fPanX;
  p->fPanY = (float)_fPanY;
  p->fTurn = (float)_fTurn;
  _fPanX = _fPanY = _fTurn = 0;
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
  C.fSaveRing = C.fLoadRing = C.fMenuRing = C.fCameraRing = C.fTrayCamRing = C.fAutoRing = 0.0f;
  C.bLevelListOpen = 0;
  EndBurst();
  _ulRequests = 0;
  EndFreeze(); /* (asks UNFREEZE, after the requests were dropped) */
  _tLastTick = 0.0;
  _bFpsBase = 0;
  _bGyroWasOk = 0;
  memset(_aubPulse, 0, sizeof(_aubPulse));
  _ulPulseGap = _ulSeen = _ulHeldLast = 0;
  _iPick = 0;
  _fPanX = _fPanY = _fTurn = 0;
  _tAutoTapLift = -1.0;
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

int IOSTouchCore_HoldsGame(void)
{
  Init();
  return C.iMode == IOSTOUCH_GAMEPLAY && (C.bWheelOpen || C.bFreeze || C.bLevelListOpen);
}

int IOSTouchCore_PhotoKinds(void)
{
  Init();
  return C.iPhotoKinds ? C.iPhotoKinds : IOSTOUCH_PHOTO_CLEAN;
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
  _fPanX = _fPanY = _fTurn = 0;
  _fAimGyroDeg = 0;
  _pBurstTouch = NULL;
  _tAutoTapLift = -1.0;
  _bFpsBase = 0;
  _bGyroWasOk = 0;
  _iGyroLastSide = 0;
  _tGyroTurned = -1e9;
  _iLevelPick = -1;
  memset(_afLaidBar, 0, sizeof(_afLaidBar));
  memset(_afLaidText, 0, sizeof(_afLaidText));
  memset(_afLaidGap, 0, sizeof(_afLaidGap));
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

/* ------------------------------------------------------------ tilt */
void IOSTouchGyro_Reset(IOSTouchGyroFilter *f) { memset(f, 0, sizeof(*f)); }

/* Sam's IOSTouch_GyroSample, turning only (POSTAL's view never tilts): how
   far the phone turned about up, smoothed where slow, a soft dead zone */
void IOSTouchGyro_Sample(IOSTouchGyroFilter *f, int side, double wx, double wy, double wz, double gx, double gy, double gz,
                         double t)
{
  (void)wy; /* (the tilt up and down: POSTAL has none) */
  const double dt = f->tLast > 0.0 ? t - f->tLast : 0.0;
  f->tLast = t;
  const double s = side;
  const double gn = sqrt(gx * gx + gy * gy + gz * gz);
  if (s == 0.0 || dt <= 0.0 || dt > IOSTOUCH_GYRO_MAX_DT || gn <= 0.1) return;
  const double ry = -s * wx, rz = wz;
  const double ux = -s * gy / gn, uy = s * gx / gn, uz = -gz / gn;
  const double tilt = sqrt(ux * ux + uy * uy);
  if (!(tilt < 0.5 || uy > -0.5 * tilt)) return; /* (on its way round to the other side) */
  double yaw = ry;
  if (uz >= 0.0 && (tilt < 0.5 || uy > 0.5 * tilt)) {
    yaw = uy * ry + uz * rz;
    const double mag = sqrt(ry * ry + rz * rz);
    yaw = copysign(fmin(fabs(yaw) * IOSTOUCH_GYRO_YAW_RELAX, mag), yaw);
  }
  yaw *= 180.0 / M_PI;
  double direct = (fabs(yaw) - IOSTOUCH_GYRO_SMOOTH * 0.5) / (IOSTOUCH_GYRO_SMOOTH * 0.5);
  direct = direct < 0.0 ? 0.0 : (direct > 1.0 ? 1.0 : direct);
  f->afSmooth[f->iNext] = yaw * (1.0 - direct);
  f->iNext = (f->iNext + 1) % IOSTOUCH_GYRO_SMOOTH_N;
  double sum = 0.0;
  for (int i = 0; i < IOSTOUCH_GYRO_SMOOTH_N; i++) sum += f->afSmooth[i];
  yaw = yaw * direct + sum / IOSTOUCH_GYRO_SMOOTH_N;
  const double m = fabs(yaw);
  if (m < IOSTOUCH_GYRO_SOFT) yaw *= m / IOSTOUCH_GYRO_SOFT;
  f->fYaw += yaw * dt;
}

int IOSTouchCore_GyroWanted(void)
{
  Init();
  return C.iMode == IOSTOUCH_GAMEPLAY && C.iGyroMode != IOSTOUCH_GYRO_OFF && C.bGyroAvailable;
}

/* TOUCH: a thumb is down where it moves or aims (the stick, the aim area,
   FIRE / DUCK / EXECUTE); not the top row, the wheel or the tray */
static int GyroThumbDown(void)
{
  for (int i = 0; i < MAX_TOUCHES; i++) {
    const Slot *s = &_aSlots[i];
    if (!s->touch) continue;
    if (s->role == ROLE_STICK || s->role == ROLE_AIM || (s->role == ROLE_BUTTON && C.aButtons[s->button].bAimWhileHeld))
      return 1;
  }
  return 0;
}

void IOSTouchCore_GyroFrame(double fYaw, int side, double now)
{
  Init();
  if (side != 0 && _iGyroLastSide != 0 && side != _iGyroLastSide) _tGyroTurned = now;
  if (side != 0) _iGyroLastSide = side;
  const int bOk = IOSTouchCore_GyroWanted() && side != 0
                  && (C.iGyroMode == IOSTOUCH_GYRO_ALWAYS || GyroThumbDown()) && !C.bTrayOpen
                  && now - _tGyroTurned >= IOSTOUCH_GYRO_TURN_HOLD && !IOSTouchCore_HoldsGame();
  const int bUse = bOk && _bGyroWasOk && now - _tGyroLastFrame <= IOSTOUCH_GYRO_MAX_FRAME;
  _bGyroWasOk = bOk;
  _tGyroLastFrame = now;
  if (!bUse) return;
  const double d = fYaw * g_afIOSTouchGyroSens[C.iGyroSens];
  if (C.bAim) {
    _fAimGyroDeg += d; /* turns the aim of the thumb that aims */
    UpdateAim();
  }
  else {
    _fTurn += d;       /* turns the dude */
  }
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
  case BTN_TRAY_GYRO: return C.bGyroAvailable && C.iGyroMode != IOSTOUCH_GYRO_OFF;
  case BTN_TRAY_CAMERA: return C.bShowCamera;
  case BTN_TRAY_CHEAT: return C.bShowCheat;
  case BTN_TRAY_FOLLOW: return C.hud.bFollowing;
  case BTN_CHEAT: return C.hud.bCheatOn;
  case BTN_CAMERA: return C.bBursting;
  case BTN_PHOTO_CLEAN: case BTN_PHOTO_HUD: case BTN_PHOTO_HUDBTNS:
    return (C.iPhotoKinds & (int)C.aButtons[b].ulAction) != 0;
  default: return 0;
  }
}

const char *IOSTouchCore_Label(int b, char *buf, int size)
{
  static const char *const astrMode[IOSTOUCH_GYRO_NUM_MODES] = { "OFF", "TOUCH", "ALWAYS" };
  if (b == BTN_TRAY_GYRO) {
    if (!C.bGyroAvailable) snprintf(buf, (size_t)size, "NO\nGYRO");
    else snprintf(buf, (size_t)size, "GYRO\n%s", astrMode[C.iGyroMode]);
    return buf;
  }
  if (b == BTN_TRAY_SENS) {
    snprintf(buf, (size_t)size, "SENS\n%.1fx", (double)g_afIOSTouchGyroSens[C.iGyroSens]);
    return buf;
  }
  if (b == BTN_SLOWMO) {
    snprintf(buf, (size_t)size, "%s", C.hud.iSlowMo >= 4 ? "\xC2\xBC\xC3\x97" : "\xC2\xBD\xC3\x97"); /* 1/4x, 1/2x */
    return buf;
  }
  snprintf(buf, (size_t)size, "%s", (b >= 0 && b < BTN_COUNT) ? C.aButtons[b].label : "");
  return buf;
}
