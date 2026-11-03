/* POSTAL for iOS: the camera icon's photos. See IOSPhotos.h. Built with ARC.
   Ported from Serious Sam's ios/IOSPhotos.m (itself the Jedi Knight
   port's iosPhotos): Photos add-only, Documents/Screenshots as the fallback,
   the flash, shutter and haptic, one message once the saving is done. */

#import <UIKit/UIKit.h>
#import <Photos/Photos.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "IOSPhotos.h"
#include "IOSTouch.h"

/* IOSTouch.m */
void IOSTouch_DrawInfo(CGContextRef c, CGFloat w, CGFloat h);
void IOSTouch_CameraFeedback(int bLight);

/* Images waiting to be encoded and saved, at most (each a screen's worth) */
#define IOSPHOTOS_MAX_PENDING 6

static dispatch_queue_t Queue(void)
{
  static dispatch_queue_t q = nil;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    q = dispatch_queue_create("postal.photos", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
  });
  return q;
}

static void FreeBytes(void *info, const void *data, size_t size)
{
  free((void *)data);
}

/* A CGImage over w x h RGBA bytes (top row first, opaque); it frees them */
static CGImageRef MakeImage(unsigned char *p, int w, int h)
{
  CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, p, (size_t)w * h * 4, FreeBytes);
  if (!prov) {
    free(p);
    return NULL;
  }
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs, kCGBitmapByteOrderDefault | kCGImageAlphaNoneSkipLast,
                                 prov, NULL, false, kCGRenderingIntentDefault);
  CGColorSpaceRelease(cs);
  CGDataProviderRelease(prov);
  return img;
}

static NSData *Png(CGImageRef img)
{
  NSMutableData *data = [NSMutableData data];
  CGImageDestinationRef dst = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, (__bridge CFStringRef)@"public.png", 1, NULL);
  if (!dst) return nil;
  CGImageDestinationAddImage(dst, img, NULL);
  const BOOL bOk = CGImageDestinationFinalize(dst);
  CFRelease(dst);
  return bOk ? data : nil;
}

/* "POSTAL 2026-11-03 14.05.09", then "-2", "-3"... within one second (main thread) */
static NSString *BaseName(NSDate *when)
{
  NSDateFormatter *f = [[NSDateFormatter alloc] init];
  f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
  f.dateFormat = @"yyyy-MM-dd HH.mm.ss";
  NSString *base = [NSString stringWithFormat:@"POSTAL %@", [f stringFromDate:when]];
  static NSString *lastBase = nil;
  static int sameSecond = 0;
  if (lastBase && [lastBase isEqualToString:base]) {
    sameSecond++;
  }
  else {
    lastBase = base;
    sameSecond = 1;
  }
  return sameSecond > 1 ? [NSString stringWithFormat:@"%@-%d", base, sameSecond] : base;
}

/* The message, once nothing more is being saved
   (main thread) */
static int _ctPending = 0;
static int _ctToPhotos = 0, _ctToFiles = 0, _ctFailed = 0, _bPhotosOff = 0;

static void Report(void)
{
  if (_ctPending > 0) return;
  char msg[160];
  int n = 0;
  msg[0] = 0;
  if (_ctToPhotos) n += snprintf(msg + n, sizeof(msg) - n, _ctToPhotos == 1 ? "1 photo saved" : "%d photos saved", _ctToPhotos);
  if (_ctToFiles && n < (int)sizeof(msg))
    n += snprintf(msg + n, sizeof(msg) - n, "%s%d saved to Files > POSTAL > Screenshots (%s)", n ? ", " : "", _ctToFiles,
                  _bPhotosOff ? "Photos access is off" : "Photos didn't take them");
  if (_ctFailed && n < (int)sizeof(msg)) snprintf(msg + n, sizeof(msg) - n, "%s%d couldn't be saved", n ? ", " : "", _ctFailed);
  _ctToPhotos = _ctToFiles = _ctFailed = _bPhotosOff = 0;
  if (msg[0]) IOSTouch_ShowMessage(msg, 3.0);
}

static void Finish(int toPhotos, int toFiles, int failed, int bPhotosOff)
{
  dispatch_async(dispatch_get_main_queue(), ^{
    _ctToPhotos += toPhotos;
    _ctToFiles += toFiles;
    _ctFailed += failed;
    if (bPhotosOff) _bPhotosOff = 1;
    if (_ctPending > 0) _ctPending--;
    Report();
  });
}

