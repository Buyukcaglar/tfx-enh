#import "settings_ui.h"

#include "config/menu.h"

/* ---- a calibration screen: instructions, a live reading, buttons ---- */

@interface TFXCalPanel : NSObject
@property(copy) NSString * (^tick)(void); /* one pass; the reading shown */
@property(copy) BOOL (^accept)(NSInteger button); /* NO: the screen stays */
@property BOOL held;                                /* the held button down (a pass sees it) */
- (instancetype)initWithTitle:(NSString *)title text:(NSString *)text buttons:(NSArray<NSString *> *)buttons;
- (void)hold:(NSString *)title; /* a button down while pressed, not a choice */
- (void)say:(NSString *)message;
- (NSInteger)run;
@end

@implementation TFXCalPanel {
    NSPanel *_panel;
    NSTextField *_reading, *_message;
    NSStackView *_buttons;
    NSTimer *_timer;
}

static NSTextField *label(NSString *text, CGFloat width) {
    NSTextField *t = [NSTextField wrappingLabelWithString:text];
    t.preferredMaxLayoutWidth = width;
    return t;
}

- (instancetype)initWithTitle:(NSString *)title text:(NSString *)text buttons:(NSArray<NSString *> *)buttons {
    if (!(self = [super init]))
        return nil;
    _panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 460, 240)
                                        styleMask:NSWindowStyleMaskTitled
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    _panel.title = title;
    NSTextField *head = [NSTextField labelWithString:title];
    head.font = [NSFont boldSystemFontOfSize:13];
    _reading = [NSTextField labelWithString:@""];
    _reading.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    _message = [NSTextField labelWithString:@""];
    _message.textColor = NSColor.systemRedColor;
    _buttons = [NSStackView new];
    for (NSUInteger i = 0; i < buttons.count; i++) {
        NSButton *b = [NSButton buttonWithTitle:buttons[i] target:self action:@selector(chose:)];
        b.tag = (NSInteger)i;
        if (i == 0)
            b.keyEquivalent = @"\r";
        else if (i == buttons.count - 1)
            b.keyEquivalent = @"\033";
        [_buttons addView:b inGravity:NSStackViewGravityTrailing];
    }
    NSStackView *v = [NSStackView stackViewWithViews:@[ head, label(text, 420), _reading, _message, _buttons ]];
    v.orientation = NSUserInterfaceLayoutOrientationVertical;
    v.alignment = NSLayoutAttributeLeading;
    v.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    v.spacing = 12;
    [_buttons.widthAnchor constraintEqualToConstant:420].active = YES;
    _panel.contentView = v;
    return self;
}

- (void)hold:(NSString *)title {
    NSButton *b = [NSButton buttonWithTitle:title target:nil action:nil];
    [b setButtonType:NSButtonTypeMomentaryPushIn];
    [b sendActionOn:NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp];
    b.target = self, b.action = @selector(held:);
    [_buttons addView:b inGravity:NSStackViewGravityLeading];
}

- (void)held:(NSButton *)b { self.held = (NSApp.currentEvent.type == NSEventTypeLeftMouseDown); }

- (void)say:(NSString *)message { _message.stringValue = message; }

- (void)chose:(NSButton *)b {
    if (self.accept && !self.accept(b.tag))
        return;
    [NSApp stopModalWithCode:b.tag];
}

- (void)pass:(NSTimer *)t {
    if (self.tick)
        _reading.stringValue = self.tick();
}

