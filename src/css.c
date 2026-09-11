#include "peek.h"

const Prop
    P_COLOR = {"color", P_INH, e_fg},
    P_BG = {"background-color", 0, e_bg},
    P_WEIGHT = {"font-weight", P_INH, e_bold},
    P_FS = {"font-style", P_INH, e_italic},
    P_DECO = {"text-decoration", 0, e_deco},
    P_ALIGN = {"text-align", 0, e_align},
    P_TRANS = {"text-transform", P_INH, e_trans},
    P_PAD = {"padding", 0, e_pad},
    P_WIDTH = {"width", 0, e_width},
    P_BORDER = {"border", 0, e_border},
    P_FSIZE = {"font-size", P_INH, 0},
    P_MARGIN = {"margin", 0, 0},
    P_DISPLAY = {"display", 0, 0};

static const Prop *const PROPS[] = {
    &P_COLOR, &P_BG, &P_WEIGHT, &P_FS, &P_DECO, &P_ALIGN,
    &P_TRANS, &P_PAD, &P_WIDTH, &P_BORDER, &P_FSIZE, &P_MARGIN, &P_DISPLAY,
};

const Prop *prop_find(const char *k) {
    for (size_t i = 0; i < sizeof PROPS / sizeof *PROPS; i++)
        if (!strcmp(PROPS[i]->name, k)) return PROPS[i];
    return 0;
}

void ua_bold(Node *n) { st_push(n, &P_WEIGHT, "bold", -1); }

void ua_link(Node *n) {
    st_push(n, &P_COLOR, "blue", -1);
    st_push(n, &P_DECO, "underline", -1);
}

static Rule *R;
static int NR, RCAP;
static void idx_build(void);

void css_reset(void) { NR = 0; }

void split_decls(char *s, char *e, Rule *r) {
    while (s < e && r->nd < 16) {
        char *semi = memchr(s, ';', e - s);
        char *de = semi ? semi : e;
        char *c = memchr(s, ':', de - s);
        if (c) {
            const Prop *p = prop_find(cut(s, c));
            if (p) {
                r->d[r->nd].p = p;
                r->d[r->nd].v = cut(c + 1, de);
                r->nd++;
            }
        }
        s = de + 1;
    }
}

void presplit(Rule *r, char *sel) {
    char *p = sel;
    r->np = 0;
    r->sep[0] = ' ';
    while (*p && r->np < 8) {
        if (*p == '>' || *p == '+' || *p == '~') {
            r->sep[r->np] = *p;
            *p++ = 0;
            continue;
        }
        while (ISWS(*p)) *p++ = 0;
        if (!*p || *p == '>' || *p == '+' || *p == '~') continue;
        r->ps[r->np++] = p;
        while (*p && !ISWS(*p) && *p != '>' && *p != '+' && *p != '~') p++;
        r->sep[r->np] = ' ';
    }
}

void parse_css(char *css) {
    char *w = css, *p = css;
    while (*p) {
        if (p[0] == '/' && p[1] == '*') {
            char *e = strstr(p + 2, "*/");
            p = e ? e + 2 : p + strlen(p);
            continue;
        }
        *w++ = *p++;
    }
    *w = 0;
    char *pos = css;
    while (*pos) {
        char *b = strchr(pos, '{');
        if (!b) break;
        char *e = strchr(b, '}');
        if (!e) break;
        char *s0 = pos;
        for (char *q = pos; q < b; q++) if (*q == '}') s0 = q + 1;
        Rule tmp = {0};
        split_decls(b + 1, e, &tmp);
        for (char *q = s0; q < b;) {
            char *c = memchr(q, ',', b - q);
            if (!c) c = b;
            if (NR >= RCAP) {
                RCAP = RCAP ? RCAP << 1 : 64;
                Rule *rp = realloc(R, (size_t)RCAP * sizeof *R);
                if (!rp) oom();
                R = rp;
            }
            Rule *r = R + NR++;
            *r = tmp;
            char *sel = cut(q, c);
            if (!*sel) NR--;
            else presplit(r, sel);
            q = c + 1;
        }
        pos = e + 1;
    }
    idx_build();
}

typedef struct { uint64_t k; int *r; int n, cap; } Bkt;
static Bkt *BK;
static int NBK, BCAP;
static int *GEN;
static int NGEN, GCAP;

static void bkt_add(uint64_t k, int ri) {
    for (int i = 0; i < NBK; i++)
        if (BK[i].k == k) {
            GROW(BK[i].r, BK[i].n, BK[i].cap, int);
            BK[i].r[BK[i].n++] = ri;
            return;
        }
    GROW(BK, NBK, BCAP, Bkt);
    BK[NBK].k = k;
    BK[NBK].r = 0;
    BK[NBK].n = 0;
    BK[NBK].cap = 0;
    GROW(BK[NBK].r, BK[NBK].n, BK[NBK].cap, int);
    BK[NBK].r[BK[NBK].n++] = ri;
    NBK++;
}

