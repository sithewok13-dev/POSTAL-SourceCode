// POSTAL for iOS: the game side of the touch controls and the extras.
// See IOSGame.h. Everything runs on the main thread (POSTAL's game loop).
#include "RSPiX.h"
#include <math.h>
#include <stdlib.h>
#include "realm.h"
#include "dude.h"
#include "person.h"
#include "SampleMaster.h"
#include "input.h"
#include "play.h"
#include "IOSGame.h"
#include "IOSTouch.h"
#include "IOSBot.h"
#include "camera.h"
#if PLATFORM_IOS
#include "IOSSupport.h"
#define IOSGAME_SCREEN_H_PT IOSSupport_ScreenHeightPoints()
#else
#define IOSGAME_SCREEN_H_PT 390.0	// the rig plays an iPhone 14 in landscape
#endif
#if PLATFORM_IOS
#include "IOSPhotos.h"
#endif

extern int wideScreenWidth;

////////////////////////////////////////////////////////////////////////////////
// State
////////////////////////////////////////////////////////////////////////////////
static IOSTouchInput	s_in;					// the overlay's input, read once per frame shown
static unsigned		s_uPresents		= 0;	// frames shown
static unsigned		s_uLastPlay		= 0;	// the frame shown when play last ran
static unsigned		s_uPlayReq		= 0;	// IOSGAME_PLAY_*
static int				s_iNext			= IOSGAME_NEXT_NONE;
static int16_t			s_sNextRealm	= 0;
static bool				s_bCheat			= false;
static bool				s_bFollow		= false;
static U16				s_idFollow		= 0;	// CIdBank::IdNil
static int32_t			s_lFollowDeadAt = 0;	// game time the followed one was first seen dead
static int				s_iSlow			= 0;	// 0, 2, 4
static int				s_iSlowSpeed	= 2;	// the speed slow motion uses (2 or 4)
static bool				s_bAutoplay		= false;
static bool				s_bKeyboard		= false;
static unsigned		s_uLevel			= 0;	// bumped for each new realm
static CRealm*			s_prealm			= NULL;	// valid only during the frame it was set
static CDude*			s_pdude			= NULL;
static bool				s_bLevelsSent	= false;

// Levels the cheat's list and autoplay choose from (Play_InitLevelSelectMenu's)
#define IOSGAME_MAX_LEVELS 40
static char				s_aszLevel[IOSGAME_MAX_LEVELS][64];
static int				s_ctLevels		= 0;

static void LoadLevelNames(void)
	{
	if (s_ctLevels > 0)
		return;
	char szFile[256], szTitle[256];
	for (int16_t i = 0; i < IOSGAME_MAX_LEVELS; i++)
		{
		szTitle[0] = '\0';
		if (Play_GetRealmInfo(false, false, false, 3, i, 1, szFile, sizeof(szFile), szTitle, sizeof(szTitle)) != 0)
			break;
		if (szFile[0] == '\0')
			break;
		strncpy(s_aszLevel[i], szTitle[0] ? szTitle : szFile, sizeof(s_aszLevel[i]) - 1);
		s_ctLevels = i + 1;
		}
	}

////////////////////////////////////////////////////////////////////////////////
// The cheat: invincible, every weapon, ammo kept full
////////////////////////////////////////////////////////////////////////////////
static void ApplyCheat(CDude* pdude)
	{
	CStockPile* ps = &pdude->m_stockpile;
	const CStockPile& max = CStockPile::ms_stockpileBackPackMax;
	ps->m_sMachineGun = ps->m_sMissileLauncher = ps->m_sShotGun = ps->m_sSprayCannon = 1;
	ps->m_sFlameThrower = ps->m_sNapalmLauncher = ps->m_sDeathWadLauncher = ps->m_sDoubleBarrel = 1;
	ps->m_sBackpack = 1;
	ps->m_sNumGrenades = max.m_sNumGrenades;
	ps->m_sNumFireBombs = max.m_sNumFireBombs;
	ps->m_sNumMissiles = max.m_sNumMissiles;
	ps->m_sNumNapalms = max.m_sNumNapalms;
	ps->m_sNumBullets = max.m_sNumBullets;
	ps->m_sNumShells = max.m_sNumShells;
	ps->m_sNumFuel = max.m_sNumFuel;
	ps->m_sNumMines = max.m_sNumMines;
	ps->m_sNumHeatseekers = max.m_sNumHeatseekers;
	if (ps->m_sKevlarLayers < CStockPile::ms_stockpileMax.m_sKevlarLayers)
		ps->m_sKevlarLayers = CStockPile::ms_stockpileMax.m_sKevlarLayers;
	pdude->IOS_SetInvincible(true);
	}

