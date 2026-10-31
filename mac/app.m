/* The macOS application (M01): AppKit over the session (session.h).
 *
 * The game folder (M02): TFX_ORIGINAL_DIR, else the folder chosen last,
 * else an open panel; each must pass the content profile (profile.h), and
 * a failure says precisely what is wrong and offers another choice. The
 * user's data lives in ~/Library/Application Support/TFX: the game's own
 * writes in Game (the store), the log in Logs. One window shows the 320 x 200 frame
 * at the original's 4:3 (each pixel 1.2 times as tall as wide), scaled
 * without smoothing and letterboxed; the keyboard reaches the game as the
 * PC's scan codes (keymap.h), the mouse as its 640 x 200 position and
 * buttons, the first game controller as the game port's joystick
 * (gamepad.h; centred, its buttons up, while TFX is not the active
 * application). The sound (M05): the samples, the AdLib's OPL2 and the
 * MPU-401's MIDI (on Apple's DLS synthesizer) through the mixer, and the
 * CD's audio from the user's own image of TFX's CD (TFX_CD_IMAGE, else the
 * one chosen last with File > Choose CD Image…; checked against the
 * release's table of contents; without one the machine has no CD-ROM
 * drive). The settings (M06, TFX > Settings…: settings_ui.h) pause the
 * game while open and restart it when saved; so do the saved data's
 * import and restore (M07, File menu: userdata.h). A timer on the main run loop (common modes: it keeps running
 * while a menu is open or the window is resized) steps the game; the
 * samples go to the default output device. Quitting runs the game's exit
 * path (its open files written) before the process ends. Control-Shift-G
 * (or View > Enhanced 3D; --enhanced starts in it) toggles the enhanced 3D
 * mode, not the original's: the world drawn at the frame rectangle's own
 * size in backing pixels (render/hires.h, as the Windows host), named for
 * 2 s by a note at the picture's top right. */
#import <AppKit/AppKit.h>
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>

#include <signal.h>
#include <sys/sysctl.h>
#include <time.h>

#include "gamepad.h"
#include "keymap.h"
#include "output.h"
#include "present.h"
#include "profile.h"
#include "session.h"
#include "settings_ui.h"
#include "userdata.h"

static uint64_t host_now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000; }

/* the game's clock, the output's too (the mixer maps one onto the other):
 * the host's less the time paused (the settings open), held while paused,
 * so the game sees no time pass; read on the audio thread */
static _Atomic uint64_t clock_held, clock_paused;
static uint64_t game_now(void) {
    uint64_t held = clock_held;
    return held ? held : host_now() - clock_paused;
}
static void clock_pause(BOOL on) {
    if (on && !clock_held)
        clock_held = host_now() - clock_paused;
    else if (!on && clock_held) {
        clock_paused = host_now() - clock_held; /* (before the release: a reader past it sees the new offset) */
        clock_held = 0;
    }
}
static uint64_t session_now(void *ctx) { return (void)ctx, game_now(); }

/* ~/Library/Application Support/TFX/<name>, created */
static NSString *support_folder(NSString *name) {
    NSURL *u = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                    inDomains:NSUserDomainMask]
                   .firstObject;
    NSString *d = [[u.path stringByAppendingPathComponent:@"TFX"] stringByAppendingPathComponent:name];
    [NSFileManager.defaultManager createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    return d;
}

/* the log: Logs/TFX.log, the previous run's kept as TFX.previous.log */
static FILE *log_file;
static void log_open(void) {
    NSString *d = support_folder(@"Logs");
    NSString *now = [d stringByAppendingPathComponent:@"TFX.log"];
    rename(now.fileSystemRepresentation, [d stringByAppendingPathComponent:@"TFX.previous.log"].fileSystemRepresentation);
    log_file = fopen(now.fileSystemRepresentation, "w");
}
static void log_line(NSString *line) {
    NSString *stamped = [NSString stringWithFormat:@"%@ %@", [NSDate date], line];
    if (log_file) {
        fprintf(log_file, "%s\n", stamped.UTF8String);
        fflush(log_file);
    }
}
static void session_log(void *ctx, const char *m) { (void)ctx, log_line([NSString stringWithUTF8String:m]); }

static NSString *architecture(void) {
#if defined(__aarch64__)
    return @"arm64";
#else
    int translated = 0;
    size_t n = sizeof translated;
    sysctlbyname("sysctl.proc_translated", &translated, &n, NULL, 0);
    return translated ? @"x86_64 (Rosetta)" : @"x86_64";
#endif
}

@interface TFXView : NSView
@property(nonatomic) tfx_mac_session *session;
@property(nonatomic, strong) CALayer *image;
- (void)present;
- (void)releaseKeys;
- (void)layoutImage;
@property(nonatomic) BOOL captured; /* the mouse held by the window (M09) */
@property(nonatomic) BOOL enhanced; /* the enhanced 3D mode (Control-Shift-G) */
@end

static void release_pixels(void *info, const void *data, size_t size) { (void)info, (void)size, free((void *)data); }

