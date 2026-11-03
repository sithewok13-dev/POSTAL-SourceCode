/* POSTAL for iOS: the touch overlay (UIKit, ARC). See IOSTouch.h.

   A thin view over SDL's: it hands UIKit's touches and CoreMotion's samples
   to the plain C core (IOSTouchCore.c, which the Linux harness tests) and
   draws what the core says. It never calls the game: IOSTouch_Update
   returns requests, IOSTouch_ReadInput plain input values.

   Ported from Serious Sam's ios/IOSTouch.m (the view, the wheel's drawing,
   the level list, tilt aiming's CoreMotion side, the settings). */

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreMotion/CoreMotion.h>
#import <AudioToolbox/AudioToolbox.h>
#include <os/lock.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"
#include "SDL_syswm.h"
#include "IOSTouch.h"
#include "IOSTouchCore.h"
#include "IOSPhotos.h"

/* Kept in the app's settings (the house keys; each app keeps its own) */
#define KEY_GYRO_MODE   @"iosTouchGyroMode"
#define KEY_GYRO_SENS   @"iosTouchGyroSens"
#define KEY_SHOW_FPS    @"iosTouchShowFps"
#define KEY_SHOW_CAMERA @"iosTouchShowCamera"
#define KEY_CAM_KINDS   @"iosTouchCameraKinds"
/* (the cheat icon is not kept: the cheat itself starts off each launch, and the icon with it) */

#define CAMERA_SOUND 1108 /* iOS's shutter */
#define BURST_SOUND  1104 /* a short tick, for a burst's later shots */

/* ---------------------------------------------------------------- tilt (CoreMotion side) */
static CMMotionManager *_pMotion = nil;
static NSOperationQueue *_pMotionQueue = nil;
static os_unfair_lock _gyroLock = OS_UNFAIR_LOCK_INIT;
static IOSTouchGyroFilter _gyroFilter; /* under _gyroLock */
static int _iGyroSide = 0;             /* under _gyroLock: which way round the screen is */
static int _bGyroRunning = 0;

static void GyroSetup(void)
{
  if (_pMotion) return;
  _pMotion = [[CMMotionManager alloc] init];
  _pMotionQueue = [[NSOperationQueue alloc] init];
  _pMotionQueue.maxConcurrentOperationCount = 1;
  _pMotionQueue.qualityOfService = NSQualityOfServiceUserInteractive;
  IOSTouchCore_Settings()->bGyroAvailable = _pMotion.deviceMotionAvailable ? 1 : 0;
}

static void GyroRun(int bRun)
{
  bRun = bRun != 0;
  if (bRun == _bGyroRunning) return;
  _bGyroRunning = bRun;
  if (!bRun) {
    [_pMotion stopDeviceMotionUpdates];
    return;
  }
  os_unfair_lock_lock(&_gyroLock);
  IOSTouchGyro_Reset(&_gyroFilter);
  os_unfair_lock_unlock(&_gyroLock);
  _pMotion.deviceMotionUpdateInterval = 1.0 / IOSTOUCH_GYRO_HZ;
  [_pMotion startDeviceMotionUpdatesUsingReferenceFrame:CMAttitudeReferenceFrameXArbitraryZVertical
                                                toQueue:_pMotionQueue
                                            withHandler:^(CMDeviceMotion *dm, NSError *err) {
    if (!dm || err) return;
    const CMRotationRate w = dm.rotationRate;
    const CMAcceleration g = dm.gravity;
    os_unfair_lock_lock(&_gyroLock);
    IOSTouchGyro_Sample(&_gyroFilter, _iGyroSide, w.x, w.y, w.z, g.x, g.y, g.z, dm.timestamp);
    os_unfair_lock_unlock(&_gyroLock);
  }];
}

/* +1 landscape left (the top of the phone on the right), -1 landscape right, 0 neither */
static int ScreenSide(UIView *v)
{
  UIWindowScene *scene = v.window.windowScene;
  if (scene) {
    const UIInterfaceOrientation o = scene.interfaceOrientation;
    return (o == UIInterfaceOrientationLandscapeLeft) ? 1 : ((o == UIInterfaceOrientationLandscapeRight) ? -1 : 0);
  }
  const SDL_DisplayOrientation o = SDL_GetDisplayOrientation(0);
  return (o == SDL_ORIENTATION_LANDSCAPE_FLIPPED) ? 1 : ((o == SDL_ORIENTATION_LANDSCAPE) ? -1 : 0);
}

/* ---------------------------------------------------------------- the wheel's icons */
/* As the game hands them over (IOSTouch_SetWeaponIcon), per slice, in the three tints */
static id _aIconImages[IOSTOUCH_WPN_MAX][3];
static unsigned int _uIconGen = 0;

static CGImageRef MakeImageRGBA(const unsigned char *pub, int w, int h)
{
  CFDataRef data = CFDataCreate(NULL, pub, (CFIndex)w * h * 4);
  CGDataProviderRef dp = CGDataProviderCreateWithCFData(data);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGImageRef im = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs, (CGBitmapInfo)kCGBitmapByteOrderDefault | (CGBitmapInfo)kCGImageAlphaLast,
                                dp, NULL, true, kCGRenderingIntentDefault);
  CGColorSpaceRelease(cs);
  CGDataProviderRelease(dp);
  CFRelease(data);
  return im;
}

/* ---------------------------------------------------------------- looks */
static UIColor *Grey(CGFloat w, CGFloat a) { return [UIColor colorWithWhite:w alpha:a]; }
static UIColor *Gold(CGFloat a) { return [UIColor colorWithRed:1.0 green:0.78 blue:0.25 alpha:a]; }
static UIColor *RingBlue(void) { return [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.95]; }

static UIFont *CondensedBold(CGFloat size)
{
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 160000
  if (@available(iOS 16.0, *)) return [UIFont systemFontOfSize:size weight:UIFontWeightBold width:UIFontWidthCondensed];
#endif
  return [UIFont boldSystemFontOfSize:size - 1];
}

static NSAttributedString *Symbol(NSString *name, CGFloat size, UIColor *colour)
{
  UIImageSymbolConfiguration *cfg = [UIImageSymbolConfiguration configurationWithPointSize:size weight:UIImageSymbolWeightSemibold];
  UIImage *img = [UIImage systemImageNamed:name withConfiguration:cfg];
  if (!img) return nil;
  NSTextAttachment *att = [[NSTextAttachment alloc] init];
  att.image = [img imageWithTintColor:colour renderingMode:UIImageRenderingModeAlwaysOriginal];
  return [NSAttributedString attributedStringWithAttachment:att];
}

/* The cheat icon: a hand grenade, POSTAL style -- an oval body with its
   grooves, the fuse head and lever on top, the pin's ring at the side.
   Drawn once per colour (gold: the cheat on; grey: off). */