int IOSPhotos_Pending(void) { return _ctPending; }

/* Into Documents/Screenshots (the Files app) */
static int SaveToFiles(NSData *data, NSString *name)
{
  NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  if (!docs) return 0;
  NSString *dir = [docs stringByAppendingPathComponent:@"Screenshots"];
  [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  return [data writeToFile:[dir stringByAppendingPathComponent:name] atomically:YES] ? 1 : 0;
}

static void SaveData(NSData *data, NSString *name, NSDate *when)
{
  const PHAuthorizationStatus st = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly];
  if (st != PHAuthorizationStatusAuthorized && st != PHAuthorizationStatusLimited) {
    const int k = SaveToFiles(data, name);
    Finish(0, k, 1 - k, 1);
    return;
  }
  [[PHPhotoLibrary sharedPhotoLibrary] performChanges:^{
    PHAssetCreationRequest *req = [PHAssetCreationRequest creationRequestForAsset];
    PHAssetResourceCreationOptions *opt = [[PHAssetResourceCreationOptions alloc] init];
    opt.originalFilename = name;
    [req addResourceWithType:PHAssetResourceTypePhoto data:data options:opt];
    req.creationDate = when;
  } completionHandler:^(BOOL bOk, NSError *err) {
    if (bOk) {
      Finish(1, 0, 0, 0);
      return;
    }
    const int k = SaveToFiles(data, name);
    Finish(0, k, 1 - k, 0);
  }];
}

int IOSPhotos_Save(const unsigned char *pubRGBA, int w, int h, int iStride, const char *pszKind, int iFlags)
{
  if (!pubRGBA || w <= 0 || h <= 0) return 0;
  if (_ctPending >= IOSPHOTOS_MAX_PENDING) {
    IOSTouch_ShowMessage("Still saving the last photos", 2.0);
    return 0;
  }
  if (iStride <= 0) iStride = w * 4;
  unsigned char *p = (unsigned char *)malloc((size_t)w * h * 4);
  if (!p) {
    IOSTouch_ShowMessage("Not enough memory for a photo", 2.0);
    return 0;
  }
  /* top row first, opaque */
  for (int y = 0; y < h; y++) {
    const unsigned char *src = pubRGBA + (size_t)((iFlags & IOSPHOTOS_BOTTOM_UP) ? h - 1 - y : y) * iStride;
    memcpy(p + (size_t)y * w * 4, src, (size_t)w * 4);
  }
  @autoreleasepool {
    CGImageRef img = MakeImage(p, w, h); /* (frees p) */
    if (!img) return 0;
    if (iFlags & IOSPHOTOS_ADD_INFO) {
      /* the overlay's game info on top, as the player sees it (UIKit's views aren't in SDL's frame) */
      UIGraphicsImageRendererFormat *fmt = [UIGraphicsImageRendererFormat preferredFormat];
      fmt.scale = 1.0;
      fmt.opaque = YES;
      fmt.preferredRange = UIGraphicsImageRendererFormatRangeStandard;
      UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(w, h) format:fmt];
      UIImage *out = [r imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
        CGContextRef c = ctx.CGContext;
        [[UIImage imageWithCGImage:img] drawInRect:CGRectMake(0, 0, w, h)];
        IOSTouch_DrawInfo(c, w, h);
      }];
      CGImageRef withInfo = out.CGImage ? CGImageRetain(out.CGImage) : NULL;
      CGImageRelease(img);
      img = withInfo;
      if (!img) return 0;
    }
    /* the frame is taken: now the flash, the shutter and a tap */
    IOSTouch_CameraFeedback(0);
    NSDate *when = [NSDate date];
    NSString *name = [NSString stringWithFormat:@"%@ %s.png", BaseName(when), (pszKind && pszKind[0]) ? pszKind : "photo"];
    _ctPending++;
    dispatch_async(Queue(), ^{
      NSData *png = nil;
      @autoreleasepool {
        png = Png(img);
      }
      CGImageRelease(img);
      if (!png) {
        Finish(0, 0, 1, 0);
        return;
      }
      SaveData(png, name, when);
    });
  }
  return 1;
}

int IOSPhotos_AskAccess(void (*done)(void))
{
  if ([PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly] != PHAuthorizationStatusNotDetermined) return 0;
  [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly handler:^(PHAuthorizationStatus st) {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (done) done();
    });
  }];
  return 1;
}
