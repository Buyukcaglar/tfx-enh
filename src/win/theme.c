#include "win/theme.h"

#include <string.h>
#include <wchar.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <vssym32.h>

/* ---- the preference -------------------------------------------------------- */

typedef int(WINAPI *mode_fn)(int);
typedef BOOL(WINAPI *window_fn)(HWND, BOOL);
typedef void(WINAPI *void_fn)(void);
static mode_fn ux_mode;
static window_fn ux_window;
static void_fn ux_refresh, ux_flush;
static int ux_tried;
static DWORD os_build;
static int forced = -1; /* --theme: -1 the system's, 0 light, 1 dark */
static int dark;        /* the mode last read */
static HBRUSH dark_bg, dark_field;

/* an export as its type (through a function pointer of no type, as C allows) */
#define PROC(type, module, name) ((type)(void (*)(void))GetProcAddress(module, name))

static DWORD build_number(void) {
    typedef LONG(WINAPI * version_fn)(OSVERSIONINFOW *);
    OSVERSIONINFOW v = {.dwOSVersionInfoSize = sizeof v};
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    version_fn fn = nt ? PROC(version_fn, nt, "RtlGetVersion") : NULL;
    if (!fn || fn(&v) != 0 || v.dwMajorVersion < 10)
        return 0;
    return v.dwBuildNumber;
}

static void load(void) {
    if (ux_tried)
        return;
    ux_tried = 1;
    os_build = build_number();
    dark_bg = CreateSolidBrush(TFX_DARK_BG);
    dark_field = CreateSolidBrush(TFX_DARK_FIELD);
    HMODULE ux;
    if (os_build < 18362 || !(ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)))
        return;
    ux_mode = PROC(mode_fn, ux, MAKEINTRESOURCEA(135));
    ux_window = PROC(window_fn, ux, MAKEINTRESOURCEA(133));
    ux_refresh = PROC(void_fn, ux, MAKEINTRESOURCEA(104));
    ux_flush = PROC(void_fn, ux, MAKEINTRESOURCEA(136));
    if (!ux_mode || !ux_window || !ux_refresh || !ux_flush)
        ux_mode = NULL, ux_window = NULL, ux_refresh = NULL, ux_flush = NULL;
}

/* 1 when the windows shall be dark: --theme, else the system's app mode
 * (light when it records none) */
static int wanted(void) {
    DWORD v = 1, n = sizeof v;
    if (forced >= 0)
        return forced;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &n) != ERROR_SUCCESS)
        return 0;
    return v == 0;
}

void tfx_win_theme_force(int mode) { forced = mode < 0 ? -1 : mode != 0; }

int tfx_win_theme_refresh(void) {
    load();
    dark = wanted();
    if (ux_mode) {
        ux_mode(dark ? 2 : 3); /* ForceDark, ForceLight: the menus as the title bar */
        ux_refresh();
        ux_flush();
    }
    return dark;
}

int tfx_win_theme_dark(void) { return dark; }

