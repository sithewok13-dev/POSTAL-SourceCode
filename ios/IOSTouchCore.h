/* POSTAL for iOS: the touch controls' logic, as plain C (no UIKit), so the
   Linux harness (ios/tests/touch_core_test.c) drives this very code.

   IOSTouch.m hands it UIKit's touches (points, seconds), calls
   IOSTouchCore_Tick once a frame and draws what IOSTouchCore_Get() says.
   Main thread only. Time always comes in as an argument (seconds, one clock:
   UITouch.timestamp and CACurrentMediaTime share it); there is no clock in
   here.

   Ported from Serious Sam's ios/IOSTouch.m (its C core) to POSTAL's layout
   (jk-files wip-postal-ios PLAN.md, rig/mock.py), with the shared numbers of
   jk-files docs/HOUSE_STYLE.md section 3. */
#ifndef IOS_TOUCH_CORE_H
#define IOS_TOUCH_CORE_H
#include "IOSTouch.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ the numbers */
/* House standard (HOUSE_STYLE 3.1, 3.2) */
#define IOSTOUCH_IDLE_ALPHA        0.65  /* untouched; a touched one at 1.0 */
#define IOSTOUCH_TOUCH_SLOP        6.0   /* a touch this near a button is on it; the nearest wins */
#define IOSTOUCH_STICK_ZONE        0.42  /* the stick starts in the left 42% of the screen */
#define IOSTOUCH_STICK_RADIUS      60.0
#define IOSTOUCH_QUICKSAVE_HOLD    0.3
#define IOSTOUCH_QUICKLOAD_HOLD    0.3
#define IOSTOUCH_MENU_HOLD         0.45
#define IOSTOUCH_TRAY_STEP         50.0
#define IOSTOUCH_TRAY_RADIUS       20.0
#define IOSTOUCH_TRAY_BACK         26.0  /* the tray's backing reaches this far round each button */
#define IOSTOUCH_FPS_PERIOD        0.5
#define IOSTOUCH_FPS_W             54.0
#define IOSTOUCH_FPS_H             18.0
#define IOSTOUCH_WHEEL_OPEN_SLIDE  10.0
#define IOSTOUCH_WHEEL_HOLD        IOSTOUCH_QUICKSAVE_HOLD
#define IOSTOUCH_WHEEL_DEAD        14.0
#define IOSTOUCH_WHEEL_LEASH       60.0
#define IOSTOUCH_WHEEL_HYST        4.0
#define IOSTOUCH_WHEEL_POP         10.0
#define IOSTOUCH_WHEEL_R1_MAX      168.0
#define IOSTOUCH_WHEEL_R0_FRAC     0.37
#define IOSTOUCH_WHEEL_CANCEL      (-2)  /* the "slice" of the gap at the bottom */
#define IOSTOUCH_WHEEL_SLICE_MAX   29.0  /* degrees a slice, at most (Sam FE: 11 x 29) */
#define IOSTOUCH_WHEEL_RING_DEG    322.0 /* the slices together, at most (Sam SE: 14 x 23); the rest is the gap */
#define IOSTOUCH_GYRO_HZ           100.0
#define IOSTOUCH_GYRO_YAW_RELAX    1.41
#define IOSTOUCH_GYRO_SMOOTH       4.0
#define IOSTOUCH_GYRO_SMOOTH_N     12
#define IOSTOUCH_GYRO_SOFT         1.5
#define IOSTOUCH_GYRO_MAX_DT       0.05
#define IOSTOUCH_GYRO_MAX_FRAME    0.5
#define IOSTOUCH_GYRO_TURN_HOLD    0.35
#define IOSTOUCH_GYRO_DEFAULT_SENS 1     /* 1.5x */
/* POSTAL's (PLAN.md, rig/mock.py v4) */
#define IOSTOUCH_STICK_DEADZONE    0.15
#define IOSTOUCH_STICK_RUN         0.60  /* pushed past this much of the radius: RUN (no RUN button); below, walking speed grows with the push */
#define IOSTOUCH_FIRE_RADIUS       42.0
#define IOSTOUCH_DUCK_RADIUS       29.0
#define IOSTOUCH_EXECUTE_RADIUS    31.0
#define IOSTOUCH_CLUSTER_GAP       30.0  /* FIRE to DUCK (left) and to EXECUTE (up), edge to edge (house) */
#define IOSTOUCH_FIRE_IN_X         60.0  /* FIRE's centre this far in from the usable right edge */
#define IOSTOUCH_FIRE_IN_Y         62.0  /* ...and up from the usable bottom */
#define IOSTOUCH_AIM_START         8.0   /* an aiming drag's first 8 pt say nothing */
#define IOSTOUCH_AIM_LEASH         60.0  /* the point an aim is measured from trails this far behind the thumb */
#define IOSTOUCH_PILL_W            50.0  /* SAVE, LOAD, MENU */
#define IOSTOUCH_WHEELPILL_W       38.0  /* NEXT WPN, PREV WPN (in the gap: as wide as fits, down to _MIN_W) */
#define IOSTOUCH_WHEELPILL_MIN_W   34.0
#define IOSTOUCH_WHEELPILL_GAP     6.0   /* between the two */
#define IOSTOUCH_PILL_GAP          5.0   /* between SAVE, LOAD, MENU (mock: 55 apart) */
#define IOSTOUCH_PILL_MIN_H        24.0
#define IOSTOUCH_PILL_MAX_H        36.0  /* (an iPad's strip is 68 pt: pills no taller than this) */
#define IOSTOUCH_STRIP_FRAC        (40.0 / 480.0) /* the strip when the game gives none */
#define IOSTOUCH_ROW_GAP           4.0   /* between icons in the top rows */
#define IOSTOUCH_ARROW_W           24.0  /* the follow arrow */
#define IOSTOUCH_SLOWMO_W          30.0  /* the slow-motion label */
#define IOSTOUCH_CAMERA_HOLD       0.5   /* camera held still: freeze */
#define IOSTOUCH_CAMERA_SLIDE      10.0  /* camera slid this far (not mostly up/down): burst; also ends tap and hold */
#define IOSTOUCH_ZOOMOUT_SWIPE     36.0  /* camera slid mostly down this far: zoom-out shot */
#define IOSTOUCH_SLOWMO_SWIPE      36.0  /* camera slid mostly up this far (or to IOSTOUCH_SLOWMO_EDGE */
#define IOSTOUCH_SLOWMO_EDGE       6.0   /*   of the top, but never less than IOSTOUCH_SLOWMO_MIN): */
#define IOSTOUCH_SLOWMO_MIN        16.0  /*   slow motion on/off */
#define IOSTOUCH_BURST_GAP         0.2
#define IOSTOUCH_BURST_MAX         10
#define IOSTOUCH_FREEZE_CLOSE_R    18.0
#define IOSTOUCH_FREEZE_TAP        0.3   /* freeze: a tap on the view shorter than this... */
#define IOSTOUCH_FREEZE_STILL      8.0   /* ...that never moved further than this takes a photo */
#define IOSTOUCH_AUTO_TAP          0.3   /* autoplay: a tap is shorter than this... */
#define IOSTOUCH_AUTO_DOUBLE       0.35  /* ...the second going down this soon after the first lifted... */
#define IOSTOUCH_AUTO_STILL        10.0  /* ...neither moving this far... */
#define IOSTOUCH_AUTO_NEAR         60.0  /* ...and this near the first */
#define IOSTOUCH_AUTO_HOLD         0.6   /* autoplay: a long press */
#define IOSTOUCH_MESSAGE_TIME      3.0
#define IOSTOUCH_VERSION_H         12.0

