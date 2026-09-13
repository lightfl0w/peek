#ifndef PEEK_H
#define PEEK_H

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <quickjs.h>

#define WS ((1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13))
#define ISWS(c) ((c) == ' ' || ((unsigned)(c) < 32 && WS >> (c) & 1))

#define RESET "\x1b[0m"
#define CLEAR "\x1b[2J\x1b[H"

#define K1(a) (uint64_t)(unsigned char)(a)
#define K2(a, b) (K1(a) | K1(b) << 8)
#define K3(a, b, c) (K2(a, b) | K1(c) << 16)
#define K4(a, b, c, d) (K3(a, b, c) | K1(d) << 24)
#define K5(a, b, c, d, e) (K4(a, b, c, d) | K1(e) << 32)
#define K6(a, b, c, d, e, f) (K5(a, b, c, d, e) | K1(f) << 40)
#define K7(a, b, c, d, e, f, g) (K6(a, b, c, d, e, f) | K1(g) << 48)
#define K8(a, b, c, d, e, f, g, h) (K7(a, b, c, d, e, f, g) | K1(h) << 56)

#define K_CLASS K5('c', 'l', 'a', 's', 's')
#define K_ID K2('i', 'd')
#define K_STYLE K5('s', 't', 'y', 'l', 'e')
#define K_A K1('a')

void oom(void);
#define GROW(a, n, cap, type) do { \
    if ((n) >= (cap)) { \
        (cap) = (cap) ? (cap) << 1 : 8; \
        void *p_ = realloc((a), (size_t)(cap) * sizeof(type)); \
        if (!p_) oom(); \
        (a) = p_; \
    } \
} while (0)

uint64_t pk(const char *s);
char *cut(char *s, char *e);
int unescape(char *s, int len);
int rgb256(int r, int g, int b);
void hsl_rgb(int h, int s, int l, int *R, int *G, int *B);
int ansi_color(const char *v);
char *sdup(const char *s, size_t n);

typedef struct Node Node;
typedef struct Tag Tag;

typedef struct Prop Prop;
struct Prop { const char *name; uint8_t f; void (*emit)(const char *v, int btn); };
enum { P_INH = 1 };

void e_fg(const char *, int), e_bg(const char *, int), e_bgs(const char *, int);
int bg_first_color(const char *v, char *out);
char *var_expand(const char *v);
void e_bold(const char *, int), e_italic(const char *, int);
void e_deco(const char *, int), e_align(const char *, int);
void e_trans(const char *, int), e_pad(const char *, int);
void e_width(const char *, int), e_border(const char *, int);
void d_br(Node *), d_hr(Node *), d_button(Node *);
void render(Node *n);
void draw_dialog(const char *m);
extern Node **BTNS;
extern Node *FOC;
extern int NBTN, FOCI;

extern const Prop
    P_COLOR, P_BG, P_WEIGHT, P_FS, P_DECO, P_ALIGN,
    P_TRANS, P_PAD, P_WIDTH, P_BORDER, P_FSIZE, P_MARGIN, P_DISPLAY,
    P_BW, P_RADIUS, P_MINW, P_LH, P_POS, P_TOP, P_LEFT,
    P_GAP, P_JUST, P_DIR, P_AI, P_BGS;
const Prop *prop_find(const char *k);
void ua_bold(Node *n);
void ua_link(Node *n);
typedef struct { const Prop *p; const char *v; uint8_t imp; } Decl;
typedef struct { const char *ps[8]; uint8_t sep[9], np; Decl d[16]; int nd; } Rule;
void parse_css(char *css);
void css_reset(void);
void presplit(Rule *r, char *sel);
void split_decls(char *s, char *e, Rule *r);
int match_selector(const Rule *r, Node *n);
void apply_styles(Node *n);
extern Node **QL;
extern int NQL;
int qquery(const char *sel, size_t sl);
int qquery_at(Node *root, const char *sel, size_t sl);

typedef struct Attr { char *k, *v; } Attr;
typedef struct { const Prop *p; const char *v; int spec; } St;

struct Node {
    const Tag *def;
    char *tag; uint64_t tagpk; uint8_t taglen;
    char *text; int tlen;
    Attr *attrs; int nattr, acap;
    Node **child; int nchild, ccap;
    Node *parent;
    St *st; int nst, scap;
    Node *frag;
};

enum { T_VOID = 1, T_BLOCK = 2, T_HIDDEN = 4, T_LIST = 8, T_TEXTN = 16 };
struct Tag { const char *name; uint8_t f; void (*draw)(Node *); void (*ua)(Node *); };

extern char N_TEXT[16], N_ROOT[16];
extern const Tag TAGS[], TAG_ANY;
const Tag *tag_find(const char *s);
extern Node *DOM;
Node *node(char *tag);
Node *text_node(const char *s, size_t n);
void dom_append(Node *p, Node *c);
void dom_insert_before(Node *p, Node *c, Node *ref);
void dom_remove(Node *c);
void push_child(Node *p, Node *c);
void st_push(Node *n, const Prop *p, const char *v, int spec);
char *attr_get(Node *n, const char *k);
void attr_set(Node *n, const char *k, const char *v);
void collect_text(Node *n, char *out, size_t cap);
Node *find_tag(Node *n, uint64_t k);
void css_links(Node *n, Node **out, int *np, int cap);

Node *parse_html(char *src);

int url_is(const char *);
char *url_join(const char *, const char *);
char *http_get(char *, size_t *);
char *http_req(const char *method, const char *url, const char *body, size_t *, int *);

void js_init(void);
void js_done(void);
void js_set_base(const char *dir);
void js_pexc(const char *where);
void run_scripts(Node *n);
void js_load_dyn(Node *n);
JSValue mk_el(JSContext *ctx, Node *n);
void js_fire(Node *n, const char *ty);
char *load_url(const char *src, size_t *out);
int oc_find(Node *n);
void js_click(int i);
int js_pump(void);
double js_next_wait(void);
extern char **LOGS, **ALERTS;
extern int NLOG, NAL;

#endif
