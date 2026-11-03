/* POSTAL for iOS: the camera icon's photos, saved to Photos (add-only
   access), or to the app's Files folder (Documents/Screenshots) where that
   access is off or saving fails. Ported from Serious Sam's ios/IOSPhotos.m.

   Main thread (the game's). The game grabs the frame; this copies the
   pixels at once (the flash, shutter and haptic follow, so none of them is
   in the photo), then encodes and saves off the main thread, in order.
   Names: "POSTAL yyyy-MM-dd HH.mm.ss <kind>.png" ("-2", "-3" for a second
   one in the same second). One message on the overlay once nothing is left
   to save: "N photos saved" (or where they went instead). */
#ifndef IOS_PHOTOS_H
#define IOS_PHOTOS_H
#ifdef __cplusplus
extern "C" {
#endif

#define IOSPHOTOS_BOTTOM_UP    1 /* the rows come bottom first (as glReadPixels gives them) */
#define IOSPHOTOS_ADD_OVERLAY  2 /* draw the touch buttons on top (the "hud+buttons" kind) */
#define IOSPHOTOS_LIGHT        4 /* a burst's shot after its first: a lighter flash and a tick */

/* Saves one iWidth x iHeight RGBA image (iStride bytes a row; 0: iWidth * 4;
   alpha is ignored). pszKind ends the name: "clean", "hud", "hud+buttons",
   "zoom-out", "burst 03"... Returns 0 if it can't (no memory, or too many
   photos still waiting to be saved: then it says so). */
int IOSPhotos_Save(const unsigned char *pubRGBA, int iWidth, int iHeight, int iStride, const char *pszKind, int iFlags);

/* Photos still being encoded or saved */
int IOSPhotos_Pending(void);

/* While a burst is going (1): the message waits for its end, so it comes once
   for the whole of it. (The overlay sets this from the burst's state.) */
void IOSPhotos_SetBatch(int bOn);

/* Asks for add-only Photos access if it was never asked: 1 if iOS's question
   is now up (done is called on the main thread once answered), 0 if it was
   answered before (done isn't called). Without it, photos go to Files. */
int IOSPhotos_AskAccess(void (*done)(void));

#ifdef __cplusplus
}
#endif
#endif
