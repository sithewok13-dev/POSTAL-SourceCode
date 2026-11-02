// POSTAL for iOS: app support (data folder, settings file, screen shape).
#import <UIKit/UIKit.h>
#include <unistd.h>
#include "SDL.h"
#include "IOSSupport.h"

static char s_docs[1024];

const char *IOSSupport_DocumentsDir(void)
{
    if (!s_docs[0]) {
        NSString *d = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
        snprintf(s_docs, sizeof(s_docs), "%s/", d ? d.fileSystemRepresentation : ".");
    }
    return s_docs;
}

// Case-insensitive check, as the game's own file lookup is.
static BOOL hasFileNoCase(NSString *root, NSArray<NSString *> *parts)
{
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *cur = root;
    for (NSString *want in parts) {
        NSString *found = nil;
        for (NSString *name in [fm contentsOfDirectoryAtPath:cur error:nil]) {
            if ([name caseInsensitiveCompare:want] == NSOrderedSame) { found = name; break; }
        }
        if (!found) return NO;
        cur = [cur stringByAppendingPathComponent:found];
    }
    return YES;
}

int IOSSupport_PrepareData(void)
{
    @autoreleasepool {
        NSString *docs = [NSString stringWithUTF8String:IOSSupport_DocumentsDir()];
        [NSFileManager.defaultManager createDirectoryAtPath:docs withIntermediateDirectories:YES attributes:nil error:nil];
        chdir(docs.fileSystemRepresentation);

        // Leave a note in the folder so it shows up in the Files app at once.
        NSString *note = [docs stringByAppendingPathComponent:@"Put the res folder here.txt"];
        BOOL haveData = hasFileNoCase(docs, @[@"res", @"shell", @"shell.sak"]);
        if (!haveData && ![NSFileManager.defaultManager fileExistsAtPath:note]) {
            [@"Copy the res folder from your own POSTAL: Classic and Uncut (GOG) install into this folder.\n"
             writeToFile:note atomically:YES encoding:NSUTF8StringEncoding error:nil];
        }

        // The app's own settings, unless the player brought theirs.
        if (!hasFileNoCase(docs, @[@"POSTAL.INI"])) {
            NSString *def = [NSBundle.mainBundle pathForResource:@"POSTAL" ofType:@"INI"];
            if (def) [NSFileManager.defaultManager copyItemAtPath:def toPath:[docs stringByAppendingPathComponent:@"POSTAL.INI"] error:nil];
        }

        if (!haveData) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "POSTAL: game files not found",
                "Copy the res folder from your own POSTAL: Classic and Uncut "
                "(GOG) install into:\n\nFiles > On My iPhone > POSTAL\n\n"
                "so that POSTAL/res/shell/shell.sak exists, then open the app again.",
                NULL);
            return -1;
        }
        if (haveData && [NSFileManager.defaultManager fileExistsAtPath:note])
            [NSFileManager.defaultManager removeItemAtPath:note error:nil];
    }
    return 0;
}

double IOSSupport_ScreenAspect(void)
{
    CGSize s = UIScreen.mainScreen.bounds.size;
    double w = MAX(s.width, s.height), h = MIN(s.width, s.height);
    return h > 0 ? w / h : 4.0 / 3.0;
}