@implementation TFXView {
    uint8_t _pressed[128];
    uint16_t _buttons;
    uint8_t _rgba[64000 * 4];
    double _x, _y;           /* the pointer (0 .. 639, 0 .. 199) */
    double _carry_x, _carry_y; /* the motion's fractions not yet sent */
    CALayer *_note;            /* the enhanced mode's note: a dark box ... */
    CATextLayer *_noteText;    /* ... with the mode's name */
}

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layer.backgroundColor = NSColor.blackColor.CGColor;
        _image = [CALayer layer];
        _image.magnificationFilter = kCAFilterNearest;
        _image.minificationFilter = kCAFilterNearest;
        _image.contentsGravity = kCAGravityResize;
        [self.layer addSublayer:_image];
        _note = [CALayer layer];
        _note.backgroundColor = [NSColor colorWithWhite:0 alpha:160.0 / 255].CGColor;
        _note.opacity = 0;
        _noteText = [CATextLayer layer];
        [_note addSublayer:_noteText];
        [_image addSublayer:_note];
        [self layoutImage];
    }
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }

/* the frame's rectangle (present.h), in the backing store's pixels so
 * integer scaling is whole device pixels */
- (NSRect)imageRect {
    NSRect b = [self convertRectToBacking:self.bounds];
    BOOL integer = [NSUserDefaults.standardUserDefaults boolForKey:@"IntegerScaling"];
    tfx_mac_rect r = tfx_mac_fit(b.size.width, b.size.height, integer);
    return [self convertRectFromBacking:NSMakeRect(round(r.x), round(r.y), round(r.w), round(r.h))];
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self layoutImage];
}

- (void)layoutImage {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.image.frame = [self imageRect];
    [self placeNote];
    [CATransaction commit];
    if (self.enhanced) /* (the picture at the new size) */
        [self present];
}

/* the note at the picture's top right: white semibold text a 24th of the
 * picture's height (14 points at least) on a 63 % black box, margin and
 * padding half the text's height */
- (void)placeNote {
    CGSize area = self.image.bounds.size;
    CGFloat fh = MAX(14, floor(area.height / 24)), pad = floor(fh / 2);
    NSDictionary *attrs = @{
        NSFontAttributeName : [NSFont systemFontOfSize:fh weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName : NSColor.whiteColor
    };
    NSAttributedString *s = [[NSAttributedString alloc] initWithString:self.enhanced ? @"Enhanced 3D" : @"Original 3D"
                                                            attributes:attrs];
    CGSize ts = s.size;
    ts = CGSizeMake(ceil(ts.width), ceil(ts.height));
    _noteText.string = s;
    _noteText.contentsScale = self.window.backingScaleFactor > 0 ? self.window.backingScaleFactor : 2;
    _noteText.frame = CGRectMake(pad, pad / 2, ts.width, ts.height);
    _note.frame = CGRectMake(area.width - ts.width - 3 * pad, area.height - ts.height - 2 * pad, ts.width + 2 * pad,
                             ts.height + pad);
}

/* the note shown for 2 s: 0.4 s fading in, 1.2 s, 0.4 s fading out */
- (void)showNote {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [self placeNote];
    [CATransaction commit];
    CAKeyframeAnimation *a = [CAKeyframeAnimation animationWithKeyPath:@"opacity"];
    a.values = @[ @0, @1, @1, @0 ];
    a.keyTimes = @[ @0, @0.2, @0.8, @1 ];
    a.duration = 2;
    [_note removeAnimationForKey:@"note"];
    [_note addAnimation:a forKey:@"note"];
}

- (void)setEnhanced:(BOOL)on {
    _enhanced = on;
    if (!on && self.session && self.session->core)
        tfx_enhanced(self.session->core, 0, 0);
    log_line(on ? @"3D: enhanced" : @"3D: original");
    [self present];
    [self showNote];
}

/* the enhanced mode's picture of the frame shown, drawn 1:1 at the frame's
 * rectangle in backing pixels; NO when there is none at that size */
- (BOOL)presentEnhanced {
    NSRect b = [self convertRectToBacking:self.image.frame];
    int w = (int)lround(b.size.width), h = (int)lround(b.size.height);
    size_t size = (size_t)(w > 0 ? w : 0) * (size_t)(h > 0 ? h : 0) * 4;
    uint8_t *px = size ? malloc(size) : NULL;
    if (!px || !tfx_mac_session_enhanced_rgba(self.session, w, h, px)) {
        free(px);
        return NO;
    }
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, px, size, release_pixels);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef img = CGImageCreate((size_t)w, (size_t)h, 8, 32, (size_t)w * 4, space,
                                   (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault, provider,
                                   NULL, false, kCGRenderingIntentDefault);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.image.contents = (__bridge id)img;
    [CATransaction commit];
    CGImageRelease(img);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    return YES;
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self layoutImage];
}

