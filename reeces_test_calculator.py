#!/usr/bin/python3.12
"""REECE Scientific Calculator - professional scientific calculator for Linux.

GUI:   ./reeces-test-calculator            (GTK3 interface)
CLI:   ./reeces-test-calculator --eval "sin(30) + 2^10" [--rad]
Test:  ./reeces-test-calculator --selftest

Supports: + - * / % ^ ! ( ) decimals, sin cos tan asin acos atan,
ln log sqrt abs exp, constants pi e, Ans memory, DEG/RAD modes, history.
Expression parsing is a hand-written recursive-descent parser (no eval).
GUI requires system GTK3 (python3-gi); CLI/selftest run on any Python 3.
"""
import math
import sys

APP_NAME = "Reece Scientific Calculator"
BRAND = "REECE"
VERSION = "v2.0"


class CalcError(Exception):
    pass


# ---------------- parser ----------------

class Parser:
    FUNCS = {"sin", "cos", "tan", "asin", "acos", "atan",
             "ln", "log", "sqrt", "abs", "exp"}
    CONSTS = {"pi": math.pi, "e": math.e}

    def __init__(self, text, deg=True, ans=0.0):
        self.s = text
        self.i = 0
        self.deg = deg
        self.ans = ans

    def error(self, msg):
        raise CalcError(msg)

    def peek(self):
        while self.i < len(self.s) and self.s[self.i].isspace():
            self.i += 1
        return self.s[self.i] if self.i < len(self.s) else ""

    def take(self, ch):
        if self.peek() == ch:
            self.i += 1
            return True
        return False

    def parse(self):
        v = self.expr()
        if self.peek() != "":
            self.error(f"unexpected character '{self.peek()}'")
        if isinstance(v, complex) or (isinstance(v, float) and (math.isnan(v) or math.isinf(v))):
            self.error("result is not a finite number")
        return float(v)

    def expr(self):
        v = self.term()
        while True:
            if self.take("+"):
                v += self.term()
            elif self.take("-"):
                v -= self.term()
            else:
                return v

    def term(self):
        v = self.unary()
        while True:
            if self.take("*"):
                v *= self.unary()
            elif self.take("/"):
                d = self.unary()
                if d == 0:
                    self.error("division by zero")
                v /= d
            elif self.take("%"):
                d = self.unary()
                if d == 0:
                    self.error("modulo by zero")
                v = math.fmod(v, d)
            else:
                return v

    def unary(self):
        if self.take("+"):
            return self.unary()
        if self.take("-"):
            return -self.unary()
        return self.power()

    def power(self):
        base = self.postfix()
        if self.take("^"):
            exp = self.unary()  # right-associative, allows 2^-3
            try:
                r = base ** exp
            except (OverflowError, ValueError):
                self.error("power out of range")
            if isinstance(r, complex):
                self.error("complex result not supported")
            return r
        return base

    def postfix(self):
        v = self.atom()
        while self.take("!"):
            if v < 0 or v != int(v):
                self.error("factorial needs a non-negative integer")
            if v > 170:
                self.error("factorial too large (max 170)")
            v = float(math.factorial(int(v)))
        return v

    def atom(self):
        c = self.peek()
        if c == "":
            self.error("unexpected end of expression")
        if c == "(":
            self.i += 1
            v = self.expr()
            if not self.take(")"):
                self.error("missing ')'")
            return v
        if c.isdigit() or c == ".":
            start = self.i
            seen_dot = False
            while self.i < len(self.s) and (self.s[self.i].isdigit() or
                                            (self.s[self.i] == "." and not seen_dot)):
                if self.s[self.i] == ".":
                    seen_dot = True
                self.i += 1
            # scientific notation: 1.5e3 / 2E-4
            if self.i < len(self.s) and self.s[self.i] in "eE":
                j = self.i + 1
                if j < len(self.s) and self.s[j] in "+-":
                    j += 1
                if j < len(self.s) and self.s[j].isdigit():
                    self.i = j
                    while self.i < len(self.s) and self.s[self.i].isdigit():
                        self.i += 1
            try:
                return float(self.s[start:self.i])
            except ValueError:
                self.error("invalid number")
        if c.isalpha():
            start = self.i
            while self.i < len(self.s) and self.s[self.i].isalpha():
                self.i += 1
            name = self.s[start:self.i].lower()
            if name in self.CONSTS:
                return self.CONSTS[name]
            if name == "ans":
                return self.ans
            if name in self.FUNCS:
                if not self.take("("):
                    self.error(f"{name} needs parentheses, e.g. {name}(30)")
                arg = self.expr()
                if not self.take(")"):
                    self.error("missing ')'")
                return self.apply_func(name, arg)
            self.error(f"unknown name '{name}'")
        self.error(f"unexpected character '{c}'")

    def apply_func(self, name, x):
        to_rad = (lambda a: math.radians(a)) if self.deg else (lambda a: a)
        from_rad = (lambda a: math.degrees(a)) if self.deg else (lambda a: a)
        if name == "sin":
            return math.sin(to_rad(x))
        if name == "cos":
            return math.cos(to_rad(x))
        if name == "tan":
            if self.deg and (x % 180) == 90:
                self.error("tan undefined at 90° + k·180°")
            return math.tan(to_rad(x))
        if name in ("asin", "acos"):
            if not -1 <= x <= 1:
                self.error(f"{name} domain is [-1, 1]")
            return from_rad(math.asin(x) if name == "asin" else math.acos(x))
        if name == "atan":
            return from_rad(math.atan(x))
        if name == "ln":
            if x <= 0:
                self.error("ln needs a positive argument")
            return math.log(x)
        if name == "log":
            if x <= 0:
                self.error("log needs a positive argument")
            return math.log10(x)
        if name == "sqrt":
            if x < 0:
                self.error("sqrt of a negative number")
            return math.sqrt(x)
        if name == "abs":
            return abs(x)
        if name == "exp":
            try:
                return math.exp(x)
            except OverflowError:
                self.error("exp out of range")
        self.error(f"unknown function '{name}'")