static UIImage *GrenadeImage(CGFloat size, UIColor *body, UIColor *metal)
{
  UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(size, size)];
  return [r imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
    CGContextRef c = ctx.CGContext;
    const CGFloat s = size;
    /* the body */
    CGRect rb = CGRectMake(s * 0.24, s * 0.30, s * 0.46, s * 0.60);
    UIBezierPath *pb = [UIBezierPath bezierPathWithOvalInRect:rb];
    [body setFill];
    [pb fill];
    /* its grooves: two across, one down */
    CGContextSaveGState(c);
    [pb addClip];
    CGContextSetStrokeColorWithColor(c, [UIColor colorWithWhite:0.0 alpha:0.45].CGColor);
    CGContextSetLineWidth(c, fmax(1.0, s * 0.045));
    for (int i = 1; i <= 2; i++) {
      const CGFloat y = rb.origin.y + rb.size.height * i / 3.0;
      CGContextMoveToPoint(c, rb.origin.x, y);
      CGContextAddLineToPoint(c, CGRectGetMaxX(rb), y);
    }
    CGContextMoveToPoint(c, CGRectGetMidX(rb), rb.origin.y);
    CGContextAddLineToPoint(c, CGRectGetMidX(rb), CGRectGetMaxY(rb));
    CGContextStrokePath(c);
    CGContextRestoreGState(c);
    /* the fuse head */
    [metal setFill];
    [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(s * 0.36, s * 0.18, s * 0.22, s * 0.15) cornerRadius:s * 0.03] fill];
    /* the lever, down the right side */
    UIBezierPath *pl = [UIBezierPath bezierPath];
    [pl moveToPoint:CGPointMake(s * 0.56, s * 0.20)];
    [pl addLineToPoint:CGPointMake(s * 0.80, s * 0.26)];
    [pl addLineToPoint:CGPointMake(s * 0.74, s * 0.62)];
    pl.lineWidth = fmax(1.2, s * 0.07);
    pl.lineCapStyle = kCGLineCapRound;
    pl.lineJoinStyle = kCGLineJoinRound;
    [metal setStroke];
    [pl stroke];
    /* the pin's ring, at the top left */
    UIBezierPath *pr = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(s * 0.08, s * 0.08, s * 0.24, s * 0.24)];
    pr.lineWidth = fmax(1.0, s * 0.055);
    [pr stroke];
    UIBezierPath *pp = [UIBezierPath bezierPath];
    [pp moveToPoint:CGPointMake(s * 0.28, s * 0.24)];
    [pp addLineToPoint:CGPointMake(s * 0.38, s * 0.25)];
    pp.lineWidth = fmax(1.0, s * 0.05);
    [pp stroke];
  }];
}

/* A slice's colour: round the colour wheel, by slice */
static void SliceColour(int i, int n, CGFloat *pr, CGFloat *pg, CGFloat *pb)
{
  UIColor *c = [UIColor colorWithHue:(n > 0 ? (CGFloat)i / n : 0) saturation:0.55 brightness:0.85 alpha:1.0];
  CGFloat a;
  [c getRed:pr green:pg blue:pb alpha:&a];
}

static UIBezierPath *WedgePath(CGPoint c, CGFloat deg0, CGFloat deg1, CGFloat r0, CGFloat r1)
{
  /* UIKit's angles go clockwise (y down) */
  const CGFloat a0 = -deg1 * M_PI / 180.0, a1 = -deg0 * M_PI / 180.0;
  UIBezierPath *p = [UIBezierPath bezierPathWithArcCenter:c radius:r1 startAngle:a0 endAngle:a1 clockwise:YES];
  [p addArcWithCenter:c radius:r0 startAngle:a1 endAngle:a0 clockwise:NO];
  [p closePath];
  return p;
}

static CGPoint AtDeg(CGPoint c, double deg, double r)
{
  return CGPointMake(c.x + r * cos(deg * M_PI / 180.0), c.y - r * sin(deg * M_PI / 180.0));
}

/* "v0.1.57", or "v0.1.57 (57)" where the build number isn't already its end */
static NSString *VersionText(void)
{
  NSDictionary *info = [[NSBundle mainBundle] infoDictionary];
  id ver = [info objectForKey:@"CFBundleShortVersionString"];
  id build = [info objectForKey:@"CFBundleVersion"];
  if (![ver isKindOfClass:[NSString class]] || [ver length] == 0) ver = @"?";
  if ([build isKindOfClass:[NSString class]] && [build length] > 0 && ![build isEqualToString:ver]
      && ![ver hasSuffix:[@"." stringByAppendingString:build]]) {
    return [NSString stringWithFormat:@"v%@ (%@)", ver, build];
  }
  return [NSString stringWithFormat:@"v%@", ver];
}

/* The level list sits on this, which keeps the touches on it */
@interface IOSTouchPanel : UIView
@end
@implementation IOSTouchPanel
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {}
@end

/* What the open wheel was last drawn for */
typedef struct {
  int ct, hot, bCancel, bTapMode, iCurrent;
  int aiState[IOSTOUCH_WPN_MAX], aiCount[IOSTOUCH_WPN_MAX];
  unsigned int uIconGen;
  double cx, cy, r1;
} WheelKey;

@interface IOSTouchOverlay : UIView <UITableViewDataSource, UITableViewDelegate> {
  UILabel *aViews[BTN_COUNT];
  CAShapeLayer *aRings[BTN_COUNT];
  CALayer *cheatIcon;
  UIImage *cheatOn, *cheatOff;
  int bCheatIconOn;
  UILabel *burstBadge;
  UIView *stickBase, *stickKnob;
  UIView *aTrayBack[2], *photoBack;
  UILabel *versionLabel, *fpsLabel, *messageLabel;
  int iFpsLabel;
  double tMessageShown;
  UIView *cameraFlash;
  CAShapeLayer *autoRing; /* autoplay: the long press filling */
  /* the wheel */
  UIView *wheelView;
  CAShapeLayer *wheelDisc, *wheelNeedle;
  CAShapeLayer *aSlices[IOSTOUCH_WPN_MAX];
  CALayer *aIcons[IOSTOUCH_WPN_MAX];
  UILabel *aCounts[IOSTOUCH_WPN_MAX], *aNames[IOSTOUCH_WPN_MAX];
  UILabel *midTitle, *midCount, *midHint, *cancelLabel;
  int bWheelShown;
  WheelKey wheelKey;
  /* the level list */
  IOSTouchPanel *levelPanel;
  UILabel *levelTitle;
  UIButton *levelClose;
  UITableView *levelTable;
  /* haptics */
  UISelectionFeedbackGenerator *selTick;
  UIImpactFeedbackGenerator *lightTap;
  /* the settings as saved */
  int iGyroModeSaved, iGyroSensSaved, bFpsSaved, bCameraSaved, iKindsSaved;
  int iLooksKey;
}
- (void)refresh;
- (void)cameraFeedback:(int)bLight;
@end

@implementation IOSTouchOverlay

static CAShapeLayer *MakeRing(void)
{
  CAShapeLayer *ring = [CAShapeLayer layer];
  ring.fillColor = [UIColor clearColor].CGColor;
  ring.strokeColor = RingBlue().CGColor;
  ring.lineWidth = 3.0;
  ring.strokeEnd = 0.0;
  ring.hidden = YES;
  return ring;
}

static UIView *MakeBack(CGFloat corner)
{
  UIView *v = [[UIView alloc] initWithFrame:CGRectZero];
  v.backgroundColor = Grey(0.0, 0.55);
  v.layer.cornerRadius = corner;
  v.layer.borderWidth = 1.5;
  v.layer.borderColor = Grey(1.0, 0.4).CGColor;
  v.userInteractionEnabled = NO;
  v.hidden = YES;
  return v;
}

static UILabel *MakeLabel(CGFloat size, CGFloat alpha)
{
  UILabel *l = [[UILabel alloc] initWithFrame:CGRectZero];
  l.textAlignment = NSTextAlignmentCenter;
  l.textColor = Grey(1.0, alpha);
  l.font = [UIFont systemFontOfSize:size weight:UIFontWeightSemibold];
  l.userInteractionEnabled = NO;
  l.hidden = YES;
  return l;
}

