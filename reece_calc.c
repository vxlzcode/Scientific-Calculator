/* REECE Scientific Calculator v2.0 — native GTK3 edition
 * Professional dark UI, scientific parser, history, DEG/RAD.
 * Build: see build.sh (gcc + system GTK3, dynamically linked, <1 MB).
 * Modes: GUI (default) | --eval "expr" [--rad] | --selftest | --smoke [png]
 * Parser: hand-written recursive descent, no eval/system.
 */
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

#define APP_NAME "Reece Scientific Calculator"
#define BRAND    "REECE"
#define VERSION  "v2.0"

/* ================= parser ================= */

static const char *p;
static char errmsg[128];
static int deg_mode = 1;
static double ans_val = 0.0;

static void fail(const char *msg) {
    if (!errmsg[0]) snprintf(errmsg, sizeof errmsg, "%s", msg);
}
static void failf(const char *fmt, ...) {
    if (!errmsg[0]) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(errmsg, sizeof errmsg, fmt, ap);
        va_end(ap);
    }
}
static char peek(void) {
    while (*p && isspace((unsigned char)*p)) p++;
    return *p;
}
static int take(char ch) {
    if (peek() == ch) { p++; return 1; }
    return 0;
}
static double parse_expr(void);

static double apply_func(const char *name, double x) {
    double r;
    if (!strcmp(name, "sin")) return sin(deg_mode ? x * M_PI / 180.0 : x);
    if (!strcmp(name, "cos")) return cos(deg_mode ? x * M_PI / 180.0 : x);
    if (!strcmp(name, "tan")) {
        if (deg_mode && fabs(fmod(x, 180.0)) == 90.0)
            fail("tan undefined at 90° + k·180°");
        return tan(deg_mode ? x * M_PI / 180.0 : x);
    }
    if (!strcmp(name, "asin") || !strcmp(name, "acos")) {
        if (x < -1.0 || x > 1.0) failf("%s domain is [-1, 1]", name);
        r = !strcmp(name, "asin") ? asin(x) : acos(x);
        return deg_mode ? r * 180.0 / M_PI : r;
    }
    if (!strcmp(name, "atan")) {
        r = atan(x);
        return deg_mode ? r * 180.0 / M_PI : r;
    }
    if (!strcmp(name, "ln")) {
        if (x <= 0) fail("ln needs a positive argument");
        return log(x);
    }
    if (!strcmp(name, "log")) {
        if (x <= 0) fail("log needs a positive argument");
        return log10(x);
    }
    if (!strcmp(name, "sqrt")) {
        if (x < 0) fail("sqrt of a negative number");
        return sqrt(x);
    }
    if (!strcmp(name, "abs")) return fabs(x);
    if (!strcmp(name, "exp")) {
        errno = 0;
        r = exp(x);
        if (errno == ERANGE || !isfinite(r)) fail("exp out of range");
        return r;
    }
    failf("unknown function '%s'", name);
    return NAN;
}

static double parse_atom(void) {
    char c = peek();
    if (!c) { fail("unexpected end of expression"); return NAN; }
    if (c == '(') {
        p++;
        double v = parse_expr();
        if (!take(')')) fail("missing ')'");
        return v;
    }
    if (isdigit((unsigned char)c) || c == '.') {
        char *end;
        double v = strtod(p, &end);
        if (end == p) { fail("invalid number"); return NAN; }
        p = end;
        return v;
    }
    if (isalpha((unsigned char)c)) {
        char name[32];
        size_t n = 0;
        while (isalpha((unsigned char)*p) && n < sizeof name - 1)
            name[n++] = (char)tolower((unsigned char)*p++);
        name[n] = '\0';
        if (!strcmp(name, "pi")) return M_PI;
        if (!strcmp(name, "e"))  return M_E;
        if (!strcmp(name, "ans")) return ans_val;
        static const char *funcs[] = {"sin","cos","tan","asin","acos","atan",
                                      "ln","log","sqrt","abs","exp",NULL};
        for (int i = 0; funcs[i]; i++) {
            if (!strcmp(name, funcs[i])) {
                if (!take('(')) failf("%s needs parentheses, e.g. sin(30)", name);
                double arg = parse_expr();
                if (!take(')')) fail("missing ')'");
                return apply_func(name, arg);
            }
        }
        failf("unknown name '%s'", name);
        return NAN;
    }
    failf("unexpected character '%c'", c);
    return NAN;
}