- (void)present {
    if (!self.session || !self.session->core)
        return;
    if (self.enhanced && [self presentEnhanced])
        return;
    tfx_mac_session_rgba(self.session, _rgba);
    CFDataRef data = CFDataCreate(NULL, _rgba, sizeof _rgba);
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef img = CGImageCreate(320, 200, 8, 32, 320 * 4, space,
                                   (CGBitmapInfo)kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault, provider,
                                   NULL, false, kCGRenderingIntentDefault);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.image.contents = (__bridge id)img;
    [CATransaction commit];
    CGImageRelease(img);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    CFRelease(data);
}

/* ---- the keyboard ---- */

- (void)key:(uint16_t)code down:(BOOL)down {
    if (!self.session || !self.session->core || code >= 128)
        return;
    uint8_t b[2];
    int n = tfx_mac_scan(code, down, b);
    for (int i = 0; i < n; i++)
        tfx_key(self.session->core, b[i]);
    _pressed[code] = down ? 1 : 0;
}

- (void)keyDown:(NSEvent *)e {
    if (e.modifierFlags & NSEventModifierFlagCommand) {
        [super keyDown:e];
        return;
    }
    NSEventModifierFlags m = e.modifierFlags;
    if (e.keyCode == 0x05 /* G */ && (m & NSEventModifierFlagControl) && (m & NSEventModifierFlagShift)) {
        if (!e.isARepeat) /* Control-Shift-G: the host's, not the game's */
            self.enhanced = !self.enhanced;
        return;
    }
    [self key:e.keyCode down:YES]; /* (a repeat sends the make code again, as the keyboard's typematic did) */
}

- (void)keyUp:(NSEvent *)e {
    if (e.keyCode < 128 && !_pressed[e.keyCode]) /* (its press was the host's: a menu's or Control-Shift-G) */
        return;
    [self key:e.keyCode down:NO];
}

- (void)flagsChanged:(NSEvent *)e {
    uint16_t code = e.keyCode;
    if (!tfx_mac_modifier(code))
        return;
    if (code == 0x39) { /* Caps Lock: a press and a release at each change */
        [self key:code down:YES];
        [self key:code down:NO];
        return;
    }
    [self key:code down:!_pressed[code]];
}

/* focus lost: the keys held are released (the game would see them held), the mouse let go */
- (void)releaseKeys {
    for (uint16_t k = 0; k < 128; k++)
        if (_pressed[k])
            [self key:k down:NO];
    _buttons = 0;
    self.captured = NO;
}

/* ---- the mouse ---- */

/* The mouse: its position over the frame (the menus' pointer) and its
 * motion (the driver's mickeys: the ARCADE and mouse-controlled flight
 * steer by them, M09), one point a mickey. Captured (View > Capture the
 * Mouse), the cursor is hidden and held, and the position follows the
 * motion, so the flight steers past the window's edges. */
- (void)mouse:(NSEvent *)e {
    if (!self.session || !self.session->core)
        return;
    NSRect r = [self imageRect];
    if (self.captured) {
        _x += e.deltaX / r.size.width * 640, _y += e.deltaY / r.size.height * 200;
    } else {
        NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
        _x = (p.x - r.origin.x) / r.size.width * 640, _y = (r.origin.y + r.size.height - p.y) / r.size.height * 200;
    }
    _x = _x < 0 ? 0 : _x > 639 ? 639 : _x;
    _y = _y < 0 ? 0 : _y > 199 ? 199 : _y;
    tfx_mouse(self.session->core, (uint16_t)_x, (uint16_t)_y, _buttons);
    _carry_x += e.deltaX, _carry_y += e.deltaY; /* (deltaY grows downwards, as mickeys do) */
    int32_t dx = (int32_t)_carry_x, dy = (int32_t)_carry_y;
    _carry_x -= dx, _carry_y -= dy;
    if (dx || dy)
        tfx_mouse_motion(self.session->core, dx, dy);
}

- (void)setCaptured:(BOOL)captured {
    if (captured == _captured)
        return;
    _captured = captured;
    CGAssociateMouseAndMouseCursorPosition(!captured);
    if (captured)
        [NSCursor hide];
    else
        [NSCursor unhide];
}

- (void)mouseMoved:(NSEvent *)e { [self mouse:e]; }
- (void)mouseDragged:(NSEvent *)e { [self mouse:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self mouse:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self mouse:e]; }
- (void)mouseDown:(NSEvent *)e { _buttons |= 1; [self mouse:e]; }
- (void)mouseUp:(NSEvent *)e { _buttons &= (uint16_t)~1; [self mouse:e]; }
- (void)rightMouseDown:(NSEvent *)e { _buttons |= 2; [self mouse:e]; }
- (void)rightMouseUp:(NSEvent *)e { _buttons &= (uint16_t)~2; [self mouse:e]; }
- (void)otherMouseDown:(NSEvent *)e { _buttons |= 4; [self mouse:e]; }
- (void)otherMouseUp:(NSEvent *)e { _buttons &= (uint16_t)~4; [self mouse:e]; }

@end