- (instancetype)initWithFrame:(CGRect)frame
{
  self = [super initWithFrame:frame];
  if (!self) return nil;
  self.backgroundColor = [UIColor clearColor];
  self.opaque = NO;
  self.multipleTouchEnabled = YES;
  self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

  /* the settings: OFF, 1.5x, no FPS, the camera icon on, CLEAN unless set (the cheat icon starts hidden) */
  GyroSetup();
  IOSTouchCore *k = IOSTouchCore_Settings();
  NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
  if ([d objectForKey:KEY_GYRO_MODE]) {
    const NSInteger m = [d integerForKey:KEY_GYRO_MODE];
    k->iGyroMode = (m >= 0 && m < IOSTOUCH_GYRO_NUM_MODES) ? (int)m : IOSTOUCH_GYRO_OFF;
  }
  if ([d objectForKey:KEY_GYRO_SENS]) {
    const float f = [d floatForKey:KEY_GYRO_SENS];
    for (int i = 0; i < IOSTOUCH_GYRO_NUM_SENS; i++) {
      if (fabsf(g_afIOSTouchGyroSens[i] - f) < fabsf(g_afIOSTouchGyroSens[k->iGyroSens] - f)) k->iGyroSens = i;
    }
  }
  k->bShowFps = [d boolForKey:KEY_SHOW_FPS] ? 1 : 0;
  if ([d objectForKey:KEY_SHOW_CAMERA]) k->bShowCamera = [d boolForKey:KEY_SHOW_CAMERA] ? 1 : 0;
  if ([d objectForKey:KEY_CAM_KINDS]) {
    const int kinds = (int)[d integerForKey:KEY_CAM_KINDS] & (IOSTOUCH_PHOTO_CLEAN | IOSTOUCH_PHOTO_HUD | IOSTOUCH_PHOTO_HUDBTNS);
    k->iPhotoKinds = kinds ? kinds : IOSTOUCH_PHOTO_CLEAN;
  }
  iGyroModeSaved = k->iGyroMode;
  iGyroSensSaved = k->iGyroSens;
  bFpsSaved = k->bShowFps;
  bCameraSaved = k->bShowCamera;
  iKindsSaved = k->iPhotoKinds;

  for (int i = 0; i < 2; i++) {
    aTrayBack[i] = MakeBack(IOSTOUCH_TRAY_BACK);
    [self addSubview:aTrayBack[i]];
  }
  photoBack = MakeBack(IOSTOUCH_TRAY_BACK);
  [self addSubview:photoBack];

  for (int b = 0; b < BTN_COUNT; b++) {
    UILabel *l = [[UILabel alloc] initWithFrame:CGRectZero];
    l.textAlignment = NSTextAlignmentCenter;
    l.textColor = Grey(1.0, 0.92);
    l.numberOfLines = 2;
    l.adjustsFontSizeToFitWidth = YES;
    l.minimumScaleFactor = 0.6;
    l.layer.borderWidth = 1.5;
    l.layer.borderColor = Grey(1.0, 0.65).CGColor;
    l.clipsToBounds = NO;
    l.userInteractionEnabled = NO;
    l.hidden = YES;
    aViews[b] = l;
    aRings[b] = MakeRing();
    [l.layer addSublayer:aRings[b]];
    [self addSubview:l];
  }
  /* the tray over the rest */
  for (int b = BTN_TRAY_FIRST; b <= BTN_PHOTO_LAST; b++) [self bringSubviewToFront:aViews[b]];
  [self insertSubview:aTrayBack[0] belowSubview:aViews[BTN_TRAY_FIRST]];
  [self insertSubview:aTrayBack[1] belowSubview:aViews[BTN_TRAY_FIRST]];
  [self insertSubview:photoBack belowSubview:aViews[BTN_TRAY_FIRST]];

  UIColor *white = Grey(1.0, 0.92);
  NSAttributedString *s;
  if ((s = Symbol(@"keyboard", 14, white))) aViews[BTN_TRAY_KEYS].attributedText = s;
  else aViews[BTN_TRAY_KEYS].text = @"TYPE";
  if ((s = Symbol(@"camera", 13, white))) aViews[BTN_TRAY_CAMERA].attributedText = s;
  else aViews[BTN_TRAY_CAMERA].text = @"CAM";
  if ((s = Symbol(@"xmark", 15, white))) aViews[BTN_FREEZECLOSE].attributedText = s;
  else aViews[BTN_FREEZECLOSE].text = @"X";
  cheatIcon = [CALayer layer];
  cheatIcon.contentsGravity = kCAGravityResizeAspect;
  [aViews[BTN_CHEAT].layer addSublayer:cheatIcon];
  bCheatIconOn = -1;

  burstBadge = MakeLabel(9, 1.0);
  burstBadge.backgroundColor = [UIColor colorWithRed:0.85 green:0.15 blue:0.15 alpha:0.95];
  burstBadge.layer.cornerRadius = 7.0;
  burstBadge.clipsToBounds = YES;
  burstBadge.font = [UIFont monospacedDigitSystemFontOfSize:9 weight:UIFontWeightBold];
  [self addSubview:burstBadge];

  stickBase = [[UIView alloc] initWithFrame:CGRectMake(0, 0, IOSTOUCH_STICK_RADIUS * 2, IOSTOUCH_STICK_RADIUS * 2)];
  stickBase.backgroundColor = Grey(1.0, 0.08);
  stickBase.layer.cornerRadius = IOSTOUCH_STICK_RADIUS;
  stickBase.layer.borderWidth = 1.5;
  stickBase.layer.borderColor = Grey(1.0, 0.45).CGColor;
  stickBase.userInteractionEnabled = NO;
  stickBase.hidden = YES;
  stickKnob = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 48, 48)];
  stickKnob.backgroundColor = Grey(1.0, 0.30);
  stickKnob.layer.cornerRadius = 24;
  stickKnob.layer.borderWidth = 1.5;
  stickKnob.layer.borderColor = Grey(1.0, 0.45).CGColor;
  stickKnob.userInteractionEnabled = NO;
  stickKnob.hidden = YES;
  [self addSubview:stickBase];
  [self addSubview:stickKnob];

  versionLabel = MakeLabel(10, 0.55);
  versionLabel.font = [UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightMedium];
  versionLabel.shadowColor = Grey(0.0, 0.6);
  versionLabel.shadowOffset = CGSizeMake(0, 1);
  versionLabel.text = VersionText();
  [versionLabel sizeToFit];
  versionLabel.bounds = CGRectMake(0, 0, versionLabel.bounds.size.width + 8, IOSTOUCH_VERSION_H);
  [self addSubview:versionLabel];

  fpsLabel = MakeLabel(11, 0.85);
  fpsLabel.font = [UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightSemibold];
  fpsLabel.backgroundColor = Grey(0.0, 0.35);
  fpsLabel.layer.cornerRadius = 9.0;
  fpsLabel.clipsToBounds = YES;
  fpsLabel.text = @"-- FPS";
  iFpsLabel = -2;
  [self addSubview:fpsLabel];

  messageLabel = MakeLabel(12, 0.95);
  messageLabel.backgroundColor = Grey(0.0, 0.6);
  messageLabel.layer.cornerRadius = 11.0;
  messageLabel.clipsToBounds = YES;
  [self addSubview:messageLabel];

  cameraFlash = [[UIView alloc] initWithFrame:self.bounds];
  cameraFlash.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  cameraFlash.backgroundColor = [UIColor whiteColor];
  cameraFlash.alpha = 0.0;
  cameraFlash.userInteractionEnabled = NO;
  [self addSubview:cameraFlash];

  autoRing = MakeRing();
  autoRing.lineWidth = 4.0;
  autoRing.bounds = CGRectMake(0, 0, 72, 72);
  autoRing.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(36, 36) radius:34 startAngle:-M_PI_2 endAngle:3 * M_PI_2 clockwise:YES].CGPath;
  [self.layer addSublayer:autoRing];

  selTick = [[UISelectionFeedbackGenerator alloc] init];
  lightTap = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];

  [self makeWheel];
  [self makeLevelPanel];
  iLooksKey = -1;
  return self;
}