- (NSInteger)run {
    [self pass:nil];
    [_panel center];
    _timer = [NSTimer timerWithTimeInterval:1.0 / 60 target:self selector:@selector(pass:) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
    NSInteger r = [NSApp runModalForWindow:_panel];
    [_timer invalidate];
    [_panel orderOut:nil];
    return r;
}

@end

/* ---- the settings ---- */

typedef struct row {
    NSString *title;
    size_t field; /* offsetof(tfx_config_settings, ..) */
    int first;    /* the first value */
    NSArray<NSString *> *names;
} row;

@implementation TFXSettings {
    tfx_mac_files _f;
    BOOL (^_pad)(tfx_mac_pad *);
    BOOL _cd;
    tfx_mac_settings _m;
    NSWindow *_window;
    NSMutableArray<NSPopUpButton *> *_popups;
    NSTextField *_sound, *_joystick, *_swap, *_where;
    NSButton *_blaster;
    tfx_joystick_ui _ui;
    tfx_mac_cal_port _port;
    BOOL _saved;
}

static const row *rows(size_t *n) {
    static row r[7];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      r[0] = (row){@"Control", offsetof(tfx_config_settings, control_mode), 0, @[ @"Keyboard", @"Joystick" ]};
      r[1] = (row){@"Joystick", offsetof(tfx_config_settings, stick), 0,
                   @[ @"Normal", @"FlightStick Pro", @"ThrustMaster" ]};
      r[2] = (row){@"Detail", offsetof(tfx_config_settings, w_detail), 0, @[ @"Low", @"Medium", @"High" ]};
      r[3] = (row){@"Flight model", offsetof(tfx_config_settings, this_model), 0,
                   @[ @"Arcade", @"Simple", @"Realistic", @"Military Spec" ]};
      r[4] = (row){@"Sound effects", offsetof(tfx_config_settings, gsound), 0,
                   @[ @"Off", @"Sound Blaster", @"Roland LAPC-I", @"Sound Blaster 16 (QSound)", @"Gravis UltraSound" ]};
      r[5] = (row){@"Music", offsetof(tfx_config_settings, music), 0,
                   @[ @"Off", @"Sound Blaster / CD", @"Roland LAPC-I", @"AdLib", @"Roland SCC-1" ]};
      r[6] = (row){@"Language", offsetof(tfx_config_settings, lang), 1,
                   @[ @"English", @"Français", @"Deutsch", @"Italiano", @"Español" ]};
    });
    *n = 7;
    return r;
}

static int32_t *field(tfx_config_settings *s, size_t at) { return (int32_t *)(void *)((char *)s + at); }

- (instancetype)initWithFiles:(tfx_mac_files)files pad:(BOOL (^)(tfx_mac_pad *))pad cd:(BOOL)cd {
    if (!(self = [super init]))
        return nil;
    _f = files, _pad = pad, _cd = cd;
    tfx_mac_settings_load(&_m, &_f, NSProcessInfo.processInfo.physicalMemory, [self controller]);
    return self;
}

/* the controller now, as the game port's counts */
- (BOOL)controller {
    tfx_mac_pad p = {0};
    BOOL on = _pad ? _pad(&p) : NO;
    uint8_t buttons;
    tfx_mac_pad_counts(&p, _port.device.count, &buttons);
    _port.device.buttons = buttons;
    _port.device.present = on ? 1 : 0;
    return on;
}

/* ---- the window ---- */

