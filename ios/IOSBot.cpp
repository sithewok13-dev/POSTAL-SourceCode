// POSTAL for iOS: autoplay's computer player, for demonstration videos.
// It hunts the nearest living hostile (bystanders when none are left), aims
// and fires when close, finishes off the wounded now and then, swaps
// weapons every so often for variety, and walks round what blocks it. It
// plays with the cheat's protection (IOSGame applies it), so it never dies.
// When the level's goal is met it ends the level; IOSGame then starts
// another random one.
#include "RSPiX.h"
#include <math.h>
#include <stdlib.h>
#include "realm.h"
#include "dude.h"
#include "doofus.h"
#include "input.h"
#include "IOSBot.h"

static double	s_dMoveVel		= 0;
static double	s_dMoveAngle	= 0;
static bool		s_bAim			= false;
static double	s_dAimAngle		= 0;
static U32		s_uInput		= 0;
static U16		s_idTarget		= 0;
static int32_t	s_lDetourUntil	= 0;
static double	s_dDetourAngle	= 0;
static int32_t	s_lNextWeapon	= 0;
static int32_t	s_lLevelStart	= 0;
static int32_t	s_lCheckAt		= 0;	// next time to compare against s_dCheckX/Z
static double	s_dCheckX = 0, s_dCheckZ = 0;
static double	s_dBestDist		= 1e18;	// closest it has got to the current target
static int32_t	s_lBestAt		= 0;		// when it last got closer
static U16		s_aidSkip[4];				// targets given up on lately
static int32_t	s_alSkipUntil[4];

// Weapons it likes to show off (owned ones only; the cheat gives them all)
static const CDude::WeaponType s_aShowOff[] =
	{
	CDude::SemiAutomatic, CDude::ShotGun, CDude::SprayCannon, CDude::Rocket,
	CDude::Heatseeker, CDude::FlameThrower, CDude::Grenade, CDude::FireBomb,
	CDude::Napalm, CDude::DoubleBarrel,
	};

void IOSBot_NewLevel(CRealm* prealm)
	{
	s_idTarget = 0;
	s_lDetourUntil = 0;
	s_lNextWeapon = 0;
	s_lLevelStart = prealm ? prealm->m_time.GetGameTime() : 0;
	s_lCheckAt = 0;
	s_dBestDist = 1e18;
	memset(s_aidSkip, 0, sizeof(s_aidSkip));
	memset(s_alSkipUntil, 0, sizeof(s_alSkipUntil));
	}

// Screen-style angle (0 east, 90 up the screen) from the dude to a point.
// The realm's z grows down the screen, so up the screen is -z.
static double AngleTo(double dx, double dz)
	{
	double d = atan2(-dz, dx) * 180.0 / M_PI;
	return d < 0 ? d + 360.0 : d;
	}

static bool Alive(CThing3d* p)
	{
	return p->m_state != CThing3d::State_Dead && p->m_state != CThing3d::State_Die;
	}

// The nearest person worth chasing: hostiles first, then anyone; the wounded
// (writhing) count, so it walks over to finish them.
static bool Skipped(U16 id, int32_t lNow)
	{
	for (int i = 0; i < 4; i++)
		if (s_aidSkip[i] == id && lNow < s_alSkipUntil[i])
			return true;
	return false;
	}

static CDoofus* FindTarget(CRealm* prealm, CDude* pdude)
	{
	int32_t lNow = prealm->m_time.GetGameTime();
	CDoofus* pbest = NULL;
	double dBest = 1e18;
	for (int pass = 0; pass < 2 && !pbest; pass++)
		{
		CListNode<CThing>* phead = &prealm->m_aclassHeads[CThing::CPersonID];
		for (CListNode<CThing>* pn = phead->m_pnNext; pn && pn->m_powner; pn = pn->m_pnNext)
			{
			CDoofus* p = (CDoofus*)pn->m_powner;
			if (!Alive(p))
				continue;
			if (pass == 0 && p->IOS_IsCivilian())
				continue;
			if (Skipped(p->GetInstanceID(), lNow))
				continue;
			double dx = p->GetX() - pdude->GetX(), dz = p->GetZ() - pdude->GetZ();
			double d = dx * dx + dz * dz;
			if (d < dBest) { dBest = d; pbest = p; }
			}
		}
	return pbest;
	}