static double parse_postfix(void) {
    double v = parse_atom();
    while (take('!')) {
        if (v < 0 || v != floor(v)) { fail("factorial needs a non-negative integer"); return NAN; }
        if (v > 170) { fail("factorial too large (max 170)"); return NAN; }
        v = tgamma(v + 1.0);
    }
    return v;
}

static double parse_unary(void);

static double parse_power(void) {
    double base = parse_postfix();
    if (take('^')) {
        double ex = parse_unary();   /* right-associative; allows 2^-3 */
        errno = 0;
        double r = pow(base, ex);
        if (errno == ERANGE || !isfinite(r)) {
            if (isnan(r) && base < 0 && ex != floor(ex))
                fail("complex result not supported");
            else fail("power out of range");
            return NAN;
        }
        return r;
    }
    return base;
}

static double parse_unary(void) {
    if (take('+')) return parse_unary();
    if (take('-')) return -parse_unary();
    return parse_power();
}

static double parse_term(void) {
    double v = parse_unary();
    for (;;) {
        if (take('*')) v *= parse_unary();
        else if (take('/')) {
            double d = parse_unary();
            if (d == 0) { fail("division by zero"); return NAN; }
            v /= d;
        } else if (take('%')) {
            double d = parse_unary();
            if (d == 0) { fail("modulo by zero"); return NAN; }
            v = fmod(v, d);
        } else return v;
    }
}

static double parse_expr(void) {
    double v = parse_term();
    for (;;) {
        if (take('+')) v += parse_term();
        else if (take('-')) v -= parse_term();
        else return v;
    }
}

/* Returns 1 on success (*out set), 0 on error (errmsg set). */
static int evaluate(const char *s, double *out) {
    p = s;
    errmsg[0] = '\0';
    double v = parse_expr();
    if (!errmsg[0] && peek() != '\0')
        failf("unexpected character '%c'", peek());
    if (!errmsg[0] && (!isfinite(v)))
        fail("result is not a finite number");
    if (errmsg[0]) return 0;
    *out = v;
    return 1;
}

static void fmt_result(double v, char *buf, size_t n) {
    if (v == floor(v) && fabs(v) < 1e15) snprintf(buf, n, "%.0f", v);
    else snprintf(buf, n, "%.12g", v);
}

/* ================= GUI ================= */

