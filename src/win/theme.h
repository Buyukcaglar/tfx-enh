/* The system's colour preference on the program's own windows (the user's
 * request of 31 October 2026): the game's window, the settings view, the
 * copy's progress window, the message boxes and the About box are dark
 * when the system's app mode is dark (HKCU ...\Themes\Personalize,
 * AppsUseLightTheme 0) and light otherwise, and a change of the preference
 * (WM_SETTINGCHANGE "ImmersiveColorSet") applies at once. The title bars
 * take DWM's documented DWMWA_USE_IMMERSIVE_DARK_MODE (20; 19 before
 * Windows 10 build 18985); the menus uxtheme's undocumented ordinals (135
 * SetPreferredAppMode, 133 AllowDarkModeForWindow, 104
 * RefreshImmersiveColorPolicyState, 136 FlushMenuThemes), only from build
 * 18362 on and only when all four are exported, else the menus stay
 * light. Push buttons are drawn by the program (no theme of this Windows
 * draws a dark one). The game's picture is never changed. As the Civ64
 * port's host does (its F108, F109 and UX4). */
#ifndef TFX_WIN_THEME_H
#define TFX_WIN_THEME_H

#include <windows.h>

#define TFX_DARK_BG RGB(32, 32, 32)     /* a dark window's background */
#define TFX_DARK_FIELD RGB(43, 43, 43)  /* a field's, a button band's */
#define TFX_DARK_TEXT RGB(255, 255, 255)
#define TFX_DARK_LINK RGB(96, 205, 255) /* a link's blue on dark */

/* -1 the system's preference (the default), 0 light, 1 dark (--theme) */
void tfx_win_theme_force(int mode);
/* the preference read again and the menus' mode set from it; 1 dark */
int tfx_win_theme_refresh(void);
/* the mode last read (tfx_win_theme_refresh) */
int tfx_win_theme_dark(void);
/* 1 for the WM_SETTINGCHANGE that reports a change of the preference */
int tfx_win_theme_changed(LPARAM lp);
/* a top-level window in the mode last read: its title bar, its controls'
 * themes, drawn again */
void tfx_win_theme_window(HWND w);
/* WM_CTLCOLOR* (and WM_ERASEBKGND's brush: msg 0) of a window in the
 * mode: the brush with the DC's colours set, NULL for the default */
HBRUSH tfx_win_theme_color(UINT msg, HDC dc, HWND ctl);
/* an owner-drawn push button in the mode (defaulted: the default's look) */
void tfx_win_theme_button(const DRAWITEMSTRUCT *di, int defaulted);

/* MessageBoxW in the mode (the types MB_OK .. MB_CANCELTRYCONTINUE, the
 * four icons, MB_DEFBUTTONn; else MessageBoxW itself) */
int tfx_win_message_box(HWND owner, const wchar_t *text, const wchar_t *caption, unsigned type);

/* the About box, modal over owner */
void tfx_win_about(HWND owner);

#endif