static void gen_add(int ri) {
    GROW(GEN, NGEN, GCAP, int);
    GEN[NGEN++] = ri;
}

static int bkt_get(uint64_t k, int **out) {
    for (int i = 0; i < NBK; i++)
        if (BK[i].k == k) {
            *out = BK[i].r;
            return BK[i].n;
        }
    return 0;
}

static void idx_build(void) {
    NBK = NGEN = 0;
    for (int i = 0; i < NR; i++) {
        Rule *r = R + i;
        if (!r->np) continue;
        const char *c = r->ps[r->np - 1];
        if (*c == '#' || *c == '.') {
            bkt_add((pk(c + 1) << 2) | (uint64_t)(*c == '.' ? 1 : 2), i);
        } else if (*c == '[' || *c == ':' || *c == '*') {
            gen_add(i);
        } else {
            char buf[64];
            int j = 0;
            while (c[j] && c[j] != '.' && c[j] != '#' && c[j] != '[' &&
                   c[j] != ':' && j < 63) {
                buf[j] = c[j];
                j++;
            }
            buf[j] = 0;
            bkt_add((pk(buf) << 2) | (uint64_t)0, i);
        }
    }
}

static int match_compound(const char *c, Node *n) {
    if (n->tag[0] == '#') return -1;
    int ids = 0, cls = 0, tags = 0, cl = 0;
    char mode = 't', cur[64];
    for (int k = 0;; k++) {
        char ch = c[k];
        if (ch == '.' || ch == '#' || ch == '[' || ch == ']' || ch == ':' || !ch) {
            cur[cl] = 0;
            if (cl) {
                uint64_t ck = pk(cur);
                if (mode == 't') {
                    if (ck != K1('*')) {
                        if (n->tagpk != ck || n->taglen != cl) return -1;
                        tags++;
                    }
                } else if (mode == '.') {
                    const char *v = 0;
                    for (int i = 0; i < n->nattr; i++)
                        if (pk(n->attrs[i].k) == K_CLASS) v = n->attrs[i].v;
                    if (!v) return -1;
                    char buf[256], pat[66];
                    snprintf(buf, sizeof buf, " %s ", v);
                    snprintf(pat, sizeof pat, " %s ", cur);
                    if (!strstr(buf, pat)) return -1;
                    cls++;
                } else if (mode == '#') {
                    const char *v = attr_get(n, "id");
                    if (!v || strcmp(v, cur)) return -1;
                    ids++;
                } else if (mode == ':') {
                    Node *pa = n->parent;
                    if (!pa) return -1;
                    int idx = 0, cnt = 0;
                    for (int i = 0; i < pa->nchild; i++) {
                        Node *sib = pa->child[i];
                        if (sib->tag[0] == '#') continue;
                        if (sib == n) idx = cnt;
                        cnt++;
                    }
                    if (!strcmp(cur, "first-child")) {
                        if (idx) return -1;
                    } else if (!strcmp(cur, "last-child")) {
                        if (idx != cnt - 1) return -1;
                    } else return -1;
                    cls++;
                } else {
                    char *eq = strchr(cur, '='), *val = 0;
                    char op = 0;
                    if (eq) {
                        val = eq + 1;
                        if (eq > cur && (eq[-1] == '^' || eq[-1] == '$' || eq[-1] == '*'))
                            op = *--eq;
                        *eq = 0;
                        size_t wl = strlen(val);
                        if (wl >= 2 && (val[0] == '"' || val[0] == '\'') &&
                            val[wl - 1] == val[0]) {
                            val[wl - 1] = 0;
                            val++;
                        }
                    }
                    const char *v = 0;
                    for (int i = 0; i < n->nattr; i++)
                        if (!strcmp(n->attrs[i].k, cur)) v = n->attrs[i].v;
                    if (!v) return -1;
                    if (!op) {
                        if (val && strcmp(v, val)) return -1;
                    } else if (op == '*') {
                        if (!strstr(v, val)) return -1;
                    } else {
                        size_t vl = strlen(v), pl = strlen(val);
                        if (pl > vl) return -1;
                        if (op == '^' && memcmp(v, val, pl)) return -1;
                        if (op == '$' && memcmp(v + vl - pl, val, pl)) return -1;
                    }
                    cls++;
                }
            }
            if (!ch) break;
            mode = ch == ']' ? 't' : ch;
            cl = 0;
        } else if (cl < 63) cur[cl++] = ch;
    }
    return (ids << 8) | (cls << 4) | tags;
}