static void SetCheat(bool bOn)
	{
	if (bOn == s_bCheat)
		return;
	s_bCheat = bOn;
	// One sound for on and off, from the tray or the icon alike (the owner picks it)
	PlaySample(g_smidDemonLaugh1, SampleMaster::UserFeedBack);
	if (!bOn && s_pdude)
		s_pdude->IOS_SetInvincible(false);	// keeps the weapons, as JK's sithewok
	IOSTouch_ShowMessage(bOn ? "Cheat on: can't die, every weapon, endless ammo" : "Cheat off", 2.5);
	}

////////////////////////////////////////////////////////////////////////////////
// The weapon wheel's view of the dude
////////////////////////////////////////////////////////////////////////////////
static const struct { CDude::WeaponType type; const char* pszName; } s_aWheel[] =
	{
	{ CDude::SemiAutomatic,		"Machine Gun" },
	{ CDude::ShotGun,				"Shotgun" },
	{ CDude::SprayCannon,		"Spray Cannon" },
	{ CDude::Grenade,				"Grenades" },
	{ CDude::Rocket,				"Missiles" },
	{ CDude::Heatseeker,			"Heatseekers" },
	{ CDude::FireBomb,			"Cocktails" },
	{ CDude::Napalm,				"Napalm" },
	{ CDude::FlameThrower,		"Flamethrower" },
	{ CDude::ProximityMine,		"Proximity Mines" },
	{ CDude::TimedMine,			"Timed Mines" },
	{ CDude::BouncingBettyMine,"Bouncing Betties" },
	{ CDude::DeathWad,			"Deathwad" },
	{ CDude::DoubleBarrel,		"Double Barrel" },
	};
#define IOSGAME_WHEEL_SLICES ((int)(sizeof(s_aWheel) / sizeof(s_aWheel[0])))

static bool Owns(CDude* pdude, CDude::WeaponType type, int16_t sAmmo)
	{
	const CStockPile* ps = &pdude->m_stockpile;
	switch (type)
		{
		case CDude::SemiAutomatic:	return ps->m_sMachineGun != 0;
		case CDude::ShotGun:			return ps->m_sShotGun != 0;
		case CDude::SprayCannon:	return ps->m_sSprayCannon != 0;
		case CDude::Rocket:
		case CDude::Heatseeker:		return ps->m_sMissileLauncher != 0;
		case CDude::Napalm:			return ps->m_sNapalmLauncher != 0;
		case CDude::FlameThrower:	return ps->m_sFlameThrower != 0;
		case CDude::DeathWad:		return ps->m_sDeathWadLauncher != 0;
		case CDude::DoubleBarrel:	return ps->m_sDoubleBarrel != 0;
		default:							return sAmmo > 0;	// thrown and laid weapons: owned while there are some
		}
	}

static void SendWeapons(CDude* pdude)
	{
	IOSTouchWeapons w;
	memset(&w, 0, sizeof(w));
	w.iCurrent = -1;
	if (pdude && pdude->m_state != CThing3d::State_Dead)
		{
		w.bValid = 1;
		w.ctWeapons = IOSGAME_WHEEL_SLICES;
		for (int i = 0; i < IOSGAME_WHEEL_SLICES; i++)
			{
			CThing::ClassIDType id;
			int16_t* psNum = NULL;
			pdude->GetWeaponInfo(s_aWheel[i].type, &id, &psNum);
			int16_t sAmmo = psNum ? *psNum : 0;
			w.aiType[i] = s_aWheel[i].type;
			w.apszName[i] = s_aWheel[i].pszName;
			w.abOwned[i] = Owns(pdude, s_aWheel[i].type, sAmmo) ? 1 : 0;
			w.aiAmmo[i] = (s_bCheat && s_aWheel[i].type == CDude::SemiAutomatic) ? -1 : sAmmo;
			if (pdude->GetCurrentWeapon() == s_aWheel[i].type)
				w.iCurrent = i;
			}
		}
	IOSTouch_SetWeapons(&w);
	}

