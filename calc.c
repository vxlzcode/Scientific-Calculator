/* calc - a simple command-line calculator for Linux
 * Build: gcc -O2 -Wall -Wextra -o calc calc.c -lm
 * Usage: ./calc "2+3*4"   (one-shot)
 *        ./calc           (interactive REPL, type 'quit' to exit)
 * Supports: + - * / % ( ) decimals, unary minus. No eval/system().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static const char *p;
static char errmsg[128];

static void fail(const char *msg) {
    if (!errmsg[0]) snprintf(errmsg, sizeof errmsg, "%s", msg);
}

static double parse_expr(void);

static double parse_factor(void) {
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+') { p++; return parse_factor(); }
    if (*p == '-') { p++; return -parse_factor(); }
    if (*p == '(') {
        p++;
        double v = parse_expr();
        while (isspace((unsigned char)*p)) p++;
        if (*p == ')') p++; else fail("missing ')'");
        return v;
    }
    if (isdigit((unsigned char)*p) || *p == '.') {
        char *end;
        double v = strtod(p, &end);
        if (end == p) { fail("invalid number"); return NAN; }
        p = end;
        return v;
    }
    if (*p == '\0') fail("unexpected end of expression");
    else {
        char m[64];
        snprintf(m, sizeof m, "unexpected character '%c'", *p);
        fail(m);
    }
    return NAN;
}

static double parse_term(void) {
    double v = parse_factor();
    for (;;) {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '*') { p++; v *= parse_factor(); }
        else if (*p == '/') {
            p++;
            double d = parse_factor();
            if (d == 0) { fail("division by zero"); return NAN; }
            v /= d;
        }
        else if (*p == '%') {
            p++;
            double d = parse_factor();
            if (d == 0) { fail("modulo by zero"); return NAN; }
            v = fmod(v, d);
        }
        else break;
    }
    return v;
}

static double parse_expr(void) {
    double v = parse_term();
    for (;;) {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '+') { p++; v += parse_term(); }
        else if (*p == '-') { p++; v -= parse_term(); }
        else break;
    }
    return v;
}

/* Returns 1 on success and stores result in *out; 0 on error (errmsg set). */
static int evaluate(const char *s, double *out) {
    p = s;
    errmsg[0] = '\0';
    double v = parse_expr();
    while (isspace((unsigned char)*p)) p++;
    if (!errmsg[0] && *p != '\0') {
        char m[64];
        snprintf(m, sizeof m, "unexpected character '%c'", *p);
        fail(m);
    }
    if (errmsg[0]) return 0;
    *out = v;
    return 1;
}

static void print_help(void) {
    puts("Operators: + - * / %  parentheses ( )  unary minus, decimals");
    puts("Commands:  help, quit (or exit / q)");
    puts("Example:   (2+3)*4.5 - 10/4");
}

int main(int argc, char **argv) {
    /* One-shot mode: ./calc "2+3*4" */
    if (argc > 1) {
        char buf[4096];
        buf[0] = '\0';
        for (int i = 1; i < argc; i++) {
            if (i > 1) strncat(buf, " ", sizeof buf - strlen(buf) - 1);
            strncat(buf, argv[i], sizeof buf - strlen(buf) - 1);
        }
        double r;
        if (evaluate(buf, &r)) {
            printf("%.10g\n", r);
            return 0;
        }
        fprintf(stderr, "error: %s\n", errmsg);
        return 1;
    }

    /* Interactive REPL */
    puts("calc - simple calculator. Type 'help' for usage, 'quit' to exit.");
    char line[4096];
    for (;;) {
        printf("calc> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) { putchar('\n'); break; }
        line[strcspn(line, "\r\n")] = '\0';

        char *s = line;
        while (isspace((unsigned char)*s)) s++;
        if (*s == '\0') continue;
        if (!strcmp(s, "quit") || !strcmp(s, "exit") || !strcmp(s, "q")) break;
        if (!strcmp(s, "help")) { print_help(); continue; }

        double r;
        if (evaluate(s, &r)) printf("= %.10g\n", r);
        else printf("error: %s\n", errmsg);
    }
    return 0;
}
