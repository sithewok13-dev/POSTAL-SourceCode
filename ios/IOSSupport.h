// POSTAL for iOS: app support (data folder, settings file, screen shape).
#ifndef IOS_SUPPORT_H
#define IOS_SUPPORT_H
#ifdef __cplusplus
extern "C" {
#endif

// The app's Documents folder (Files app: On My iPhone > POSTAL), ending in "/".
const char *IOSSupport_DocumentsDir(void);

// Makes Documents the working folder, copies the app's default POSTAL.INI
// there if the player has none, and checks for the game files. Returns 0 if
// the game can start; otherwise it has told the player what to copy, and the
// app should quit.
int IOSSupport_PrepareData(void);

// Screen shape for the game's widescreen mode: width / height in landscape.
double IOSSupport_ScreenAspect(void);
// The screen's height in landscape, in points.
double IOSSupport_ScreenHeightPoints(void);

#ifdef __cplusplus
}
#endif
#endif