////////////////////////////////////////////////////////////////////////////////
// Follow cam: someone else to watch
////////////////////////////////////////////////////////////////////////////////
static CThing3d* FollowedThing(CRealm* prealm)
	{
	CThing* pthing = NULL;
	if (s_idFollow != CIdBank::IdNil && prealm->m_idbank.GetThingByID(&pthing, s_idFollow) == 0)
		return (CThing3d*)pthing;
	return NULL;
	}

// The next living person after the one followed now (in the realm's list order)
static void FollowNext(CRealm* prealm)
	{
	CListNode<CThing>* phead = &prealm->m_aclassHeads[CThing::CPersonID];
	CListNode<CThing>* pstart = phead->m_pnNext;
	CThing3d* pcur = FollowedThing(prealm);
	if (pcur)
		{
		for (CListNode<CThing>* pn = phead->m_pnNext; pn && pn->m_powner; pn = pn->m_pnNext)
			if (pn->m_powner == pcur) { pstart = pn->m_pnNext; break; }
		}
	// Two passes round the list from just after the current one
	for (int pass = 0; pass < 2; pass++)
		{
		for (CListNode<CThing>* pn = (pass == 0 ? pstart : phead->m_pnNext); pn && pn->m_powner; pn = pn->m_pnNext)
			{
			CThing3d* p = (CThing3d*)pn->m_powner;
			if (p != pcur && p->m_state != CThing3d::State_Dead && p->m_state != CThing3d::State_Die)
				{
				s_idFollow = p->GetInstanceID();
				s_lFollowDeadAt = 0;
				return;
				}
			}
		}
	s_idFollow = CIdBank::IdNil;
	}

bool IOSGame_CameraTarget(CRealm* prealm, double* pdX, double* pdY, double* pdZ)
	{
	if (!s_bFollow)
		return false;
	CThing3d* p = FollowedThing(prealm);
	if (!p)
		{
		FollowNext(prealm);
		p = FollowedThing(prealm);
		if (!p)
			return false;
		}
	// Dead: stay on the body for 2 s of game time, then the next one
	if (p->m_state == CThing3d::State_Dead || p->m_state == CThing3d::State_Die)
		{
		int32_t lNow = prealm->m_time.GetGameTime();
		if (s_lFollowDeadAt == 0)
			s_lFollowDeadAt = lNow;
		else if (lNow - s_lFollowDeadAt > 2000)
			{
			FollowNext(prealm);
			CThing3d* pn = FollowedThing(prealm);
			if (pn)
				p = pn;
			}
		}
	*pdX = p->GetX();
	*pdY = p->GetY();
	*pdZ = p->GetZ();
	return true;
	}

////////////////////////////////////////////////////////////////////////////////
// Per frame
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Camera: photos, bursts, the zoom-out photo, freeze and pan
////////////////////////////////////////////////////////////////////////////////
enum { PHOTO_NONE = 0, PHOTO_ONE, PHOTO_BURST };
static int		s_iPhoto		= PHOTO_NONE;	// a grab wanted on the next frame shown
static int		s_iBurstShot	= 0;			// the shot number in the current burst
static unsigned	s_uLastBurstAt	= 0;			// the frame shown of the last burst request
static bool		s_bZoomOut		= false;		// a zoom-out photo wanted on the next frame of play
static bool		s_bFreeze		= false;		// freeze and pan is open
static double	s_dPanX = 0, s_dPanY = 0;		// its drag so far, in game pixels