/* ---------------------------------------------------------------- layout */
- (void)placeButton:(int)b
{
  const IOSTouchButton *p = &IOSTouchCore_Get()->aButtons[b];
  UILabel *l = aViews[b];
  const CGRect r = CGRectMake(0, 0, p->hw * 2, p->r * 2);
  if (!CGRectEqualToRect(l.bounds, r)) {
    l.bounds = r;
    l.layer.cornerRadius = p->r;
    aRings[b].frame = r;
    aRings[b].path = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(r, 2.5, 2.5) cornerRadius:p->r - 2.5].CGPath;
    if (b == BTN_CHEAT) cheatIcon.frame = CGRectInset(r, 3, 3);
    [self styleButton:b];
  }
  const CGPoint c = CGPointMake(p->x, p->y);
  if (!CGPointEqualToPoint(l.center, c)) l.center = c;
}

/* Fonts and fills (sizes follow the layout: an iPad's pills are taller) */
- (void)styleButton:(int)b
{
  const IOSTouchCore *k = IOSTouchCore_Get();
  const IOSTouchButton *p = &k->aButtons[b];
  UILabel *l = aViews[b];
  const CGFloat ph = k->fPillH;
  if (b == BTN_FIRE) {
    l.font = [UIFont boldSystemFontOfSize:16];
    l.backgroundColor = [UIColor colorWithRed:0.59 green:0.08 blue:0.08 alpha:0.52];
    l.layer.borderColor = [UIColor colorWithRed:1.0 green:0.6 blue:0.5 alpha:0.75].CGColor;
  }
  else if (b == BTN_DUCK || b == BTN_EXECUTE) {
    l.font = [UIFont boldSystemFontOfSize:b == BTN_DUCK ? 11 : 10];
  }
  else if (b == BTN_NEXTWPN || b == BTN_PREVWPN) {
    l.font = [UIFont boldSystemFontOfSize:fmax(7.0, ph * 0.27)];
  }
  else if (b == BTN_SAVE || b == BTN_LOAD || b == BTN_MENU) {
    l.font = [UIFont boldSystemFontOfSize:fmax(8.0, ph * 0.33)];
  }
  else if (b == BTN_SLOWMO) {
    l.font = [UIFont boldSystemFontOfSize:12];
    l.textColor = Gold(1.0);
  }
  else if (b >= BTN_TRAY_FIRST) {
    l.font = (b == BTN_TRAY_SENS || b == BTN_TRAY_GYRO) ? CondensedBold(10) : [UIFont boldSystemFontOfSize:9];
  }
  if (b != BTN_FIRE) l.backgroundColor = Grey(0.12, 0.52);
  if (b == BTN_CAMERA) {
    NSAttributedString *s = Symbol(@"camera.fill", fmax(10.0, p->r * 0.95), Grey(1.0, 0.92));
    if (s) l.attributedText = s;
    else l.text = @"CAM";
  }
  if (b == BTN_FOLLOWNEXT) {
    NSAttributedString *s = Symbol(@"arrowtriangle.right.fill", fmax(9.0, p->r * 0.9), Grey(1.0, 0.92));
    if (s) l.attributedText = s;
    else l.text = @">";
  }
  if (b == BTN_CHEAT) {
    const CGFloat sz = fmax(16.0, p->r * 2 - 6);
    cheatOn = GrenadeImage(sz, [UIColor colorWithRed:0.95 green:0.72 blue:0.18 alpha:1.0], Grey(0.92, 1.0));
    cheatOff = GrenadeImage(sz, [UIColor colorWithRed:0.42 green:0.47 blue:0.30 alpha:1.0], Grey(0.75, 1.0));
    bCheatIconOn = -1;
  }
}

- (void)layoutSubviews
{
  [super layoutSubviews];
  const CGRect bnds = self.bounds;
  const UIEdgeInsets in = self.safeAreaInsets;
  IOSTouchCore_Layout(bnds.size.width, bnds.size.height, in.left, in.top, in.right, in.bottom);
  for (int b = 0; b < BTN_COUNT; b++) [self placeButton:b];
  const IOSTouchCore *k = IOSTouchCore_Get();
  fpsLabel.frame = CGRectMake(k->rFps.x0, k->rFps.y0, k->rFps.x1 - k->rFps.x0, k->rFps.y1 - k->rFps.y0);
  memset(&wheelKey, 0, sizeof(wheelKey)); /* (drawn afresh) */
  [self placeLevelPanel];
  iLooksKey = -1;
  [self refresh];
}