static const char *CSS =
"* { outline: none; }"
"window, .root { background-color: #0b0d12; }"
"label { color: #f2f4f8; }"
".wordmark { font-family: 'JetBrainsMono Nerd Font', monospace; font-size: 16px;"
"  font-weight: 800; letter-spacing: 3px; color: #f2f4f8; }"
".overline { font-family: 'DejaVu Sans', sans-serif; font-size: 8px;"
"  letter-spacing: 2px; color: #5c6470; }"
".logo { background-color: #e8a33d; color: #14161c;"
"  font-family: 'JetBrainsMono Nerd Font', monospace; font-size: 16px;"
"  font-weight: 800; border-radius: 9px; padding: 4px 9px; }"
".card { background-color: #12151d; border: 1px solid #232836; border-radius: 14px; }"
".histline { font-family: 'DejaVu Sans', sans-serif; font-size: 11px; color: #5c6470; }"
"entry.display { background-color: #0d1017; background-image: none; color: #f2f4f8;"
"  caret-color: #e8a33d; font-family: 'JetBrainsMono Nerd Font', monospace;"
"  font-size: 26px; border: none; border-radius: 10px; padding: 12px 14px;"
"  margin: 0; box-shadow: none; }"
"entry.display selection { background-color: #3a3320; color: #f5b841; }"
"button.key { background-image: none; background-color: #1a1e28; color: #f2f4f8;"
"  font-family: 'JetBrainsMono Nerd Font', monospace; font-size: 16px;"
"  font-weight: 500; border: 1px solid #262c3a; border-radius: 11px;"
"  box-shadow: none; text-shadow: none; padding: 0; margin: 0;"
"  min-height: 0; min-width: 0;"
"  transition: background-color 120ms ease, border-color 120ms ease; }"
"button.key:hover { background-color: #232836; border-color: #333b4e; }"
"button.key:active { background-color: #141821; }"
"button.sci { background-color: #141821; color: #9aa3b2; font-size: 12px;"
"  border-color: #1a1f2b; }"
"button.sci:hover { background-color: #1c212c; border-color: #262c3a; color: #d0d6e0; }"
"button.sci:active { background-color: #10131a; }"
"button.op { background-color: #232836; color: #f0b03f; border-color: #2d3446; }"
"button.op:hover { background-color: #2d3446; border-color: #3a4358; }"
"button.op:active { background-color: #1c2130; }"
"button.danger { color: #ff6b6b; }"
"button.danger:hover { color: #ff8787; }"
"button.eq { background-color: #e8a33d; color: #14161c; font-size: 22px;"
"  font-weight: 800; border: none; }"
"button.eq:hover { background-color: #f5b841; }"
"button.eq:active { background-color: #cf8f2e; }"
".segbox { background-color: #12151d; border: 1px solid #232836;"
"  border-radius: 999px; padding: 2px; }"
"button.seg { background-image: none; background-color: transparent; color: #5c6470;"
"  border: none; border-radius: 999px; box-shadow: none;"
"  font-family: 'JetBrainsMono Nerd Font', monospace; font-size: 11px;"
"  font-weight: 700; letter-spacing: 1px; padding: 4px 13px; margin: 0;"
"  min-height: 0; min-width: 0;"
"  transition: background-color 120ms ease, color 120ms ease; }"
"button.seg:hover { color: #9aa3b2; }"
"button.seg.active { background-color: #2a2413; color: #e8a33d; }"
"button.seg.active:hover { color: #f5b841; }"
"button.ghost { background-image: none; background-color: transparent; color: #5c6470;"
"  border: 1px solid #232836; border-radius: 9px; box-shadow: none; text-shadow: none;"
"  font-family: 'DejaVu Sans', sans-serif; font-size: 12px; padding: 4px 10px;"
"  margin: 0; min-height: 0; min-width: 0; }"
"button.ghost:hover { background-color: #1a1e28; color: #9aa3b2; }"
"button.ghost:active { background-color: #141821; }"
".histhead { font-family: 'DejaVu Sans', sans-serif; font-size: 9px;"
"  letter-spacing: 2px; color: #5c6470; }"
"listbox.history { background-color: transparent; }"
"listbox.history row { background-color: transparent; border-radius: 8px;"
"  padding: 5px 10px; margin: 1px 0; }"
"listbox.history row:hover { background-color: #1a1e28; }"
"listbox.history row:selected { background-color: #2a2413; }"
".hexpr { font-family: 'JetBrainsMono Nerd Font', monospace; font-size: 11px;"
"  color: #9aa3b2; }"
".hres { font-family: 'JetBrainsMono Nerd Font', monospace; font-size: 12px;"
"  font-weight: 700; color: #e8a33d; }"
".status { color: #ff6b6b; font-family: 'DejaVu Sans', sans-serif; font-size: 11px; }"
".hint { color: #5c6470; font-family: 'DejaVu Sans', sans-serif; font-size: 9px; }"
"scrolledwindow, viewport { background-color: transparent; border: none; }"
"scrollbar { background-color: transparent; }"
"scrollbar slider { background-color: #232836; border-radius: 999px; min-width: 6px; }";

typedef struct { int deg; double ans; int just_eval; } State;
static State S = {1, 0.0, 0};

static GtkWidget *win, *entry, *histline, *status_lbl, *revealer, *histlist;
static GtkWidget *seg_deg, *seg_rad;

#define HIST_MAX 50
static char hist_expr[HIST_MAX][256];
static char hist_res[HIST_MAX][64];
static int hist_n = 0;

static void add_class(GtkWidget *w, const char *cls) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

static void gui_insert(const char *text) {
    if (S.just_eval) {
        char c = text[0];
        if (isdigit((unsigned char)c) || isalpha((unsigned char)c) || c == '.' || c == '(')
            gtk_entry_set_text(GTK_ENTRY(entry), "");
        S.just_eval = 0;
    }
    gint pos = gtk_editable_get_position(GTK_EDITABLE(entry));
    gtk_editable_insert_text(GTK_EDITABLE(entry), text, -1, &pos);
    gtk_editable_set_position(GTK_EDITABLE(entry), pos);
    gtk_label_set_text(GTK_LABEL(status_lbl), "");
    gtk_widget_grab_focus(entry);
}

