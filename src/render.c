#include "peek.h"

static struct { int pad, wmin, fg, bg, flags; } BTN;
static struct { int center, up, lo; } TXT;
Node *FOC;
Node **BTNS;
int NBTN, FOCI;

static int COL;

static void col_nl(void) { putchar('\n'); COL = 0; }

void e_fg(const char *v, int btn) {
    int c = ansi_color(v);
    if (c < 0) return;
    if (btn) BTN.fg = c;
    else printf("\x1b[38;5;%dm", c);
}

void e_bg(const char *v, int btn) {
    int c = ansi_color(v);
    if (c < 0) return;
    if (btn) BTN.bg = c;
    else printf("\x1b[48;5;%dm", c);
}

void e_bgs(const char *v, int btn) {
    char col[32];
    if (!bg_first_color(v, col)) return;
    e_bg(col, btn);
}

void e_bold(const char *v, int btn) {
    if (strcmp(v, "bold")) return;
    if (btn) BTN.flags |= 1;
    else fputs("\x1b[1m", stdout);
}

void e_italic(const char *v, int btn) {
    if (strcmp(v, "italic")) return;
    if (btn) BTN.flags |= 2;
    else fputs("\x1b[3m", stdout);
}

void e_deco(const char *v, int btn) {
    if (!strcmp(v, "underline")) {
        if (btn) BTN.flags |= 4;
        else fputs("\x1b[4m", stdout);
    } else if (!strcmp(v, "line-through")) {
        if (btn) BTN.flags |= 32;
        else fputs("\x1b[9m", stdout);
    }
}

void e_align(const char *v, int btn) {
    if (strcmp(v, "center")) return;
    if (btn) BTN.flags |= 8;
    else TXT.center = 1;
}

void e_trans(const char *v, int btn) {
    if (!strcmp(v, "uppercase")) {
        if (btn) BTN.flags |= 64;
        else TXT.up = 1;
    } else if (!strcmp(v, "lowercase")) {
        if (btn) BTN.flags |= 128;
        else TXT.lo = 1;
    }
}

void e_pad(const char *v, int btn) { if (btn) BTN.pad = atoi(v); }
void e_width(const char *v, int btn) { if (btn) BTN.wmin = atoi(v); }
void e_border(const char *v, int btn) {
    if (btn && !strcmp(v, "none")) BTN.flags |= 16;
}

static void emit_styles(Node *n, int btn) {
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].p->emit) n->st[i].p->emit(n->st[i].v, btn);
}

static int chlen(const char *s, int i, int n) {
    int cl = 1;
    while (i + cl < n && ((unsigned char)s[i + cl] & 0xC0) == 0x80) cl++;
    return cl;
}

static int chwidth(const char *s, int i) {
    return (unsigned char)s[i] < 0x80 ? 1 : 2;
}

static int dispw(const char *s, int n) {
    int w = 0;
    for (int i = 0; i < n; ) { w += chwidth(s, i); i += chlen(s, i, n); }
    return w;
}

static int wordw(const char *s, int n, int i) {
    int w = 0;
    while (i < n && s[i] != ' ') {
        w += chwidth(s, i);
        i += chlen(s, i, n);
    }
    return w;
}

static void put_text(const char *s, int n) {
    for (int i = 0; i < n; ) {
        if (s[i] == ' ') {
            int w = wordw(s, n, i + 1);
            if (COL && COL + 1 + w > 80) { col_nl(); i++; continue; }
            if (COL < 80) { putchar(' '); COL++; }
            i++;
            continue;
        }
        int cl = chlen(s, i, n), cw = chwidth(s, i);
        if (COL + cw > 80) col_nl();
        char ch = s[i];
        if (cl == 1) {
            if (TXT.up) ch = (char)toupper((unsigned char)ch);
            else if (TXT.lo) ch = (char)tolower((unsigned char)ch);
            putchar(ch);
        } else {
            fwrite(s + i, 1, (size_t)cl, stdout);
        }
        COL += cw;
        i += cl;
    }
}