// Saves pixels (0xAARRGGBB, top row first) as a photo of one kind
static void SavePhoto(const uint32_t* pul, int w, int h, int iRowFrom, int iRows, const char* pszKind, int iFlags)
	{
	unsigned char* pub = (unsigned char*)malloc((size_t)w * iRows * 4);
	if (!pub)
		return;
	for (int y = 0; y < iRows; y++)
		{
		const uint32_t* src = pul + (size_t)(iRowFrom + y) * w;
		unsigned char* dst = pub + (size_t)y * w * 4;
		for (int x = 0; x < w; x++, dst += 4)
			{
			uint32_t c = src[x];
			dst[0] = (unsigned char)(c >> 16); dst[1] = (unsigned char)(c >> 8); dst[2] = (unsigned char)c; dst[3] = 255;
			}
		}
#if PLATFORM_IOS
	IOSPhotos_Save(pub, w, iRows, 0, pszKind, iFlags);
#else
	// Test rig: a PPM in the rig folder
	const char* pszDir = getenv("POSTAL_RIG_DIR");
	char szPath[1024];
	snprintf(szPath, sizeof(szPath), "%s/photo %s.ppm", pszDir ? pszDir : ".", pszKind);
	FILE* f = fopen(szPath, "wb");
	if (f)
		{
		fprintf(f, "P6\n%d %d\n255\n", w, iRows);
		for (int i = 0; i < w * iRows; i++)
			fwrite(pub + i * 4, 3, 1, f);
		fclose(f);
		}
	fprintf(stderr, "RIG PHOTO: %s %dx%d\n", pszKind, w, iRows);
	(void)iFlags;
#endif
	free(pub);
	}

void IOSGame_FrameShown(const uint32_t* pul, int w, int h)
	{
	if (s_iPhoto == PHOTO_NONE || !pul || w <= 0 || h <= 0)
		return;
	// The game's picture without its status strips is lines 40..440 of 480
	int iTop = h * 40 / 480, iRows = h * 400 / 480;
	if (s_iPhoto == PHOTO_BURST)
		{
		char szKind[32];
		snprintf(szKind, sizeof(szKind), "burst %02d", s_iBurstShot);
#if PLATFORM_IOS
		SavePhoto(pul, w, h, iTop, iRows, szKind, s_iBurstShot > 1 ? IOSPHOTOS_LIGHT : 0);
#else
		SavePhoto(pul, w, h, iTop, iRows, szKind, 0);
#endif
		}
	else
		{
		int iKinds = IOSTouch_PhotoKinds();
		if (iKinds & IOSTOUCH_PHOTO_CLEAN)
			SavePhoto(pul, w, h, iTop, iRows, "clean", 0);
		if (iKinds & IOSTOUCH_PHOTO_HUD)
			SavePhoto(pul, w, h, 0, h, "hud", 0);
#if PLATFORM_IOS
		if (iKinds & IOSTOUCH_PHOTO_HUDBTNS)
			SavePhoto(pul, w, h, 0, h, "hud+buttons", IOSPHOTOS_ADD_OVERLAY);
#endif
		}
	s_iPhoto = PHOTO_NONE;
	}

// The zoom-out photo: the scene drawn at twice the view's size around the
// camera's centre, then shrunk to the view's size (2x2 pixels averaged)
static void ZoomOutPhoto(CRealm* prealm, CCamera* pcam)
	{
	int16_t w = pcam->m_sViewW, h = pcam->m_sViewH;
	if (w <= 0 || h <= 0)
		return;
	RImage im;
	if (im.CreateImage(w * 2, h * 2, RImage::BMP8) != 0)
		return;
	memset(im.m_pData, 0, (size_t)im.m_lPitch * im.m_sHeight);
	int16_t sX = pcam->m_sSceneViewX - w / 2, sY = pcam->m_sSceneViewY - h / 2;
	pcam->Snap(w * 2, h * 2, &prealm->m_scene, prealm->m_phood, sX, sY, &im, 0, 0);
	uint32_t* pul = (uint32_t*)malloc((size_t)w * h * 4);
	if (!pul)
		return;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			{
			unsigned r = 0, g = 0, b = 0;
			for (int k = 0; k < 4; k++)
				{
				uint8_t idx = im.m_pData[(size_t)(y * 2 + (k >> 1)) * im.m_lPitch + x * 2 + (k & 1)];
				uint32_t c = IOS_PaletteARGB(idx);
				r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255;
				}
			pul[(size_t)y * w + x] = 0xFF000000u | ((r / 4) << 16) | ((g / 4) << 8) | (b / 4);
			}
	SavePhoto(pul, w, h, 0, h, "zoom-out", 0);
	free(pul);
	}

void IOSGame_Pan2D(int16_t* psX, int16_t* psY)
	{
	if (!s_bFreeze)
		return;
	*psX = (int16_t)(*psX - s_dPanX);
	*psY = (int16_t)(*psY - s_dPanY);
	}