static void gui_clear(void) {
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    gtk_label_set_text(GTK_LABEL(histline), "");
    gtk_label_set_text(GTK_LABEL(status_lbl), "");
    S.just_eval = 0;
    gtk_widget_grab_focus(entry);
}

static void gui_backspace(void) {
    gint pos = gtk_editable_get_position(GTK_EDITABLE(entry));
    if (pos > 0) {
        gtk_editable_delete_text(GTK_EDITABLE(entry), pos - 1, pos);
        gtk_editable_set_position(GTK_EDITABLE(entry), pos - 1);
    }
    S.just_eval = 0;
    gtk_widget_grab_focus(entry);
}

static void gui_set_mode(int deg) {
    S.deg = deg;
    GtkStyleContext *cd = gtk_widget_get_style_context(seg_deg);
    GtkStyleContext *cr = gtk_widget_get_style_context(seg_rad);
    if (deg) { gtk_style_context_add_class(cd, "active");
               gtk_style_context_remove_class(cr, "active"); }
    else     { gtk_style_context_add_class(cr, "active");
               gtk_style_context_remove_class(cd, "active"); }
    gtk_widget_grab_focus(entry);
}

static void destroy_child(GtkWidget *w, gpointer d) {
    (void)d;
    gtk_widget_destroy(w);
}

static void refresh_histlist(void) {
    gtk_container_foreach(GTK_CONTAINER(histlist), destroy_child, NULL);
    for (int i = 0; i < hist_n; i++) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget *le = gtk_label_new(hist_expr[i]);
        GtkWidget *lr = gtk_label_new(NULL);
        char buf[300];
        snprintf(buf, sizeof buf, "= %s", hist_res[i]);
        gtk_label_set_text(GTK_LABEL(lr), buf);
        gtk_label_set_xalign(GTK_LABEL(le), 0.0);
        gtk_label_set_xalign(GTK_LABEL(lr), 1.0);
        gtk_label_set_ellipsize(GTK_LABEL(le), PANGO_ELLIPSIZE_END);
        add_class(le, "hexpr");
        add_class(lr, "hres");
        gtk_box_pack_start(GTK_BOX(box), le, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(box), lr, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(row), box);
        g_object_set_data_full(G_OBJECT(row), "res", g_strdup(hist_res[i]), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(histlist), row, -1);
    }
    gtk_widget_show_all(histlist);
}

static void gui_equals(void) {
    const char *raw = gtk_entry_get_text(GTK_ENTRY(entry));
    char expr[256];
    snprintf(expr, sizeof expr, "%s", raw);
    g_strstrip(expr);
    if (!expr[0]) return;
    double result;
    deg_mode = S.deg;
    ans_val = S.ans;
    if (!evaluate(expr, &result)) {
        char buf[200];
        snprintf(buf, sizeof buf, "Error: %s", errmsg);
        gtk_label_set_text(GTK_LABEL(status_lbl), buf);
        return;
    }
    S.ans = result;
    char res[64], line[600];
    fmt_result(result, res, sizeof res);
    snprintf(line, sizeof line, "%s =", expr);
    gtk_label_set_text(GTK_LABEL(histline), line);
    gtk_entry_set_text(GTK_ENTRY(entry), res);
    gtk_editable_set_position(GTK_EDITABLE(entry), (gint)strlen(res));
    gtk_label_set_text(GTK_LABEL(status_lbl), "");
    S.just_eval = 1;
    if (hist_n < HIST_MAX) hist_n++;
    for (int i = hist_n - 1; i > 0; i--) {
        memcpy(hist_expr[i], hist_expr[i-1], sizeof hist_expr[0]);
        memcpy(hist_res[i],  hist_res[i-1],  sizeof hist_res[0]);
    }
    snprintf(hist_expr[0], sizeof hist_expr[0], "%s", expr);
    snprintf(hist_res[0],  sizeof hist_res[0],  "%s", res);
    refresh_histlist();
    gtk_widget_grab_focus(entry);
}

static void gui_toggle_history(void) {
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer),
        !gtk_revealer_get_reveal_child(GTK_REVEALER(revealer)));
    gtk_widget_grab_focus(entry);
}

static void gui_clear_history(void) {
    hist_n = 0;
    refresh_histlist();
    gtk_widget_grab_focus(entry);
}