@interface TFXApp : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation TFXApp {
    NSWindow *_window;
    TFXView *_view;
    NSTimer *_timer;
    tfx_mac_session _session;
    tfx_mac_mixer _mixer;
    tfx_mac_output *_output;
    id _activity;
    BOOL _ended;
    NSString *_folder;
    tfx_mac_profile _profile;
    GCController *_pad;
    tfx_mac_cd _cd;
    NSString *_cdPath; /* the CD image's cue sheet, nil: none */
    BOOL _paused;      /* the settings open */
    uint64_t _shown;   /* the frames shown since the start (logged: the first, and at the end) */
    CFAbsoluteTime _startedAt;
    dispatch_source_t _sigterm;
}

static NSString *const kFolder = @"OriginalFolder", *const kCD = @"CDImage";

/* the profile of folder d (logged); nil when it is the supported release, else what is wrong */
- (NSString *)problemWith:(NSString *)d {
    tfx_mac_profile p;
    int ok = tfx_mac_profile_check(d.fileSystemRepresentation, &p);
    log_line([NSString stringWithFormat:@"game folder %@: %s (known files: %d same, %d different, %d absent)", d,
                                        ok ? p.edition : p.message, p.matched, p.differ, p.absent]);
    if (ok) {
        _profile = p;
        return nil;
    }
    return [NSString stringWithUTF8String:p.message];
}

/* asks for a folder until one passes; nil when the user gives up */
- (NSString *)askFolderAfter:(NSString *)problem {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.message = @"Choose your TFX game folder, the one holding DID.DAT, TFX.DAT and START.EXE. It is only read.";
    panel.prompt = @"Choose";
    for (;;) {
        if (problem) {
            NSAlert *a = [NSAlert new];
            a.messageText = @"That folder cannot be used.";
            a.informativeText = problem;
            [a addButtonWithTitle:@"Choose Another Folder"];
            [a addButtonWithTitle:@"Quit"];
            if ([a runModal] != NSAlertFirstButtonReturn)
                return nil;
        }
        if ([panel runModal] != NSModalResponseOK)
            return nil;
        NSString *d = panel.URL.path;
        problem = [self problemWith:d];
        if (!problem) {
            [NSUserDefaults.standardUserDefaults setObject:d forKey:kFolder];
            return d;
        }
    }
}

/* the original game folder: the environment's, the last chosen, else asked */
- (NSString *)gameFolder {
    NSString *env = NSProcessInfo.processInfo.environment[@"TFX_ORIGINAL_DIR"];
    NSString *last = [NSUserDefaults.standardUserDefaults stringForKey:kFolder];
    NSString *problem = nil;
    for (NSString *d in @[ env ? env : @"", last ? last : @"" ]) {
        if (!d.length)
            continue;
        NSString *why = [self problemWith:d];
        if (!why)
            return d;
        problem = [NSString stringWithFormat:@"%@\n\n(%@)", why, d];
    }
    return [self askFolderAfter:problem];
}

- (NSString *)storeFolder { return support_folder(@"Game"); }

/* starts the game on folder (stopping a run under way); NO when the core does not start */
- (BOOL)startOn:(NSString *)folder {
    if (_session.core)
        tfx_mac_session_stop(&_session);
    tfx_mac_mixer_reset(&_mixer);
    pthread_mutex_lock(&_mixer.lock); /* (the CD stopped with the run) */
    if (_cdPath)
        tfx_mac_cd_stop(&_cd);
    _mixer.cd = _cdPath ? &_cd : NULL;
    pthread_mutex_unlock(&_mixer.lock);
    _session = (tfx_mac_session){
        .mixer = &_mixer, .now_us = session_now, .log = session_log, .cd = _cdPath ? &_cd : NULL};
    _folder = folder;
    _ended = NO;
    _shown = 0, _startedAt = CFAbsoluteTimeGetCurrent();
    log_line([NSString stringWithFormat:@"start: %@, store %@, %@", folder, [self storeFolder], architecture()]);
    return tfx_mac_session_start(&_session, folder.fileSystemRepresentation, [self storeFolder].fileSystemRepresentation) != 0;
}

/* the reason shown (the session's: the store, say), then the app ends */
- (void)couldNotStart {
    NSAlert *a = [NSAlert new];
    a.messageText = @"TFX could not start.";
    if (_session.error[0])
        a.informativeText = [NSString stringWithUTF8String:_session.error];
    [a runModal];
    [NSApp terminate:nil];
}

/* the CD image at path (nil: none) taken, checked against the release's
 * disc; NO (and none) when it cannot be, the reason shown when asked */
- (BOOL)useCD:(NSString *)path tell:(BOOL)tell {
    pthread_mutex_lock(&_mixer.lock);
    _mixer.cd = NULL;
    pthread_mutex_unlock(&_mixer.lock);
    if (_cdPath)
        tfx_mac_cd_close(&_cd);
    _cdPath = nil;
    if (!path) {
        log_line(@"CD image: none");
        return YES;
    }
    if (!tfx_mac_cd_open(&_cd, path.fileSystemRepresentation) || !tfx_mac_cd_profile(&_cd)) {
        NSString *why = [NSString stringWithUTF8String:_cd.error];
        tfx_mac_cd_close(&_cd);
        log_line([NSString stringWithFormat:@"CD image %@: %@", path, why]);
        if (tell) {
            NSAlert *a = [NSAlert new];
            a.messageText = @"This CD image cannot be used.";
            a.informativeText = [NSString stringWithFormat:@"%@\n\n(%@)\n\nTFX runs as without a CD-ROM drive.", why, path];
            [a runModal];
        }
        return NO;
    }
    _cdPath = path;
    log_line([NSString stringWithFormat:@"CD image %@: TFX, Ocean CD (1995), %d tracks", path, _cd.tracks]);
    return YES;
}