static void SetFrameOf(UIView *v, const IOSTouchRect *r)
{
  const CGRect f = CGRectMake(r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
  if (!CGRectEqualToRect(v.frame, f)) v.frame = f;
}

- (void)setRing:(int)b progress:(CGFloat)f
{
  [self setRingLayer:aRings[b] progress:f];
}

- (void)setRingLayer:(CAShapeLayer *)ring progress:(CGFloat)f
{
  const BOOL bHide = f <= 0.0;
  if (ring.hidden != bHide) ring.hidden = bHide;
  if (!bHide && ring.strokeEnd != f) ring.strokeEnd = f;
}

/* ---------------------------------------------------------------- refresh */
/* Everything as the core says, after every touch and frame */
- (void)refresh
{
  const IOSTouchCore *k = IOSTouchCore_Get();
  const double now = CACurrentMediaTime();
  [CATransaction begin];
  [CATransaction setDisableActions:YES];

  /* buttons: shown, lit (100%) or idle (65%), switches in gold */
  char buf[64];
  for (int b = 0; b < BTN_COUNT; b++) {
    UILabel *l = aViews[b];
    const int bShown = IOSTouchCore_IsShown(b);
    if (l.hidden == bShown) l.hidden = !bShown;
    if (!bShown) continue;
    [self placeButton:b]; /* (the tray changes with what it holds; the strip when FPS is switched) */
    const int bLit = IOSTouchCore_IsLit(b), bOn = IOSTouchCore_IsOn(b);
    CGFloat alpha = (bLit || bOn || b >= BTN_TRAY_FIRST) ? 1.0 : IOSTOUCH_IDLE_ALPHA;
    if (b == BTN_TRAY_GYRO && !k->bGyroAvailable) alpha *= 0.45;
    if (b == BTN_TRAY_SENS && (!k->bGyroAvailable || k->iGyroMode == IOSTOUCH_GYRO_OFF)) alpha *= 0.45;
    if (l.alpha != alpha) l.alpha = alpha;
    if (b != BTN_FIRE) {
      UIColor *bg = bLit ? Grey(1.0, 0.30) : Grey(0.12, 0.52);
      l.backgroundColor = bg;
      l.layer.borderColor = bOn ? Gold(0.95).CGColor : Grey(1.0, 0.65).CGColor;
    }
    const char *label = IOSTouchCore_Label(b, buf, sizeof(buf));
    if (label[0] && b != BTN_TRAY_KEYS && b != BTN_TRAY_CAMERA) {
      NSString *t = [NSString stringWithUTF8String:label];
      if (![l.text isEqualToString:t]) l.text = t;
    }
  }
  /* the cheat icon: gold while the cheat is on */
  if (bCheatIconOn != (k->hud.bCheatOn != 0)) {
    bCheatIconOn = k->hud.bCheatOn != 0;
    cheatIcon.contents = (__bridge id)(bCheatIconOn ? cheatOn : cheatOff).CGImage;
  }
  /* the burst's count on the camera */
  const int bBadge = k->bBursting && IOSTouchCore_IsShown(BTN_CAMERA);
  if (burstBadge.hidden == bBadge) burstBadge.hidden = !bBadge;
  if (bBadge) {
    burstBadge.text = [NSString stringWithFormat:@"%d", k->ctBurstShots];
    burstBadge.bounds = CGRectMake(0, 0, 16, 14);
    const IOSTouchButton *c = &k->aButtons[BTN_CAMERA];
    burstBadge.center = CGPointMake(c->x + c->r * 0.8, c->y + c->r * 0.8);
  }

  /* rings */
  [self setRing:BTN_SAVE progress:k->fSaveRing];
  [self setRing:BTN_LOAD progress:k->fLoadRing];
  [self setRing:BTN_MENU progress:k->fMenuRing];
  [self setRing:BTN_NEXTWPN progress:k->afWheelRing[0]];
  [self setRing:BTN_PREVWPN progress:k->afWheelRing[1]];
  [self setRing:BTN_CAMERA progress:k->fCameraRing];
  [self setRing:BTN_TRAY_CAMERA progress:k->fTrayCamRing];

  /* the stick, only under the thumb */
  if (k->bStickActive && k->iMode == IOSTOUCH_GAMEPLAY) {
    stickBase.center = CGPointMake(k->fStickOX, k->fStickOY);
    stickKnob.center = CGPointMake(k->fStickOX + k->fStickX * IOSTOUCH_STICK_RADIUS, k->fStickOY + k->fStickY * IOSTOUCH_STICK_RADIUS);
    stickBase.hidden = stickKnob.hidden = NO;
  }
  else {
    stickBase.hidden = stickKnob.hidden = YES;
  }

  /* the tray's backings and the version */
  const int bTray = k->bTrayOpen && k->iMode == IOSTOUCH_GAMEPLAY;
  for (int i = 0; i < 2; i++) {
    const int bShow = bTray && i < k->ctTrayRows && k->arTrayBack[i].bValid;
    if (bShow) SetFrameOf(aTrayBack[i], &k->arTrayBack[i]);
    if (aTrayBack[i].hidden == bShow) aTrayBack[i].hidden = !bShow;
  }
  const int bPhoto = bTray && k->bPhotoRowOpen;
  if (bPhoto) SetFrameOf(photoBack, &k->rPhotoBack);
  if (photoBack.hidden == bPhoto) photoBack.hidden = !bPhoto;
  if (bTray) versionLabel.center = CGPointMake(k->fVersionX, k->fVersionY);
  if (versionLabel.hidden == bTray) versionLabel.hidden = !bTray;

  /* the FPS readout (not under the open tray) */
  SetFrameOf(fpsLabel, &k->rFps);
  const int bFps = k->bShowFps && k->iMode == IOSTOUCH_GAMEPLAY && !k->bFreeze && !(bTray && !k->bFpsInStrip);
  if (fpsLabel.hidden == bFps) fpsLabel.hidden = !bFps;
  if (iFpsLabel != k->iFpsShown) {
    iFpsLabel = k->iFpsShown;
    fpsLabel.text = k->iFpsShown < 0 ? @"-- FPS" : [NSString stringWithFormat:@"%d FPS", k->iFpsShown];
  }

  /* the message */
  const int bMsg = IOSTouchCore_MessageShows(now) && (k->iMode == IOSTOUCH_GAMEPLAY || k->iMode == IOSTOUCH_AUTOPLAY);
  if (bMsg && tMessageShown != k->tMessage) {
    tMessageShown = k->tMessage;
    messageLabel.text = [NSString stringWithUTF8String:k->szMessage] ?: @"";
    const CGSize sz = [messageLabel sizeThatFits:CGSizeMake(self.bounds.size.width - 40, 22)];
    messageLabel.bounds = CGRectMake(0, 0, fmin(sz.width + 24, self.bounds.size.width - 24), 22);
    messageLabel.center = CGPointMake(k->fMessageX, k->fMessageY);
  }
  if (messageLabel.hidden == bMsg) messageLabel.hidden = !bMsg;

  /* autoplay: only the long press's ring, round the finger, once it is clearly a press (not a tap) */
  const int bAutoRing = k->iMode == IOSTOUCH_AUTOPLAY && k->fAutoRing > 0.2f;
  if (bAutoRing) {
    [self.layer addSublayer:autoRing]; /* (on top) */
    /* (above the finger, where it can be seen; below it near the top) */
    autoRing.position = CGPointMake(k->fAutoX, k->fAutoY >= 90.0 ? k->fAutoY - 50.0 : k->fAutoY + 50.0);
  }
  [self setRingLayer:autoRing progress:(bAutoRing ? k->fAutoRing : 0.0)];

  [self syncWheel];
  [self syncLevels];
  [CATransaction commit];

  /* haptics */
  IOSTouchCore *w = IOSTouchCore_Settings();
  if (w->ctWheelTicks > 0) {
    if (k->bWheelOpen) [selTick selectionChanged];
    w->ctWheelTicks = 0;
  }
  if (w->ctLevelTicks > 0) {
    [selTick selectionChanged];
    w->ctLevelTicks = 0;
  }
  if (w->ctCameraTaps > 0) {
    [lightTap impactOccurred];
    w->ctCameraTaps = 0;
  }
  [self saveSettings];
}

- (void)saveSettings
{
  const IOSTouchCore *k = IOSTouchCore_Get();
  NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
  if (iGyroModeSaved != k->iGyroMode) { iGyroModeSaved = k->iGyroMode; [d setInteger:k->iGyroMode forKey:KEY_GYRO_MODE]; }
  if (iGyroSensSaved != k->iGyroSens) {
    iGyroSensSaved = k->iGyroSens;
    [d setFloat:g_afIOSTouchGyroSens[k->iGyroSens] forKey:KEY_GYRO_SENS];
  }
  if (bFpsSaved != k->bShowFps) { bFpsSaved = k->bShowFps; [d setBool:(k->bShowFps ? YES : NO) forKey:KEY_SHOW_FPS]; }
  if (bCameraSaved != k->bShowCamera) { bCameraSaved = k->bShowCamera; [d setBool:(k->bShowCamera ? YES : NO) forKey:KEY_SHOW_CAMERA]; }
  if (iKindsSaved != k->iPhotoKinds) { iKindsSaved = k->iPhotoKinds; [d setInteger:k->iPhotoKinds forKey:KEY_CAM_KINDS]; }
}

/* Right after a photo's pixels were taken (IOSPhotos), so none of this is in it */
- (void)cameraFeedback:(int)bLight
{
  [cameraFlash.layer removeAllAnimations];
  [self bringSubviewToFront:cameraFlash];
  cameraFlash.alpha = bLight ? 0.35 : 0.85;
  [UIView animateWithDuration:(bLight ? 0.12 : 0.3) delay:0
                      options:UIViewAnimationOptionCurveEaseOut | UIViewAnimationOptionAllowUserInteraction
                   animations:^{ self->cameraFlash.alpha = 0.0; } completion:nil];
  AudioServicesPlaySystemSound(bLight ? BURST_SOUND : CAMERA_SOUND);
  [lightTap impactOccurred];
}

/* ---------------------------------------------------------------- the wheel */
- (void)makeWheel
{
  wheelView = [[UIView alloc] initWithFrame:self.bounds];
  wheelView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  wheelView.backgroundColor = Grey(0.0, 0.45);
  wheelView.userInteractionEnabled = NO;
  wheelView.hidden = YES;
  wheelDisc = [CAShapeLayer layer];
  wheelDisc.fillColor = Grey(0.0, 0.85).CGColor;
  [wheelView.layer addSublayer:wheelDisc];
  for (int i = 0; i < IOSTOUCH_WPN_MAX; i++) {
    aSlices[i] = [CAShapeLayer layer];
    aSlices[i].lineJoin = kCALineJoinRound;
    aSlices[i].hidden = YES;
    [wheelView.layer addSublayer:aSlices[i]];
  }
  for (int i = 0; i < IOSTOUCH_WPN_MAX; i++) {
    aIcons[i] = [CALayer layer];
    aIcons[i].contentsGravity = kCAGravityResizeAspect;
    aIcons[i].magnificationFilter = kCAFilterNearest; /* (POSTAL's icons are pixel art) */
    aIcons[i].zPosition = 2.0;
    aIcons[i].hidden = YES;
    [wheelView.layer addSublayer:aIcons[i]];
    aCounts[i] = MakeLabel(9, 0.9);
    aCounts[i].font = [UIFont monospacedDigitSystemFontOfSize:9 weight:UIFontWeightSemibold];
    aCounts[i].layer.zPosition = 2.0;
    [wheelView addSubview:aCounts[i]];
    aNames[i] = MakeLabel(9, 0.9);
    aNames[i].numberOfLines = 2;
    aNames[i].adjustsFontSizeToFitWidth = YES;
    aNames[i].minimumScaleFactor = 0.5;
    aNames[i].layer.zPosition = 2.0;
    [wheelView addSubview:aNames[i]];
  }
  wheelNeedle = [CAShapeLayer layer];
  wheelNeedle.lineWidth = 3.0;
  wheelNeedle.lineCap = kCALineCapRound;
  wheelNeedle.fillColor = [UIColor clearColor].CGColor;
  wheelNeedle.strokeColor = Grey(1.0, 0.8).CGColor;
  wheelNeedle.zPosition = 2.0;
  wheelNeedle.hidden = YES;
  [wheelView.layer addSublayer:wheelNeedle];
  midTitle = MakeLabel(15, 1.0);
  midTitle.font = [UIFont boldSystemFontOfSize:15];
  midTitle.adjustsFontSizeToFitWidth = YES;
  midTitle.minimumScaleFactor = 0.5;
  midCount = MakeLabel(13, 0.9);
  midCount.font = [UIFont monospacedDigitSystemFontOfSize:13 weight:UIFontWeightSemibold];
  midHint = MakeLabel(10, 0.8);
  midHint.adjustsFontSizeToFitWidth = YES;
  midHint.minimumScaleFactor = 0.6;
  cancelLabel = MakeLabel(9, 0.6);
  cancelLabel.text = @"cancel";
  for (UILabel *l in @[ midTitle, midCount, midHint, cancelLabel ]) {
    l.layer.zPosition = 2.0;
    [wheelView addSubview:l];
  }
  [self addSubview:wheelView];
}

- (void)syncWheel
{
  const IOSTouchCore *k = IOSTouchCore_Get();
  const int bOpen = k->bWheelOpen && k->iMode == IOSTOUCH_GAMEPLAY;
  if (bOpen && !bWheelShown) {
    bWheelShown = 1;
    memset(&wheelKey, 0, sizeof(wheelKey));
    [self bringSubviewToFront:wheelView];
    [self bringSubviewToFront:aViews[k->iWheelButton]]; /* (its button over the dim) */
    [selTick prepare];
    wheelView.hidden = NO;
  }
  else if (!bOpen && bWheelShown) {
    bWheelShown = 0;
    wheelView.hidden = YES;
  }
  if (bWheelShown) [self drawWheel];
}

- (void)drawWheel
{
  const IOSTouchCore *k = IOSTouchCore_Get();
  WheelKey key;
  memset(&key, 0, sizeof(key));
  key.ct = IOSTouchCore_WheelSlices();
  key.hot = IOSTouchCore_WheelPointed(&key.bCancel);
  key.bTapMode = k->bWheelTapMode;
  key.iCurrent = k->wpn.iCurrent;
  for (int i = 0; i < key.ct; i++) {
    key.aiState[i] = IOSTouchCore_WheelSliceState(i);
    key.aiCount[i] = IOSTouchCore_WheelSliceCount(i);
  }
  key.uIconGen = _uIconGen + 1;
  key.cx = k->fWheelCX;
  key.cy = k->fWheelCY;
  key.r1 = k->fWheelR1;
  const CGPoint c = CGPointMake(k->fWheelCX, k->fWheelCY);
  const double R0 = k->fWheelR0, R1 = k->fWheelR1;
  if (memcmp(&key, &wheelKey, sizeof(key)) != 0) {
    wheelKey = key;
    wheelView.frame = self.bounds;
    wheelDisc.path = [UIBezierPath bezierPathWithArcCenter:c radius:R1 + 14.0 startAngle:0 endAngle:2 * M_PI clockwise:YES].CGPath;
    const double width = IOSTouchCore_WheelSliceWidth();
    for (int i = 0; i < IOSTOUCH_WPN_MAX; i++) {
      const int bOn = i < key.ct;
      aSlices[i].hidden = !bOn;
      aIcons[i].hidden = YES;
      aCounts[i].hidden = aNames[i].hidden = YES;
      if (!bOn) continue;
      const int st = key.aiState[i], bHot = key.hot == i;
      const double deg = IOSTouchCore_WheelSliceDeg(i), half = width * 0.5 - 1.0;
      const double r1 = R1 + (bHot ? IOSTOUCH_WHEEL_POP : 0.0);
      CGFloat r, g, b;
      SliceColour(i, key.ct, &r, &g, &b);
      CGFloat fill = st == WSTATE_READY ? (bHot ? 0.85 : 0.50) : (st == WSTATE_NOAMMO ? 0.22 : 0.10);
      UIColor *cFill = st == WSTATE_READY ? [UIColor colorWithRed:r * fill green:g * fill blue:b * fill alpha:0.95]
                                          : Grey(fill, 0.9);
      aSlices[i].path = WedgePath(c, deg - half, deg + half, R0, r1).CGPath;
      aSlices[i].fillColor = cFill.CGColor;
      aSlices[i].strokeColor = (i == key.iCurrent) ? Grey(1.0, 0.95).CGColor
                               : (st == WSTATE_READY ? [UIColor colorWithRed:r green:g blue:b alpha:bHot ? 1.0 : 0.6].CGColor : Grey(0.4, 0.5).CGColor);
      aSlices[i].lineWidth = (i == key.iCurrent) ? 2.5 : 1.0;
      aSlices[i].zPosition = bHot ? 1.0 : 0.0;
      /* the icon (or the name) and the count under it, upright in the slice */
      const double rm = (R0 + r1) * 0.5 + (bHot ? 2.0 : 0.0);
      const double arc = rm * width * M_PI / 180.0;
      const double sz = fmin(fmin(34.0, arc * 0.78), (r1 - R0) * 0.55);
      const CGPoint m = AtDeg(c, deg, rm);
      const int cnt = key.aiCount[i];
      const CGFloat iy = m.y - (cnt >= 0 ? 6.0 : 0.0);
      id image = _aIconImages[i][st];
      if (image) {
        aIcons[i].frame = CGRectMake(m.x - sz * 0.5, iy - sz * 0.5, sz, sz);
        aIcons[i].contents = image;
        aIcons[i].hidden = NO;
      }
      else {
        /* no icon from the game (the Deathwad has none): its name */
        const char *name = IOSTouchCore_WheelSliceName(i);
        NSString *ns = [NSString stringWithUTF8String:(name[0] ? name : "?")];
        if (!ns) ns = [NSString stringWithCString:name encoding:NSWindowsCP1252StringEncoding];
        aNames[i].text = [(ns ?: @"?") stringByReplacingOccurrencesOfString:@" " withString:@"\n"];
        aNames[i].textColor = Grey(st == WSTATE_READY ? 1.0 : 0.6, st == WSTATE_NOTFOUND ? 0.45 : 0.95);
        aNames[i].bounds = CGRectMake(0, 0, fmax(sz, arc * 0.9), sz);
        aNames[i].center = CGPointMake(m.x, iy);
        aNames[i].hidden = NO;
      }
      if (cnt >= 0) {
        aCounts[i].text = [NSString stringWithFormat:@"%d", cnt];
        aCounts[i].textColor = Grey(st == WSTATE_READY ? 1.0 : 0.6, 0.95);
        aCounts[i].bounds = CGRectMake(0, 0, 30, 12);
        aCounts[i].center = CGPointMake(m.x, iy + sz * 0.5 + 6.0);
        aCounts[i].hidden = NO;
      }
    }
    /* the middle */
    const char *title, *hint;
    int count;
    IOSTouchCore_WheelMiddle(&title, &count, &hint);
    midTitle.text = [NSString stringWithUTF8String:title] ?: @"";
    midTitle.bounds = CGRectMake(0, 0, 2 * R0 - 16, 20);
    midTitle.center = CGPointMake(c.x, c.y - 14);
    midTitle.hidden = title[0] == 0;
    midCount.text = count >= 0 ? [NSString stringWithFormat:@"%d", count] : @"";
    midCount.bounds = CGRectMake(0, 0, 2 * R0 - 16, 16);
    midCount.center = CGPointMake(c.x, c.y + 3);
    midCount.hidden = count < 0;
    midHint.text = [NSString stringWithUTF8String:hint];
    midHint.bounds = CGRectMake(0, 0, 2 * R0 - 12, 14);
    midHint.center = CGPointMake(c.x, c.y + 19);
    midHint.hidden = NO;
    cancelLabel.bounds = CGRectMake(0, 0, 60, 12);
    cancelLabel.center = CGPointMake(c.x, c.y + (R0 + R1) * 0.5);
    cancelLabel.textColor = key.hot == IOSTOUCH_WHEEL_CANCEL ? Grey(1.0, 0.95) : Grey(0.6, 1.0);
    cancelLabel.hidden = NO;
  }
  double deg;
  if (IOSTouchCore_WheelNeedle(&deg)) {
    UIBezierPath *p = [UIBezierPath bezierPath];
    [p moveToPoint:AtDeg(c, deg, R0 * 0.55)];
    [p addLineToPoint:AtDeg(c, deg, R0 - 4.0)];
    wheelNeedle.path = p.CGPath;
    wheelNeedle.hidden = NO;
  }
  else {
    wheelNeedle.hidden = YES;
  }
}

/* ---------------------------------------------------------------- the level list */
- (void)makeLevelPanel
{
  levelPanel = [[IOSTouchPanel alloc] initWithFrame:CGRectZero];
  levelPanel.backgroundColor = Grey(0.0, 0.82);
  levelPanel.layer.cornerRadius = 18.0;
  levelPanel.layer.borderWidth = 1.5;
  levelPanel.layer.borderColor = Gold(0.8).CGColor;
  levelPanel.clipsToBounds = YES;
  levelPanel.hidden = YES;
  levelTitle = [[UILabel alloc] initWithFrame:CGRectZero];
  levelTitle.font = CondensedBold(17);
  levelTitle.textColor = Gold(1.0);
  [levelPanel addSubview:levelTitle];
  levelClose = [UIButton buttonWithType:UIButtonTypeSystem];
  UIImage *x = [UIImage systemImageNamed:@"xmark"
                       withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightBold]];
  if (x) [levelClose setImage:x forState:UIControlStateNormal];
  else [levelClose setTitle:@"X" forState:UIControlStateNormal];
  levelClose.tintColor = Grey(1.0, 0.85);
  [levelClose addTarget:self action:@selector(closeLevelsTapped) forControlEvents:UIControlEventTouchUpInside];
  [levelPanel addSubview:levelClose];
  levelTable = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
  levelTable.backgroundColor = [UIColor clearColor];
  levelTable.separatorColor = Grey(1.0, 0.12);
  levelTable.indicatorStyle = UIScrollViewIndicatorStyleWhite;
  levelTable.dataSource = self;
  levelTable.delegate = self;
  [levelPanel addSubview:levelTable];
  [self addSubview:levelPanel];
}

