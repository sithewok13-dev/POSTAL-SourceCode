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
#include "score.h"
#include "IOSGame.h"
#include "IOSTouch.h"
#include "camera.h"
#include "hood.h"
#include "menus.h"	// (menuMain; WishPiX menu.h: GetCurrentMenu)
extern void IOS_MenuCancel(void);	// Added: iOS: WishPiX/Menu/menu.cpp
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
static bool				s_bKeyboard		= false;
static int				s_ctRestart		= 0;	// frames left to send the revive key (a tap after death)
static unsigned		s_uLevel			= 0;	// bumped for each new realm
static int32_t		s_lGoalUntil		= 0;	// game time the goal line shows until (its first seconds of play)
static CRealm*			s_prealm			= NULL;	// valid only during the frame it was set
static CDude*			s_pdude			= NULL;
static bool				s_bLevelsSent	= false;
static CDude*			s_pdudeInvincibleSet = NULL;	// the dude the cheat made invincible
static bool				s_bWasInvincible = false;		// ...and whether it already was (the game's own cheat)

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
	if (s_pdudeInvincibleSet != pdude)
		{
		s_pdudeInvincibleSet = pdude;
		s_bWasInvincible = pdude->IOS_IsInvincible();
		}
	pdude->IOS_SetInvincible(true);
	}

static void EndInvincible(void)
	{
	if (s_pdude && s_pdudeInvincibleSet == s_pdude)
		s_pdude->IOS_SetInvincible(s_bWasInvincible);	// as it was before the cheat
	s_pdudeInvincibleSet = NULL;
	}