- (void)build {
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 560, 480)
                                          styleMask:NSWindowStyleMaskTitled
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"TFX Settings";
    NSGridView *grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
    grid.rowSpacing = 8;
    grid.columnSpacing = 12;
    _popups = [NSMutableArray new];
    size_t n;
    const row *r = rows(&n);
    for (size_t i = 0; i < n; i++) {
        NSPopUpButton *p = [NSPopUpButton new];
        for (NSUInteger k = 0; k < r[i].names.count; k++) {
            [p addItemWithTitle:r[i].names[k]];
            p.lastItem.tag = r[i].first + (NSInteger)k;
        }
        p.tag = (NSInteger)i;
        p.target = self, p.action = @selector(changed:);
        [_popups addObject:p];
        [grid addRowWithViews:@[ [NSTextField labelWithString:[r[i].title stringByAppendingString:@":"]], p ]];
    }
    _swap = [NSTextField labelWithString:@""];
    [grid addRowWithViews:@[ [NSTextField labelWithString:@"Swap file:"], _swap ]];
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    _joystick = label(@"", 500);
    _sound = label(@"", 500);
    _where = label(@"", 500);
    _where.textColor = NSColor.secondaryLabelColor;
    _where.font = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];

    NSButton * (^button)(NSString *, SEL) = ^(NSString *t, SEL a) {
      return [NSButton buttonWithTitle:t target:self action:a];
    };
    _blaster = button(@"Sound Blaster…", @selector(soundBlaster:));
    NSStackView *tools = [NSStackView stackViewWithViews:@[
        button(@"System Check…", @selector(systemCheck:)), button(@"Calibrate Joystick…", @selector(calibrate:)),
        _blaster, button(@"About…", @selector(about:))
    ]];
    NSStackView *tools2 = [NSStackView stackViewWithViews:@[
        button(@"Boot Disk…", @selector(bootDisk:)), button(@"Export TFX.CFG…", @selector(exportFile:))
    ]];
    NSButton *save = button(@"Save", @selector(save:)), *cancel = button(@"Cancel", @selector(cancel:));
    save.keyEquivalent = @"\r";
    cancel.keyEquivalent = @"\033";
    NSStackView *end = [NSStackView new];
    [end addView:cancel inGravity:NSStackViewGravityTrailing];
    [end addView:save inGravity:NSStackViewGravityTrailing];
    NSStackView *v = [NSStackView stackViewWithViews:@[ grid, _joystick, _sound, tools, tools2, _where, end ]];
    v.orientation = NSUserInterfaceLayoutOrientationVertical;
    v.alignment = NSLayoutAttributeLeading;
    v.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    v.spacing = 14;
    [end.widthAnchor constraintEqualToConstant:520].active = YES;
    _window.contentView = v;
}

/* the controls from the working copy */
- (void)show {
    size_t n;
    const row *r = rows(&n);
    for (size_t i = 0; i < n; i++) {
        NSPopUpButton *p = _popups[i];
        int32_t v = *field(&_m.s, r[i].field);
        if (![p selectItemWithTag:v]) { /* a value CONFIG does not offer: kept as it is */
            [p addItemWithTitle:[NSString stringWithFormat:@"%d (not one of CONFIG's)", v]];
            p.lastItem.tag = v;
            [p selectItem:p.lastItem];
        }
    }
    const tfx_config_settings *s = &_m.s;
    _swap.stringValue = s->swap_file_on ? [NSString stringWithFormat:@"%s (no effect on a Mac: kept in TFX.CFG)",
                                                                     s->swap_filename]
                                        : @"Off (a Mac needs none)";
    _joystick.stringValue =
        s->A_joystick_x_center ? [NSString stringWithFormat:@"Joystick calibrated: centre %d, %d; range %d, %d.",
                                                            s->A_joystick_x_center, s->A_joystick_y_center,
                                                            s->A_joystick_x_range, s->A_joystick_y_range]
                               : @"Joystick not calibrated: in Joystick control Save asks for a calibration first.";
    char d[512];
    tfx_mac_sound_describe(s->gsound, s->music, _cd, d, sizeof d);
    NSString *note = [NSString stringWithFormat:@"Sound here: %s.", d];
    if (s->gsound == 4)
        note = [note stringByAppendingString:@" (Under DOS the UltraSound had no FM chip, so CONFIG warned that "
                                             @"the Sound Blaster music would not work with it.)"];
    _sound.stringValue = note;
    _blaster.enabled = s->gsound == 1 || s->gsound == 3; /* (as CONFIG's button) */
    NSString *from = _m.from == TFX_MAC_CFG_STORE  ? @"your saved settings"
                     : _m.from == TFX_MAC_CFG_GAME ? @"the game folder's TFX.CFG"
                                                   : @"none found: CONFIG's first run, System Check applied";
    _where.stringValue = [NSString
        stringWithFormat:@"Read from %@. Saved to %s/TFX.CFG (the game folder is never changed); TFX restarts to "
                         @"use them.",
                         from, _f.store];
}