- (void)placeLevelPanel
{
  const CGRect b = self.bounds;
  const UIEdgeInsets in = self.safeAreaInsets;
  const CGFloat w = fmin(440.0, b.size.width - in.left - in.right - 40.0);
  const CGFloat h = fmax(120.0, b.size.height - fmax(in.top, 8.0) - fmax(in.bottom, 8.0) - 16.0);
  const CGFloat left = in.left + (b.size.width - in.left - in.right - w) * 0.5;
  levelPanel.frame = CGRectMake(left, fmax(in.top, 8.0) + 8.0, w, h);
  levelTitle.frame = CGRectMake(18, 10, w - 70, 26);
  levelClose.frame = CGRectMake(w - 46, 4, 42, 38);
  levelTable.frame = CGRectMake(0, 46, w, h - 46);
}

- (void)syncLevels
{
  const IOSTouchCore *k = IOSTouchCore_Get();
  const int bOpen = k->bLevelListOpen && k->iMode == IOSTOUCH_GAMEPLAY;
  if (bOpen && levelPanel.hidden) {
    levelTitle.text = [NSString stringWithFormat:@"LEVEL  (%d)", k->ctLevels];
    [self placeLevelPanel];
    [levelTable reloadData];
    [levelTable setContentOffset:CGPointZero animated:NO];
    levelPanel.hidden = NO;
    [self bringSubviewToFront:levelPanel];
    [selTick prepare];
  }
  else if (!bOpen && !levelPanel.hidden) {
    levelPanel.hidden = YES;
  }
}