/* key table: action 0=insert, 1=clear, 2=backspace, 3=equals */
typedef struct { const char *label, *ins, *cls; int act, r, c, w, h; } KeyDef;
static const KeyDef KEYS[] = {
    {"C",   NULL, "key op danger", 1, 0,0,1,1},
    {"⌫",  NULL, "key op danger", 2, 0,1,1,1},
    {"(",   "(",  "key op",        0, 0,2,1,1},
    {")",   ")",  "key op",        0, 0,3,1,1},
    {"%",   "%",  "key op",        0, 0,4,1,1},
    {"sin", "sin(","key sci",      0, 1,0,1,1},
    {"cos", "cos(","key sci",      0, 1,1,1,1},
    {"tan", "tan(","key sci",      0, 1,2,1,1},
    {"xʸ",  "^",  "key op",        0, 1,3,1,1},
    {"÷",   "/",  "key op",        0, 1,4,1,1},
    {"asin","asin(","key sci",     0, 2,0,1,1},
    {"acos","acos(","key sci",     0, 2,1,1,1},
    {"atan","atan(","key sci",     0, 2,2,1,1},
    {"√",   "sqrt(","key sci",     0, 2,3,1,1},
    {"×",   "*",  "key op",        0, 2,4,1,1},
    {"ln",  "ln(","key sci",       0, 3,0,1,1},
    {"log", "log(","key sci",      0, 3,1,1,1},
    {"x!",  "!",  "key sci",       0, 3,2,1,1},
    {"π",   "pi", "key sci",       0, 3,3,1,1},
    {"−",   "-",  "key op",        0, 3,4,1,1},
    {"7","7","key",0,4,0,1,1}, {"8","8","key",0,4,1,1,1}, {"9","9","key",0,4,2,1,1},
    {"e",   "e",  "key sci",       0, 4,3,1,1},
    {"+",   "+",  "key op",        0, 4,4,1,1},
    {"4","4","key",0,5,0,1,1}, {"5","5","key",0,5,1,1,1}, {"6","6","key",0,5,2,1,1},
    {"Ans", "ans","key sci",       0, 5,3,1,1},
    {"=",   NULL, "key eq",        3, 5,4,1,2},
    {"1","1","key",0,6,0,1,1}, {"2","2","key",0,6,1,1,1}, {"3","3","key",0,6,2,1,1},
    {"x²",  "^2", "key sci",       0, 6,3,1,1},
    {"0",   "0",  "key",           0, 7,0,2,1},
    {".",   ".",  "key",           0, 7,2,1,1},
    {"eˣ",  "exp(","key sci",      0, 7,3,1,1},
    {"1/x", "^(-1)","key sci",     0, 7,4,1,1},
};

static void on_key(GtkButton *b, gpointer data) {
    (void)b;
    const KeyDef *k = data;
    switch (k->act) {
        case 0: gui_insert(k->ins); break;
        case 1: gui_clear(); break;
        case 2: gui_backspace(); break;
        case 3: gui_equals(); break;
    }
}
static void on_seg_deg(GtkButton *b, gpointer d) { (void)b;(void)d; gui_set_mode(1); }
static void on_seg_rad(GtkButton *b, gpointer d) { (void)b;(void)d; gui_set_mode(0); }
static void on_hist(GtkButton *b, gpointer d)   { (void)b;(void)d; gui_toggle_history(); }
static void on_hclear(GtkButton *b, gpointer d) { (void)b;(void)d; gui_clear_history(); }
static void on_activate(GtkEntry *e, gpointer d) { (void)e;(void)d; gui_equals(); }
static void on_row(GtkListBox *lb, GtkListBoxRow *row, gpointer d) {
    (void)lb; (void)d;
    if (!row) return;
    const char *res = g_object_get_data(G_OBJECT(row), "res");
    if (res) {
        if (S.just_eval) { gtk_entry_set_text(GTK_ENTRY(entry), ""); S.just_eval = 0; }
        gui_insert(res);
    }
}
static gboolean on_keypress(GtkWidget *w, GdkEventKey *ev, gpointer d) {
    (void)w; (void)d;
    if (ev->keyval == GDK_KEY_Escape) { gui_clear(); return TRUE; }
    if ((ev->keyval == GDK_KEY_h || ev->keyval == GDK_KEY_H) &&
        (ev->state & GDK_CONTROL_MASK)) { gui_toggle_history(); return TRUE; }
    return FALSE;
}

