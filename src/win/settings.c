#include "win/settings.h"

#include <stdlib.h>
#include <string.h>

#include "config/joycal.h"
#include "config/menu.h"
#include "host/settings.h"
#include "host/sha256.h"
#include "win/theme.h"

#include <windows.h>

/* CONFIG.EXE of the baseline (plan/evidence/reference-manifest.json) and
 * where AboutTFX_'s text lies in it */
#define CONFIG_SIZE 341897
#define CONFIG_SHA "b97c4f32a7cf7183a8978fbb0717f95cbb71fe803cf34362150e8fa020006504"
#define CREDITS_AT 0x4274c
#define CREDITS_LEN 813

enum { ID_STATE = 1000, ID_ROW = 200, ID_SWAP = 300, ID_SWAPNAME, ID_FIELD = 400, ID_CHECK = 500, ID_NOTE = 600,
       ID_READOUT, ID_TEXT };

/* a page: one of CONFIG's screens as a native window */
typedef struct ctl {
    const char *name;
    HWND h;
    int id;
} ctl;
typedef struct view view;
typedef struct page {
    view *v;
    HWND wnd;
    int result; /* the next state; 0 while open */
    int y;
    ctl ctls[40];
    int n;
    void *data;
    int (*command)(struct page *p, int id, int code); /* 1 taken */
    void (*tick)(struct page *p);
    int (*leave)(struct page *p, int state); /* the state, or 0 to stay */
} page;

struct view {
    const tfx_win_settings_host *host;
    HINSTANCE inst;
    HFONT font;
    char ahead[256]; /* the script's next line, read and not used */
    int have_ahead;
    int saved, gus_noted;
    tfx_settings_stick stick;
};

static void logm(view *v, const char *fmt, const char *a) {
    char line[600];
    snprintf(line, sizeof line, fmt, a);
    for (char *c = line; *c; c++) /* one line in the log */
        if (*c == '\n')
            *c = '/';
    if (v->host->log)
        v->host->log(line);
}

static void widen(const char *s, wchar_t *out, size_t cap) {
    size_t i = 0, j = 0;
    for (; s[i] && j + 2 < cap; i++) {
        if (s[i] == '\n')
            out[j++] = L'\r';
        out[j++] = (unsigned char)s[i];
    }
    out[j] = 0;
}

/* ---- the script ---------------------------------------------------------- */

static int script_line(view *v, char *line, size_t cap) {
    if (v->have_ahead) {
        v->have_ahead = 0;
        snprintf(line, cap, "%s", v->ahead);
        return 1;
    }
    while (v->host->script && fgets(line, (int)cap, v->host->script)) {
        line[strcspn(line, "\r\n")] = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p && *p != '#') {
            memmove(line, p, strlen(p) + 1);
            return 1;
        }
    }
    return 0;
}

/* a message: OK; a question: Yes / No (scripted by "answer") */
static int dialog(view *v, const char *text, int question) {
    logm(v, question ? "settings: ask: %s" : "settings: message: %s", text);
    if (v->host->script) { /* a message is only logged; a question takes "answer" */
        char line[256];
        if (!question || !script_line(v, line, sizeof line))
            return 0;
        if (!strncmp(line, "answer ", 7))
            return !strcmp(line + 7, "yes");
        snprintf(v->ahead, sizeof v->ahead, "%s", line), v->have_ahead = 1;
        return 0;
    }
    wchar_t w[1200];
    widen(text, w, 1200);
    return tfx_win_message_box(NULL, w, L"TFX settings", question ? MB_YESNO | MB_ICONQUESTION : MB_OK) == IDYES;
}

/* ---- the page window ----------------------------------------------------- */

