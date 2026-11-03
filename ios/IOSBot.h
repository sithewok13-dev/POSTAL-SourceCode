// POSTAL for iOS: autoplay's computer player (drives the dude for demo videos).
#ifndef IOS_BOT_H
#define IOS_BOT_H
#include "System.h"
class CRealm;
class CDude;
void IOSBot_NewLevel(CRealm* prealm);
void IOSBot_Think(CRealm* prealm, CDude* pdude);	// once per frame of play
U32  IOSBot_Input(U32 input);
bool IOSBot_Move(double* pdVel, double* pdAngle);
bool IOSBot_Aim(double* pdAngle);
#endif