- (void)changed:(NSPopUpButton *)p {
    size_t n;
    *field(&_m.s, rows(&n)[p.tag].field) = (int32_t)p.selectedTag;
    [self show];
}

static void alert(NSString *title, NSString *text) {
    NSAlert *a = [NSAlert new];
    a.messageText = title;
    a.informativeText = text;
    [a runModal];
}

/* ---- System Check (ExamineSystem_), first on CONFIG's first run ---- */

- (void)report {
    const tfx_examine_report *r = &_m.report;
    size_t n;
    const row *rw = rows(&n);
    NSAlert *a = [NSAlert new];
    a.messageText = @"System Check";
    a.informativeText = [NSString
        stringWithFormat:@"Memory: %u KB%@.\nJoystick: %@.\nFlight model: %@, Detail: %@ (this Mac is faster than any "
                         @"PC the rules knew).\nSound: unchanged (a Mac has no BLASTER setting to detect).\n\n"
                         @"These are applied; change them here before saving.",
                         r->memory_k, r->swap_required ? @" (a swap file advised)" : @": enough",
                         _m.s.control_mode ? @"detected: Joystick control" : @"none: Keyboard control",
                         rw[3].names[(NSUInteger)_m.s.this_model & 3], rw[2].names[(NSUInteger)_m.s.w_detail % 3]];
    [a addButtonWithTitle:@"Continue"];
    [a addButtonWithTitle:@"Boot Disk…"];
    if ([a runModal] == NSAlertSecondButtonReturn)
        [self bootDisk:nil];
}

- (void)systemCheck:(id)sender {
    tfx_mac_settings_examine(&_m, NSProcessInfo.processInfo.physicalMemory, [self controller]);
    [self show];
    [self report];
}

- (void)bootDisk:(id)sender {
    alert(@"Make Boot Disk is not available.",
          @"Under DOS it wrote a boot floppy (system files and a minimal CONFIG.SYS and AUTOEXEC.BAT with the mouse "
          @"and CD-ROM drivers) to free conventional memory for TFX. A Mac has no such limit, and this application "
          @"never writes disks or system files.");
}

- (void)about:(id)sender {
    char text[1100];
    if (tfx_mac_settings_about(&_f, text, sizeof text))
        alert(@"About TFX", [[NSString alloc] initWithBytes:text length:strlen(text) encoding:NSISOLatin1StringEncoding]);
    else
        alert(@"About TFX", @"CONFIG's About text is read from your own CONFIG.EXE, which is not the supported "
                            @"release's here, so it is not shown.");
}

/* ---- the Sound Blaster's fields (ConfigSoundBlaster_): kept for TFX.CFG ---- */