static LRESULT CALLBACK page_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    page *p = (page *)GetWindowLongPtrW(w, GWLP_USERDATA);
    switch (m) {
    case WM_COMMAND:
        if (p) {
            int id = LOWORD(wp), code = HIWORD(wp);
            if (p->command && p->command(p, id, code))
                return 0;
            if (id >= ID_STATE && id < ID_STATE + 10 && code == BN_CLICKED) {
                int next = id - ID_STATE;
                p->result = p->leave ? p->leave(p, next) : next;
            }
        }
        return 0;
    case WM_TIMER:
        if (p && p->tick)
            p->tick(p);
        return 0;
    case WM_CLOSE:
        if (p)
            p->result = p->leave ? p->leave(p, TFX_CFGS_EXIT) : TFX_CFGS_EXIT;
        return 0;
    case WM_DRAWITEM: /* the push buttons, in the system's mode (win/theme.h) */
        tfx_win_theme_button((const DRAWITEMSTRUCT *)lp, 0);
        return TRUE;
    case WM_ERASEBKGND: {
        HBRUSH br = tfx_win_theme_color(0, NULL, NULL);
        if (!br)
            break;
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, br);
        return 1;
    }
    case WM_SETTINGCHANGE:
        if (tfx_win_theme_changed(lp))
            tfx_win_theme_refresh(), tfx_win_theme_window(w);
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HBRUSH br = tfx_win_theme_color(m, (HDC)wp, (HWND)lp);
        if (br)
            return (LRESULT)br;
        if (m != WM_CTLCOLORSTATIC)
            break;
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void page_open(page *p, view *v, const char *title, int height) {
    memset(p, 0, sizeof *p);
    p->v = v;
    wchar_t t[120];
    widen(title, t, 120);
    RECT r = {0, 0, 600, height};
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    p->wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TFXSettings", t, WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                             CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, v->inst, NULL);
    SetWindowLongPtrW(p->wnd, GWLP_USERDATA, (LONG_PTR)p);
    p->y = 14;
    logm(v, "settings: page %s", title);
}

static HWND add(page *p, const char *name, const wchar_t *cls, const char *text, DWORD style, int x, int y, int w,
                int h, int id) {
    wchar_t t[1200];
    widen(text ? text : "", t, 1200);
    HWND c = CreateWindowExW(0, cls, t, WS_CHILD | WS_VISIBLE | style, x, y, w, h, p->wnd, (HMENU)(INT_PTR)id,
                             p->v->inst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)p->v->font, TRUE);
    if (p->n < 40)
        p->ctls[p->n++] = (ctl){name, c, id};
    return c;
}
static HWND label(page *p, const char *text, int x, int w, int h) {
    return add(p, NULL, L"STATIC", text, 0, x, p->y, w, h, 0);
}
static HWND button(page *p, const char *name, const char *text, int state, int x) {
    return add(p, name, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, x, p->y, 130, 28, ID_STATE + state);
}
static HWND find_ctl(page *p, const char *name) {
    for (int i = 0; i < p->n; i++)
        if (p->ctls[i].name && !_stricmp(p->ctls[i].name, name))
            return p->ctls[i].h;
    return NULL;
}
static void set_text(HWND h, const char *text) {
    wchar_t t[1200];
    widen(text, t, 1200);
    SetWindowTextW(h, t);
}
static void get_text(HWND h, char *out, int cap) {
    wchar_t t[260];
    GetWindowTextW(h, t, 260);
    WideCharToMultiByte(CP_ACP, 0, t, -1, out, cap, NULL, NULL);
}

static void pump(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* one automation line on the page; 0 when the script has ended */
static int page_script(page *p) {
    view *v = p->v;
    char line[256], a[128], b[128];
    if (!script_line(v, line, sizeof line))
        return 0;
    logm(v, "settings: auto: %s", line);
    HWND h;
    int n;
    if (sscanf(line, "set %127s %127[^\n]", a, b) == 2 && (h = find_ctl(p, a))) {
        int k = (int)SendMessageW(h, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < k; i++) {
            wchar_t item[80];
            char s[80];
            SendMessageW(h, CB_GETLBTEXT, (WPARAM)i, (LPARAM)item);
            WideCharToMultiByte(CP_ACP, 0, item, -1, s, sizeof s, NULL, NULL);
            if (!_stricmp(s, b) || (b[0] >= '0' && b[0] <= '9' && atoi(b) == i + (int)GetWindowLongPtrW(h, GWLP_USERDATA))) {
                SendMessageW(h, CB_SETCURSEL, (WPARAM)i, 0);
                SendMessageW(p->wnd, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), CBN_SELCHANGE), (LPARAM)h);
                return 1;
            }
        }
        logm(v, "settings: auto: no such value: %s", b);
    } else if ((sscanf(line, "field %127s %127[^\n]", a, b) == 2 || sscanf(line, "swapfile %127[^\n]", b) == 1) &&
               (h = find_ctl(p, !strncmp(line, "swapfile", 8) ? "swapfile" : a))) {
        set_text(h, b);
    } else if ((sscanf(line, "check %127s %d", a, &n) == 2 || sscanf(line, "swap %d", &n) == 1) &&
               (h = find_ctl(p, !strncmp(line, "swap ", 5) ? "swap" : a))) {
        if ((SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED) != (n != 0))
            SendMessageW(h, BM_CLICK, 0, 0);
    } else if (sscanf(line, "click %127s", a) == 1 && (h = find_ctl(p, a))) {
        SendMessageW(h, BM_CLICK, 0, 0);
    } else if (sscanf(line, "pass %d", &n) == 1 && p->tick) {
        while (n-- > 0)
            p->tick(p);
    } else if (!v->host->command || !v->host->command(line))
        logm(v, "settings: auto: not on this page: %s", line);
    return 1;
}