void IOSGame_PlayFrame(CRealm* prealm, CDude* pdude, CCamera* pcamera)
	{
	if (s_bZoomOut && pcamera)
		{
		ZoomOutPhoto(prealm, pcamera);
		s_bZoomOut = false;
		}
	if (prealm != s_prealm)
		{
		s_uLevel++;
		s_idFollow = CIdBank::IdNil;
		if (s_bAutoplay)
			IOSBot_NewLevel(prealm);
		}
	s_prealm = prealm;
	s_pdude = pdude;
	s_uLastPlay = s_uPresents;
	if (pdude && (s_bCheat || s_bAutoplay))
		ApplyCheat(pdude);
	SendWeapons(pdude);
#ifdef POSTAL_RIG
	// Test rig only: the dude's state, for scripted checks
	if (pdude)
		{
		const char* pszDir = getenv("POSTAL_RIG_DIR");
		char szPath[1024];
		snprintf(szPath, sizeof(szPath), "%s/dude.state", pszDir ? pszDir : ".");
		FILE* f = fopen(szPath, "w");
		if (f)
			{
			fprintf(f, "x %.1f\ny %.1f\nz %.1f\nrot %.1f\nhp %d\nbullets %d\nshells %d\nweapon %d\nstate %d\ntime %ld\n",
				pdude->GetX(), pdude->GetY(), pdude->GetZ(), pdude->m_dRot, pdude->m_stockpile.m_sHitPoints,
				pdude->m_stockpile.m_sNumBullets, pdude->m_stockpile.m_sNumShells, (int)pdude->GetCurrentWeapon(),
				(int)pdude->m_state, (long)prealm->m_time.GetGameTime());
			fclose(f);
			}
		}
#endif
	if (s_bAutoplay && pdude)
		IOSBot_Think(prealm, pdude);
	}

static void TopBar(IOSTouchHud* phud)
	{
	// The game's status strip: the top 40 of 480 lines; its text the left ~650 of
	// wideScreenWidth; "Population N" ends near 165, "Hostiles" starts near 295.
	double w = wideScreenWidth > 0 ? wideScreenWidth : 640;
	phud->afTopBar[0] = 0; phud->afTopBar[1] = 0; phud->afTopBar[2] = 1; phud->afTopBar[3] = 40.0f / 480.0f;
	phud->afTopText[0] = 0; phud->afTopText[1] = 0; phud->afTopText[2] = (float)(650.0 / w); phud->afTopText[3] = 40.0f / 480.0f;
	phud->afTopGap[0] = (float)(170.0 / w); phud->afTopGap[1] = 0;
	phud->afTopGap[2] = (float)(290.0 / w); phud->afTopGap[3] = 40.0f / 480.0f;
	}