def evaluate(text, deg=True, ans=0.0):
    """Evaluate an expression string; raises CalcError on bad input."""
    text = (text.replace("π", "pi").replace("×", "*")
                .replace("÷", "/").replace("−", "-").replace("√(", "sqrt("))
    return Parser(text, deg=deg, ans=ans).parse()


def fmt(v):
    if v == int(v) and abs(v) < 1e15:
        return str(int(v))
    return format(v, ".12g")


# ---------------- GUI (GTK3) ----------------

CSS = """
* { outline: none; }
window, .root { background-color: #0b0d12; }
label { color: #f2f4f8; }

.wordmark {
  font-family: 'JetBrainsMono Nerd Font', monospace;
  font-size: 16px; font-weight: 800; letter-spacing: 3px;
  color: #f2f4f8;
}
.overline {
  font-family: 'DejaVu Sans', sans-serif;
  font-size: 8px; letter-spacing: 2px; color: #5c6470;
}
.logo {
  background-color: #e8a33d; color: #14161c;
  font-family: 'JetBrainsMono Nerd Font', monospace;
  font-size: 16px; font-weight: 800;
  border-radius: 9px; padding: 4px 9px;
}

.card {
  background-color: #12151d;
  border: 1px solid #232836;
  border-radius: 14px;
}
.histline {
  font-family: 'DejaVu Sans', sans-serif;
  font-size: 11px; color: #5c6470;
}
entry.display {
  background-color: #0d1017; background-image: none;
  color: #f2f4f8; caret-color: #e8a33d;
  font-family: 'JetBrainsMono Nerd Font', monospace;
  font-size: 26px;
  border: none; border-radius: 10px;
  padding: 12px 14px; margin: 0;
  box-shadow: none;
}
entry.display selection { background-color: #3a3320; color: #f5b841; }

button.key {
  background-image: none; background-color: #1a1e28;
  color: #f2f4f8;
  font-family: 'JetBrainsMono Nerd Font', monospace;
  font-size: 16px; font-weight: 500;
  border: 1px solid #262c3a; border-radius: 11px;
  box-shadow: none; text-shadow: none;
  padding: 0; margin: 0; min-height: 0; min-width: 0;
  transition: background-color 120ms ease, border-color 120ms ease;
}
button.key:hover { background-color: #232836; border-color: #333b4e; }
button.key:active { background-color: #141821; }

button.sci {
  background-color: #141821; color: #9aa3b2;
  font-size: 12px; border-color: #1a1f2b;
}
button.sci:hover { background-color: #1c212c; border-color: #262c3a; color: #d0d6e0; }
button.sci:active { background-color: #10131a; }

button.op { background-color: #232836; color: #f0b03f; border-color: #2d3446; }
button.op:hover { background-color: #2d3446; border-color: #3a4358; }
button.op:active { background-color: #1c2130; }

button.danger { color: #ff6b6b; }
button.danger:hover { color: #ff8787; }

button.eq {
  background-color: #e8a33d; color: #14161c;
  font-size: 22px; font-weight: 800; border: none;
}
button.eq:hover { background-color: #f5b841; }
button.eq:active { background-color: #cf8f2e; }

.segbox {
  background-color: #12151d;
  border: 1px solid #232836; border-radius: 999px; padding: 2px;
}
button.seg {
  background-image: none; background-color: transparent;
  color: #5c6470; border: none; border-radius: 999px; box-shadow: none;
  font-family: 'JetBrainsMono Nerd Font', monospace;
  font-size: 11px; font-weight: 700; letter-spacing: 1px;
  padding: 4px 13px; margin: 0; min-height: 0; min-width: 0;
  transition: background-color 120ms ease, color 120ms ease;
}
button.seg:hover { color: #9aa3b2; }
button.seg.active { background-color: #2a2413; color: #e8a33d; }
button.seg.active:hover { color: #f5b841; }

button.ghost {
  background-image: none; background-color: transparent;
  color: #5c6470; border: 1px solid #232836; border-radius: 9px;
  box-shadow: none; text-shadow: none;
  font-family: 'DejaVu Sans', sans-serif; font-size: 12px;
  padding: 4px 10px; margin: 0; min-height: 0; min-width: 0;
}
button.ghost:hover { background-color: #1a1e28; color: #9aa3b2; }
button.ghost:active { background-color: #141821; }

.histhead {
  font-family: 'DejaVu Sans', sans-serif;
  font-size: 9px; letter-spacing: 2px; color: #5c6470;
}
listbox.history { background-color: transparent; }
listbox.history row {
  background-color: transparent; border-radius: 8px;
  padding: 5px 10px; margin: 1px 0;
}
listbox.history row:hover { background-color: #1a1e28; }
listbox.history row:selected { background-color: #2a2413; }
.hexpr {
  font-family: 'JetBrainsMono Nerd Font', monospace;
  font-size: 11px; color: #9aa3b2;
}
.hres {
  font-family: 'JetBrainsMono Nerd Font', monospace;
  font-size: 12px; font-weight: 700; color: #e8a33d;
}
.status { color: #ff6b6b; font-family: 'DejaVu Sans', sans-serif; font-size: 11px; }
.hint { color: #5c6470; font-family: 'DejaVu Sans', sans-serif; font-size: 9px; }
scrolledwindow, viewport { background-color: transparent; border: none; }
scrollbar { background-color: transparent; }
scrollbar slider { background-color: #232836; border-radius: 999px; min-width: 6px; }
"""