static GtkWidget *make_label(const char *text, const char *cls, float xalign) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    add_class(l, cls);
    return l;
}

static void build_ui(void) {
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);
    g_object_unref(prov);

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), APP_NAME);
    gtk_window_set_default_size(GTK_WINDOW(win), 470, 720);
    gtk_widget_set_size_request(win, 430, 620);
    gtk_window_set_icon_name(GTK_WINDOW(win), "accessories-calculator");
    add_class(win, "root");

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);
    add_class(root, "root");
    gtk_container_add(GTK_CONTAINER(win), root);

    /* header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *logo = make_label("R", "logo", 0.5);
    GtkWidget *brandcol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_box_pack_start(GTK_BOX(brandcol), make_label(BRAND, "wordmark", 0), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(brandcol),
        make_label("SCIENTIFIC CALCULATOR · " VERSION, "overline", 0), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), logo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), brandcol, FALSE, FALSE, 0);

    GtkWidget *segbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    add_class(segbox, "segbox");
    seg_deg = gtk_button_new_with_label("DEG");
    seg_rad = gtk_button_new_with_label("RAD");
    add_class(seg_deg, "seg"); add_class(seg_rad, "seg");
    add_class(seg_deg, "active");
    gtk_widget_set_can_focus(seg_deg, FALSE);
    gtk_widget_set_can_focus(seg_rad, FALSE);
    g_signal_connect(seg_deg, "clicked", G_CALLBACK(on_seg_deg), NULL);
    g_signal_connect(seg_rad, "clicked", G_CALLBACK(on_seg_rad), NULL);
    gtk_box_pack_start(GTK_BOX(segbox), seg_deg, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(segbox), seg_rad, FALSE, FALSE, 0);
    GtkWidget *hist_btn = gtk_button_new_with_label("≡");
    add_class(hist_btn, "ghost");
    gtk_widget_set_tooltip_text(hist_btn, "History (Ctrl+H)");
    gtk_widget_set_can_focus(hist_btn, FALSE);
    g_signal_connect(hist_btn, "clicked", G_CALLBACK(on_hist), NULL);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(right), segbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), hist_btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), right, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    /* display card */
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    add_class(card, "card");
    histline = make_label("", "histline", 1.0);
    gtk_label_set_ellipsize(GTK_LABEL(histline), PANGO_ELLIPSIZE_START);
    gtk_widget_set_margin_top(histline, 10);
    gtk_widget_set_margin_start(histline, 16);
    gtk_widget_set_margin_end(histline, 16);
    entry = gtk_entry_new();
    add_class(entry, "display");
    gtk_entry_set_alignment(GTK_ENTRY(entry), 1.0);
    gtk_entry_set_has_frame(GTK_ENTRY(entry), FALSE);
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_bottom(entry, 12);
    g_signal_connect(entry, "activate", G_CALLBACK(on_activate), NULL);
    gtk_box_pack_start(GTK_BOX(card), histline, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), card, FALSE, FALSE, 0);

    /* history drawer */
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
    GtkWidget *hcard = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    add_class(hcard, "card");
    GtkWidget *hhead = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *htitle = make_label("HISTORY", "histhead", 0);
    gtk_widget_set_margin_top(htitle, 10);
    gtk_widget_set_margin_start(htitle, 14);
    GtkWidget *hclear = gtk_button_new_with_label("clear");
    add_class(hclear, "ghost");
    gtk_widget_set_can_focus(hclear, FALSE);
    gtk_widget_set_margin_top(hclear, 6);
    gtk_widget_set_margin_end(hclear, 8);
    g_signal_connect(hclear, "clicked", G_CALLBACK(on_hclear), NULL);
    gtk_box_pack_start(GTK_BOX(hhead), htitle, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hhead), hclear, FALSE, FALSE, 0);
    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 120);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 160);
    gtk_widget_set_margin_start(scroller, 8);
    gtk_widget_set_margin_end(scroller, 8);
    gtk_widget_set_margin_bottom(scroller, 8);
    histlist = gtk_list_box_new();
    add_class(histlist, "history");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(histlist), GTK_SELECTION_SINGLE);
    g_signal_connect(histlist, "row-activated", G_CALLBACK(on_row), NULL);
    gtk_container_add(GTK_CONTAINER(scroller), histlist);
    gtk_box_pack_start(GTK_BOX(hcard), hhead, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hcard), scroller, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(revealer), hcard);
    gtk_box_pack_start(GTK_BOX(root), revealer, FALSE, FALSE, 0);

    /* keypad */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
        const KeyDef *k = &KEYS[i];
        GtkWidget *b = gtk_button_new_with_label(k->label);
        char cls[64];
        snprintf(cls, sizeof cls, "%s", k->cls);
        for (char *tok = strtok(cls, " "); tok; tok = strtok(NULL, " "))
            add_class(b, tok);
        gtk_widget_set_can_focus(b, FALSE);
        g_signal_connect(b, "clicked", G_CALLBACK(on_key), (gpointer)k);
        gtk_grid_attach(GTK_GRID(grid), b, k->c, k->r, k->w, k->h);
    }
    gtk_box_pack_start(GTK_BOX(root), grid, TRUE, TRUE, 0);

    /* status bar */
    GtkWidget *statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    status_lbl = make_label("", "status", 0);
    gtk_label_set_ellipsize(GTK_LABEL(status_lbl), PANGO_ELLIPSIZE_END);
    GtkWidget *hint = make_label("↵ evaluate · esc clear · ctrl+h history", "hint", 1.0);
    gtk_box_pack_start(GTK_BOX(statusbar), status_lbl, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(statusbar), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), statusbar, FALSE, FALSE, 0);

    g_signal_connect(win, "key-press-event", G_CALLBACK(on_keypress), NULL);
    gtk_widget_show_all(win);
    gtk_widget_grab_focus(entry);
}