/* ------------------------------------------------------------ the buttons */
enum {
  BTN_FIRE, BTN_DUCK, BTN_EXECUTE,
  BTN_NEXTWPN, BTN_PREVWPN,
  BTN_SAVE, BTN_LOAD, BTN_MENU,
  BTN_CHEAT, BTN_CAMERA, BTN_FOLLOWNEXT, BTN_SLOWMO,
  BTN_FREEZECLOSE,
  /* MENU's tray, left to right (LEVEL and FOLLOW only while they apply) */
  BTN_TRAY_SENS, BTN_TRAY_GYRO, BTN_TRAY_FPS, BTN_TRAY_KEYS, BTN_TRAY_CHEAT, BTN_TRAY_LEVEL,
  BTN_TRAY_AUTOPLAY, BTN_TRAY_CAMERA, BTN_TRAY_FOLLOW,
  /* the tray CAMERA's PHOTO options (held), under it */
  BTN_PHOTO_CLEAN, BTN_PHOTO_HUD, BTN_PHOTO_HUDBTNS,
  BTN_COUNT
};
#define BTN_TRAY_FIRST BTN_TRAY_SENS
#define BTN_TRAY_LAST  BTN_TRAY_FOLLOW
#define BTN_PHOTO_FIRST BTN_PHOTO_CLEAN
#define BTN_PHOTO_LAST  BTN_PHOTO_HUDBTNS