void IOSBot_Think(CRealm* prealm, CDude* pdude)
	{
	int32_t lNow = prealm->m_time.GetGameTime();
	double dLastVel = s_dMoveVel;
	s_uInput = 0;
	s_bAim = false;
	s_dMoveVel = 0;

	// Done: the level's goal met (or 4 minutes up) -> end it
	if (prealm->IsEndOfLevelGoalMet(false) || lNow - s_lLevelStart > 4 * 60 * 1000)
		{
		prealm->m_bPressedEndLevelKey = true;
		return;
		}

	// Every 6-10 s, another weapon to show off
	if (lNow >= s_lNextWeapon)
		{
		int n = (int)(sizeof(s_aShowOff) / sizeof(s_aShowOff[0]));
		pdude->SetWeapon(s_aShowOff[rand() % n], false);
		s_lNextWeapon = lNow + 6000 + rand() % 4000;
		}

	CDoofus* ptarget = FindTarget(prealm, pdude);
	if (!ptarget)
		return;
	double dx = ptarget->GetX() - pdude->GetX();
	double dz = ptarget->GetZ() - pdude->GetZ();
	double dist = sqrt(dx * dx + dz * dz);
	double dAngle = AngleTo(dx, dz);

	// A new target: start measuring progress towards it
	if (ptarget->GetInstanceID() != s_idTarget)
		{
		s_idTarget = ptarget->GetInstanceID();
		s_dBestDist = dist;
		s_lBestAt = lNow;
		}
	else if (dist < s_dBestDist - 10)
		{
		s_dBestDist = dist;
		s_lBestAt = lNow;
		}
	// No closer for 8 s: give up on this one for 20 s and try another
	if (lNow - s_lBestAt > 8000 && dist > 140)
		{
		static int s_iSkip = 0;
		s_aidSkip[s_iSkip] = s_idTarget;
		s_alSkipUntil[s_iSkip] = lNow + 20000;
		s_iSkip = (s_iSkip + 1) % 4;
		s_idTarget = 0;
		}

	// Stuck (moved under 15 units in the last second while walking): walk off
	// to one side for a moment
	if (lNow >= s_lCheckAt)
		{
		double moved = fabs(pdude->GetX() - s_dCheckX) + fabs(pdude->GetZ() - s_dCheckZ);
		bool bWalking = dLastVel > 0 && lNow >= s_lDetourUntil;
		if (bWalking && s_lCheckAt != 0 && moved < 15)
			{
			s_dDetourAngle = dAngle + ((rand() & 1) ? 90 : -90) + (rand() % 60 - 30);
			s_lDetourUntil = lNow + 900 + rand() % 900;
			}
		s_dCheckX = pdude->GetX();
		s_dCheckZ = pdude->GetZ();
		s_lCheckAt = lNow + 1000;
		}

	bool bWrithing = ptarget->m_state == CThing3d::State_Writhing;
	if (lNow < s_lDetourUntil)
		{
		s_dMoveVel = 1.0;
		s_dMoveAngle = s_dDetourAngle;
		s_uInput |= INPUT_RUN;
		}
	else if (dist > (bWrithing ? 18 : 140))
		{
		s_dMoveVel = 1.0;
		s_dMoveAngle = dAngle;
		if (dist > 260)
			s_uInput |= INPUT_RUN;
		}

	// Aim at the target; fire when in range (execute the wounded up close)
	s_bAim = true;
	s_dAimAngle = dAngle;
	if (bWrithing && dist <= 24)
		s_uInput |= INPUT_EXECUTE;
	else if (dist < 260)
		s_uInput |= INPUT_FIRE;
	}

U32 IOSBot_Input(U32 input)
	{
	return input | s_uInput;
	}

bool IOSBot_Move(double* pdVel, double* pdAngle)
	{
	if (s_dMoveVel <= 0)
		return false;
	*pdVel = s_dMoveVel;
	*pdAngle = s_dMoveAngle;
	return true;
	}

bool IOSBot_Aim(double* pdAngle)
	{
	if (!s_bAim)
		return false;
	*pdAngle = s_dAimAngle;
	return true;
	}