- (void)closeLevelsTapped
{
  IOSTouchCore_CloseLevelList();
  [self refresh];
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section
{
  return IOSTouchCore_Get()->ctLevels;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
  UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"level"];
  if (!cell) {
    cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"level"];
    cell.backgroundColor = [UIColor clearColor];
    UIView *sel = [[UIView alloc] init];
    sel.backgroundColor = Gold(0.3);
    cell.selectedBackgroundView = sel;
  }
  const int row = (int)indexPath.row;
  UIFont *font = CondensedBold(16);
  NSMutableAttributedString *text = [[NSMutableAttributedString alloc]
    initWithString:[NSString stringWithFormat:@"%d.  ", row + 1]
        attributes:@{ NSFontAttributeName : font, NSForegroundColorAttributeName : Gold(1.0) }];
  const char *name = IOSTouchCore_LevelName(row);
  NSString *n = [NSString stringWithUTF8String:name];
  if (!n) n = [NSString stringWithCString:name encoding:NSWindowsCP1252StringEncoding]; /* (the game's own 8-bit letters) */
  [text appendAttributedString:[[NSAttributedString alloc] initWithString:(n ?: @"")
                                                               attributes:@{ NSFontAttributeName : font,
                                                                             NSForegroundColorAttributeName : Grey(1.0, 0.92) }]];
  cell.textLabel.attributedText = text;
  return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
  [tableView deselectRowAtIndexPath:indexPath animated:NO];
  IOSTouchCore_PickLevel((int)indexPath.row);
  [self refresh];
}