- (void)chooseCD:(id)sender {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.message = @"Choose the CUE sheet of your image of the TFX CD (the music's audio tracks).";
    panel.allowedFileTypes = @[ @"cue" ];
    if ([panel runModal] != NSModalResponseOK)
        return;
    NSString *path = panel.URL.path;
    if ([self useCD:path tell:YES])
        [NSUserDefaults.standardUserDefaults setObject:path forKey:kCD];
    if (![self startOn:_folder])
        [self couldNotStart];
}

- (void)noCD:(id)sender {
    [NSUserDefaults.standardUserDefaults removeObjectForKey:kCD];
    [self useCD:nil tell:NO];
    if (![self startOn:_folder])
        [self couldNotStart];
}

- (void)chooseFolder:(id)sender {
    NSString *d = [self askFolderAfter:nil];
    if (d && ![self startOn:d])
        [self couldNotStart];
}

/* ---- the user's data (M07, userdata.h) ---- */

- (tfx_mac_files)files {
    tfx_mac_files f;
    snprintf(f.root, sizeof f.root, "%s", _folder ? _folder.fileSystemRepresentation : "");
    snprintf(f.store, sizeof f.store, "%s", [self storeFolder].fileSystemRepresentation);
    return f;
}

static void collect(void *ctx, const char *name, int written, const char *why) {
    [(__bridge NSMutableString *)ctx appendFormat:@"%s: %s\n", name, written ? "written" : why];
}

/* the game paused around a modal action, restarted when it changed the store */
- (void)paused:(BOOL (^)(void))action {
    if (_paused)
        return;
    _paused = YES;
    clock_pause(YES);
    [_view releaseKeys];
    BOOL changed = action();
    clock_pause(NO);
    _paused = NO;
    if (changed && _folder) {
        log_line(@"saved data changed: restarting");
        if (![self startOn:_folder])
            [self couldNotStart];
    }
}

- (void)importData:(id)sender {
    [self paused:^BOOL {
      NSOpenPanel *panel = [NSOpenPanel openPanel];
      panel.canChooseDirectories = YES;
      panel.canChooseFiles = NO;
      panel.message = @"Choose a folder holding TFX's saved files (TFX.CFG, TFX.HS, TFXSAV.n, VIDEO.REC), a copy "
                      @"of a DOS installation say. Each is checked as the game reads it; it is only read.";
      panel.prompt = @"Import";
      if ([panel runModal] != NSModalResponseOK)
          return NO;
      tfx_mac_files f = [self files];
      tfx_mac_user_flow fl;
      tfx_mac_user_flow_load(&fl, &f);
      NSMutableString *text = [NSMutableString new];
      int n = tfx_mac_user_import(&f, &fl, panel.URL.fileSystemRepresentation, collect, (__bridge void *)text);
      log_line([NSString stringWithFormat:@"import %@: %d written\n%@", panel.URL.path, n, text]);
      NSAlert *a = [NSAlert new];
      a.messageText = n > 0 ? [NSString stringWithFormat:@"%d file%s imported; TFX restarts with them.", n, n == 1 ? "" : "s"]
                            : @"Nothing was imported.";
      a.informativeText = text.length ? text
                                      : @"The folder holds none of the game's saved files. The versions they "
                                        @"replace are kept (File > Restore a Previous Version…).";
      [a runModal];
      return n > 0;
    }];
}

- (void)exportData:(id)sender {
    [self paused:^BOOL {
      NSOpenPanel *panel = [NSOpenPanel openPanel];
      panel.canChooseDirectories = YES;
      panel.canChooseFiles = NO;
      panel.canCreateDirectories = YES;
      panel.message = @"Choose a folder for a copy of your saved files, named as the DOS game reads them (not "
                      @"the game folder).";
      panel.prompt = @"Export";
      if ([panel runModal] != NSModalResponseOK)
          return NO;
      tfx_mac_files f = [self files];
      NSMutableString *text = [NSMutableString new];
      int n = tfx_mac_user_export(&f, panel.URL.fileSystemRepresentation, collect, (__bridge void *)text);
      NSAlert *a = [NSAlert new];
      a.messageText = n < 0 ? @"Not into the game folder or the saved data's own folder."
                            : [NSString stringWithFormat:@"%d file%s exported.", n, n == 1 ? "" : "s"];
      a.informativeText = text;
      [a runModal];
      return NO;
    }];
}