def build_ui():
    """Construct the full GUI; returns a namespace of widgets/actions.
    Does not start the main loop (used by run_gui and automated tests)."""
    import gi
    gi.require_version("Gtk", "3.0")
    from gi.repository import Gtk, Gdk, GLib, Pango

    class NS:
        pass
    ui = NS()
    ui.state = {"deg": True, "ans": 0.0, "just_evaluated": False}
    ui.history = []          # list of (expr, result_str), newest first
    ui.keys = {}             # label -> Gtk.Button

    provider = Gtk.CssProvider()
    provider.load_from_data(CSS.encode())
    Gtk.StyleContext.add_provider_for_screen(
        Gdk.Screen.get_default(), provider,
        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION + 10)

    win = Gtk.Window(title=APP_NAME)
    win.set_default_size(470, 720)
    win.set_size_request(430, 620)
    try:
        win.set_icon_name("accessories-calculator")
    except Exception:
        pass
    win.get_style_context().add_class("root")
    ui.win = win

    root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
    root.set_margin_top(16); root.set_margin_bottom(12)
    root.set_margin_start(16); root.set_margin_end(16)
    root.get_style_context().add_class("root")
    win.add(root)

    # ---- header: brand + mode segment + history toggle ----
    header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
    logo = Gtk.Label(label="R")
    logo.get_style_context().add_class("logo")
    brandcol = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=1)
    wordmark = Gtk.Label(label=BRAND, xalign=0)
    wordmark.get_style_context().add_class("wordmark")
    overline = Gtk.Label(label=f"SCIENTIFIC CALCULATOR · {VERSION}", xalign=0)
    overline.get_style_context().add_class("overline")
    brandcol.pack_start(wordmark, False, False, 0)
    brandcol.pack_start(overline, False, False, 0)
    header.pack_start(logo, False, False, 0)
    header.pack_start(brandcol, False, False, 0)

    segbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=2)
    segbox.get_style_context().add_class("segbox")
    seg_deg = Gtk.Button(label="DEG")
    seg_rad = Gtk.Button(label="RAD")
    for b in (seg_deg, seg_rad):
        b.get_style_context().add_class("seg")
        b.set_can_focus(False)
    seg_deg.get_style_context().add_class("active")
    segbox.pack_start(seg_deg, False, False, 0)
    segbox.pack_start(seg_rad, False, False, 0)

    hist_btn = Gtk.Button(label="≡")
    hist_btn.get_style_context().add_class("ghost")
    hist_btn.set_tooltip_text("History (Ctrl+H)")
    hist_btn.set_can_focus(False)

    right = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
    right.pack_start(segbox, False, False, 0)
    right.pack_start(hist_btn, False, False, 0)
    header.pack_end(right, False, False, 0)
    root.pack_start(header, False, False, 0)
    ui.seg_deg, ui.seg_rad, ui.hist_btn = seg_deg, seg_rad, hist_btn

    # ---- display card ----
    card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
    card.get_style_context().add_class("card")
    card.set_margin_top(2); card.set_margin_bottom(2)
    histline = Gtk.Label(label="", xalign=1)
    histline.get_style_context().add_class("histline")
    histline.set_ellipsize(Pango.EllipsizeMode.START)
    histline.set_margin_top(10); histline.set_margin_start(16); histline.set_margin_end(16)
    display = Gtk.Entry()
    display.get_style_context().add_class("display")
    display.set_alignment(1.0)
    display.set_has_frame(False)
    display.set_margin_start(12); display.set_margin_end(12)
    display.set_margin_bottom(12)
    card.pack_start(histline, False, False, 0)
    card.pack_start(display, False, False, 0)
    root.pack_start(card, False, False, 0)
    ui.display, ui.histline = display, histline

    # ---- history drawer (revealer) ----
    revealer = Gtk.Revealer()
    revealer.set_transition_type(Gtk.RevealerTransitionType.SLIDE_DOWN)
    revealer.set_transition_duration(180)
    revealer.set_reveal_child(False)
    hcard = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
    hcard.get_style_context().add_class("card")
    hhead = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
    htitle = Gtk.Label(label="HISTORY", xalign=0)
    htitle.get_style_context().add_class("histhead")
    htitle.set_margin_top(10); htitle.set_margin_start(14)
    hclear = Gtk.Button(label="clear")
    hclear.get_style_context().add_class("ghost")
    hclear.set_can_focus(False)
    hclear.set_margin_top(6); hclear.set_margin_end(8)
    hhead.pack_start(htitle, True, True, 0)
    hhead.pack_end(hclear, False, False, 0)
    scroller = Gtk.ScrolledWindow()
    scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
    scroller.set_min_content_height(120)
    scroller.set_max_content_height(160)
    scroller.set_margin_start(8); scroller.set_margin_end(8); scroller.set_margin_bottom(8)
    histlist = Gtk.ListBox()
    histlist.get_style_context().add_class("history")
    histlist.set_selection_mode(Gtk.SelectionMode.SINGLE)
    scroller.add(histlist)
    hcard.pack_start(hhead, False, False, 0)
    hcard.pack_start(scroller, False, False, 0)
    revealer.add(hcard)
    root.pack_start(revealer, False, False, 0)
    ui.revealer, ui.histlist, ui.hclear = revealer, histlist, hclear

    # ---- keypad ----
    grid = Gtk.Grid()
    grid.set_row_spacing(8); grid.set_column_spacing(8)
    grid.set_row_homogeneous(True); grid.set_column_homogeneous(True)
    grid.set_hexpand(True); grid.set_vexpand(True)
    root.pack_start(grid, True, True, 0)

    # ---- status bar ----
    statusbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
    status = Gtk.Label(label="", xalign=0)
    status.get_style_context().add_class("status")
    status.set_ellipsize(Pango.EllipsizeMode.END)
    hint = Gtk.Label(label="↵ evaluate · esc clear · ctrl+h history", xalign=1)
    hint.get_style_context().add_class("hint")
    statusbar.pack_start(status, True, True, 0)
    statusbar.pack_end(hint, False, False, 0)
    root.pack_start(statusbar, False, False, 0)
    ui.status = status

    # ---- actions ----
    def insert(text):
        if ui.state["just_evaluated"]:
            if text[0].isdigit() or text[0].isalpha() or text[0] in ".(":
                display.set_text("")
            ui.state["just_evaluated"] = False
        pos = display.get_position()
        display.insert_text(text, pos)
        display.set_position(pos + len(text))
        status.set_text("")
        display.grab_focus()

    def clear():
        display.set_text("")
        histline.set_text("")
        status.set_text("")
        ui.state["just_evaluated"] = False
        display.grab_focus()

    def backspace():
        pos = display.get_position()
        if pos > 0:
            display.delete_text(pos - 1, pos)
            display.set_position(pos - 1)
        ui.state["just_evaluated"] = False
        display.grab_focus()

    def set_mode(deg):
        ui.state["deg"] = deg
        for b, active in ((seg_deg, deg), (seg_rad, not deg)):
            ctx = b.get_style_context()
            if active:
                ctx.add_class("active")
            else:
                ctx.remove_class("active")
        display.grab_focus()

    def refresh_histlist():
        for row in histlist.get_children():
            histlist.remove(row)
        for expr, res in ui.history:
            row = Gtk.ListBoxRow()
            box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
            le = Gtk.Label(label=expr, xalign=0)
            le.get_style_context().add_class("hexpr")
            le.set_ellipsize(Pango.EllipsizeMode.END)
            lr = Gtk.Label(label="= " + res, xalign=1)
            lr.get_style_context().add_class("hres")
            box.pack_start(le, True, True, 0)
            box.pack_end(lr, False, False, 0)
            row.add(box)
            row._result = res
            histlist.add(row)
        histlist.show_all()

    def equals():
        expr = display.get_text().strip()
        if not expr:
            return
        try:
            result = evaluate(expr, deg=ui.state["deg"], ans=ui.state["ans"])
        except CalcError as e:
            status.set_text(f"Error: {e}")
            return
        ui.state["ans"] = result
        res_str = fmt(result)
        histline.set_text(f"{expr} =")
        display.set_text(res_str)
        display.set_position(len(res_str))
        status.set_text("")
        ui.state["just_evaluated"] = True
        ui.history.insert(0, (expr, res_str))
        del ui.history[50:]
        refresh_histlist()
        display.grab_focus()

    def toggle_history(*_):
        revealer.set_reveal_child(not revealer.get_reveal_child())
        display.grab_focus()

    def on_hist_row(_listbox, row):
        if row is not None and getattr(row, "_result", None):
            if ui.state["just_evaluated"]:
                display.set_text("")
                ui.state["just_evaluated"] = False
            insert(row._result)

    def clear_history(*_):
        ui.history.clear()
        refresh_histlist()
        display.grab_focus()

    def mkkey(label, cmd, classes=("key",), r=0, c=0, w=1, h=1):
        b = Gtk.Button(label=label)
        ctx = b.get_style_context()
        for cl in classes:
            ctx.add_class(cl)
        b.set_can_focus(False)
        b.connect("clicked", lambda *_: cmd())
        grid.attach(b, c, r, w, h)
        ui.keys[label] = b
        return b

    ins = lambda t: (lambda: insert(t))
    mkkey("C", clear, ("key", "op", "danger"), 0, 0)
    mkkey("⌫", backspace, ("key", "op", "danger"), 0, 1)
    mkkey("(", ins("("), ("key", "op"), 0, 2)
    mkkey(")", ins(")"), ("key", "op"), 0, 3)
    mkkey("%", ins("%"), ("key", "op"), 0, 4)

    for c, name in enumerate(("sin", "cos", "tan")):
        mkkey(name, ins(name + "("), ("key", "sci"), 1, c)
    mkkey("xʸ", ins("^"), ("key", "op"), 1, 3)
    mkkey("÷", ins("/"), ("key", "op"), 1, 4)

    for c, name in enumerate(("asin", "acos", "atan")):
        mkkey(name, ins(name + "("), ("key", "sci"), 2, c)
    mkkey("√", ins("sqrt("), ("key", "sci"), 2, 3)
    mkkey("×", ins("*"), ("key", "op"), 2, 4)

    mkkey("ln", ins("ln("), ("key", "sci"), 3, 0)
    mkkey("log", ins("log("), ("key", "sci"), 3, 1)
    mkkey("x!", ins("!"), ("key", "sci"), 3, 2)
    mkkey("π", ins("pi"), ("key", "sci"), 3, 3)
    mkkey("−", ins("-"), ("key", "op"), 3, 4)

    digits = {7: (4, 0), 8: (4, 1), 9: (4, 2),
              4: (5, 0), 5: (5, 1), 6: (5, 2),
              1: (6, 0), 2: (6, 1), 3: (6, 2)}
    for d, (r, c) in digits.items():
        mkkey(str(d), ins(str(d)), ("key",), r, c)
    mkkey("e", ins("e"), ("key", "sci"), 4, 3)
    mkkey("+", ins("+"), ("key", "op"), 4, 4)
    mkkey("Ans", ins("ans"), ("key", "sci"), 5, 3)
    mkkey("=", equals, ("key", "eq"), 5, 4, 1, 2)
    mkkey("x²", ins("^2"), ("key", "sci"), 6, 3)
    mkkey("0", ins("0"), ("key",), 7, 0, 2, 1)
    mkkey(".", ins("."), ("key",), 7, 2)
    mkkey("eˣ", ins("exp("), ("key", "sci"), 7, 3)
    mkkey("1/x", ins("^(-1)"), ("key", "sci"), 7, 4)

    seg_deg.connect("clicked", lambda *_: set_mode(True))
    seg_rad.connect("clicked", lambda *_: set_mode(False))
    hist_btn.connect("clicked", toggle_history)
    hclear.connect("clicked", clear_history)
    histlist.connect("row-activated", on_hist_row)
    display.connect("activate", lambda *_: equals())

    def on_key(_w, event):
        if event.keyval == Gdk.KEY_Escape:
            clear()
            return True
        if event.keyval in (Gdk.KEY_h, Gdk.KEY_H) and \
                event.state & Gdk.ModifierType.CONTROL_MASK:
            toggle_history()
            return True
        return False
    win.connect("key-press-event", on_key)

    ui.insert, ui.clear, ui.equals = insert, clear, equals
    ui.set_mode, ui.toggle_history = set_mode, toggle_history
    ui.refresh_histlist = refresh_histlist

    win.show_all()
    display.grab_focus()
    return ui