- (void)soundBlaster:(id)sender {
    tfx_blaster b;
    tfx_blaster_parse(_m.s.blaster, &b);
    NSTextField *port = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%03X", (unsigned)(b.port & 0xffff)]];
    NSTextField *irq = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%d", (int)(int16_t)b.irq]];
    NSTextField *base = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%X", (unsigned)_m.s.irqbase]];
    NSButton *reset = [NSButton checkboxWithTitle:@"Reset" target:nil action:nil];
    NSButton *setvol = [NSButton checkboxWithTitle:@"Set volume" target:nil action:nil];
    reset.state = b.flags & 1 ? NSControlStateValueOn : NSControlStateValueOff;
    setvol.state = b.flags & 2 ? NSControlStateValueOn : NSControlStateValueOff;
    NSGridView *g = [NSGridView gridViewWithViews:@[
        @[ [NSTextField labelWithString:@"Port (hex):"], port ], @[ [NSTextField labelWithString:@"IRQ:"], irq ],
        @[ [NSTextField labelWithString:@"IRQ base (hex):"], base ], @[ [NSGridCell emptyContentView], reset ],
        @[ [NSGridCell emptyContentView], setvol ]
    ]];
    for (NSTextField *t in @[ port, irq, base ])
        [t.widthAnchor constraintEqualToConstant:80].active = YES;
    g.frame = NSMakeRect(0, 0, 260, 150);
    NSAlert *a = [NSAlert new];
    a.messageText = @"Sound Blaster";
    a.informativeText = @"The card's settings had effect only under DOS; they are kept in TFX.CFG as BLASTER and "
                        @"IRQBASE. The sound plays through this Mac's output.";
    a.accessoryView = g;
    [a addButtonWithTitle:@"Continue"];
    [a runModal];
    /* (CONFIG rebuilds BLASTER on leaving the screen, from the fields read back) */
    unsigned v = 0;
    if ([[NSScanner scannerWithString:port.stringValue] scanHexInt:&v])
        b.port = (int32_t)(uint16_t)v;
    b.irq = (int32_t)(int16_t)irq.intValue;
    if ([[NSScanner scannerWithString:base.stringValue] scanHexInt:&v])
        _m.s.irqbase = (int32_t)v;
    b.flags = (b.flags & ~3) | (reset.state == NSControlStateValueOn ? 1 : 0) |
              (setvol.state == NSControlStateValueOn ? 2 : 0);
    tfx_blaster_apply(&_m.s, &b);
    [self show];
}

/* ---- the joystick (ConfigureJoystick_ and its screens) on the controller ---- */

static int ui_present(void *ctx) {
    TFXSettings *me = (__bridge TFXSettings *)ctx;
    /* CONFIG times only stick A's axes for a ThrustMaster (0x3000), so
     * its rudder and hat read clipped to stick A's longest count, while
     * the game, once they are calibrated, times them too (0x7000, 0x8000):
     * measured here as the game reads them */
    tfx_mac_cal_port_init(&me->_port, me->_m.s.stick == 2 ? 0xf000u : me->_ui.axis_mask);
    [me controller];
    return tfx_mac_cal_present(&me->_port);
}

