#include "peek.h"

static void parse_attrs(char *s, Node *n) {
    while (*s) {
        while (ISWS(*s)) *s++ = 0;
        if (!*s) break;
        char *k = s, *v = "";
        while (*s && !ISWS(*s) && *s != '=') s++;
        char *ke = s;
        if (*s == '=') {
            *s++ = 0;
            char q = 0;
            if (*s == '"' || *s == '\'') q = *s++;
            v = s;
            while (*s && *s != q && (q || !ISWS(*s))) s++;
            if (*s) *s++ = 0;
        } else if (*s) *s++ = 0;
        *ke = 0;
        unescape(v, strlen(v));
        GROW(n->attrs, n->nattr, n->acap, Attr);
        n->attrs[n->nattr].k = k;
        n->attrs[n->nattr].v = v;
        n->nattr++;
    }
}

static void add_text(char *s, char *e, Node *parent) {
    char sv = *e;
    char *t = cut(s, e);
    if (*t) {
        Node *n = node(N_TEXT);
        n->text = t;
        n->tlen = unescape(t, (int)strlen(t));
        n->parent = parent;
        push_child(parent, n);
    }
    *e = sv;
}

Node *parse_html(char *src) {
    Node **stk = 0;
    int scap = 0, top = 0;
    GROW(stk, 1, scap, Node *);
    stk[top++] = node(N_ROOT);
    char *p = src;
    while (*p) {
        if (*p != '<') {
            char *e = strchr(p, '<');
            if (!e) e = p + strlen(p);
            add_text(p, e, stk[top - 1]);
            p = e;
        } else {
            char *e = strchr(p, '>');
            if (!e) {
                char *nx = strchr(p + 1, '<');
                char *end = nx ? nx : p + strlen(p);
                add_text(p + 1, end, stk[top - 1]);
                p = end;
                continue;
            }
            char *t = cut(p + 1, e);
            p = e + 1;
            if (!strncmp(t, "!--", 3)) {
                char *c = strstr(p, "-->");
                p = c ? c + 3 : p + strlen(p);
            } else if (*t == '!') {
            } else if (*t == '/') {
                while (top > 1 && stk[top - 1]->tagpk != pk(t + 1)) top--;
                if (top > 1) top--;
            } else if (*t) {
                int sc = t[strlen(t) - 1] == '/';
                if (sc) t[strlen(t) - 1] = 0;
                char *sp = strchr(t, ' ');
                Node *n = node(sp ? cut(t, sp) : t);
                if (sp) parse_attrs(sp + 1, n);
                n->parent = stk[top - 1];
                push_child(stk[top - 1], n);
                if (!sc && !(n->def->f & T_VOID)) {
                    GROW(stk, top + 1, scap, Node *);
                    stk[top++] = n;
                }
            }
        }
    }
    Node *root = stk[0];
    free(stk);
    return root;
}
