// POSTAL for iOS: the game side of the touch controls and the extras
// (cheat, level list, follow cam, slow motion, autoplay, saving and loading).
// Called from small "Added: iOS" hooks in the engine; talks to the overlay
// only through IOSTouch.h. Built for the app (PLATFORM_IOS) and for the Linux
// test rig (POSTAL_RIG, with ios/rig/IOSTouchRig.c standing in for the overlay).
#ifndef IOS_GAME_H
#define IOS_GAME_H

#include "System.h"

class CRealm;
class CDude;

// Requests the play loop carries out (play.cpp, CPlayInput::CoreLoopUserInput)
enum {
	IOSGAME_PLAY_MENU       = 1 << 0,	// open the game menu
	IOSGAME_PLAY_SAVESCREEN = 1 << 1,	// open the game's save screen
	IOSGAME_PLAY_QUICKSAVE  = 1 << 2,	// save to the quick slot now
	IOSGAME_PLAY_ABORT      = 1 << 3,	// leave the level (a load, a level pick or autoplay follows)
	IOSGAME_PLAY_ASKABORT   = 1 << 4,	// ask "quit this level?" first (LOAD tapped)
};

// What the main game loop does after the level ends (game.cpp)
enum {
	IOSGAME_NEXT_NONE = 0,
	IOSGAME_NEXT_QUICKLOAD,		// load the quick slot
	IOSGAME_NEXT_LOADSCREEN,	// open the load screen
	IOSGAME_NEXT_LEVEL,			// start level *psRealm on its own
};

class CCamera;

// Every frame of play (play.cpp CoreLoopRender, after Update): the realm, the player, the camera
void IOSGame_PlayFrame(CRealm* prealm, CDude* pdude, CCamera* pcamera);
// Every frame shown, before IOSGame_Present (Bdisp): the frame as 0xAARRGGBB pixels, for photos
void IOSGame_FrameShown(const uint32_t* pulARGB, int iWidth, int iHeight);
// Freeze and pan: moves the camera's 2D target by the player's drag
void IOSGame_Pan2D(int16_t* psX, int16_t* psY);
// Bdisp's palette, for the zoom-out photo (0xAARRGGBB)
uint32_t IOS_PaletteARGB(int iIndex);
// Every frame shown, menus too (Bdisp rspPresentFrame): runs the overlay
void IOSGame_Present(void* pSDLWindow);

unsigned IOSGame_TakePlayRequests(void);
int IOSGame_TakeNext(int16_t* psRealm);

// The player's input for this frame (input.cpp): adds the buttons' bits
U32 IOSGame_Input(U32 input);
// Twin-stick movement and facing (Bjoy.cpp): false if the touch controls don't move/aim
bool IOSGame_Move(double* pdVel, double* pdAngle);
bool IOSGame_Aim(double* pdAngle);

// The camera's target (play.cpp): false = follow the dude as usual
bool IOSGame_CameraTarget(CRealm* prealm, double* pdX, double* pdY, double* pdZ);

// Game time per real time: 1, 0.5, 0.25, or 0 while the wheel/list holds the game
double IOSGame_TimeScale(void);

// The quick save slot's file (rspix path)
const char* IOSGame_QuickSaveFile(void);

// Autoplay is running (the bot drives the dude)
bool IOSGame_Autoplay(void);

#endif