static void pump_ms(int ms) {
    gint64 end = g_get_monotonic_time() + (gint64)ms * 1000;
    while (g_get_monotonic_time() < end) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(10000);
    }
}

/* ================= tests / CLI ================= */

static int selftest(void) {
    struct { const char *expr; int deg; double want; } ok[] = {
        {"2+3*4",1,14},{"(2+3)*4",1,20},{"10/4",1,2.5},{"-5+3",1,-2},
        {"7%3",1,1},{"2^10",1,1024},{"2^3^2",1,512},{"2^-3",1,0.125},
        {"-2^2",1,-4},{"sqrt(16)",1,4},{"ln(e)",1,1},{"log(100)",1,2},
        {"5!",1,120},{"3!!",1,720},{"abs(-7)",1,7},{"exp(0)",1,1},
        {"sin(30)",1,0.5},{"cos(60)",1,0.5},{"tan(45)",1,1},
        {"asin(0.5)",1,30},{"acos(1)",1,0},{"atan(1)",1,45},
        {"sin(pi/2)",0,1},{"cos(pi)",0,-1},{"atan(1)",0,M_PI/4},
        {"1.5e3+1",1,1501},{"2E-2*100",1,2},{"pi",1,M_PI},{"e",1,M_E},
        {"2*(1.5+0.5)",1,4},{"0.1+0.2",1,0.30000000000000004},
        {"9^(-1)",1,1.0/9},
    };
    const char *bad[] = {"1/0","5%0","sqrt(-1)","ln(0)","log(-2)","asin(2)",
        "(-1)!","2.5!","171!","2+","(1+2","abc","sin","1..2","tan(90)",
        "2^10000","",NULL};
    int n = 0;
    double v;
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        deg_mode = ok[i].deg;
        if (!evaluate(ok[i].expr, &v) ||
            fabs(v - ok[i].want) > 1e-9 * (1 + fabs(ok[i].want))) {
            fprintf(stderr, "FAIL %s: got %g (%s)\n", ok[i].expr, v, errmsg);
            return 1;
        }
        n++;
    }
    deg_mode = 1; ans_val = 5.0;
    if (!evaluate("ans*2", &v) || v != 10) { fprintf(stderr, "FAIL ans*2\n"); return 1; }
    n++;
    if (!evaluate("ans+1", &v) || v != 6) { fprintf(stderr, "FAIL ans+1\n"); return 1; }
    n++;
    ans_val = 0;
    for (int i = 0; bad[i]; i++) {
        if (evaluate(bad[i], &v)) { fprintf(stderr, "FAIL expected error: %s\n", bad[i]); return 1; }
        n++;
    }
    printf("selftest: %d cases passed\n", n);
    return 0;
}