static int page_run(page *p) {
    view *v = p->v;
    tfx_win_theme_window(p->wnd);
    if (!v->host->script) {
        ShowWindow(p->wnd, SW_SHOW);
        SetForegroundWindow(p->wnd);
    }
    while (!p->result) {
        if (v->host->script) {
            pump();
            if (!p->result && !page_script(p)) {
                logm(v, "settings: auto: the script ended on a page: %s", "QUIT");
                p->result = p->leave ? p->leave(p, TFX_CFGS_EXIT) : TFX_CFGS_EXIT;
            }
        } else {
            MSG msg;
            if (GetMessageW(&msg, NULL, 0, 0) <= 0) {
                p->result = TFX_CFGS_EXIT;
                break;
            }
            if (!IsDialogMessageW(p->wnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
    KillTimer(p->wnd, 1);
    DestroyWindow(p->wnd);
    pump();
    return p->result;
}

/* ---- main (state 2) ---------------------------------------------------- */

static const char *note_for(const tfx_config_settings *s) {
    switch (s->music) {
    case 1: return "MUSIC SB/CD: the AdLib tunes through the native OPL2; in flight the CD's tracks when its image is found.";
    case 2: return "MUSIC LAPC1: MT-32 music. Windows' synthesizer plays it with General MIDI sounds: name an MT-32 "
                   "(a device or an emulator's MIDI port) with --midi N.";
    case 3: return "MUSIC ADLIB: kept as CONFIG keeps it; the game plays it as SB/CD.";
    case 4: return "MUSIC SCC1: GS music through Windows' MIDI synthesizer (or the device --midi N names).";
    }
    return "MUSIC OFF.";
}

typedef struct main_data {
    tfx_config_settings *s;
} main_data;

static void main_refresh(page *p) {
    main_data *d = p->data;
    EnableWindow(find_ctl(p, "sb"), d->s->gsound == 1 || d->s->gsound == 3);
    set_text(GetDlgItem(p->wnd, ID_NOTE), note_for(d->s));
}

static int main_command(page *p, int id, int code) {
    main_data *d = p->data;
    if (id >= ID_ROW && id < ID_ROW + TFX_SETTINGS_ROWS && code == CBN_SELCHANGE) {
        const tfx_settings_row *r = &tfx_settings_rows[id - ID_ROW];
        HWND h = GetDlgItem(p->wnd, id);
        int i = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (i >= 0 && i < r->count)
            *tfx_settings_field(d->s, r) = r->first + i;
        if (!strcmp(r->key, "SOUNDFX") && d->s->gsound == 4 && !p->v->gus_noted) {
            p->v->gus_noted = 1; /* CONFIG's one-time GRAVIS note, as the native program has it */
            dialog(p->v, "GUS: the UltraSound's samples play through the native mixer; the music plays through "
                         "the device the MUSIC row chooses.", 0);
        }
        main_refresh(p);
        return 1;
    }
    if (id == ID_SWAP && code == BN_CLICKED) {
        d->s->swap_file_on = SendMessageW(GetDlgItem(p->wnd, ID_SWAP), BM_GETCHECK, 0, 0) == BST_CHECKED;
        return 1;
    }
    return 0;
}

static int main_leave(page *p, int state) {
    main_data *d = p->data;
    char name[TFX_CFG_SWAPNAME];
    get_text(GetDlgItem(p->wnd, ID_SWAPNAME), name, sizeof name);
    if (name[0]) /* (ValidateSwapPath_'s check has no native meaning: kept as typed) */
        snprintf(d->s->swap_filename, sizeof d->s->swap_filename, "%s", name);
    return state;
}

static int scr_main(void *ctx, tfx_config_settings *s) {
    view *v = ctx;
    page p;
    main_data d = {s};
    page_open(&p, v, "TFX SETTINGS", 520);
    p.data = &d, p.command = main_command, p.leave = main_leave;
    for (int i = 0; i < TFX_SETTINGS_ROWS; i++) {
        const tfx_settings_row *r = &tfx_settings_rows[i];
        label(&p, r->key, 20, 120, 20);
        HWND c = add(&p, r->key, L"COMBOBOX", NULL, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 150, p.y - 3, 260,
                     200, ID_ROW + i);
        SetWindowLongPtrW(c, GWLP_USERDATA, r->first);
        for (int k = 0; k < r->count; k++) {
            wchar_t w[40];
            widen(r->names[k], w, 40);
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w);
        }
        int value = *tfx_settings_field(s, r);
        if (value >= r->first && value < r->first + r->count)
            SendMessageW(c, CB_SETCURSEL, (WPARAM)(value - r->first), 0);
        else { /* a value CONFIG has no switch for: shown, kept unless changed */
            char t[40];
            snprintf(t, sizeof t, "%d (as read)", (int)value);
            wchar_t w[40];
            widen(t, w, 40);
            SendMessageW(c, CB_SETCURSEL, (WPARAM)SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)w), 0);
        }
        p.y += 34;
    }
    HWND sw = add(&p, "swap", L"BUTTON", "SWAP FILE (DOS only)", WS_TABSTOP | BS_AUTOCHECKBOX, 20, p.y, 180, 22,
                  ID_SWAP);
    SendMessageW(sw, BM_SETCHECK, s->swap_file_on ? BST_CHECKED : BST_UNCHECKED, 0);
    add(&p, "swapfile", L"EDIT", s->swap_filename, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 210, p.y, 200, 22,
        ID_SWAPNAME);
    p.y += 30;
    label(&p, "The swap file and the Sound Blaster's fields are kept for CONFIG.EXE and DOS; the native program does "
              "not use them.",
          20, 560, 34);
    p.y += 40;
    add(&p, NULL, L"STATIC", "", 0, 20, p.y, 560, 50, ID_NOTE);
    p.y += 60;
    button(&p, "save", "SAVE", TFX_CFGS_SAVE, 20);
    button(&p, "quit", "QUIT", TFX_CFGS_EXIT, 160);
    button(&p, "joystick", "CONF JOYSTICK", TFX_CFGS_JOYSTICK, 300);
    button(&p, "sb", "SOUND BLASTER", TFX_CFGS_SOUNDBLASTER, 440);
    p.y += 36;
    button(&p, "examine", "EXAMINE SYSTEM", TFX_CFGS_EXAMINE, 20);
    button(&p, "about", "ABOUT", TFX_CFGS_ABOUT, 160);
    main_refresh(&p);
    return page_run(&p);
}

/* ---- the joystick (state 3) ---------------------------------------------- */

enum { CAL_A, CAL_THROTTLE, CAL_VIEWS, CAL_RUDDER };
typedef struct cal_data {
    tfx_config_settings *s;
    int kind;
    tfx_joy_cal a;
    tfx_throttle_cal t;
    tfx_rudder_cal r;
    tfx_hat_guess hat;
    int32_t loops;
} cal_data;

static void cal_tick(page *p) {
    cal_data *d = p->data;
    view *v = p->v;
    char t[200];
    switch (d->kind) {
    case CAL_A:
        tfx_joy_cal_step(&d->a, tfx_settings_stick_read, &v->stick);
        snprintf(t, sizeof t, "X %d .. %d   Y %d .. %d   centre %d, %d", (int)d->a.x_min, (int)d->a.x_max,
                 (int)d->a.y_min, (int)d->a.y_max, (int)d->a.x_center, (int)d->a.y_center);
        break;
    case CAL_THROTTLE:
        tfx_throttle_cal_step(&d->t, tfx_settings_stick_read, &v->stick);
        snprintf(t, sizeof t, "THROTTLE %d .. %d", (int)d->t.y_min, (int)d->t.y_max);
        break;
    case CAL_VIEWS: {
        tfx_hat_views_step(&d->loops, tfx_settings_stick_read, &v->stick);
        int32_t m = (int32_t)v->stick.st.ring_4[0];
        const char *names[] = {"UP", "DOWN", "RIGHT", "LEFT", "MIDDLE"};
        int32_t at[] = {d->hat.up, d->hat.down, d->hat.right, d->hat.left, d->hat.middle};
        int best = 0;
        for (int i = 1; i < 5; i++)
            if (labs((long)(m - at[i])) < labs((long)(m - at[best])))
                best = i;
        snprintf(t, sizeof t, "HAT %d: %s", (int)m, names[best]);
        break;
    }
    default: {
        int enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 && GetForegroundWindow() == p->wnd;
        tfx_rudder_cal_step(&d->r, tfx_settings_stick_read, &v->stick, enter);
        snprintf(t, sizeof t, "RUDDER %d .. %d   centre %d (Enter sets it)", (int)d->r.x_min, (int)d->r.x_max,
                 (int)d->r.x_center);
    }
    }
    set_text(GetDlgItem(p->wnd, ID_READOUT), t);
}

static int cal_leave(page *p, int state) {
    cal_data *d = p->data;
    if (d->kind == CAL_A && state == TFX_CFGS_MAIN && !tfx_joy_cal_center_set(&d->a)) {
        set_text(GetDlgItem(p->wnd, ID_NOTE), "PLEASE SET CENTER");
        logm(p->v, "settings: %s", "PLEASE SET CENTER");
        return 0;
    }
    switch (d->kind) { /* on CONTINUE or QUIT alike, as the original */
    case CAL_A: tfx_joy_cal_finish(&d->a, d->s); break;
    case CAL_THROTTLE: tfx_throttle_cal_finish(&d->t, d->s); break;
    case CAL_VIEWS: d->s->hat_m_pos = d->hat.middle; break;
    default: tfx_rudder_cal_finish(&d->r, d->s);
    }
    return state;
}

static int calibrate(view *v, tfx_config_settings *s, int kind) {
    static const char *const titles[] = {"JOYSTICK CALIBRATION", "FLIGHTSTICK PRO THROTTLE", "THRUSTMASTER VIEWS",
                                         "THRUSTMASTER RUDDER"};
    static const char *const help[] = {
        "Move the stick to all four limits, then centre it and press a button. Then CONTINUE.",
        "Move the throttle through its whole travel. Then CONTINUE.",
        "Move the hat through its positions; the nearest is shown. Then CONTINUE.",
        "Move the rudder to both limits; centre it and press Enter. Then CONTINUE."};
    page p;
    cal_data d = {.s = s, .kind = kind};
    tfx_settings_stick_begin(&v->stick, (int)s->stick, v->host->stick, NULL);
    /* the ring primed: stick B's positions are its first entry, empty until
     * the fourth read (CONFIG's ring had been filled by earlier reads) */
    for (int i = 0; i < 4; i++)
        tfx_settings_stick_read(&v->stick);
    switch (kind) {
    case CAL_A: tfx_joy_cal_begin(&d.a); break;
    case CAL_THROTTLE: tfx_throttle_cal_begin(&d.t); break;
    case CAL_VIEWS: d.hat = tfx_hat_views_begin(tfx_settings_stick_read, &v->stick, &d.loops); break;
    default: tfx_rudder_cal_begin(&d.r, tfx_settings_stick_read, &v->stick);
    }
    page_open(&p, v, titles[kind], 230);
    p.data = &d, p.tick = cal_tick, p.leave = cal_leave;
    label(&p, help[kind], 20, 560, 40);
    p.y += 50;
    add(&p, NULL, L"STATIC", "", 0, 20, p.y, 560, 22, ID_READOUT);
    p.y += 30;
    add(&p, NULL, L"STATIC", "", 0, 20, p.y, 560, 22, ID_NOTE);
    p.y += 40;
    button(&p, "continue", "CONTINUE", TFX_CFGS_MAIN, 20);
    button(&p, "quit", "QUIT", TFX_CFGS_EXIT, 160);
    if (!v->host->script)
        SetTimer(p.wnd, 1, 15, NULL);
    return page_run(&p);
}

static int joy_present(void *ctx) {
    view *v = ctx;
    tfx_stick_device d = {0};
    return v->host->stick && v->host->stick(NULL, &d);
}
static void joy_error(void *ctx, const char *message) { dialog(ctx, message, 0); }
static int joy_ask(void *ctx, const char *question) {
    char q[120];
    snprintf(q, sizeof q, "Is a %s fitted?", question);
    return dialog(ctx, q, 1);
}
static int joy_flightstick(void *ctx, tfx_config_settings *s) { return calibrate(ctx, s, CAL_THROTTLE); }
static int joy_tm_info(void *ctx, tfx_config_settings *s) {
    (void)s;
    dialog(ctx, "W.C.S. MARK II: its buttons are programmed by the controller's own software (TFX.ADV, the "
                "game's program for it).",
           0);
    return TFX_CFGS_MAIN;
}
static int joy_tm_views(void *ctx, tfx_config_settings *s) { return calibrate(ctx, s, CAL_VIEWS); }
static int joy_tm_rudder(void *ctx, tfx_config_settings *s) { return calibrate(ctx, s, CAL_RUDDER); }
static int joy_stick_a(void *ctx, tfx_config_settings *s) { return calibrate(ctx, s, CAL_A); }

static int scr_joystick(void *ctx, tfx_config_settings *s) {
    tfx_joystick_ui ui = {ctx,         joy_present,  joy_error,     joy_ask,     joy_flightstick,
                          joy_tm_info, joy_tm_views, joy_tm_rudder, joy_stick_a, 0};
    return tfx_config_joystick(s, &ui);
}

/* ---- save (state 4) ------------------------------------------------------ */

static int scr_save(void *ctx, tfx_config_settings *s) {
    view *v = ctx;
    char out[2048];
    size_t n;
    tfx_config_save_result r = tfx_settings_save(s, joy_present(v), out, sizeof out, &n);
    if (r == TFX_CONFIG_NEEDS_CALIBRATION) {
        logm(v, "settings: %s", "the joystick is not calibrated: to the joystick's page");
        return TFX_CFGS_JOYSTICK;
    }
    if (n > sizeof out || !tfx_win_content_write(v->host->content, "TFX.CFG", (const uint8_t *)out, (uint32_t)n)) {
        dialog(v, "COULDN'T CREATE TFX.CFG in the user store.", 0);
        return TFX_CFGS_MAIN;
    }
    v->saved = 1;
    logm(v, "settings: saved TFX.CFG to the user store%s", "");
    return TFX_CFGS_EXIT;
}

/* ---- System Check (state 5) ---------------------------------------------- */

static int scr_examine(void *ctx, tfx_config_settings *s) {
    view *v = ctx;
    tfx_examine_report r;
    int joy = joy_present(v);
    tfx_settings_examine(s, joy, v->host->getenv, NULL, &r);
    char text[900];
    snprintf(text, sizeof text,
             "CONFIG's System Check measured a DOS PC. The native program runs the game at full speed on this "
             "machine, so its recommendations are a fast PC's, applied as CONFIG applies them:\n\n"
             "MEMORY: enough (no swap file, no low-memory mode)\n"
             "JOYSTICK: %s (CONTROL %s)\n"
             "MODEL: %s   DETAIL: %s\n"
             "SOUND: %s\n\n"
             "SAVE keeps them; QUIT leaves the settings as they were.",
             joy ? "found" : "none", joy ? "JOYSTICK" : "KEYBOARD", tfx_settings_rows[2].names[s->this_model & 3],
             tfx_settings_rows[1].names[s->w_detail > 2 ? 2 : s->w_detail < 0 ? 0 : s->w_detail],
             r.blaster_detected ? "Sound Blaster and its music (the native mixer)" : "off");
    page p;
    page_open(&p, v, "SYSTEM CHECK", 330);
    label(&p, text, 20, 560, 220);
    p.y += 240;
    button(&p, "continue", "CONTINUE", TFX_CFGS_MAIN, 20);
    button(&p, "bootdisk", "MAKE BOOT DISK", TFX_CFGS_BOOTDISK, 160);
    button(&p, "quit", "QUIT", TFX_CFGS_EXIT, 300);
    return page_run(&p);
}

/* ---- About (state 6) ----------------------------------------------------- */

int tfx_win_settings_credits(const tfx_win_content *c, char *out, size_t cap) {
    size_t size = 0;
    uint8_t *buf = tfx_win_content_game_load(c, "C:\\TFX\\CONFIG.EXE", &size);
    if (!buf) {
        snprintf(out, cap, "The credits are read from the game folder's CONFIG.EXE, which is missing.");
        return 0;
    }
    int ok = size == CONFIG_SIZE;
    if (ok) {
        tfx_sha256 sha;
        uint8_t digest[32];
        char hex[65];
        tfx_sha256_init(&sha);
        tfx_sha256_add(&sha, buf, CONFIG_SIZE);
        tfx_sha256_done(&sha, digest);
        tfx_sha256_hex(digest, hex);
        ok = !strcmp(hex, CONFIG_SHA);
    }
    if (!ok) {
        free(buf);
        snprintf(out, cap, "The credits are read from the game folder's CONFIG.EXE, which is another edition.");
        return 0;
    }
    size_t n = CREDITS_LEN < cap - 1 ? CREDITS_LEN : cap - 1;
    memcpy(out, buf + CREDITS_AT, n);
    out[n] = 0;
    free(buf);
    while (*out == '\n')
        memmove(out, out + 1, strlen(out));
    return 1;
}

static int scr_about(void *ctx, tfx_config_settings *s) {
    (void)s;
    view *v = ctx;
    char text[CREDITS_LEN + 200];
    int ok = tfx_win_settings_credits(v->host->content, text, sizeof text);
    logm(v, "settings: about: %s", ok ? "the credits read from CONFIG.EXE" : text);
    page p;
    page_open(&p, v, "TACTICAL FIGHTER EXPERIMENT", 420);
    add(&p, NULL, L"EDIT", text, ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER, 20, p.y, 560, 340, ID_TEXT);
    p.y += 356;
    button(&p, "continue", "CONTINUE", TFX_CFGS_MAIN, 20);
    button(&p, "quit", "QUIT", TFX_CFGS_EXIT, 160);
    return page_run(&p);
}

/* ---- the boot disk (state 7): unavailable -------------------------------- */

static int scr_bootdisk(void *ctx, tfx_config_settings *s) {
    (void)s;
    view *v = ctx;
    page p;
    page_open(&p, v, "MAKE BOOT DISK", 200);
    label(&p,
          "Not available. CONFIG made a DOS boot floppy (its CONFIG.SYS and AUTOEXEC.BAT, the mouse and CD-ROM "
          "drivers) to free conventional memory under DOS. The native program needs none of it, and writes no "
          "disk and no system file.",
          20, 560, 90);
    p.y += 110;
    button(&p, "ok", "OK", TFX_CFGS_MAIN, 20);
    button(&p, "quit", "QUIT", TFX_CFGS_EXIT, 160);
    return page_run(&p);
}

/* ---- the Sound Blaster (state 8) ----------------------------------------- */

typedef struct sb_data {
    tfx_config_settings *s;
    tfx_blaster b;
} sb_data;

static int sb_leave(page *p, int state) { /* on CONTINUE or QUIT alike, as the original */
    sb_data *d = p->data;
    char t[64];
    get_text(GetDlgItem(p->wnd, ID_FIELD + 0), t, sizeof t);
    d->b.port = (int32_t)strtoul(t, NULL, 16);
    get_text(GetDlgItem(p->wnd, ID_FIELD + 1), t, sizeof t);
    d->b.irq = atoi(t);
    get_text(GetDlgItem(p->wnd, ID_FIELD + 2), t, sizeof t);
    d->s->irqbase = (int32_t)strtoul(t, NULL, 16);
    int reset = SendMessageW(GetDlgItem(p->wnd, ID_CHECK), BM_GETCHECK, 0, 0) == BST_CHECKED;
    int setvol = SendMessageW(GetDlgItem(p->wnd, ID_CHECK + 1), BM_GETCHECK, 0, 0) == BST_CHECKED;
    d->b.flags = (d->b.flags & ~3) | reset | setvol << 1;
    tfx_blaster_apply(d->s, &d->b);
    logm(p->v, "settings: BLASTER %s", d->s->blaster);
    return state;
}

static int scr_blaster(void *ctx, tfx_config_settings *s) {
    view *v = ctx;
    page p;
    sb_data d = {s, {0}};
    tfx_blaster_parse(s->blaster, &d.b);
    page_open(&p, v, "SOUND BLASTER", 300);
    p.data = &d, p.leave = sb_leave;
    label(&p, "Kept for CONFIG.EXE and DOS: the native sound does not use these.", 20, 560, 22);
    p.y += 34;
    const char *names[] = {"port", "irq", "irqbase"};
    const char *labels[] = {"PORT (hex)", "IRQ", "IRQ BASE (hex; needs 5 free vectors)"};
    char values[3][16];
    snprintf(values[0], 16, "%03hX", (unsigned short)d.b.port);
    snprintf(values[1], 16, "%hd", (short)d.b.irq);
    snprintf(values[2], 16, "%3X", (unsigned)s->irqbase);
    for (int i = 0; i < 3; i++) {
        label(&p, labels[i], 20, 260, 20);
        add(&p, names[i], L"EDIT", values[i], WS_TABSTOP | WS_BORDER, 290, p.y - 3, 100, 22, ID_FIELD + i);
        p.y += 32;
    }
    HWND r = add(&p, "reset", L"BUTTON", "RESET", WS_TABSTOP | BS_AUTOCHECKBOX, 20, p.y, 120, 22, ID_CHECK);
    HWND sv = add(&p, "setvol", L"BUTTON", "SETVOL", WS_TABSTOP | BS_AUTOCHECKBOX, 160, p.y, 120, 22, ID_CHECK + 1);
    SendMessageW(r, BM_SETCHECK, d.b.flags & 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(sv, BM_SETCHECK, d.b.flags & 2 ? BST_CHECKED : BST_UNCHECKED, 0);
    p.y += 50;
    button(&p, "continue", "CONTINUE", TFX_CFGS_MAIN, 20);
    button(&p, "quit", "QUIT", TFX_CFGS_EXIT, 160);
    return page_run(&p);
}

/* ---- the view ---------------------------------------------------------- */

int tfx_win_settings(void *instance, const tfx_win_settings_host *host) {
    view v = {.host = host, .inst = instance};
    NONCLIENTMETRICSW m = {.cbSize = sizeof m};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    v.font = CreateFontIndirectW(&m.lfMessageFont);
    WNDCLASSEXW wc = {.cbSize = sizeof wc};
    wc.lpfnWndProc = page_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"TFXSettings";
    RegisterClassExW(&wc);

    /* the file: the store's, else the game folder's (read only) */
    tfx_config_settings s;
    long len = -1;
    char *file = NULL;
    wchar_t where[300];
    long long size = -1;
    int in_store = 0;
    if (tfx_win_content_locate(host->content, "TFX.CFG", 1, where, 300))
        size = tfx_win_file_size(where), in_store = size >= 0;
    if (size < 0)
        size = tfx_win_content_game_size(host->content, "TFX.CFG");
    if (size >= 0 && size < (1 << 20) && (file = malloc((size_t)size + 1)) &&
        (size == 0 || tfx_win_content_read(host->content, "TFX.CFG", (uint8_t *)file, (uint32_t)size)))
        len = (long)size;
    tfx_settings_load(&s, file, len, host->getenv, NULL);
    free(file);
    logm(&v, "settings: %s",
         len < 0 ? "no TFX.CFG: the first run (System Check)"
         : in_store ? "TFX.CFG read from the user store" : "TFX.CFG read from the game folder");
    tfx_config_screens ui = {{NULL}, &v};
    ui.screen[TFX_CFGS_MAIN] = scr_main, ui.screen[TFX_CFGS_JOYSTICK] = scr_joystick;
    ui.screen[TFX_CFGS_SAVE] = scr_save, ui.screen[TFX_CFGS_EXAMINE] = scr_examine;
    ui.screen[TFX_CFGS_ABOUT] = scr_about, ui.screen[TFX_CFGS_BOOTDISK] = scr_bootdisk;
    ui.screen[TFX_CFGS_SOUNDBLASTER] = scr_blaster;
    tfx_config_menu(&s, &ui, 1000);
    logm(&v, "settings: %s", v.saved ? "closed, saved" : "closed without saving");
    DeleteObject(v.font);
    return v.saved;
}
