/* POSTAL for iOS: on-screen touch controls (a UIKit overlay over SDL's view).
   Layout and behaviour: jk-files wip-postal-ios PLAN.md and docs/HOUSE_STYLE.md
   section 3 (ported from Serious Sam's ios/IOSTouch.m).

   The overlay (IOSTouch.m) only turns touches into the plain values below.
   The game side (IOSGame.cpp) reads them and acts on POSTAL: the dude's
   movement and facing, its input bits, the menus, saving and loading, the
   cheat and the camera.

   Threads: everything here runs on the main thread (POSTAL is single
   threaded: its game loop runs on the main thread, between SDL event pumps). */
#ifndef IOS_TOUCH_H
#define IOS_TOUCH_H
#ifdef __cplusplus
extern "C" {
#endif

/* What the game is doing, which decides what the overlay shows */
enum {
  IOSTOUCH_HIDDEN = 0,   /* menus, cutscenes, loading: no overlay, touches are SDL mouse clicks */
  IOSTOUCH_GAMEPLAY = 1, /* a level is being played */
  IOSTOUCH_MENU = 2,     /* the game's menus are up: only a BACK button (top left); every other touch is an
                            SDL mouse click, as in HIDDEN */
};

/* What the HUD and game state are, for the overlay's layout and context buttons.
   Rectangles are x0, y0, x1, y1 as fractions of the screen. */
typedef struct IOSTouchHud {
  float afTopBar[4];     /* the game's top status strip (where the pills go) */
  float afTopText[4];    /* the part of the strip the status text covers now */
  float afTopGap[4];     /* the free gap inside the status text (between "Population N" and "Hostiles")
                            where NEXT/PREV WPN go if it is at least 2*38+6 pt wide; empty (x1 <= x0): none */
  int   bCheatOn;        /* the cheat is on (invincible, weapons topped up) */
  int   bFollowing;      /* the follow cam is following someone else */
  int   iSlowMo;         /* 0 normal, 2 half speed, 4 quarter speed */
  unsigned int uLevel;   /* changes with every level started or loaded */
  /* Full-screen play (the game draws no status strip and no weapon bar): what
     they showed, for the overlay to draw. All zero/empty outside play. */
  int   bFullScreen;     /* 1: afTopBar/afTopText/afTopGap are empty; use the standard top row */
  char  szStatus[160];   /* the strip's first line, e.g. "Population 38 Hostiles 30 Killed 0 (0% / 87%)" */
  char  szGoal[160];     /* its second line while the game shows it, e.g. "You must kill 87% of the hostiles." */
  int   iHealth;         /* health as the bar showed it: % of full (100; more with the cheat), -1: no dude */
  int   iKevlar;         /* kevlar layers (body armour), 0 none */
  int   iAmmo;           /* ammo of the weapon in hand, -1: none to show (infinite) */
  const char *pszWeapon; /* name of the weapon in hand, or NULL */
  int   bDead;           /* the dude is dead: the game restarts the level on FIRE */
} IOSTouchHud;

/* What the player asked for (IOSTouch_Update's result), done by the game side */
enum {
  IOSTOUCH_REQ_MENU        = 1 << 0,  /* MENU tap: the game menu (Escape) */
  IOSTOUCH_REQ_SAVESCREEN  = 1 << 1,  /* SAVE tap: the game's save screen */
  IOSTOUCH_REQ_LOADSCREEN  = 1 << 2,  /* LOAD tap: "quit this level?", then the load screen */
  IOSTOUCH_REQ_QUICKSAVE   = 1 << 3,  /* SAVE held 0.3 s */
  IOSTOUCH_REQ_QUICKLOAD   = 1 << 4,  /* LOAD held 0.3 s */
  IOSTOUCH_REQ_CHEAT       = 1 << 5,  /* toggle the cheat (tray or the cheat icon); the game plays the sound */
  IOSTOUCH_REQ_LEVEL       = 1 << 6,  /* tray LEVEL (cheat on): the level select with every level open */
  IOSTOUCH_REQ_RESTART     = 1 << 7,  /* dead: a tap on the screen restarts the level */
  IOSTOUCH_REQ_BACK        = 1 << 8,  /* IOSTOUCH_MENU: BACK tapped -- the menu's cancel, as Escape */
  /* (1 << 9: was autoplay's) */
  IOSTOUCH_REQ_KEYBOARD    = 1 << 10, /* tray keyboard: type one of POSTAL's cheat words */
  IOSTOUCH_REQ_FOLLOW      = 1 << 11, /* tray FOLLOW: follow cam on/off */
  IOSTOUCH_REQ_FOLLOWNEXT  = 1 << 12, /* the arrow by the camera icon: follow the next person */
  IOSTOUCH_REQ_SLOWMO      = 1 << 13, /* tray SLOW (or the slow-motion label): off -> 1/2 -> 1/4 -> off */
  /* (1 << 14: was the slow-motion speed) */
  IOSTOUCH_REQ_PHOTO       = 1 << 15, /* camera icon tapped (lifted where it went down, however long): a screenshot */
  /* (1 << 16: was the burst) */
  /* (1 << 17: was freeze and pan) */
  /* (1 << 18: was the zoom-out shot) */
  /* (1 << 19: was the end of freeze) */
};
/* IOSTOUCH_REQ_LEVEL is informational: the overlay has opened its own level
   list (IOSTouch_SetLevels); the pick comes from IOSTouch_TakeLevelPick. */

/* Buttons held this frame (IOSTouchInput.ulButtons) */
enum {
  IOSTOUCH_FIRE       = 1 << 0,
  IOSTOUCH_DUCK       = 1 << 1,
  IOSTOUCH_EXECUTE    = 1 << 2,
  IOSTOUCH_NEXTWEAPON = 1 << 3, /* one frame per quick tap */
  IOSTOUCH_PREVWEAPON = 1 << 4,
  IOSTOUCH_RUN        = 1 << 5, /* the stick pushed to its edge */
};

typedef struct IOSTouchInput {
  float fMoveX, fMoveY;   /* move stick, -1..1 in screen terms: right +, up + (0,0: not moving) */
  int   bAim;             /* the right thumb is aiming (drag area or FIRE) */
  float fAimX, fAimY;     /* aim direction in screen terms (right +, up +), unit length when bAim */
  unsigned int ulButtons; /* IOSTOUCH_* */
  int   iSelectWeapon;    /* 0 none, else 1 + the weapon type the wheel picked (one frame) */
} IOSTouchInput;

/* The weapon wheel: what the dude has (the game tells the overlay every frame) */
#define IOSTOUCH_WPN_MAX 16
typedef struct IOSTouchWeapons {
  int bValid;              /* a dude exists and can change weapon */
  int ctWeapons;           /* slices, in the game's NEXT order */
  int aiType[IOSTOUCH_WPN_MAX];      /* the game's weapon type for each slice */
  const char *apszName[IOSTOUCH_WPN_MAX];
  int abOwned[IOSTOUCH_WPN_MAX];
  int aiAmmo[IOSTOUCH_WPN_MAX];      /* -1: no count (infinite) */
  int iCurrent;            /* slice of the weapon in hand, -1 none */
} IOSTouchWeapons;

/* Main thread, once per frame drawn. pSDLWindow: the SDL_Window *. Returns IOSTOUCH_REQ_* */
unsigned int IOSTouch_Update(void *pSDLWindow, int iMode, const IOSTouchHud *pHud, unsigned int ulFramesDrawn);
void IOSTouch_ReadInput(IOSTouchInput *pInput);
void IOSTouch_SetWeapons(const IOSTouchWeapons *pWeapons);
void IOSTouch_SetWeaponIcon(int iSlice, const unsigned char *pubRGBA, int iWidth, int iHeight);
int  IOSTouch_HoldsGame(void);          /* the wheel or the level list is open: the game should hold still */
void IOSTouch_ShowMessage(const char *pszText, double fSeconds);

/* The level list (tray LEVEL, cheat on): the game calls SetLevels once (names copied, in order);
   TakeLevelPick: -1 none, else the index tapped (returned once). The game holds still while it is open. */
void IOSTouch_SetLevels(const char *const *ppszNames, int ctLevels);
int  IOSTouch_TakeLevelPick(void);


#ifdef __cplusplus
}
#endif
#endif