static void ui_error(void *ctx, const char *m) {
    (void)ctx;
    NSString *t = [[NSString stringWithUTF8String:m]
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    alert(@"Calibrate Joystick", [NSString stringWithFormat:@"%@. Connect a game controller and try again.",
                                                            [t stringByReplacingOccurrencesOfString:@"\n" withString:@": "]]);
}

static int ui_ask(void *ctx, const char *q) {
    (void)ctx;
    NSAlert *a = [NSAlert new];
    a.messageText = @"ThrustMaster";
    a.informativeText = [NSString stringWithFormat:@"Do you have the ThrustMaster %s?", q];
    [a addButtonWithTitle:@"Yes"];
    [a addButtonWithTitle:@"No"];
    return [a runModal] == NSAlertFirstButtonReturn;
}

/* a read for the screens' logic: the controller now, CONFIG's port read */
static tfx_joy_read cal_read(void *ctx) {
    TFXSettings *me = (__bridge TFXSettings *)ctx;
    [me controller];
    return tfx_mac_cal_read(&me->_port);
}

static int ui_flightstick(void *ctx, tfx_config_settings *s) {
    TFXSettings *me = (__bridge TFXSettings *)ctx;
    __block tfx_throttle_cal c;
    tfx_throttle_cal_begin(&c);
    TFXCalPanel *p = [[TFXCalPanel alloc]
        initWithTitle:@"FlightStick Pro"
                 text:@"Move the throttle through its whole travel (on the controller: the right trigger, released "
                      @"and fully pressed)."
              buttons:@[ @"Continue", @"Cancel" ]];
    p.tick = ^NSString * {
      tfx_throttle_cal_step(&c, cal_read, ctx);
      return [NSString stringWithFormat:@"Throttle %4d    lowest %4d  highest %4d", (int)me->_port.st.ring_4[0],
                                        c.y_min, c.y_max];
    };
    [p run];
    tfx_throttle_cal_finish(&c, s); /* (CONFIG ignores the screen's result) */
    return 2;
}

static int ui_tm_info(void *ctx, tfx_config_settings *s) {
    (void)ctx, (void)s;
    alert(@"ThrustMaster W.C.S. Mark II",
          @"Use your W.C.S. II's download software to program its buttons with TFX's keys; it types them as a "
          @"keyboard would. There is nothing to calibrate here.");
    return 2;
}

static int ui_tm_views(void *ctx, tfx_config_settings *s) {
    TFXSettings *me = (__bridge TFXSettings *)ctx;
    __block int32_t loops;
    tfx_hat_guess g = tfx_hat_views_begin(cal_read, ctx, &loops);
    s->hat_m_pos = g.middle; /* (GuessThrustMasterPos_: the middle is the one saved) */
    TFXCalPanel *p = [[TFXCalPanel alloc]
        initWithTitle:@"ThrustMaster Flight Control System"
                 text:[NSString stringWithFormat:@"The hat's middle position is registered (%d). Move the view hat to "
                                                 @"its five locations (on the controller: the d-pad, released in the "
                                                 @"middle); the one found is shown as the game uses it.",
                                                 g.middle]
              buttons:@[ @"Continue", @"Cancel" ]];
    p.tick = ^NSString * {
      tfx_hat_views_step(&loops, cal_read, ctx);
      int32_t at = (int32_t)me->_port.st.ring_4[0], d = g.middle / 11;
      /* the game's own reading (views.c): 0 forward, 1/4 left, 1/2 behind,
       * 3/4 right (CONFIG's screen labels the side two the other way) */
      static NSString *const NAME[] = {@"forward", @"left", @"behind", @"right"};
      const int32_t pos[] = {0, g.middle >> 2, g.middle >> 1, 3 * g.middle >> 2};
      NSString *view = @"middle (no view change)";
      for (int i = 0; i < 4; i++)
          if (at < pos[i] + d && at > pos[i] - d)
              view = NAME[i];
      return [NSString stringWithFormat:@"Hat %4d    %@", at, view];
    };
    [p run];
    return 2;
}

static int ui_tm_rudder(void *ctx, tfx_config_settings *s) {
    TFXSettings *me = (__bridge TFXSettings *)ctx;
    __block tfx_rudder_cal c;
    tfx_rudder_cal_begin(&c, cal_read, ctx);
    TFXCalPanel *p = [[TFXCalPanel alloc]
        initWithTitle:@"ThrustMaster Rudder Control System"
                 text:@"Move the rudder to both ends (on the controller: the right stick, left and right). Then "
                      @"register its centre position: hold Set Centre with the rudder centred."
              buttons:@[ @"Continue", @"Cancel" ]];
    [p hold:@"Set Centre"];
    __weak TFXCalPanel *wp = p;
    p.tick = ^NSString * {
      tfx_rudder_cal_step(&c, cal_read, ctx, wp.held);
      return [NSString stringWithFormat:@"Rudder %4d    left %4d  right %4d  centre %4d", (int)me->_port.st.ring_3[0],
                                        c.x_min, c.x_max, c.x_center];
    };
    [p run];
    tfx_rudder_cal_finish(&c, s);
    return 2;
}

static int ui_stick_a(void *ctx, tfx_config_settings *s) {
    TFXSettings *me = (__bridge TFXSettings *)ctx;
    __block tfx_joy_cal c;
    tfx_joy_cal_begin(&c);
    TFXCalPanel *p = [[TFXCalPanel alloc]
        initWithTitle:@"Calibrate Joystick"
                 text:@"Move the stick (the controller's left stick) to all its extremes. Then register its centre "
                      @"position: let it rest in the middle and press a button."
              buttons:@[ @"Continue", @"Cancel" ]];
    __weak TFXCalPanel *wp = p;
    p.tick = ^NSString * {
      tfx_joy_cal_step(&c, cal_read, ctx);
      if (tfx_joy_cal_center_set(&c))
          [wp say:@""];
      NSString *centre = tfx_joy_cal_center_set(&c)
                             ? [NSString stringWithFormat:@"%4d %4d", c.x_center, c.y_center]
                             : @"not set";
      return [NSString stringWithFormat:@"Stick  %4d %4d\nx      %4d .. %4d\ny      %4d .. %4d\ncentre %@",
                                        (int)me->_port.st.x, (int)me->_port.st.y, c.x_min, c.x_max, c.y_min,
                                        c.y_max, centre];
    };
    p.accept = ^BOOL(NSInteger b) {
      if (b == 0 && !tfx_joy_cal_center_set(&c)) {
          [wp say:@"Please set the centre: let the stick rest and press a button."];
          return NO;
      }
      return YES;
    };
    NSInteger r = [p run];
    tfx_joy_cal_finish(&c, s); /* (on leaving either way, as CONFIG) */
    return r == 0 ? TFX_CFGS_MAIN : TFX_CFGS_EXIT;
}

- (void)calibrate:(id)sender {
    tfx_config_settings before = _m.s;
    _ui = (tfx_joystick_ui){(__bridge void *)self, ui_present, ui_error, ui_ask, ui_flightstick, ui_tm_info,
                            ui_tm_views, ui_tm_rudder, ui_stick_a, 0};
    /* CONFIG's QUIT left it unsaved; Cancel here keeps the settings as before */
    if (tfx_config_joystick(&_m.s, &_ui) != TFX_CFGS_MAIN)
        _m.s = before;
    [self show];
}

/* ---- leaving ---- */

- (void)save:(id)sender {
    switch (tfx_mac_settings_save(&_m, &_f, [self controller])) {
    case TFX_CONFIG_SAVED:
        _saved = YES;
        [NSApp stopModal];
        break;
    case TFX_CONFIG_NEEDS_CALIBRATION: /* (CONFIG goes to the joystick's screen) */
        alert(@"The joystick needs calibrating.",
              @"Joystick control needs a calibrated joystick before the settings can be saved.");
        [self calibrate:nil];
        break;
    default:
        alert(@"The settings could not be saved.",
              [NSString stringWithFormat:@"TFX.CFG could not be written in %s.", _f.store]);
    }
}

- (void)cancel:(id)sender { [NSApp stopModal]; }

/* a DOS TFX.CFG to a folder the user chooses (never the game folder) */
- (void)exportFile:(id)sender {
    char out[2048];
    size_t len;
    tfx_config_settings s = _m.s;
    tfx_config_save_result r = tfx_config_save(&s, [self controller], 1, out, sizeof out, &len);
    if (r == TFX_CONFIG_NEEDS_CALIBRATION) {
        alert(@"The joystick needs calibrating.", @"Joystick control needs a calibrated joystick first.");
        return;
    }
    if (r != TFX_CONFIG_SAVED || len > sizeof out)
        return;
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = @"TFX.CFG";
    panel.message = @"Export the settings as a DOS TFX.CFG (CRLF), for the original game elsewhere.";
    if ([panel runModal] != NSModalResponseOK)
        return;
    char root[TFX_MAC_PATH];
    if (realpath(_f.root, root) && [panel.URL.URLByDeletingLastPathComponent.path.stringByResolvingSymlinksInPath
                                       isEqualToString:[NSString stringWithUTF8String:root]]) {
        alert(@"Not in the game folder.", @"The game folder is never written; choose another folder.");
        return;
    }
    if (![[NSData dataWithBytes:out length:len] writeToURL:panel.URL atomically:YES])
        alert(@"The file could not be written.", panel.URL.path);
}

- (BOOL)run {
    [self build];
    [self show];
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    if (_m.examined) /* CONFIG's first run starts in System Check */
        [self report];
    [NSApp runModalForWindow:_window];
    [_window orderOut:nil];
    return _saved;
}

@end