enum {
  KIND_HOLD = 0,  /* FIRE, DUCK, EXECUTE: held while touched; a drag on them aims */
  KIND_WHEEL,     /* NEXT/PREV WPN: tap = one step; slide 10 pt or hold 0.3 s = the wheel */
  KIND_HOLDSAVE,  /* SAVE, LOAD: tap = its screen; held 0.3 s = quick save / load */
  KIND_MENU,      /* tap = the menu; held 0.45 s = the tray */
  KIND_TAP,       /* asks for its request as it lifts on it */
  KIND_CAMERA,    /* the camera icon's gestures */
  KIND_CLOSE,     /* freeze's X */
  KIND_TRAY,      /* the tray's buttons: act as they lift on them */
  KIND_PHOTOKIND, /* PHOTO's ticks */
};

enum { IOSTOUCH_GYRO_OFF = 0, IOSTOUCH_GYRO_TOUCH, IOSTOUCH_GYRO_ALWAYS, IOSTOUCH_GYRO_NUM_MODES };
#define IOSTOUCH_GYRO_NUM_SENS 4
extern const float g_afIOSTouchGyroSens[IOSTOUCH_GYRO_NUM_SENS]; /* 1.0, 1.5, 2.0, 3.0 */

typedef struct {
  int bValid;
  double x0, y0, x1, y1;
} IOSTouchRect;

typedef struct {
  const char *label;
  int kind;
  unsigned int ulAction;  /* KIND_HOLD, KIND_WHEEL: IOSTOUCH_FIRE...; KIND_TAP, KIND_HOLDSAVE, KIND_MENU: IOSTOUCH_REQ_*; KIND_PHOTOKIND: IOSTOUCH_PHOTO_* */
  int bPill;              /* a pill (hw x r half sizes) or a circle (radius r) */
  double r, hw;
  int bAimWhileHeld;      /* a drag on it also aims */
  double x, y;            /* centre, points (set by the layout) */
} IOSTouchButton;

/* Wheel slice states */
enum { WSTATE_READY = 0, WSTATE_NOAMMO, WSTATE_NOTFOUND };

/* Everything the view draws (read it; only the core writes it, except the
   settings block, which the view sets from NSUserDefaults before the first
   layout and saves back when it changes) */
typedef struct {
  /* settings */
  int iGyroMode, iGyroSens, bGyroAvailable;
  int bShowFps, bShowCamera, bShowCheat;
  int iPhotoKinds;              /* IOSTOUCH_PHOTO_* bits, never 0 */

  int iMode;                    /* IOSTOUCH_HIDDEN / _GAMEPLAY / _AUTOPLAY */
  IOSTouchHud hud;
  double W, H;                  /* the layout's screen, points */
  double left, right, top, bottom; /* the usable part (safe area, at least 8 pt in at the sides and bottom) */
  IOSTouchButton aButtons[BTN_COUNT];
  int aButtonHeld[BTN_COUNT];   /* fingers on each */
  IOSTouchRect rStrip;          /* the game's status strip (points) */
  double fPillH;
  IOSTouchRect rFps;            /* the FPS readout (left of SAVE in the strip where there is room, else in the second row) */
  int bFpsInStrip;
  double fRow2Y;                /* the second top row's middle (icons that don't fit in the strip, the FPS readout) */
  IOSTouchRect arTrayBack[2]; int ctTrayRows;
  IOSTouchRect rPhotoBack;
  double fVersionX, fVersionY;
  double fMessageX, fMessageY;
  int iFpsShown;                /* -1: none measured yet */

  /* the stick, only while its thumb is down */
  int bStickActive;
  double fStickOX, fStickOY;    /* where the thumb landed */
  double fStickX, fStickY;      /* -1..1 of the radius, screen axes (y down) */

  /* aiming (for drawing a hint; the game reads IOSTouchInput) */
  int bAim; double fAimX, fAimY; /* screen terms: right +, up + */

  /* hold rings, 0..1 */
  float fSaveRing, fLoadRing, fMenuRing, afWheelRing[2], fCameraRing, fTrayCamRing, fAutoRing;
  double fAutoX, fAutoY;        /* autoplay: where the long press is (its ring, fAutoRing) */

  int bTrayOpen, bPhotoRowOpen;
  int bLevelListOpen;
  int ctLevels;

  /* camera */
  int bFreeze, bBursting, ctBurstShots;

  /* the weapon wheel */
  IOSTouchWeapons wpn;
  int bWheelOpen, bWheelTapMode, iWheelButton;
  double fWheelCX, fWheelCY, fWheelR0, fWheelR1;
  int iWheelHot;

  /* haptics the view still has to give (it zeroes them) */
  int ctWheelTicks, ctCameraTaps, ctLevelTicks;

  /* a short message (IOSTouch_ShowMessage, or the core's own) */
  char szMessage[192];
  double tMessage, fMessageSeconds;
} IOSTouchCore;

const IOSTouchCore *IOSTouchCore_Get(void);
IOSTouchCore *IOSTouchCore_Settings(void); /* the same, writable: only for the settings block and the haptic counts */