def run_gui():
    import gi
    gi.require_version("Gtk", "3.0")
    from gi.repository import Gtk
    ui = build_ui()
    ui.win.connect("destroy", Gtk.main_quit)
    Gtk.main()


# ---------------- CLI / tests ----------------

def selftest():
    cases = [
        ("2+3*4", True, 14), ("(2+3)*4", True, 20), ("10/4", True, 2.5),
        ("-5+3", True, -2), ("7%3", True, 1), ("2^10", True, 1024),
        ("2^3^2", True, 512), ("2^-3", True, 0.125), ("-2^2", True, -4),
        ("sqrt(16)", True, 4), ("ln(e)", True, 1), ("log(100)", True, 2),
        ("5!", True, 120), ("3!!", True, 720), ("abs(-7)", True, 7),
        ("exp(0)", True, 1), ("sin(30)", True, 0.5), ("cos(60)", True, 0.5),
        ("tan(45)", True, 1), ("asin(0.5)", True, 30), ("acos(1)", True, 0),
        ("atan(1)", True, 45), ("sin(pi/2)", False, 1), ("cos(pi)", False, -1),
        ("atan(1)", False, math.pi / 4), ("1.5e3+1", True, 1501),
        ("2E-2*100", True, 2), ("pi", True, math.pi), ("e", True, math.e),
        ("2*(1.5+0.5)", True, 4), ("0.1+0.2", True, 0.30000000000000004),
        ("9^(-1)", True, 1 / 9),
    ]
    ans_cases = [("ans*2", True, 10, 5.0), ("ans+1", True, 6, 5.0)]
    errors = ["1/0", "5%0", "sqrt(-1)", "ln(0)", "log(-2)", "asin(2)",
              "(-1)!", "2.5!", "171!", "2+", "(1+2", "abc", "sin", "1..2",
              "tan(90)", "2^10000", ""]
    n = 0
    for expr, deg, want in cases:
        got = evaluate(expr, deg=deg)
        assert math.isclose(got, want, rel_tol=1e-9, abs_tol=1e-9), \
            f"{expr}: got {got}, want {want}"
        n += 1
    for expr, deg, want, ans in ans_cases:
        got = evaluate(expr, deg=deg, ans=ans)
        assert math.isclose(got, want, rel_tol=1e-9), f"{expr}: got {got}"
        n += 1
    for expr in errors:
        try:
            evaluate(expr)
        except CalcError:
            n += 1
        else:
            raise AssertionError(f"expected error for {expr!r}")
    print(f"selftest: {n} cases passed")


def main():
    args = sys.argv[1:]
    if "--selftest" in args:
        selftest()
        return 0
    if args and args[0] == "--eval":
        deg = "--rad" not in args
        expr = " ".join(a for a in args[1:] if a != "--rad")
        try:
            print(fmt(evaluate(expr, deg=deg)))
            return 0
        except CalcError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
    run_gui()
    return 0


if __name__ == "__main__":
    sys.exit(main())