static void expect_text(const char *want, const char *label) {
    const char *got = gtk_entry_get_text(GTK_ENTRY(entry));
    if (strcmp(got, want)) {
        fprintf(stderr, "SMOKE FAIL %s: got '%s' want '%s'\n", label, got, want);
        exit(1);
    }
    printf("GUI OK  %s: %s\n", label, got);
}

static int smoke(const char *png) {
    build_ui();
    pump_ms(150);
    gui_insert("7"); gui_insert("*"); gui_insert("8"); gui_equals();
    expect_text("56", "7×8");
    gui_insert("ans"); gui_insert("*"); gui_insert("2"); gui_equals();
    expect_text("112", "ans×2");
    gui_clear(); gui_insert("sin("); gui_insert("3"); gui_insert("0");
    gui_insert(")"); gui_equals();
    expect_text("0.5", "sin(30) DEG");
    gui_set_mode(0);
    gtk_entry_set_text(GTK_ENTRY(entry), "sin(pi/6)");
    gui_equals();
    expect_text("0.5", "sin(pi/6) RAD");
    gui_set_mode(1);
    gtk_entry_set_text(GTK_ENTRY(entry), "2^10+5!"); gui_equals();
    expect_text("1144", "2^10+5!");
    gui_clear(); gui_insert("9"); gui_insert("^2"); gui_equals();
    expect_text("81", "9 x²");
    gui_insert("^(-1)"); gui_equals();
    expect_text("0.0123456790123", "81 1/x");
    gtk_entry_set_text(GTK_ENTRY(entry), "1/0"); gui_equals();
    if (strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "1/0") ||
        !strstr(gtk_label_get_text(GTK_LABEL(status_lbl)), "division by zero")) {
        fprintf(stderr, "SMOKE FAIL error status\n"); return 1;
    }
    printf("GUI OK  error status: %s\n", gtk_label_get_text(GTK_LABEL(status_lbl)));
    gtk_entry_set_text(GTK_ENTRY(entry), "123");
    gtk_editable_set_position(GTK_EDITABLE(entry), 3);
    gui_backspace();
    expect_text("12", "backspace");
    gui_toggle_history(); pump_ms(300);
    if (!gtk_revealer_get_reveal_child(GTK_REVEALER(revealer)) || hist_n < 6) {
        fprintf(stderr, "SMOKE FAIL history drawer\n"); return 1;
    }
    printf("GUI OK  history: %d rows, newest = %s = %s\n",
           hist_n, hist_expr[0], hist_res[0]);
    gui_clear_history();
    if (hist_n != 0) { fprintf(stderr, "SMOKE FAIL history clear\n"); return 1; }
    printf("GUI OK  history clear\n");
    gui_toggle_history(); pump_ms(300);
    gtk_entry_set_text(GTK_ENTRY(entry), "sin(30)+2^10"); gui_equals();
    pump_ms(250);
    if (png) {
        GdkWindow *gw = gtk_widget_get_window(win);
        int w, h;
        gdk_window_get_geometry(gw, NULL, NULL, &w, &h);
        GdkPixbuf *pb = gdk_pixbuf_get_from_window(gw, 0, 0, w, h);
        if (!pb || !gdk_pixbuf_save(pb, png, "png", NULL, NULL)) {
            fprintf(stderr, "SMOKE FAIL screenshot\n"); return 1;
        }
        g_object_unref(pb);
        printf("screenshot: %s\n", png);
    }
    printf("ALL GUI SMOKE TESTS PASSED\n");
    gtk_widget_destroy(win);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--selftest"))
        return selftest();
    if (argc > 1 && !strcmp(argv[1], "--eval")) {
        char buf[4096] = "";
        int rad = 0;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--rad")) { rad = 1; continue; }
            if (buf[0]) strncat(buf, " ", sizeof buf - strlen(buf) - 1);
            strncat(buf, argv[i], sizeof buf - strlen(buf) - 1);
        }
        deg_mode = !rad;
        double v;
        if (evaluate(buf, &v)) {
            char out[64];
            fmt_result(v, out, sizeof out);
            printf("%s\n", out);
            return 0;
        }
        fprintf(stderr, "error: %s\n", errmsg);
        return 1;
    }
    gtk_init(&argc, &argv);
    if (argc > 1 && !strcmp(argv[1], "--smoke"))
        return smoke(argc > 2 ? argv[2] : NULL);
    build_ui();
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_main();
    return 0;
}
