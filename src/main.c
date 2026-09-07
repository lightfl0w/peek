#include "peek.h"
#include <termios.h>
#include <unistd.h>

static char BUF[1 << 16];

static struct termios SAVED;

static void raw_on(void) {
    struct termios t;
    if (tcgetattr(0, &SAVED)) return;
    t = SAVED;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    tcsetattr(0, TCSANOW, &t);
}

static void raw_off(void) { tcsetattr(0, TCSANOW, &SAVED); }

static int getkey(void) {
    unsigned char c;
    if (read(0, &c, 1) != 1) return 'q';
    if (c != 27) return c;
    unsigned char b[2];
    if (read(0, b, 1) != 1) return 'q';
    if (read(0, b + 1, 1) != 1) return 'q';
    if (b[0] == '[' && b[1] == 'A') return 1;
    if (b[0] == '[' && b[1] == 'B') return 2;
    return 0;
}

static void frame(void) {
    fputs(CLEAR, stdout);
    render(DOM);
    if (NLOG) putchar('\n');
    for (int i = 0; i < NLOG; i++) puts(LOGS[i]);
}

static int ASEEN;

static void show_alerts(void) {
    for (; ASEEN < NAL; ASEEN++) {
        putchar('\n');
        draw_dialog(ALERTS[ASEEN]);
        fputs("\x1b[90mpress any key to dismiss\x1b[0m\n", stdout);
        fflush(stdout);
        unsigned char c;
        if (read(0, &c, 1) != 1) return;
    }
}

static void hint(void) {
    if (NBTN)
        fputs("\x1b[90m[\xe2\x86\x91\xe2\x86\x93/Tab] \xe9\x80\x89\xe6\x8b\xa9  [Enter] \xe7\x82\xb9\xe5\x87\xbb  [q] \xe9\x80\x80\xe5\x87\xba\x1b[0m\n", stdout);
    else
        fputs("\x1b[90m[q] \xe9\x80\x80\xe5\x87\xba\x1b[0m\n", stdout);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: %s <file.html>\n", argv[0]), 1;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return perror(argv[1]), 1;
    size_t len = fread(BUF, 1, sizeof BUF - 8, f);
    if (ferror(f)) return perror(argv[1]), 1;
    if (!feof(f))
        fprintf(stderr, "peek: warning: input truncated at %zu bytes\n", sizeof BUF - 8);
    BUF[len] = 0;
    fclose(f);
    fputs(CLEAR, stdout);
    Node *dom = parse_html(BUF);
    DOM = dom;
    js_init();
    run_scripts(dom);
    static char NOCSS[1] = "";
    char *css = NOCSS;
    Node *st = find_tag(dom, K_STYLE);
    if (st && st->nchild && (st->child[0]->def->f & T_TEXTN)) {
        Node *t = st->child[0];
        css = t->text, t->text[t->tlen] = 0;
    }
    parse_css(css);
    apply_styles(dom);
    if (!isatty(0) || !isatty(1)) {
        render(dom);
        if (NLOG) putchar('\n');
        for (int i = 0; i < NLOG; i++) puts(LOGS[i]);
        if (NAL) putchar('\n');
        for (int i = 0; i < NAL; i++) draw_dialog(ALERTS[i]);
        js_done();
        return 0;
    }
    qquery("button", 6);
    NBTN = NQL;
    if (NBTN) {
        BTNS = malloc((size_t)NQL * sizeof *BTNS);
        if (!BTNS) oom();
        memcpy(BTNS, QL, (size_t)NQL * sizeof *BTNS);
        FOC = BTNS[0];
    }
    raw_on();
    fputs("\x1b[?25l", stdout);
    frame();
    show_alerts();
    hint();
    for (;;) {
        int k = getkey();
        if (k == 'q' || k == 3) break;
        if (k == 1 || k == 2 || k == '\t') {
            if (NBTN) {
                FOCI = k == 1 ? (FOCI + NBTN - 1) % NBTN : (FOCI + 1) % NBTN;
                FOC = BTNS[FOCI];
            }
        } else if (k == '\r' || k == '\n' || k == ' ') {
            if (NBTN) {
                js_click(oc_find(BTNS[FOCI]));
                apply_styles(dom);
            }
        }
        frame();
        show_alerts();
        hint();
    }
    js_done();
    raw_off();
    fputs("\x1b[?25h", stdout);
    putchar('\n');
    return 0;
}