- (void)restoreData:(id)sender {
    [self paused:^BOOL {
      tfx_mac_files f = [self files];
      NSPopUpButton *pick = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 380, 26)];
      NSArray *names = [[NSFileManager.defaultManager contentsOfDirectoryAtPath:[self storeFolder] error:nil]
          sortedArrayUsingSelector:@selector(compare:)];
      NSDateFormatter *df = [NSDateFormatter new];
      df.dateStyle = NSDateFormatterMediumStyle, df.timeStyle = NSDateFormatterMediumStyle;
      for (NSString *name in names) {
          if (!tfx_mac_user_kind_of(name.UTF8String))
              continue;
          for (int k = 1; k <= TFX_MAC_KEPT; k++) {
              int64_t when;
              uint32_t size;
              if (!tfx_mac_user_version(&f, name.UTF8String, k, &when, &size))
                  break;
              NSString *t = [NSString stringWithFormat:@"%@ — replaced version %d (%@, %u bytes)", name.uppercaseString,
                                                       k, [df stringFromDate:[NSDate dateWithTimeIntervalSince1970:(double)when]], size];
              [pick addItemWithTitle:t];
              pick.lastItem.representedObject = @[ name, @(k) ];
          }
      }
      NSAlert *a = [NSAlert new];
      if (!pick.numberOfItems) {
          a.messageText = @"No previous versions are kept yet.";
          a.informativeText = @"Each time the game, the settings or an import replaces a saved file, the version it "
                              @"replaces is kept (the last three).";
          [a runModal];
          return NO;
      }
      a.messageText = @"Restore a previous version";
      a.informativeText = @"The file it replaces is kept in turn, so this can be undone. TFX restarts with it.";
      a.accessoryView = pick;
      [a addButtonWithTitle:@"Restore"];
      [a addButtonWithTitle:@"Cancel"];
      if ([a runModal] != NSAlertFirstButtonReturn)
          return NO;
      NSArray *chosen = pick.selectedItem.representedObject;
      BOOL ok = tfx_mac_user_restore(&f, [chosen[0] UTF8String], [chosen[1] intValue]) != 0;
      log_line([NSString stringWithFormat:@"restore %@ version %@: %s", chosen[0], chosen[1], ok ? "done" : "failed"]);
      return ok;
    }];
}

- (void)showData:(id)sender { [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:[self storeFolder]]]; }

/* the mouse held for the flight (Control-Command-M again, or another
 * window, lets it go) */
- (void)toggleCapture:(NSMenuItem *)item {
    _view.captured = !_view.captured;
    log_line(_view.captured ? @"mouse captured" : @"mouse released");
}

- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(toggleCapture:))
        item.state = _view.captured ? NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(toggleEnhanced:))
        item.state = _view.enhanced ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}

/* the enhanced 3D mode on or off (Control-Shift-G; the key's repeat ignored) */
- (void)toggleEnhanced:(id)sender {
    NSEvent *e = NSApp.currentEvent;
    if (e.type == NSEventTypeKeyDown && e.isARepeat)
        return;
    _view.enhanced = !_view.enhanced;
}

- (void)toggleInteger:(NSMenuItem *)item {
    BOOL on = ![NSUserDefaults.standardUserDefaults boolForKey:@"IntegerScaling"];
    [NSUserDefaults.standardUserDefaults setBool:on forKey:@"IntegerScaling"];
    item.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    [_view layoutImage];
}

- (void)supportInfo:(id)sender {
    NSDictionary *b = NSBundle.mainBundle.infoDictionary;
    NSString *version = b[@"CFBundleShortVersionString"], *build = b[@"CFBundleVersion"];
    NSString *text = [NSString stringWithFormat:
        @"TFX %@ (%@), %@, macOS %@\nGame folder: %@\nEdition: %s (known files: %d same, %d different, %d absent)\n"
        @"CD image: %@\nSound: %s\nMIDI synthesizer: %@\nUser data: %@\nLog: %@",
        version ? version : @"?", build ? build : @"?", architecture(),
        NSProcessInfo.processInfo.operatingSystemVersionString, _folder ? _folder : @"none", _profile.edition, _profile.matched,
        _profile.differ, _profile.absent, _cdPath ? _cdPath : @"none", _session.audio,
        tfx_mac_output_synth(_output) ? @"Apple DLS" : @"none (the MPU-401's music is silent)", support_folder(@"Game"),
        [support_folder(@"Logs") stringByAppendingPathComponent:@"TFX.log"]];
    NSAlert *a = [NSAlert new];
    a.messageText = @"Support Information";
    a.informativeText = text;
    [a addButtonWithTitle:@"Copy"];
    [a addButtonWithTitle:@"Close"];
    if ([a runModal] == NSAlertFirstButtonReturn) {
        [NSPasteboard.generalPasteboard clearContents];
        [NSPasteboard.generalPasteboard setString:text forType:NSPasteboardTypeString];
    }
}