static void SetCheat(bool bOn)
	{
	if (bOn == s_bCheat)
		return;
	s_bCheat = bOn;
	// The laugh when it goes on (from the tray or the icon alike); off is silent
	if (bOn)
		PlaySample(g_smidDemonLaugh1, SampleMaster::UserFeedBack);
	if (!bOn)
		EndInvincible();					// keeps the weapons, as JK's sithewok
	if (!bOn)
		s_bFollow = false;					// FOLLOW is a cheat's tool: it goes with it
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

// The wheel's pictures: cut from the hood's full toolbar picture, where the
// bottom bar draws each weapon (toolbar.cpp's rectangles, in 640x480 terms).
// The double barrel borrows the shotgun's; the deathwad has none (its name shows).
static const struct { CDude::WeaponType type; int16_t x, y, w, h; bool bFire; } s_aIcon[] =
	{
	{ CDude::SemiAutomatic,		123, 443, 48, 23 },
	{ CDude::ShotGun,				176, 444, 42, 19 },
	{ CDude::SprayCannon,		221, 442, 44, 22 },
	{ CDude::Grenade,				272, 443, 19, 23 },
	{ CDude::Rocket,				306, 442, 54, 21 },
	{ CDude::Heatseeker,			337, 464, 26, 13 },
	{ CDude::FireBomb,			390, 442, 20, 24 },
	{ CDude::Napalm,				413, 441, 49, 23 },
	{ CDude::FlameThrower,		464, 442, 52, 22 },
	{ CDude::ProximityMine,		567, 443, 30, 23 },
	{ CDude::TimedMine,			599, 443, 30, 26 },
	{ CDude::BouncingBettyMine,538, 444, 28, 28 },
	{ CDude::DoubleBarrel,		176, 444, 42, 19 },
	// The Deathwad (a cheat's secret weapon: a rocket stuffed with napalm, fuel and
	// grenades) was never on the bar: the missile launcher, in fire colours
	{ CDude::DeathWad,			306, 442, 54, 21, true },
	};

static CHood* s_phoodIcons = NULL;	// the hood the icons were last cut from

static void SendIcons(CRealm* prealm)
	{
	CHood* phood = prealm ? prealm->m_phood : NULL;
	if (!phood || phood == s_phoodIcons || !phood->m_pimFullBar || !phood->m_pimFullBar->m_pData)
		return;
	s_phoodIcons = phood;
	RImage* pim = phood->m_pimFullBar;
	// The empty bar is the same picture without the weapons: where the two match is
	// the bar's background, which the wheel shows see-through (just the weapon)
	RImage* pimEmpty = phood->m_pimEmptyBar;
	if (pimEmpty && (!pimEmpty->m_pData || pimEmpty->m_sWidth != pim->m_sWidth || pimEmpty->m_sHeight != pim->m_sHeight))
		pimEmpty = NULL;
	// The bar picture may be just the bottom strip, or the whole 480 lines
	int16_t sYOff = (pim->m_sHeight < 480) ? (int16_t)(480 - pim->m_sHeight) : 0;
	for (int i = 0; i < IOSGAME_WHEEL_SLICES; i++)
		{
		for (unsigned k = 0; k < sizeof(s_aIcon) / sizeof(s_aIcon[0]); k++)
			{
			if (s_aIcon[k].type != s_aWheel[i].type)
				continue;
			int16_t x0 = s_aIcon[k].x, y0 = s_aIcon[k].y - sYOff, w = s_aIcon[k].w, h = s_aIcon[k].h;
			if (x0 < 0 || y0 < 0 || x0 + w > pim->m_sWidth || y0 + h > pim->m_sHeight)
				break;
			unsigned char* pub = (unsigned char*)malloc((size_t)w * h * 4);
			if (!pub)
				break;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
					{
					size_t o = (size_t)(y0 + y) * pim->m_lPitch + x0 + x;
					uint8_t idx = pim->m_pData[o];
					uint32_t c = IOS_PaletteARGB(idx);
					unsigned char* d = pub + ((size_t)y * w + x) * 4;
					d[0] = (unsigned char)(c >> 16); d[1] = (unsigned char)(c >> 8); d[2] = (unsigned char)c;
					bool bBack = idx == 0;	// index 0 is the bar's see-through black
					if (!bBack && pimEmpty)
						{
						// the same colour as the empty bar there, or close to it (the weapons'
						// soft shadows on the bar): background
						uint32_t e = IOS_PaletteARGB(pimEmpty->m_pData[(size_t)(y0 + y) * pimEmpty->m_lPitch + x0 + x]);
						int dr = (int)((c >> 16) & 255) - (int)((e >> 16) & 255);
						int dg = (int)((c >> 8) & 255) - (int)((e >> 8) & 255);
						int db = (int)(c & 255) - (int)(e & 255);
						int r = (int)((c >> 16) & 255), b = (int)(c & 255);
						bBack = dr == 0 && dg == 0 && db == 0
							|| (r - b > 18 && dr * dr + dg * dg + db * db < 48 * 48);	// (the weapons are grey; the bar and its shadows brown)
						}
					d[3] = bBack ? 0 : 255;
					if (s_aIcon[k].bFire)
						{
						// its brightness on a dark red -> orange -> yellow ramp
						int l = (int)(((c >> 16) & 255) * 30 + ((c >> 8) & 255) * 59 + (c & 255) * 11) / 100;
						l = l * 2 > 255 ? 255 : l * 2;
						d[0] = (unsigned char)(l < 128 ? 96 + l : 224 + (l - 128) / 4);
						d[1] = (unsigned char)(l < 128 ? l / 3 : 42 + (l - 128) * 3 / 2);
						d[2] = (unsigned char)(l < 192 ? 0 : (l - 192) * 2);
						}
					}
			// Clear stray dots: a pixel with at most one solid neighbour
			for (int pass = 0; pass < 1; pass++)
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++)
						{
						unsigned char* d = pub + ((size_t)y * w + x) * 4;
						if (!d[3])
							continue;
						int ct = 0;
						for (int dy = -1; dy <= 1; dy++)
							for (int dx = -1; dx <= 1; dx++)
								if ((dx || dy) && x + dx >= 0 && x + dx < w && y + dy >= 0 && y + dy < h
									&& pub[((size_t)(y + dy) * w + x + dx) * 4 + 3])
									ct++;
						if (ct <= 1)
							d[3] = 0;
						}
			IOSTouch_SetWeaponIcon(i, pub, w, h);
			free(pub);
			break;
			}
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
	if (s_idFollow != CIdBank::IdNil && prealm->m_idbank.GetThingByID(&pthing, s_idFollow) == 0
		&& pthing->GetClassID() == CThing::CPersonID)	// an ID can be reused by another kind of thing
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
	for (int pass = 0; pass < 1; pass++)
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
enum { PHOTO_NONE = 0, PHOTO_ONE };
static int		s_iPhoto		= PHOTO_NONE;	// a grab wanted on the next frame shown
static CCamera*	s_pcamera		= NULL;		// the play camera, valid while s_prealm is

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
	// Full-screen play: the whole frame is the game's picture (no strips to crop)
	int iTop = 0, iRows = h;
#if PLATFORM_IOS
	SavePhoto(pul, w, h, iTop, iRows, "screenshot", IOSPHOTOS_ADD_INFO);	// what's on screen, without the buttons
#else
	SavePhoto(pul, w, h, iTop, iRows, "screenshot", 0);
#endif
	s_iPhoto = PHOTO_NONE;
	}

void IOSGame_PlayFrame(CRealm* prealm, CDude* pdude, CCamera* pcamera, bool bLive)
	{
	// Demos (the attract mode, the ending) play recorded input: leave them alone
	if (!bLive)
		return;
	// A new level: another realm or hood, or the realm's clock started again
	// (one CRealm serves every level of a Play(), and the in-game menu skips frames)
	static CRealm* s_prealmSeen = NULL;
	static CHood* s_phoodSeen = NULL;
	static int32_t s_lTimeSeen = 0;
	int32_t lTime = prealm->m_time.GetGameTime();
	if (prealm != s_prealmSeen || prealm->m_phood != s_phoodSeen || lTime + 1000 < s_lTimeSeen)
		{
		s_uLevel++;
		s_lGoalUntil = lTime + 8000;	// as the PC strip did: the goal for its first 8 seconds
		s_idFollow = CIdBank::IdNil;
		s_phoodIcons = NULL;
		s_pdudeInvincibleSet = NULL;
		}
	s_prealmSeen = prealm;
	s_phoodSeen = prealm->m_phood;
	s_lTimeSeen = lTime;
	s_pcamera = pcamera;
	s_prealm = prealm;
	s_pdude = pdude;
	s_uLastPlay = s_uPresents;
	if (pdude && s_bCheat)
		ApplyCheat(pdude);
	SendIcons(prealm);
	SendWeapons(pdude);
#ifdef POSTAL_RIG
	// Test rig only: the dude's state, for scripted checks
	if (pdude)
		{
		const char* pszDir = getenv("POSTAL_RIG_DIR");
		char szPath[1024];
		snprintf(szPath, sizeof(szPath), "%s/dude.state.tmp", pszDir ? pszDir : ".");	// (written whole, then renamed: a reader never sees half)
		FILE* f = fopen(szPath, "w");
		if (f)
			{
			fprintf(f, "kills %d\nx %.1f\ny %.1f\nz %.1f\nrot %.1f\nhp %d\nbullets %d\nshells %d\nweapon %d\nstate %d\ntime %ld\n", prealm->m_sHostileKills,
				pdude->GetX(), pdude->GetY(), pdude->GetZ(), pdude->m_dRot, pdude->m_stockpile.m_sHitPoints,
				pdude->m_stockpile.m_sNumBullets, pdude->m_stockpile.m_sNumShells, (int)pdude->GetCurrentWeapon(),
				(int)pdude->m_state, (long)prealm->m_time.GetGameTime());
			fclose(f);
			char szFinal[1024];
			snprintf(szFinal, sizeof(szFinal), "%s/dude.state", pszDir ? pszDir : ".");
			rename(szPath, szFinal);
			}
		}
#endif
	}

// Full-screen play: the strip's text and the weapon bar's numbers, for the overlay
static void FillFullScreenHud(IOSTouchHud* phud)
	{
	phud->bFullScreen = 1;
	phud->iHealth = -1;
	phud->iAmmo = -1;
	if (!s_prealm)
		return;
	ScoreGetStatusText(s_prealm, phud->szStatus, sizeof(phud->szStatus), phud->szGoal, sizeof(phud->szGoal));
	// (counted from the first frame played, not the realm's clock: that already ran
	// through the level's intro screen)
	if (s_prealm->m_time.GetGameTime() >= s_lGoalUntil)
		phud->szGoal[0] = '\0';
	if (s_pdude)
		{
		// As the bar shows it: a percentage of full health (at least 1 while alive)
		int iHp = s_pdude->m_sOrigHitPoints > 0 ? s_pdude->m_stockpile.m_sHitPoints * 100 / s_pdude->m_sOrigHitPoints : 0;
		if (iHp < 1 && s_pdude->m_stockpile.m_sHitPoints > 0)
			iHp = 1;
		phud->iHealth = s_pdude->IsDead() ? 0 : iHp;
		phud->iKevlar = s_pdude->m_stockpile.m_sKevlarLayers;
		phud->bDead = s_pdude->IsDead() ? 1 : 0;
		CDude::WeaponType wt = s_pdude->GetCurrentWeapon();
		for (int i = 0; i < IOSGAME_WHEEL_SLICES; i++)
			if (s_aWheel[i].type == wt)
				{
				phud->pszWeapon = s_aWheel[i].pszName;
				CThing::ClassIDType id;
				int16_t* psNum = NULL;
				s_pdude->GetWeaponInfo(wt, &id, &psNum);
				phud->iAmmo = (wt == CDude::SemiAutomatic || !psNum) ? -1 : *psNum;	// the machine gun never runs out
				}
		}
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
	// Out of play, one of the game's menus (not the title menu, where Escape would
	// only offer to quit): the BACK button, as Escape
	Menu* pmenu = bPlaying ? NULL : GetCurrentMenu();
	bool bMenu = pmenu != NULL && pmenu != &menuMain;
	int iMode = bPlaying ? IOSTOUCH_GAMEPLAY : (bMenu ? IOSTOUCH_MENU : IOSTOUCH_HIDDEN);

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
	// (no status strip in full-screen play: TopBar's rectangles stay empty)
	hud.bCheatOn = s_bCheat;
	FillFullScreenHud(&hud);
	hud.bFollowing = s_bFollow;
	hud.iSlowMo = s_iSlow;
	hud.uLevel = s_uLevel;

	static unsigned s_uDrawn = 0;
	unsigned req = IOSTouch_Update(pSDLWindow, iMode, &hud, ++s_uDrawn);
	IOSTouch_ReadInput(&s_in);
	if (bMenu && (req & IOSTOUCH_REQ_BACK))
		IOS_MenuCancel();
	if (!bPlaying)
		{
		// Out of play (menus, cutscenes, loading) the buttons can't ask for
		// anything; drop what may have come in on the way out, so nothing
		// fires later at the wrong moment.
		memset(&s_in, 0, sizeof(s_in));
		s_ctRestart = 0;
		req = 0;
		(void)IOSTouch_TakeLevelPick();
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
		// Only leave the level if there is a quick save to load
		RFile file;
		if (file.Open((char*)IOSGame_QuickSaveFile(), "rb", RFile::LittleEndian) == 0)
			{
			file.Close();
			s_iNext = IOSGAME_NEXT_QUICKLOAD;
			s_uPlayReq |= IOSGAME_PLAY_ABORT;
			}
		else
			IOSTouch_ShowMessage("No quick save yet: hold SAVE to make one", 2.5);
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
		s_bFollow = !s_bFollow && s_bCheat;
		s_idFollow = CIdBank::IdNil;
		}
	if ((req & IOSTOUCH_REQ_FOLLOWNEXT) && s_bFollow && s_prealm)
		FollowNext(s_prealm);
	// Camera
	if (req & IOSTOUCH_REQ_PHOTO)
		s_iPhoto = PHOTO_ONE;
	// Dead: a tap anywhere restarts the level (sent for a few frames, like a held key)
	if (req & IOSTOUCH_REQ_RESTART)
		s_ctRestart = 3;
	// Tray SLOW: off -> 1/2 -> 1/4 -> off
	if (req & IOSTOUCH_REQ_SLOWMO)
		s_iSlow = (s_iSlow == 0) ? 2 : (s_iSlow == 2 ? 4 : 0);
	}

unsigned IOSGame_TakePlayRequests(void)
	{
	unsigned u = s_uPlayReq;
	s_uPlayReq = 0;
	return u;
	}

int IOSGame_TakeNext(int16_t* psRealm)
	{
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
	if (s_ctRestart > 0)
		{
		s_ctRestart--;
		if (s_pdude && s_pdude->IsDead())
			input |= INPUT_REVIVE;				// dead: a tap restarts the level (the game's revive key)
		}
	if (s_bFollow)
		return input & ~(INPUT_FIRE | INPUT_EXECUTE | INPUT_DUCK);	// the dude waits while the camera is away
	// Dead: FIRE restarts the level (the game's revive key), but only a fresh press:
	// a FIRE (or aim stick) still held from the fight that killed him must be let go first
	static bool s_bReviveArmed = false;
	bool bDead = s_pdude && s_pdude->IsDead();
	if (bDead)
		{
		if (!(s_in.ulButtons & IOSTOUCH_FIRE))
			s_bReviveArmed = true;
		else if (s_bReviveArmed)
			input |= INPUT_REVIVE;
		}
	else
		s_bReviveArmed = false;
	if ((s_in.ulButtons & IOSTOUCH_FIRE) && !bDead)	input |= INPUT_FIRE;
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
		{
		if (s_pdude->SetWeapon((CDude::WeaponType)(s_in.iSelectWeapon - 1), false))
			PlaySample(g_smidLoadedWeapon, SampleMaster::UserFeedBack);	// as NEXT/PREV do
		}
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
	if (s_bFollow)
		return false;
	double v = sqrt((double)x * x + (double)y * y);
	if (v < 0.01)
		return false;
	*pdVel = v > 1.0 ? 1.0 : v;
	*pdAngle = ScreenAngle(x, y);
	return true;
	}

bool IOSGame_Aim(double* pdAngle)
	{
	if (!s_in.bAim || s_bFollow)
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