void IOSGame_Present(void* pSDLWindow)
	{
	s_uPresents++;
	bool bPlaying = (s_uPresents - s_uLastPlay) <= 1 && s_pdude != NULL;
	if (!bPlaying)
		{
		s_prealm = NULL;
		s_pdude = NULL;
		}
	int iMode = !bPlaying ? IOSTOUCH_HIDDEN : (s_bAutoplay ? IOSTOUCH_AUTOPLAY : IOSTOUCH_GAMEPLAY);

	if (!s_bLevelsSent && bPlaying)
		{
		LoadLevelNames();
		const char* apsz[IOSGAME_MAX_LEVELS];
		for (int i = 0; i < s_ctLevels; i++)
			apsz[i] = s_aszLevel[i];
		IOSTouch_SetLevels(apsz, s_ctLevels);
		s_bLevelsSent = true;
		}

	IOSTouchHud hud;
	memset(&hud, 0, sizeof(hud));
	TopBar(&hud);
	hud.bCheatOn = s_bCheat;
	hud.bFollowing = s_bFollow;
	hud.iSlowMo = s_iSlow;
	hud.uLevel = s_uLevel;

	static unsigned s_uDrawn = 0;
	unsigned req = IOSTouch_Update(pSDLWindow, iMode, &hud, ++s_uDrawn);
	IOSTouch_ReadInput(&s_in);
	if (!bPlaying)
		{
		// Out of play (menus, cutscenes, loading) the buttons can't ask for
		// anything; drop what may have come in on the way out, so nothing
		// fires later at the wrong moment.
		memset(&s_in, 0, sizeof(s_in));
		req = 0;
		s_bFreeze = false;
		(void)IOSTouch_TakeLevelPick();
		// Autoplay: tap through the level's intro and score screens
		static unsigned s_uLastTap = 0;
		if (s_bAutoplay && s_uPresents - s_uLastTap > 90)
			{
			s_uLastTap = s_uPresents;
			SDL_Event ev;
			memset(&ev, 0, sizeof(ev));
			ev.type = SDL_KEYDOWN;
			ev.key.keysym.sym = SDLK_SPACE;
			ev.key.keysym.scancode = SDL_SCANCODE_SPACE;
			SDL_PushEvent(&ev);
			ev.type = SDL_KEYUP;
			SDL_PushEvent(&ev);
			}
		return;
		}

	if (req & IOSTOUCH_REQ_MENU)			s_uPlayReq |= IOSGAME_PLAY_MENU;
	if (req & IOSTOUCH_REQ_SAVESCREEN)	s_uPlayReq |= IOSGAME_PLAY_SAVESCREEN;
	if (req & IOSTOUCH_REQ_QUICKSAVE)	s_uPlayReq |= IOSGAME_PLAY_QUICKSAVE;
	if (req & IOSTOUCH_REQ_LOADSCREEN)
		{
		s_iNext = IOSGAME_NEXT_LOADSCREEN;
		s_uPlayReq |= IOSGAME_PLAY_ASKABORT;
		}
	if (req & IOSTOUCH_REQ_QUICKLOAD)
		{
		s_iNext = IOSGAME_NEXT_QUICKLOAD;
		s_uPlayReq |= IOSGAME_PLAY_ABORT;
		}
	if (req & IOSTOUCH_REQ_CHEAT)
		SetCheat(!s_bCheat);
	int iPick = IOSTouch_TakeLevelPick();
	if (iPick >= 0 && iPick < s_ctLevels)
		{
		s_iNext = IOSGAME_NEXT_LEVEL;
		s_sNextRealm = (int16_t)iPick;
		s_uPlayReq |= IOSGAME_PLAY_ABORT;
		}
	if ((req & (IOSTOUCH_REQ_AUTOPLAY | IOSTOUCH_REQ_AUTONEXT)) && s_ctLevels > 0)
		{
		s_bAutoplay = true;
		s_iNext = IOSGAME_NEXT_LEVEL;
		s_sNextRealm = (int16_t)(rand() % s_ctLevels);
		s_uPlayReq |= IOSGAME_PLAY_ABORT;
		}
	if (req & IOSTOUCH_REQ_AUTOSTOP)
		{
		s_bAutoplay = false;
		s_bFollow = false;
		s_iNext = IOSGAME_NEXT_NONE;
		s_uPlayReq |= IOSGAME_PLAY_ABORT;
		if (s_pdude && !s_bCheat)
			s_pdude->IOS_SetInvincible(false);
		}
	if (req & IOSTOUCH_REQ_KEYBOARD)
		{
		s_bKeyboard = !s_bKeyboard;
		if (s_bKeyboard)
			SDL_StartTextInput();
		else
			SDL_StopTextInput();
		}
	if (req & IOSTOUCH_REQ_FOLLOW)
		{
		s_bFollow = !s_bFollow && (s_bCheat || s_bAutoplay);
		s_idFollow = CIdBank::IdNil;
		}
	if ((req & IOSTOUCH_REQ_FOLLOWNEXT) && s_bFollow && s_prealm)
		FollowNext(s_prealm);
	// Camera
	if (req & IOSTOUCH_REQ_PHOTO)
		s_iPhoto = PHOTO_ONE;
	if (req & IOSTOUCH_REQ_BURST)
		{
		s_iBurstShot = (s_uPresents - s_uLastBurstAt > 30) ? 1 : s_iBurstShot + 1;
		s_uLastBurstAt = s_uPresents;
		s_iPhoto = PHOTO_BURST;
		}
	if (req & IOSTOUCH_REQ_ZOOMOUT)
		s_bZoomOut = true;
	if (req & IOSTOUCH_REQ_FREEZE)
		{
		s_bFreeze = true;
		s_dPanX = s_dPanY = 0;
		}
	if (req & IOSTOUCH_REQ_UNFREEZE)
		s_bFreeze = false;
	if (s_bFreeze)
		{
		// Points dragged to game pixels: the game is 480 high on the screen
		double dScale = 480.0 / IOSGAME_SCREEN_H_PT;
		s_dPanX += s_in.fPanX * dScale;
		s_dPanY += s_in.fPanY * dScale;	// UIKit points: y grows down, as the scene's does
		}
	if (req & IOSTOUCH_REQ_SLOWMO)
		s_iSlow = s_iSlow ? 0 : s_iSlowSpeed;
	if ((req & IOSTOUCH_REQ_SLOWSPEED) && s_iSlow)
		s_iSlow = s_iSlowSpeed = (s_iSlowSpeed == 2 ? 4 : 2);
	}