/* ------------------------------------------------------------ the calls */
void IOSTouchCore_Reset(void);  /* back to a fresh launch (the harness; settings to their defaults) */
void IOSTouchCore_SetMode(int iMode, double now);
/* The HUD as the game says, every frame. Returns 1 if the layout must be made again (its rectangles moved). */
int  IOSTouchCore_SetHud(const IOSTouchHud *pHud);
void IOSTouchCore_Layout(double W, double H, double inLeft, double inTop, double inRight, double inBottom);
void IOSTouchCore_TouchBegan(const void *touch, double x, double y, double t);
void IOSTouchCore_TouchMoved(const void *touch, double x, double y, double t);
void IOSTouchCore_TouchEnded(const void *touch, double x, double y, double t, int bCancelled);
/* Once a frame: the holds, the burst, autoplay's long press, the FPS count.
   Returns the IOSTOUCH_REQ_* asked for since the last call (and forgets them). */
unsigned int IOSTouchCore_Tick(double now, unsigned int ulFramesDrawn);
void IOSTouchCore_ReadInput(IOSTouchInput *pInput);
void IOSTouchCore_SetWeapons(const IOSTouchWeapons *pWeapons);
int  IOSTouchCore_HoldsGame(void);
void IOSTouchCore_ShowMessage(const char *pszText, double fSeconds, double now);
int  IOSTouchCore_MessageShows(double now);
int  IOSTouchCore_PhotoKinds(void);

/* The level list (the tray's LEVEL, cheat on) */
void IOSTouchCore_SetLevels(const char *const *ppszNames, int ctLevels);
const char *IOSTouchCore_LevelName(int i);
void IOSTouchCore_PickLevel(int i);   /* the view's list: row i tapped (closes the list) */
void IOSTouchCore_CloseLevelList(void);
int  IOSTouchCore_TakeLevelPick(void);

/* Buttons */
int  IOSTouchCore_IsShown(int button);
int  IOSTouchCore_ButtonAt(double x, double y);
int  IOSTouchCore_IsLit(int button);  /* a finger on it, or a thumb held on MENU over it */
int  IOSTouchCore_IsOn(int button);   /* a switch that is on (FPS, GYRO, CAMERA, CHEAT, ticks...) */
const char *IOSTouchCore_Label(int button, char *buf, int size); /* what it says now (GYRO's, SENS's modes) */

/* The wheel (open) */
int    IOSTouchCore_WheelSlices(void);
double IOSTouchCore_WheelSliceWidth(void);   /* degrees */
double IOSTouchCore_WheelSliceDeg(int i);    /* its middle, degrees counter-clockwise from pointing right */
int    IOSTouchCore_WheelSliceState(int i);  /* WSTATE_* */
int    IOSTouchCore_WheelSliceCount(int i);  /* the ammo it shows, at most 999, or -1 */
const char *IOSTouchCore_WheelSliceName(int i); /* the game's name for it (drawn where it has no icon), or "" */
int    IOSTouchCore_WheelPointed(int *pbCancel); /* a slice, IOSTOUCH_WHEEL_CANCEL, or -1 */
int    IOSTouchCore_WheelNeedle(double *pfDeg);  /* 1 while a sliding thumb points (its direction) */
/* The middle's words: the title (a weapon's name or ""), its count (-1 none), the hint */
void   IOSTouchCore_WheelMiddle(const char **ppszTitle, int *piCount, const char **ppszHint);
/* An icon's tint for a state (RGBA, not premultiplied): grey and faded for no ammo / not found */
void   IOSTouchCore_TintIcon(const unsigned char *pubSrc, int ctTexels, int iState, unsigned char *pubDst);

/* Tilt aiming. The filter turns CoreMotion's samples (rotation rate rad/s,
   gravity, the device's axes; side: +1 landscape left, -1 right, 0 neither)
   into degrees turned about up (+: counter-clockwise, seen from above);
   the view keeps one under a lock on the motion queue. */
typedef struct {
  double tLast;
  double afSmooth[IOSTOUCH_GYRO_SMOOTH_N];
  int iNext;
  double fYaw;      /* degrees since the last take */
} IOSTouchGyroFilter;
void   IOSTouchGyro_Reset(IOSTouchGyroFilter *f);
void   IOSTouchGyro_Sample(IOSTouchGyroFilter *f, int side, double wx, double wy, double wz,
                           double gx, double gy, double gz, double t);
int    IOSTouchCore_GyroWanted(void);  /* whether the motion updates should run now */
/* Once a frame, after the touches: fYaw from the filter since the last
   frame; side as above. Turns the aim (or the dude, IOSTouchInput.fTurn). */
void   IOSTouchCore_GyroFrame(double fYaw, int side, double now);

#ifdef __cplusplus
}
#endif
#endif