- (void)menus {
    NSMenu *bar = [NSMenu new], *app = [NSMenu new], *file = [NSMenu new], *view = [NSMenu new];
    NSMenuItem *appItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    [app addItemWithTitle:@"Settings…" action:@selector(settings:) keyEquivalent:@","].target = self;
    [app addItemWithTitle:@"Support Information" action:@selector(supportInfo:) keyEquivalent:@""].target = self;
    [app addItem:NSMenuItem.separatorItem];
    [app addItemWithTitle:@"Hide TFX" action:@selector(hide:) keyEquivalent:@"h"];
    [app addItem:NSMenuItem.separatorItem];
    [app addItemWithTitle:@"Quit TFX" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = app;
    NSMenuItem *fileItem = [bar addItemWithTitle:@"File" action:nil keyEquivalent:@""];
    file.title = @"File";
    [file addItemWithTitle:@"Choose Game Folder…" action:@selector(chooseFolder:) keyEquivalent:@"o"].target = self;
    [file addItemWithTitle:@"Choose CD Image…" action:@selector(chooseCD:) keyEquivalent:@""].target = self;
    [file addItemWithTitle:@"Play Without CD" action:@selector(noCD:) keyEquivalent:@""].target = self;
    [file addItem:NSMenuItem.separatorItem];
    [file addItemWithTitle:@"Import Saved Data…" action:@selector(importData:) keyEquivalent:@""].target = self;
    [file addItemWithTitle:@"Export Saved Data…" action:@selector(exportData:) keyEquivalent:@""].target = self;
    [file addItemWithTitle:@"Restore a Previous Version…" action:@selector(restoreData:) keyEquivalent:@""].target = self;
    [file addItemWithTitle:@"Show Saved Data in Finder" action:@selector(showData:) keyEquivalent:@""].target = self;
    fileItem.submenu = file;
    NSMenuItem *viewItem = [bar addItemWithTitle:@"View" action:nil keyEquivalent:@""];
    view.title = @"View";
    NSMenuItem *fs = [view addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:)
                              keyEquivalent:@"f"];
    fs.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
    NSMenuItem *integer = [view addItemWithTitle:@"Integer Scaling" action:@selector(toggleInteger:) keyEquivalent:@""];
    integer.target = self;
    integer.state = [NSUserDefaults.standardUserDefaults boolForKey:@"IntegerScaling"] ? NSControlStateValueOn
                                                                                       : NSControlStateValueOff;
    [view addItem:NSMenuItem.separatorItem];
    NSMenuItem *capture = [view addItemWithTitle:@"Capture the Mouse" action:@selector(toggleCapture:) keyEquivalent:@"m"];
    capture.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
    capture.target = self;
    NSMenuItem *enhanced = [view addItemWithTitle:@"Enhanced 3D" action:@selector(toggleEnhanced:) keyEquivalent:@"g"];
    enhanced.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagShift;
    enhanced.target = self;
    viewItem.submenu = view;
    NSApp.mainMenu = bar;
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    log_open();
    /* a termination signal (a logout, `kill`) quits as the menu's Quit does */
    signal(SIGTERM, SIG_IGN);
    _sigterm = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(_sigterm, ^{
      [NSApp terminate:nil];
    });
    dispatch_resume(_sigterm);
    [self menus];
    NSString *folder = [self gameFolder];
    if (!folder) {
        [NSApp terminate:nil];
        return;
    }
    tfx_mac_mixer_init(&_mixer, TFX_MAC_OUTPUT_RATE, 60000);
    _output = tfx_mac_output_open(&_mixer, game_now);
    if (!_output)
        log_line(@"no audio output device: running silent");
    else if (!tfx_mac_output_synth(_output))
        log_line(@"no MIDI synthesizer: the MPU-401's music is silent");
    const char *cd_env = getenv("TFX_CD_IMAGE");
    NSString *cd = cd_env ? [NSString stringWithUTF8String:cd_env]
                          : [NSUserDefaults.standardUserDefaults stringForKey:kCD];
    if (cd && ![self useCD:cd tell:NO] && !cd_env)
        [NSUserDefaults.standardUserDefaults removeObjectForKey:kCD];
    if (![self startOn:folder]) {
        [self couldNotStart];
        return;
    }
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 720)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"TFX";
    _window.delegate = self;
    _window.contentMinSize = NSMakeSize(320, 240);
    _window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    _window.acceptsMouseMovedEvents = YES;
    _view = [[TFXView alloc] initWithFrame:_window.contentView.bounds];
    _view.session = &_session;
    _window.contentView = _view;
    if ([NSProcessInfo.processInfo.arguments containsObject:@"--enhanced"])
        _view.enhanced = YES;
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_view];
    [NSApp activateIgnoringOtherApps:YES];
    _activity = [NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical
                                                             reason:@"the game runs"];
    for (NSNotificationName name in @[ GCControllerDidConnectNotification, GCControllerDidDisconnectNotification ])
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(controllers:) name:name object:nil];
    [self controllers:nil];
    _timer = [NSTimer timerWithTimeInterval:0.001 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
}

