/* The settings view (M06): CONFIG's main screen as a native window over
 * the recovered CONFIG (settings.h): the same settings and values, System
 * Check, the joystick calibration flows on the controller, the Sound
 * Blaster's fields (kept for TFX.CFG), About (the user's own CONFIG.EXE's
 * text), the boot disk utility explained as unavailable, Cancel and Save
 * (CONFIG's writer into the user's store). */
#import <AppKit/AppKit.h>

#include "gamepad.h"
#include "settings.h"

@interface TFXSettings : NSObject
/* over the session's files; pad fills the controller's state now and
 * says whether one is connected; cd: an image of the CD is in use */
- (instancetype)initWithFiles:(tfx_mac_files)files pad:(BOOL (^)(tfx_mac_pad *))pad cd:(BOOL)cd;
/* the window run modally; YES when the settings were saved */
- (BOOL)run;
@end