unsigned IOSGame_TakePlayRequests(void)
	{
	unsigned u = s_uPlayReq;
	s_uPlayReq = 0;
	return u;
	}

int IOSGame_TakeNext(int16_t* psRealm)
	{
	if (s_iNext == IOSGAME_NEXT_NONE && s_bAutoplay && s_ctLevels > 0)
		{
		s_iNext = IOSGAME_NEXT_LEVEL;		// a level ended in autoplay: another random one
		s_sNextRealm = (int16_t)(rand() % s_ctLevels);
		}
	int i = s_iNext;
	s_iNext = IOSGAME_NEXT_NONE;
	if (psRealm)
		*psRealm = s_sNextRealm;
	return i;
	}

////////////////////////////////////////////////////////////////////////////////
// Input
////////////////////////////////////////////////////////////////////////////////
U32 IOSGame_Input(U32 input)
	{
	if (s_bAutoplay)
		return IOSBot_Input(input);
	if (s_in.ulButtons & IOSTOUCH_FIRE)			input |= INPUT_FIRE;
	if (s_in.ulButtons & IOSTOUCH_DUCK)			input |= INPUT_DUCK;
	if (s_in.ulButtons & IOSTOUCH_EXECUTE)		input |= INPUT_EXECUTE;
	// POSTAL 2015 runs by default (its run key walks). The stick walks below its
	// edge and runs at the edge (the overlay sets IOSTOUCH_RUN there).
	if (s_in.fMoveX != 0 || s_in.fMoveY != 0)
		{
		if (s_in.ulButtons & IOSTOUCH_RUN)
			input |= INPUT_RUN;
		else
			input &= ~INPUT_RUN;
		}
	if (s_in.ulButtons & IOSTOUCH_NEXTWEAPON)	input |= INPUT_WEAPON_NEXT;
	if (s_in.ulButtons & IOSTOUCH_PREVWEAPON)	input |= INPUT_WEAPON_PREV;
	if (s_in.iSelectWeapon > 0 && s_pdude)
		s_pdude->SetWeapon((CDude::WeaponType)(s_in.iSelectWeapon - 1), false);
	return input;
	}

static double ScreenAngle(float x, float y)
	{
	// Screen terms (right +, up +) to the dude's rotation: 0 east, 90 up the screen
	double d = atan2((double)y, (double)x) * 180.0 / M_PI;
	if (d < 0)
		d += 360.0;
	return d;
	}

bool IOSGame_Move(double* pdVel, double* pdAngle)
	{
	float x = s_in.fMoveX, y = s_in.fMoveY;
	if (s_bAutoplay)
		return IOSBot_Move(pdVel, pdAngle);
	double v = sqrt((double)x * x + (double)y * y);
	if (v < 0.01)
		return false;
	// Walking speed fills the stick up to its run edge (85%), so a light push still moves
	v /= 0.85;
	*pdVel = v > 1.0 ? 1.0 : v;
	*pdAngle = ScreenAngle(x, y);
	return true;
	}

bool IOSGame_Aim(double* pdAngle)
	{
	if (s_bAutoplay)
		return IOSBot_Aim(pdAngle);
	if (!s_in.bAim)
		return false;
	*pdAngle = ScreenAngle(s_in.fAimX, s_in.fAimY);
	return true;
	}

double IOSGame_TimeScale(void)
	{
	if (IOSTouch_HoldsGame())
		return 0.0;
	return s_iSlow ? 1.0 / s_iSlow : 1.0;
	}

const char* IOSGame_QuickSaveFile(void)
	{
	return "savegame/quick.gme";
	}

bool IOSGame_Autoplay(void)
	{
	return s_bAutoplay;
	}