/* the joystick: the first controller with a gamepad's controls */
- (void)controllers:(NSNotification *)n {
    GCController *pad = nil;
    for (GCController *c in GCController.controllers)
        if (c.extendedGamepad) {
            pad = c;
            break;
        }
    if (pad != _pad)
        log_line(pad ? [NSString stringWithFormat:@"joystick: %@", pad.vendorName ? pad.vendorName : @"a controller"]
                     : @"joystick: none");
    _pad = pad;
}

/* the controller's state now (none, or TFX not active: centred, its
 * buttons up); NO when none is connected */
- (BOOL)readPad:(tfx_mac_pad *)pp {
    tfx_mac_pad p = {0};
    GCExtendedGamepad *g = _pad.extendedGamepad;
    if (g && NSApp.isActive) {
        p.lx = g.leftThumbstick.xAxis.value, p.ly = g.leftThumbstick.yAxis.value;
        p.rx = g.rightThumbstick.xAxis.value, p.trigger = g.rightTrigger.value;
        p.dpad = (g.dpad.up.pressed ? TFX_PAD_UP : 0) | (g.dpad.down.pressed ? TFX_PAD_DOWN : 0) |
                 (g.dpad.left.pressed ? TFX_PAD_LEFT : 0) | (g.dpad.right.pressed ? TFX_PAD_RIGHT : 0);
        p.buttons = (g.buttonA.pressed ? 1u : 0) | (g.buttonB.pressed ? 2u : 0) | (g.buttonX.pressed ? 4u : 0) |
                    (g.buttonY.pressed ? 8u : 0);
    }
    *pp = p;
    return g != nil;
}

- (void)pollPad {
    tfx_mac_pad p;
    BOOL on = [self readPad:&p];
    uint16_t count[4];
    uint8_t buttons;
    tfx_mac_pad_counts(&p, count, &buttons);
    tfx_stick(_session.core, on, count, buttons);
}

/* the settings (M06): the game paused while they are open (its clock
 * held, its keys released, the stick centred); saved, TFX restarts on
 * them, as the game reads TFX.CFG when it starts */
- (void)settings:(id)sender {
    if (_paused || !_folder)
        return;
    _paused = YES;
    clock_pause(YES);
    [_view releaseKeys];
    if (_session.core) {
        uint16_t centre[4];
        uint8_t buttons;
        tfx_mac_pad_counts(&(tfx_mac_pad){0}, centre, &buttons);
        tfx_stick(_session.core, _pad != nil, centre, 0);
    }
    tfx_mac_files f;
    snprintf(f.root, sizeof f.root, "%s", _folder.fileSystemRepresentation);
    snprintf(f.store, sizeof f.store, "%s", [self storeFolder].fileSystemRepresentation);
    __weak TFXApp *me = self;
    TFXSettings *s = [[TFXSettings alloc] initWithFiles:f
                                                    pad:^BOOL(tfx_mac_pad *p) { return [me readPad:p]; }
                                                     cd:_cdPath != nil];
    BOOL saved = [s run];
    clock_pause(NO);
    _paused = NO;
    [_window makeKeyAndOrderFront:nil];
    if (saved) {
        log_line(@"settings saved: restarting");
        if (![self startOn:_folder])
            [self couldNotStart];
    }
}

- (void)tick:(NSTimer *)t {
    if (_ended || _paused || !_session.core)
        return;
    [self pollPad];
    int r = tfx_mac_session_pump(&_session, 8000);
    if (r & TFX_MAC_SHOWN) {
        [_view present];
        if (!_shown++)
            log_line([NSString stringWithFormat:@"first frame shown after %.1f s",
                                                CFAbsoluteTimeGetCurrent() - _startedAt]);
    }
    if (r & TFX_MAC_ENDED) {
        _ended = YES;
        const char *msg;
        tfx_end e = tfx_ended(_session.core, &msg);
        log_line([NSString stringWithFormat:@"end %d%s%s", (int)e, msg ? ": " : "", msg ? msg : ""]);
        if (e == TFX_END_FATAL) {
            NSAlert *a = [NSAlert new];
            a.messageText = @"TFX stopped.";
            a.informativeText = [NSString stringWithUTF8String:msg ? msg : ""];
            [a runModal];
        }
        [NSApp terminate:nil];
    }
}

- (void)windowDidResignKey:(NSNotification *)n { [_view releaseKeys]; }

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a { return YES; }

- (void)applicationWillTerminate:(NSNotification *)n {
    [_timer invalidate];
    log_line([NSString stringWithFormat:@"quit: %llu frames shown in %.1f s", (unsigned long long)_shown,
                                        _startedAt != 0 ? CFAbsoluteTimeGetCurrent() - _startedAt : 0.0]);
    if (_session.core)
        tfx_mac_session_stop(&_session);
    tfx_mac_output_close(_output);
    _output = NULL;
    [self useCD:nil tell:NO];
    if (_activity)
        [NSProcessInfo.processInfo endActivity:_activity];
}

@end

int main(int argc, const char **argv) {
    (void)argc, (void)argv;
    @autoreleasepool {
        NSApplication *app = NSApplication.sharedApplication;
        TFXApp *d = [TFXApp new];
        app.delegate = d;
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        [app run];
    }
    return 0;
}