/* ---------------------------------------------------------------- touches */
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  for (UITouch *t in touches) {
    const CGPoint p = [t locationInView:self];
    IOSTouchCore_TouchBegan((__bridge const void *)t, p.x, p.y, t.timestamp);
  }
  [self refresh];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  for (UITouch *t in touches) {
    /* the places UIKit merged into this move, so a slide off SAVE and back still counts */
    for (UITouch *c in [event coalescedTouchesForTouch:t]) {
      const CGPoint q = [c locationInView:self];
      IOSTouchCore_TouchMoved((__bridge const void *)t, q.x, q.y, c.timestamp);
    }
    const CGPoint p = [t locationInView:self];
    IOSTouchCore_TouchMoved((__bridge const void *)t, p.x, p.y, t.timestamp);
  }
  [self refresh];
}

- (void)endTouches:(NSSet<UITouch *> *)touches cancelled:(int)bCancelled
{
  for (UITouch *t in touches) {
    const CGPoint p = [t locationInView:self];
    IOSTouchCore_TouchEnded((__bridge const void *)t, p.x, p.y, t.timestamp, bCancelled);
  }
  [self refresh];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self endTouches:touches cancelled:0]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event { [self endTouches:touches cancelled:1]; }

@end

/* ---------------------------------------------------------------- C API */
static IOSTouchOverlay *_pOverlay = nil;

UIView *IOSTouch_OverlayView(void) { return _pOverlay; }

void IOSTouch_CameraFeedback(int bLight)
{
  [_pOverlay cameraFeedback:bLight];
}

static UIView *HostView(SDL_Window *pWindow)
{
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!pWindow || !SDL_GetWindowWMInfo(pWindow, &info) || info.subsystem != SDL_SYSWM_UIKIT) return nil;
  UIWindow *w = info.info.uikit.window;
  if (!w) return nil;
  return w.rootViewController.view ? w.rootViewController.view : w;
}

static void SetMode(int iMode)
{
  IOSTouchCore_SetMode(iMode, CACurrentMediaTime());
  if (_pOverlay) {
    /* hidden: SDL gets the touches (as mouse clicks); autoplay: shown, but draws nothing */
    _pOverlay.hidden = (iMode == IOSTOUCH_HIDDEN);
    [_pOverlay refresh];
  }
}

static unsigned int UpdateOverlay(void *pSDLWindow, int iMode, const IOSTouchHud *pHud, unsigned int ulFramesDrawn)
{
  UIView *host = HostView((SDL_Window *)pSDLWindow);
  const int bRelayout = IOSTouchCore_SetHud(pHud);
  if (!_pOverlay) {
    if (iMode == IOSTOUCH_HIDDEN || !host) {
      SetMode(IOSTOUCH_HIDDEN);
      return IOSTouchCore_Tick(CACurrentMediaTime(), ulFramesDrawn);
    }
    _pOverlay = [[IOSTouchOverlay alloc] initWithFrame:host.bounds];
    _pOverlay.hidden = YES;
    [host addSubview:_pOverlay];
    [_pOverlay setNeedsLayout];
    [_pOverlay layoutIfNeeded];
  }
  else if (host && _pOverlay.superview != host) {
    /* SDL's window was made again: move across */
    [_pOverlay removeFromSuperview];
    _pOverlay.frame = host.bounds;
    [host addSubview:_pOverlay];
    IOSTouchCore_SetMode(IOSTOUCH_HIDDEN, CACurrentMediaTime()); /* (touches on the old one never end) */
  }
  /* on top of anything SDL adds later (its text field for the keyboard) */
  if (_pOverlay.superview && _pOverlay.superview.subviews.lastObject != _pOverlay) {
    [_pOverlay.superview bringSubviewToFront:_pOverlay];
  }
  if (iMode != IOSTouchCore_Get()->iMode || _pOverlay.hidden != (iMode == IOSTOUCH_HIDDEN)) SetMode(iMode);
  if (bRelayout) {
    [_pOverlay setNeedsLayout];
    [_pOverlay layoutIfNeeded];
  }
  IOSPhotos_SetBatch(IOSTouchCore_Get()->bBursting);
  /* Photos access asked once the camera icon shows in play (if iOS never asked) */
  static int bPhotosAsked = 0;
  if (!bPhotosAsked && iMode == IOSTOUCH_GAMEPLAY && IOSTouchCore_Get()->bShowCamera) {
    bPhotosAsked = 1;
    IOSPhotos_AskAccess(NULL);
  }

  /* tilt, after the frame's touches came in */
  const int bActive = [UIApplication sharedApplication].applicationState == UIApplicationStateActive;
  const int side = ScreenSide(_pOverlay);
  GyroRun(bActive && side != 0 && !_pOverlay.hidden && IOSTouchCore_GyroWanted());
  os_unfair_lock_lock(&_gyroLock);
  _iGyroSide = side;
  const double yaw = _gyroFilter.fYaw;
  _gyroFilter.fYaw = 0.0;
  os_unfair_lock_unlock(&_gyroLock);
  IOSTouchCore_GyroFrame(_bGyroRunning ? yaw : 0.0, _bGyroRunning ? side : 0, CACurrentMediaTime());

  const unsigned int ul = IOSTouchCore_Tick(CACurrentMediaTime(), ulFramesDrawn);
  if (!_pOverlay.hidden) [_pOverlay refresh];
  return ul;
}

/* These run in the game's own loop, where no autorelease pool is drained */
unsigned int IOSTouch_Update(void *pSDLWindow, int iMode, const IOSTouchHud *pHud, unsigned int ulFramesDrawn)
{
  unsigned int ul;
  @autoreleasepool {
    ul = UpdateOverlay(pSDLWindow, iMode, pHud, ulFramesDrawn);
  }
  return ul;
}

void IOSTouch_ReadInput(IOSTouchInput *pInput) { IOSTouchCore_ReadInput(pInput); }

void IOSTouch_SetWeapons(const IOSTouchWeapons *pWeapons) { IOSTouchCore_SetWeapons(pWeapons); }

void IOSTouch_SetWeaponIcon(int iSlice, const unsigned char *pubRGBA, int iWidth, int iHeight)
{
  if (iSlice < 0 || iSlice >= IOSTOUCH_WPN_MAX) return;
  @autoreleasepool {
    for (int st = 0; st < 3; st++) _aIconImages[iSlice][st] = nil;
    _uIconGen++;
    if (!pubRGBA || iWidth <= 0 || iHeight <= 0 || iWidth > 256 || iHeight > 256) return; /* (its name shows instead) */
    const int n = iWidth * iHeight;
    unsigned char *tmp = (unsigned char *)malloc((size_t)n * 4);
    if (!tmp) return;
    for (int st = WSTATE_READY; st <= WSTATE_NOTFOUND; st++) {
      IOSTouchCore_TintIcon(pubRGBA, n, st, tmp);
      _aIconImages[iSlice][st] = CFBridgingRelease(MakeImageRGBA(tmp, iWidth, iHeight));
    }
    free(tmp);
  }
}

int IOSTouch_HoldsGame(void) { return IOSTouchCore_HoldsGame(); }

void IOSTouch_ShowMessage(const char *pszText, double fSeconds)
{
  IOSTouchCore_ShowMessage(pszText, fSeconds, CACurrentMediaTime());
}

void IOSTouch_SetLevels(const char *const *ppszNames, int ctLevels) { IOSTouchCore_SetLevels(ppszNames, ctLevels); }

int IOSTouch_TakeLevelPick(void) { return IOSTouchCore_TakeLevelPick(); }

int IOSTouch_PhotoKinds(void) { return IOSTouchCore_PhotoKinds(); }