int tfx_win_theme_changed(LPARAM lp) {
    return lp && CompareStringOrdinal((LPCWCH)lp, -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL;
}

/* a control's theme in the mode: the combo boxes' and single-line fields'
 * dark theme, a multi-line field's dark scroll bar, a check box classic on
 * dark (a themed one draws its text black whatever the colour asked) */
static BOOL CALLBACK theme_child(HWND c, LPARAM lp) {
    (void)lp;
    wchar_t cls[32];
    GetClassNameW(c, cls, 32);
    LONG style = GetWindowLongW(c, GWL_STYLE);
    const wchar_t *app = NULL, *ids = NULL;
    if (!_wcsicmp(cls, L"ComboBox"))
        app = dark ? L"DarkMode_CFD" : NULL;
    else if (!_wcsicmp(cls, L"Edit"))
        app = dark ? (style & ES_MULTILINE ? L"DarkMode_Explorer" : L"DarkMode_CFD") : NULL;
    else if (!_wcsicmp(cls, L"Button")) {
        LONG type = style & BS_TYPEMASK;
        if (type == BS_OWNERDRAW) {
            InvalidateRect(c, NULL, TRUE);
            return TRUE;
        }
        if (dark && type != BS_PUSHBUTTON && type != BS_DEFPUSHBUTTON)
            app = L"", ids = L"";
    } else if (!_wcsicmp(cls, L"ScrollBar"))
        app = dark ? L"DarkMode_Explorer" : NULL;
    else
        return TRUE;
    if (ux_window)
        ux_window(c, dark ? TRUE : FALSE);
    SetWindowTheme(c, app, ids);
    SendMessageW(c, WM_THEMECHANGED, 0, 0);
    return TRUE;
}

void tfx_win_theme_window(HWND w) {
    BOOL on = dark ? TRUE : FALSE;
    load();
    if (!w)
        return;
    if (ux_window)
        ux_window(w, on);
    if (os_build >= 17763)
        DwmSetWindowAttribute(w, os_build >= 18985 ? 20 : 19, &on, sizeof on);
    EnumChildWindows(w, theme_child, 0);
    SetWindowPos(w, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RedrawWindow(w, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

HBRUSH tfx_win_theme_color(UINT msg, HDC dc, HWND ctl) {
    load();
    if (!dark)
        return NULL;
    wchar_t cls[16] = L"";
    if (ctl)
        GetClassNameW(ctl, cls, 16);
    int field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || !_wcsicmp(cls, L"Edit");
    if (dc) {
        SetTextColor(dc, ctl && !IsWindowEnabled(ctl) ? RGB(128, 128, 128) : TFX_DARK_TEXT);
        SetBkColor(dc, field ? TFX_DARK_FIELD : TFX_DARK_BG);
    }
    return field ? dark_field : dark_bg;
}

/* Light: the system's themed push button, else the classic one; dark: a
 * flat face with a lighter border, the link's blue when it has the focus */
void tfx_win_theme_button(const DRAWITEMSTRUCT *di, int defaulted) {
    RECT r = di->rcItem;
    wchar_t text[64] = L"";
    int pressed = (di->itemState & ODS_SELECTED) != 0, focus = (di->itemState & ODS_FOCUS) != 0;
    int disabled = (di->itemState & ODS_DISABLED) != 0;
    HFONT font = (HFONT)SendMessageW(di->hwndItem, WM_GETFONT, 0, 0);
    HGDIOBJ old = font ? SelectObject(di->hDC, font) : NULL;
    GetWindowTextW(di->hwndItem, text, 64);
    if (dark) {
        HBRUSH face = CreateSolidBrush(pressed ? RGB(45, 45, 45) : RGB(60, 60, 60));
        HBRUSH edge = CreateSolidBrush(focus ? TFX_DARK_LINK : RGB(100, 100, 100));
        FillRect(di->hDC, &r, face);
        FrameRect(di->hDC, &r, edge);
        DeleteObject(face);
        DeleteObject(edge);
        SetTextColor(di->hDC, disabled ? RGB(128, 128, 128) : TFX_DARK_TEXT);
    } else {
        HTHEME th = OpenThemeData(di->hwndItem, L"BUTTON");
        if (th) {
            FillRect(di->hDC, &r, GetSysColorBrush(COLOR_BTNFACE));
            DrawThemeBackground(th, di->hDC, BP_PUSHBUTTON,
                                disabled  ? PBS_DISABLED
                                : pressed ? PBS_PRESSED
                                          : (defaulted ? PBS_DEFAULTED : PBS_NORMAL),
                                &r, NULL);
            CloseThemeData(th);
        } else
            DrawFrameControl(di->hDC, &r, DFC_BUTTON, DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : 0));
        SetTextColor(di->hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
        if (focus && !(di->itemState & ODS_NOFOCUSRECT)) {
            RECT f = r;
            InflateRect(&f, -3, -3);
            DrawFocusRect(di->hDC, &f);
        }
    }
    SetBkMode(di->hDC, TRANSPARENT);
    if (pressed)
        OffsetRect(&r, 1, 1);
    DrawTextW(di->hDC, text, -1, &r,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | (di->itemState & ODS_NOACCEL ? DT_HIDEPREFIX : 0));
    if (old)
        SelectObject(di->hDC, old);
}

/* ---- in-memory dialogs: a WORD-aligned header, DWORD-aligned items ----------- */

typedef struct tmpl {
    WORD buf[512];
    size_t n;
} tmpl;

static void t_word(tmpl *t, WORD w) {
    if (t->n < 512)
        t->buf[t->n++] = w;
}
static void t_str(tmpl *t, const wchar_t *s) {
    do
        t_word(t, (WORD)*s);
    while (*s++);
}
static void t_head(tmpl *t, DWORD style, int items, int cx, int cy, const wchar_t *title) {
    memset(t, 0, sizeof *t);
    t_word(t, LOWORD(style));
    t_word(t, HIWORD(style));
    t_word(t, 0);
    t_word(t, 0);
    t_word(t, (WORD)items);
    t_word(t, 0);
    t_word(t, 0);
    t_word(t, (WORD)cx);
    t_word(t, (WORD)cy);
    t_word(t, 0); /* no menu */
    t_word(t, 0); /* the dialog class */
    t_str(t, title);
    t_word(t, 9);
    t_str(t, L"Segoe UI");
}
static void t_item(tmpl *t, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls,
                   const wchar_t *text) {
    if (t->n & 1)
        t_word(t, 0);
    t_word(t, LOWORD(style));
    t_word(t, HIWORD(style));
    t_word(t, 0);
    t_word(t, 0); /* dwExtendedStyle */
    t_word(t, (WORD)x);
    t_word(t, (WORD)y);
    t_word(t, (WORD)cx);
    t_word(t, (WORD)cy);
    t_word(t, id);
    t_word(t, 0xFFFF);
    t_word(t, cls);
    t_str(t, text);
    t_word(t, 0); /* no creation data */
}

#define DLG_STYLE (DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU)

/* ---- the message boxes ------------------------------------------------------
 * What MessageBoxW gives: the caption and the text (word-wrapped, no
 * prefix, tabs expanded), the icon of MB_ICONHAND, _QUESTION, _EXCLAMATION
 * or _ASTERISK and its sound, the buttons of MB_OK .. MB_CANCELTRYCONTINUE
 * with Windows' own labels, right-aligned, MB_DEFBUTTONn the default, the
 * answer IDOK .. IDCONTINUE, Escape and the close box answering Cancel (OK
 * in an MB_OK box; neither in MB_YESNO and MB_ABORTRETRYIGNORE boxes,
 * whose close box is grayed), the text's area above a band with the
 * buttons, centred on the owner's monitor. The default follows the focus.
 * Any other flag, or a dialog that cannot be made: MessageBoxW. */
#define ID_BOX_ICON 301
#define ID_BOX_TEXT 302

typedef struct box_kind {
    int n, ids[3], cancel; /* cancel: Escape's and the close box's answer, 0 neither */
} box_kind;

static const box_kind kinds[7] = {
    {1, {IDOK}, IDOK},
    {2, {IDOK, IDCANCEL}, IDCANCEL},
    {3, {IDABORT, IDRETRY, IDIGNORE}, 0},
    {3, {IDYES, IDNO, IDCANCEL}, IDCANCEL},
    {2, {IDYES, IDNO}, 0},
    {2, {IDRETRY, IDCANCEL}, IDCANCEL},
    {3, {IDCANCEL, IDTRYAGAIN, IDCONTINUE}, IDCANCEL},
};

typedef struct box {
    const wchar_t *text, *caption;
    const box_kind *kind;
    int def, icon_kind;
    HICON icon;
    int band; /* the button band's top (client y) */
} box;

static int box_supported(unsigned type) {
    return (type & MB_TYPEMASK) <= MB_CANCELTRYCONTINUE && (type & MB_ICONMASK) <= MB_ICONASTERISK &&
           (type & MB_DEFMASK) <= MB_DEFBUTTON4 &&
           !(type & ~(unsigned)(MB_TYPEMASK | MB_ICONMASK | MB_DEFMASK | MB_TASKMODAL));
}

/* a button's label: Windows' own (MB_GetString), else English */
static const wchar_t *box_label(int id) {
    typedef LPCWSTR(WINAPI * mb_string_fn)(UINT);
    static mb_string_fn fn;
    static int tried;
    if (!tried) {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        tried = 1;
        fn = u ? PROC(mb_string_fn, u, "MB_GetString") : NULL;
    }
    const wchar_t *s = fn && id >= IDOK && id <= IDCONTINUE ? fn((UINT)(id - 1)) : NULL;
    if (s && s[0] && wcslen(s) < 32)
        return s;
    switch (id) {
    case IDOK: return L"OK";
    case IDCANCEL: return L"Cancel";
    case IDABORT: return L"&Abort";
    case IDRETRY: return L"&Retry";
    case IDIGNORE: return L"&Ignore";
    case IDYES: return L"&Yes";
    case IDNO: return L"&No";
    case IDTRYAGAIN: return L"&Try Again";
    default: return L"&Continue";
    }
}

static int dlu(HWND dlg, int n, int vertical) {
    RECT r = {0, 0, vertical ? 0 : n, vertical ? n : 0};
    MapDialogRect(dlg, &r);
    return vertical ? r.bottom : r.right;
}

static void measure(HDC dc, const wchar_t *s, UINT fmt, int maxw, int *w, int *h) {
    RECT r = {0, 0, maxw, 0};
    if (!dc || !DrawTextW(dc, s, -1, &r, DT_CALCRECT | fmt))
        r.right = r.bottom = 0;
    *w = r.right, *h = r.bottom;
}

/* MessageBoxW's arrangement in dialog units: a 10-unit margin; the icon at
 * the top left, the text 8 units to its right (centred on a taller icon);
 * below, a band with the buttons, at least 50 x 14 units, 6 apart, right-
 * aligned, 7 units from the band's edges; at least as wide as the caption;
 * lines at most 260 units wide; centred in the work area */
static void box_layout(HWND dlg, box *b) {
    UINT dpi = GetDpiForWindow(dlg);
    int mx = dlu(dlg, 10, 0), my = dlu(dlg, 10, 1), bh = dlu(dlg, 14, 1), gap = dlu(dlg, 6, 0);
    int pad = dlu(dlg, 7, 1), ip = b->icon ? GetSystemMetricsForDpi(SM_CXICON, dpi) : 0;
    int maxw = dlu(dlg, 260, 0), bw[3], row = 0, capw = 0, tw, th;
    MONITORINFO mi = {.cbSize = sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(dlg, MONITOR_DEFAULTTOPRIMARY), &mi))
        SetRect(&mi.rcWork, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (maxw > (mi.rcWork.right - mi.rcWork.left) * 3 / 4)
        maxw = (mi.rcWork.right - mi.rcWork.left) * 3 / 4;
    HDC dc = GetDC(dlg);
    HFONT f = (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0);
    HGDIOBJ old = dc && f ? SelectObject(dc, f) : NULL;
    measure(dc, b->text, DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX | DT_EDITCONTROL, maxw, &tw, &th);
    for (int i = 0; i < b->kind->n; i++) {
        int lw, lh;
        measure(dc, box_label(b->kind->ids[i]), DT_SINGLELINE, 0, &lw, &lh);
        bw[i] = lw + dlu(dlg, 16, 0);
        if (bw[i] < dlu(dlg, 50, 0))
            bw[i] = dlu(dlg, 50, 0);
        row += bw[i] + (i ? gap : 0);
    }
    NONCLIENTMETRICSW ncm = {.cbSize = sizeof ncm}; /* the caption in its font, with the close box */
    if (dc && SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi)) {
        HFONT cf = CreateFontIndirectW(&ncm.lfCaptionFont);
        if (cf) {
            int lh;
            HGDIOBJ prev = SelectObject(dc, cf);
            measure(dc, b->caption, DT_SINGLELINE | DT_NOPREFIX, 0, &capw, &lh);
            SelectObject(dc, prev);
            DeleteObject(cf);
            capw += 2 * GetSystemMetricsForDpi(SM_CXSIZE, dpi) + mx;
        }
    }
    if (old)
        SelectObject(dc, old);
    if (dc)
        ReleaseDC(dlg, dc);
    int tl = mx + (ip ? ip + dlu(dlg, 8, 0) : 0), ch = th > ip ? th : ip, cw = tl + tw + mx;
    if (cw < 2 * mx + row)
        cw = 2 * mx + row;
    if (cw < capw)
        cw = capw;
    if (ip) {
        HWND ic = GetDlgItem(dlg, ID_BOX_ICON);
        SendMessageW(ic, STM_SETICON, (WPARAM)b->icon, 0);
        SetWindowPos(ic, NULL, mx, my, ip, ip, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(ic, SW_SHOWNA);
    }
    SetWindowPos(GetDlgItem(dlg, ID_BOX_TEXT), NULL, tl, my + (th < ip ? (ip - th) / 2 : 0), tw, th,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    int x = cw - mx - row;
    for (int i = 0; i < b->kind->n; i++) {
        SetWindowPos(GetDlgItem(dlg, b->kind->ids[i]), NULL, x, my + ch + my + pad, bw[i], bh,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        x += bw[i] + gap;
    }
    b->band = my + ch + my;
    RECT wr = {0, 0, cw, b->band + pad + bh + pad};
    AdjustWindowRectExForDpi(&wr, (DWORD)GetWindowLongPtrW(dlg, GWL_STYLE), FALSE,
                             (DWORD)GetWindowLongPtrW(dlg, GWL_EXSTYLE), dpi);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    int mw = mi.rcWork.right - mi.rcWork.left, mh = mi.rcWork.bottom - mi.rcWork.top;
    SetWindowPos(dlg, NULL, mi.rcWork.left + (mw > ww ? (mw - ww) / 2 : 0), mi.rcWork.top + (mh > wh ? (mh - wh) / 2 : 0),
                 ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

static INT_PTR CALLBACK box_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    box *b = (box *)GetWindowLongPtrW(dlg, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG:
        b = (box *)lp;
        SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)b);
        if (b->icon_kind) /* IDI_HAND, _QUESTION, _EXCLAMATION, _ASTERISK */
            b->icon = LoadIconW(NULL, MAKEINTRESOURCEW(32512 + b->icon_kind));
        SetWindowTextW(dlg, b->caption);
        SetDlgItemTextW(dlg, ID_BOX_TEXT, b->text);
        for (int i = 0; i < b->kind->n; i++)
            SetDlgItemTextW(dlg, b->kind->ids[i], box_label(b->kind->ids[i]));
        if (!b->kind->cancel) /* no Cancel answer: the close box grayed */
            EnableMenuItem(GetSystemMenu(dlg, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        tfx_win_theme_window(dlg);
        box_layout(dlg, b);
        SendMessageW(dlg, DM_SETDEFID, (WPARAM)b->kind->ids[b->def], 0);
        SetFocus(GetDlgItem(dlg, b->kind->ids[b->def]));
        return FALSE; /* the focus was set */
    case WM_SETTINGCHANGE:
        if (b && tfx_win_theme_changed(lp))
            tfx_win_theme_refresh(), tfx_win_theme_window(dlg);
        break;
    case WM_DRAWITEM:
        if (b && wp != ID_BOX_ICON && wp != ID_BOX_TEXT) {
            const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)lp;
            LRESULT def = SendMessageW(dlg, DM_GETDEFID, 0, 0);
            int id = HIWORD(def) == DC_HASDEFID ? (int)LOWORD(def) : b->kind->ids[b->def];
            tfx_win_theme_button(di, (int)di->CtlID == id);
            return TRUE;
        }
        break;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: { /* the text's area: its background, the icon and the text */
        HBRUSH br = tfx_win_theme_color(WM_CTLCOLORDLG, (HDC)wp, NULL);
        if (br)
            return (INT_PTR)br;
        SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
        return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_PAINT: /* the button band below the text's area */
        if (b) {
            PAINTSTRUCT ps;
            RECT cr;
            HDC dc = BeginPaint(dlg, &ps);
            if (dc && GetClientRect(dlg, &cr)) {
                cr.top = b->band;
                HBRUSH band = tfx_win_theme_color(WM_CTLCOLOREDIT, NULL, NULL);
                FillRect(dc, &cr, band ? band : GetSysColorBrush(COLOR_BTNFACE));
            }
            EndPaint(dlg, &ps);
            return TRUE;
        }
        break;
    case WM_COMMAND: {
        int id = LOWORD(wp), i;
        if (!b)
            break;
        for (i = 0; i < b->kind->n && id != b->kind->ids[i]; i++)
            ;
        if (i < b->kind->n && HIWORD(wp) == BN_SETFOCUS) { /* the default follows the focus */
            SendMessageW(dlg, DM_SETDEFID, (WPARAM)id, 0);
            for (int j = 0; j < b->kind->n; j++)
                InvalidateRect(GetDlgItem(dlg, b->kind->ids[j]), NULL, TRUE);
            return TRUE;
        }
        if (HIWORD(wp) != BN_CLICKED)
            break;
        if (i < b->kind->n)
            EndDialog(dlg, id);
        else if (id == IDCANCEL && b->kind->cancel)
            EndDialog(dlg, b->kind->cancel); /* Escape, the close box */
        return TRUE;
    }
    }
    return FALSE;
}

int tfx_win_message_box(HWND owner, const wchar_t *text, const wchar_t *caption, unsigned type) {
    const wchar_t *tx = text ? text : L"", *cp = caption ? caption : L"";
    if (!box_supported(type))
        return MessageBoxW(owner, tx, cp, type);
    if (!ux_tried)
        tfx_win_theme_refresh();
    box b = {.text = tx, .caption = cp, .kind = &kinds[type & MB_TYPEMASK], .def = (int)((type & MB_DEFMASK) >> 8),
             .icon_kind = (int)((type & MB_ICONMASK) >> 4)};
    if (b.def >= b.kind->n)
        b.def = 0;
    tmpl t;
    t_head(&t, DLG_STYLE, 2 + b.kind->n, 200, 60, L""); /* laid out again in WM_INITDIALOG */
    t_item(&t, WS_CHILD | SS_ICON, 0, 0, 1, 1, ID_BOX_ICON, 0x0082, L"");
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_EDITCONTROL, 0, 0, 1, 1, ID_BOX_TEXT, 0x0082, L"");
    for (int i = 0; i < b.kind->n; i++) /* owner-drawn, BS_NOTIFY for BN_SETFOCUS */
        t_item(&t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == 0 ? WS_GROUP : 0) | BS_OWNERDRAW | BS_NOTIFY, 0, 0, 1,
               1, (WORD)b.kind->ids[i], 0x0080, L"");
    if (type & MB_ICONMASK)
        MessageBeep(type & MB_ICONMASK);
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)t.buf, owner, box_proc, (LPARAM)&b);
    return r > 0 ? (int)r : MessageBoxW(owner, tx, cp, type);
}

/* ---- the About box ----------------------------------------------------------
 * The window menu's "About..." (above Close), laid out as the Civ64 port's:
 * the icon, the user's two lines, the link (the project's repository,
 * opened in the default browser), the licence of the program's own code
 * with the note that the original game is not part of it, and OK. */
#define TFX_VERSION L"0.1" /* (the macOS bundle's short version) */
#define ID_ABOUT_ICON 201
#define ID_ABOUT_LINK 202
#define ID_ABOUT_LICENSE 203
#define ABOUT_URL L"https://github.com/Buyukcaglar/tfx-enh" /* (the public repository: TFX Enhanced) */

static const wchar_t about_title[] = L"About TFX v" TFX_VERSION;
static const wchar_t license[] =
    L"MIT License\r\n\r\n"
    L"Copyright (c) 2026 Onur BUYUKCAGLAR\r\n\r\n"
    L"Permission is hereby granted, free of charge, to any person obtaining a copy of this "
    L"software and associated documentation files (the \"Software\"), to deal in the Software "
    L"without restriction, including without limitation the rights to use, copy, modify, merge, "
    L"publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons "
    L"to whom the Software is furnished to do so, subject to the following conditions:\r\n\r\n"
    L"The above copyright notice and this permission notice shall be included in all copies or "
    L"substantial portions of the Software.\r\n\r\n"
    L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, "
    L"INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR "
    L"PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE "
    L"FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR "
    L"OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER "
    L"DEALINGS IN THE SOFTWARE.\r\n\r\n"
    L"The licence covers this port's own code (TFX for Windows). TFX, its program and its data "
    L"belong to their owners; they are not part of this software, which needs a copy of the "
    L"original game to run.";

static INT_PTR CALLBACK about_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        SendDlgItemMessageW(dlg, ID_ABOUT_ICON, STM_SETICON, (WPARAM)(icon ? icon : LoadIconW(NULL, IDI_APPLICATION)),
                            0);
        SetDlgItemTextW(dlg, ID_ABOUT_LICENSE, license);
        tfx_win_theme_window(dlg);
        SetFocus(GetDlgItem(dlg, IDOK));
        return FALSE; /* the focus was set */
    }
    case WM_SETTINGCHANGE:
        if (tfx_win_theme_changed(lp))
            tfx_win_theme_refresh(), tfx_win_theme_window(dlg);
        break;
    case WM_DRAWITEM:
        if (wp == IDOK) {
            tfx_win_theme_button((const DRAWITEMSTRUCT *)lp, 1);
            return TRUE;
        }
        break;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        HBRUSH br = tfx_win_theme_color(msg, (HDC)wp, (HWND)lp);
        if ((HWND)lp == GetDlgItem(dlg, ID_ABOUT_LINK)) {
            SetTextColor((HDC)wp, br ? TFX_DARK_LINK : RGB(0, 0, 204)); /* a link's blue */
            SetBkMode((HDC)wp, TRANSPARENT);
            return (INT_PTR)(br ? br : GetSysColorBrush(COLOR_BTNFACE));
        }
        if (br)
            return (INT_PTR)br;
        break;
    }
    case WM_SETCURSOR:
        if ((HWND)wp == GetDlgItem(dlg, ID_ABOUT_LINK)) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_ABOUT_LINK && HIWORD(wp) == STN_CLICKED) {
            ShellExecuteW(dlg, L"open", ABOUT_URL, NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            EndDialog(dlg, 1);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void tfx_win_about(HWND owner) {
    tmpl t;
    if (!ux_tried)
        tfx_win_theme_refresh();
    t_head(&t, DLG_STYLE, 7, 260, 196, about_title);
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_ICON, 10, 10, 21, 20, ID_ABOUT_ICON, 0x0082, L"");
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT, 42, 10, 208, 9, 0xFFFF, 0x0082, L"64bit version by Onur BUYUKCAGLAR");
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT, 42, 21, 208, 9, 0xFFFF, 0x0082, L"with Anthropic Claude AI assistance");
    t_item(&t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_LEFT | SS_NOTIFY, 42, 32, 208, 9, ID_ABOUT_LINK, 0x0082,
           ABOUT_URL);
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 48, 240, 9, 0xFFFF, 0x0082, L"Licenses:");
    t_item(&t, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 10, 59,
           240, 108, ID_ABOUT_LICENSE, 0x0081, L"");
    /* owner-drawn; Enter still chooses it (the dialog manager's default is IDOK) */
    t_item(&t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 200, 174, 50, 15, IDOK, 0x0080, L"OK");
    DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)t.buf, owner, about_proc, 0);
}