int match_selector(const Rule *r, Node *n) {
    if (!r->np) return -1;
    int spec = match_compound(r->ps[r->np - 1], n);
    if (spec < 0) return -1;
    Node *m = n;
    for (int k = r->np - 2; k >= 0; k--) {
        const char *part = r->ps[k];
        Node *hit = 0;
        int s = 0;
        switch (r->sep[k + 1]) {
            case '>':
                if (!m->parent) return -1;
                s = match_compound(part, m->parent);
                if (s < 0) return -1;
                hit = m->parent;
                break;
            case '+': case '~': {
                Node *q = m->parent;
                if (!q) return -1;
                int mi = -1;
                for (int i = 0; i < q->nchild; i++)
                    if (q->child[i] == m) { mi = i; break; }
                if (mi < 0) return -1;
                for (int i = mi - 1; i >= 0; i--) {
                    Node *sib = q->child[i];
                    if (sib->tag[0] == '#') continue;
                    s = match_compound(part, sib);
                    if (s >= 0) { hit = sib; break; }
                    if (r->sep[k + 1] == '+') return -1;
                }
                if (!hit) return -1;
                break;
            }
            default: {
                Node *q = m->parent;
                while (q && (s = match_compound(part, q)) < 0) q = q->parent;
                if (!q) return -1;
                hit = q;
                break;
            }
        }
        spec += s;
        m = hit;
    }
    return spec;
}

static void apply_decls(Rule *r, Node *n, int spec) {
    for (int i = 0; i < r->nd; i++) {
        const Prop *p = r->d[i].p;
        int j = 0;
        while (j < n->nst && n->st[j].p != p) j++;
        if (j == n->nst) st_push(n, p, r->d[i].v, spec);
        else if (!n->st[j].v || spec >= n->st[j].spec) {
            n->st[j].v = r->d[i].v;
            n->st[j].spec = spec;
        }
    }
}

static int *CAND;
static int NCAND, CCAP;

static void cand_push(const int *r, int n) {
    for (int i = 0; i < n; i++) {
        int j = 0;
        while (j < NCAND && CAND[j] != r[i]) j++;
        if (j == NCAND) {
            GROW(CAND, NCAND, CCAP, int);
            CAND[NCAND++] = r[i];
        }
    }
}

void apply_styles(Node *n) {
    if (n->tag[0] != '#') {
        NCAND = 0;
        int *rl;
        int rn = bkt_get(n->tagpk << 2, &rl);
        cand_push(rl, rn);
        for (int i = 0; i < n->nattr; i++) {
            if (pk(n->attrs[i].k) == K_CLASS) {
                const char *cv = n->attrs[i].v;
                char buf[64];
                int bl = 0;
                for (int ci = 0;; ci++) {
                    char ch = cv[ci];
                    if (ch && !ISWS(ch)) {
                        if (bl < 63) buf[bl++] = ch;
                        continue;
                    }
                    buf[bl] = 0;
                    if (bl) {
                        rn = bkt_get((pk(buf) << 2) | (uint64_t)1, &rl);
                        cand_push(rl, rn);
                    }
                    bl = 0;
                    if (!ch) break;
                }
            } else if (!strcmp(n->attrs[i].k, "id")) {
                rn = bkt_get((pk(n->attrs[i].v) << 2) | (uint64_t)2, &rl);
                cand_push(rl, rn);
            }
        }
        cand_push(GEN, NGEN);
        for (int i = 0; i < NCAND; i++) {
            int spec = match_selector(R + CAND[i], n);
            if (spec >= 0) apply_decls(R + CAND[i], n, spec);
        }
        for (int i = 0; i < n->nattr; i++)
            if (!strcmp(n->attrs[i].k, "style")) {
                Rule t = {0};
                split_decls(n->attrs[i].v,
                            n->attrs[i].v + strlen(n->attrs[i].v), &t);
                apply_decls(&t, n, 1 << 16);
            }
    }
    for (int c = 0; c < n->nchild; c++) {
        Node *ch = n->child[c];
        if (ch->tag[0] == '#') continue;
        for (int i = 0; i < n->nst; i++) {
            const Prop *p = n->st[i].p;
            if (!(p->f & P_INH)) continue;
            int j = 0;
            while (j < ch->nst && ch->st[j].p != p) j++;
            if (j == ch->nst) st_push(ch, p, n->st[i].v, -1);
        }
        apply_styles(ch);
    }
}

Node **QL;
int NQL;
static int QCAP;

static void qpush(Node *n) {
    for (int i = 0; i < NQL; i++) if (QL[i] == n) return;
    GROW(QL, NQL, QCAP, Node *);
    QL[NQL++] = n;
}

static void qsel(Node *n, Rule *r) {
    for (int i = 0; i < n->nchild; i++) {
        Node *c = n->child[i];
        if (c->tag[0] != '#' && match_selector(r, c) >= 0) qpush(c);
        qsel(c, r);
    }
}

int qquery_at(Node *root, const char *sel, size_t sl) {
    NQL = 0;
    char *tmp = sdup(sel, sl);
    char *p = tmp;
    while (p && *p) {
        char *cm = strchr(p, ',');
        if (cm) *cm = 0;
        char *seg = cut(p, cm ? cm : p + strlen(p));
        if (*seg) {
            Rule r = {0};
            presplit(&r, seg);
            qsel(root, &r);
        }
        p = cm ? cm + 1 : 0;
    }
    free(tmp);
    return NQL;
}

int qquery(const char *sel, size_t sl) {
    return qquery_at(DOM, sel, sl);
}
