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
#include "IOSGame.h"

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

// Is the line from the dude to (x, z) clear of walls and terrain?
static bool ClearShot(CRealm* prealm, CDude* pdude, double x, double z)
	{
	return prealm->IsPathClear((int16_t)pdude->GetX(), (int16_t)pdude->GetY(), (int16_t)pdude->GetZ(),
		4.0, (int16_t)x, (int16_t)z, 40);
	}

// A weapon that suits the range (owned ones only; autoplay has the cheat's arsenal)
static void PickWeaponForRange(CDude* pdude, double dist)
	{
	static const CDude::WeaponType aNear[] = { CDude::ShotGun, CDude::FlameThrower, CDude::DoubleBarrel, CDude::SprayCannon };
	static const CDude::WeaponType aMid[]  = { CDude::SemiAutomatic, CDude::SprayCannon, CDude::ShotGun, CDude::FireBomb };
	static const CDude::WeaponType aFar[]  = { CDude::Rocket, CDude::Heatseeker, CDude::Grenade, CDude::Napalm, CDude::SemiAutomatic };
	const CDude::WeaponType* a = dist < 90 ? aNear : (dist < 200 ? aMid : aFar);
	int n = dist < 90 ? 4 : (dist < 200 ? 4 : 5);
	pdude->SetWeapon(a[rand() % n], false);
	}

void IOSBot_Think(CRealm* prealm, CDude* pdude)
	{
	int32_t lNow = prealm->m_time.GetGameTime();
	double dLastVel = s_dMoveVel;
	s_uInput = 0;
	s_bAim = false;
	s_dMoveVel = 0;

	// Done: the level's goal met -> press the end key (a standard level wants it
	// pressed as well as the kills; other modes end on their own). 4 minutes
	// without getting there -> IOSGame moves on to another level.
	bool bGoal = (prealm->m_ScoringMode == CRealm::Standard) ? prealm->IsEndOfLevelGoalMet(true)
	                                                          : prealm->IsEndOfLevelGoalMet(false);
	if (bGoal)
		prealm->m_bPressedEndLevelKey = true;
	if (lNow - s_lLevelStart > 4 * 60 * 1000)
		{
		IOSGame_AutoNext();
		s_lLevelStart = lNow;	// once
		}

	CDoofus* ptarget = FindTarget(prealm, pdude);
	if (!ptarget)
		{
		// Nobody to chase: wander, a new heading every 3 s
		static int32_t s_lWanderUntil = 0;
		static double s_dWander = 0;
		if (lNow >= s_lWanderUntil)
			{
			s_dWander = rand() % 360;
			s_lWanderUntil = lNow + 3000;
			}
		s_dMoveVel = 0.6;
		s_dMoveAngle = s_dWander;
		return;
		}
	double dx = ptarget->GetX() - pdude->GetX();
	double dz = ptarget->GetZ() - pdude->GetZ();
	double dist = sqrt(dx * dx + dz * dz);
	double dAngle = AngleTo(dx, dz);
	bool bClear = ClearShot(prealm, pdude, ptarget->GetX(), ptarget->GetZ());

	// A weapon for the range, now and then (every 6-10 s)
	if (lNow >= s_lNextWeapon)
		{
		PickWeaponForRange(pdude, dist);
		s_lNextWeapon = lNow + 6000 + rand() % 4000;
		}

	// A new target: start measuring progress towards it
	if (ptarget->GetInstanceID() != s_idTarget)
		{
		s_idTarget = ptarget->GetInstanceID();
		s_dBestDist = dist;
		s_lBestAt = lNow;
		}
	else if (dist < s_dBestDist - 10 || (bClear && dist < 260))
		{
		s_dBestDist = MIN(dist, s_dBestDist);
		s_lBestAt = lNow;
		}
	// No closer (and no clear shot) for 8 s: give up on this one for 20 s
	if (lNow - s_lBestAt > 8000)
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
		}
	else if (dist > (bWrithing ? 18 : 140) || !bClear)
		{
		// Close in (and round whatever blocks the shot)
		s_dMoveVel = 1.0;
		s_dMoveAngle = dAngle;
		}

	// Aim at the target; fire when in range with a clear line (execute the wounded up close)
	s_bAim = true;
	s_dAimAngle = dAngle;
	if (bWrithing && dist <= 24)
		s_uInput |= INPUT_EXECUTE;
	else if (dist < 260 && (bClear || dist < 120))
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