static void hline(int cx, const char *seq, int w) {
    printf("%*s%s+", cx, "", seq);
    for (int i = 0; i < w; i++) putchar('-');
    fputs("+" RESET "\n", stdout);
    COL = 0;
}

void d_br(Node *n) { (void)n; col_nl(); }

void d_hr(Node *n) {
    (void)n;
    puts("-------------------------------------------------------------");
    COL = 0;
}

void d_button(Node *n) {
    char text[256] = "";
    collect_text(n, text, sizeof text);
    memset(&BTN, 0, sizeof BTN);
    BTN.fg = BTN.bg = -1;
    BTN.pad = 1;
    emit_styles(n, 1);
    if (BTN.flags & 64)
        for (char *q = text; *q; q++) *q = toupper((unsigned char)*q);
    else if (BTN.flags & 128)
        for (char *q = text; *q; q++) *q = tolower((unsigned char)*q);
    char seq[64] = "";
    if (BTN.fg >= 0) sprintf(seq + strlen(seq), "\x1b[38;5;%dm", BTN.fg);
    if (BTN.bg >= 0) sprintf(seq + strlen(seq), "\x1b[48;5;%dm", BTN.bg);
    if (BTN.flags & 1) strcat(seq, "\x1b[1m");
    if (BTN.flags & 2) strcat(seq, "\x1b[3m");
    if (BTN.flags & 4) strcat(seq, "\x1b[4m");
    if (BTN.flags & 32) strcat(seq, "\x1b[9m");
    if (n == FOC) strcat(seq, "\x1b[7m");
    int tl = (int)strlen(text), w = tl + BTN.pad * 2;
    if (BTN.wmin > w) w = BTN.wmin;
    int cx = BTN.flags & 8 && w < 80 ? (80 - w) >> 1 : 0;
    if (BTN.flags & 16) {
        printf("%*s%s%s%s\n", cx, "", seq, text, RESET);
        COL = 0;
        return;
    }
    int left = (w - tl) >> 1, right = w - tl - left;
    hline(cx, seq, w);
    printf("%*s%s|%*s%s%*s|%s\n", cx, "", seq, left, "", text, right, "", RESET);
    hline(cx, seq, w);
}

void render(Node *n) {
    const Tag *d = n->def;
    if (d->f & T_TEXTN) {
        Node *p = n->parent;
        if (p && !p->def->draw) {
            memset(&TXT, 0, sizeof TXT);
            for (int i = 0; i < p->nst; i++)
                if (p->st[i].p == &P_ALIGN || p->st[i].p == &P_TRANS)
                    p->st[i].p->emit(p->st[i].v, 0);
            int cx = 0;
            if (TXT.center) {
                int w = dispw(n->text, n->tlen);
                if (w < 80) cx = (80 - w) >> 1;
            }
            if (cx > 0) { printf("%*s", cx, ""); COL += cx; }
            emit_styles(p, 0);
            if (p == FOC) fputs("\x1b[7m", stdout);
            put_text(n->text, n->tlen);
            fputs(RESET, stdout);
        }
        return;
    }
    if (d->f & T_HIDDEN) return;
    for (int i = 0; i < n->nst; i++)
        if (n->st[i].p == &P_DISPLAY && !strcmp(n->st[i].v, "none")) return;
    if (d->draw) { d->draw(n); return; }
    if (d->f & T_LIST) { fputs("  \x1b[36m•\x1b[0m ", stdout); COL += 4; }
    for (int i = 0; i < n->nchild; i++) render(n->child[i]);
    if (d->f & T_BLOCK) col_nl();
}

void draw_dialog(const char *m) {
    int ml = (int)strlen(m);
    int W = ml + 4;
    if (W > 80) W = 80;
    int shown = ml < W - 4 ? ml : W - 4;
    int cx = W < 80 ? (80 - W) >> 1 : 0;
    int left = (W - 2 - shown) >> 1, right = W - 2 - shown - left;
    hline(cx, "", W - 2);
    printf("%*s|%*s%.*s%*s|\n", cx, "", left, "", shown, m, right, "");
    hline(cx, "", W - 2);
}
