#include "peek.h"

char N_TEXT[16] = "#text", N_ROOT[16] = "#root";
Node *DOM;

#define CHUNK 128
typedef struct Chunk { struct Chunk *next; Node n[CHUNK]; } Chunk;
static Chunk FIRST, *CH;
static int CN;

void push_child(Node *p, Node *c) {
    GROW(p->child, p->nchild, p->ccap, Node *);
    p->child[p->nchild++] = c;
}

void st_push(Node *n, const Prop *p, const char *v, int spec) {
    GROW(n->st, n->nst, n->scap, St);
    St *s = n->st + n->nst++;
    s->p = p; s->v = v; s->spec = spec;
}

const Tag TAGS[] = {
    {.name = N_ROOT}, {.name = N_TEXT, .f = T_TEXTN},
    {.name = "html", .f = T_BLOCK}, {.name = "body", .f = T_BLOCK},
    {.name = "head", .f = T_HIDDEN}, {.name = "title", .f = T_HIDDEN},
    {.name = "style", .f = T_HIDDEN}, {.name = "script", .f = T_HIDDEN},
    {.name = "p", .f = T_BLOCK}, {.name = "div", .f = T_BLOCK}, {.name = "span"},
    {.name = "h1", .f = T_BLOCK, .ua = ua_bold}, {.name = "h2", .f = T_BLOCK, .ua = ua_bold},
    {.name = "h3", .f = T_BLOCK, .ua = ua_bold}, {.name = "h4", .f = T_BLOCK, .ua = ua_bold},
    {.name = "h5", .f = T_BLOCK, .ua = ua_bold}, {.name = "h6", .f = T_BLOCK, .ua = ua_bold},
    {.name = "ul", .f = T_BLOCK}, {.name = "ol", .f = T_BLOCK},
    {.name = "li", .f = T_BLOCK | T_LIST},
    {.name = "header", .f = T_BLOCK}, {.name = "footer", .f = T_BLOCK},
    {.name = "section", .f = T_BLOCK}, {.name = "article", .f = T_BLOCK},
    {.name = "nav", .f = T_BLOCK}, {.name = "aside", .f = T_BLOCK},
    {.name = "main", .f = T_BLOCK}, {.name = "blockquote", .f = T_BLOCK},
    {.name = "figure", .f = T_BLOCK}, {.name = "figcaption", .f = T_BLOCK},
    {.name = "br", .f = T_VOID, .draw = d_br},
    {.name = "hr", .f = T_VOID | T_BLOCK, .draw = d_hr},
    {.name = "button", .draw = d_button},
};

const Tag TAG_ANY = {0};

const Tag *tag_find(const char *s) {
    for (size_t i = 0; i < sizeof TAGS / sizeof *TAGS; i++)
        if (!strcmp(TAGS[i].name, s)) return TAGS + i;
    return &TAG_ANY;
}

Node *node(char *tag) {
    if (!CH) CH = &FIRST;
    if (CN >= CHUNK) {
        Chunk *c = calloc(1, sizeof *c);
        if (!c) oom();
        CH->next = c; CH = c; CN = 0;
    }
    Node *n = CH->n + CN++;
    n->tag = tag;
    n->tagpk = pk(tag);
    n->taglen = (uint8_t)strlen(tag);
    n->def = tag_find(tag);
    if (n->def->ua) n->def->ua(n);
    return n;
}

char *attr_get(Node *n, const char *k) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->attrs[i].k, k)) return n->attrs[i].v;
    return 0;
}

void attr_set(Node *n, const char *k, const char *v) {
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->attrs[i].k, k)) {
            n->attrs[i].v = sdup(v, strlen(v));
            return;
        }
    GROW(n->attrs, n->nattr, n->acap, Attr);
    n->attrs[n->nattr].k = sdup(k, strlen(k));
    n->attrs[n->nattr].v = sdup(v, strlen(v));
    n->nattr++;
}

void collect_text(Node *n, char *out, size_t cap) {
    if (n->def->f & T_TEXTN) {
        int room = (int)cap - (int)strlen(out) - 1;
        strncat(out, n->text, n->tlen < room ? n->tlen : room);
        return;
    }
    for (int i = 0; i < n->nchild; i++) collect_text(n->child[i], out, cap);
}

Node *find_tag(Node *n, uint64_t k) {
    if (n->tagpk == k) return n;
    for (int i = 0; i < n->nchild; i++) {
        Node *r = find_tag(n->child[i], k);
        if (r) return r;
    }
    return NULL;
}
