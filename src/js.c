#include "peek.h"
#include <time.h>

static JSRuntime *RT;
static JSContext *CTX;
static JSClassID CLS;
static JSValue ELPROTO;
char **LOGS, **ALERTS;
int NLOG, NAL;
static int LCAP, ACAP;

static void tsize(Node *n, size_t *t) {
    if (n->def->f & T_TEXTN) { *t += n->tlen; return; }
    for (int i = 0; i < n->nchild; i++) tsize(n->child[i], t);
}

static void tfill(Node *n, char *b, size_t *w) {
    if (n->def->f & T_TEXTN) {
        memcpy(b + *w, n->text, n->tlen);
        *w += n->tlen;
        return;
    }
    for (int i = 0; i < n->nchild; i++) tfill(n->child[i], b, w);
}

static const char *style_get(Node *n, const char *k, int *vl) {
    const char *s = attr_get(n, "style");
    while (s && *s) {
        const char *e = strchr(s, ';');
        if (!e) e = s + strlen(s);
        const char *c = memchr(s, ':', e - s);
        if (c) {
            const char *ks = s, *ke = c;
            while (ks < ke && ISWS(*ks)) ks++;
            while (ke > ks && ISWS(ke[-1])) ke--;
            int kl = (int)(ke - ks);
            if (kl == (int)strlen(k) && !memcmp(ks, k, kl)) {
                const char *vs = c + 1, *ve = e;
                while (vs < ve && ISWS(*vs)) vs++;
                while (ve > vs && ISWS(ve[-1])) ve--;
                *vl = (int)(ve - vs);
                return vs;
            }
        }
        if (!*e) break;
        s = e + 1;
    }
    return 0;
}

static void style_set(Node *n, const char *k, const char *v) {
    const char *old = attr_get(n, "style");
    size_t cap = (old ? strlen(old) * 2 + 16 : 0) + strlen(k) + strlen(v) + 8;
    char *buf = malloc(cap);
    if (!buf) oom();
    int w = 0;
    const char *s = old;
    while (s && *s) {
        const char *e = strchr(s, ';');
        if (!e) e = s + strlen(s);
        const char *c = memchr(s, ':', e - s);
        if (c) {
            const char *ks = s, *ke = c;
            while (ks < ke && ISWS(*ks)) ks++;
            while (ke > ks && ISWS(ke[-1])) ke--;
            int kl = (int)(ke - ks);
            if (kl && (kl != (int)strlen(k) || memcmp(ks, k, kl))) {
                const char *vs = c + 1, *ve = e;
                while (vs < ve && ISWS(*vs)) vs++;
                while (ve > vs && ISWS(ve[-1])) ve--;
                if (w) buf[w++] = ' ';
                memcpy(buf + w, ks, kl);
                w += kl;
                w += sprintf(buf + w, ": ");
                memcpy(buf + w, vs, ve - vs);
                w += (int)(ve - vs);
                buf[w++] = ';';
            }
        }
        if (!*e) break;
        s = e + 1;
    }
    sprintf(buf + w, "%s%s: %s;", w ? " " : "", k, v);
    attr_set(n, "style", buf);
    free(buf);
}

static JSValue j_qs(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t sl;
    const char *s = JS_ToCStringLen(ctx, &sl, av[0]);
    if (!s) return JS_EXCEPTION;
    int n = qquery(s, sl);
    JS_FreeCString(ctx, s);
    return n ? mk_el(ctx, QL[0]) : JS_NULL;
}

static JSValue j_qsa(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t sl;
    const char *s = JS_ToCStringLen(ctx, &sl, av[0]);
    if (!s) return JS_EXCEPTION;
    int n = qquery(s, sl);
    JS_FreeCString(ctx, s);
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n; i++) JS_SetPropertyUint32(ctx, arr, i, mk_el(ctx, QL[i]));
    return arr;
}

static JSValue j_body(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *b = find_tag(DOM, K4('b', 'o', 'd', 'y'));
    return b ? mk_el(ctx, b) : JS_NULL;
}

static JSValue j_tg(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    size_t tot = 0, w = 0;
    tsize(n, &tot);
    char *b = sdup("", 0);
    char *big = realloc(b, tot + 1);
    if (!big) oom();
    tfill(n, big, &w);
    JSValue r = JS_NewStringLen(ctx, big, w);
    free(big);
    return r;
}

static JSValue j_ts(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    size_t vl;
    const char *v = JS_ToCStringLen(ctx, &vl, av[1]);
    if (!v) return JS_EXCEPTION;
    if (n->def->f & T_TEXTN) {
        n->text = sdup(v, vl);
        n->tlen = (int)vl;
        JS_FreeCString(ctx, v);
        return JS_UNDEFINED;
    }
    n->nchild = 0;
    Node *t = text_node(v, vl);
    t->parent = n;
    push_child(n, t);
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue j_tn(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    return JS_NewString(ctx, n->tag);
}

static JSValue j_sg(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[1]);
    if (!k) return JS_EXCEPTION;
    int vl;
    const char *v = style_get(n, k, &vl);
    JS_FreeCString(ctx, k);
    return v ? JS_NewStringLen(ctx, v, vl) : JS_UNDEFINED;
}

static JSValue j_ss(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[1]), *v = JS_ToCString(ctx, av[2]);
    if (!k || !v) return JS_EXCEPTION;
    style_set(n, k, v);
    JS_FreeCString(ctx, k);
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue j_ga(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)ac;
    Node *n = JS_GetOpaque(thisv, CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[0]);
    if (!k) return JS_EXCEPTION;
    char *v = attr_get(n, k);
    JS_FreeCString(ctx, k);
    return v ? JS_NewString(ctx, v) : JS_NULL;
}

static JSValue j_sa(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)ac;
    Node *n = JS_GetOpaque(thisv, CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[0]), *v = JS_ToCString(ctx, av[1]);
    if (!k || !v) return JS_EXCEPTION;
    attr_set(n, k, v);
    JS_FreeCString(ctx, k);
    JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}

static JSValue j_log(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, av[0]);
    if (!s) return JS_EXCEPTION;
    GROW(LOGS, NLOG, LCAP, char *);
    LOGS[NLOG++] = sdup(s, l);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue j_alert(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t l;
    const char *s = JS_ToCStringLen(ctx, &l, av[0]);
    if (!s) return JS_EXCEPTION;
    GROW(ALERTS, NAL, ACAP, char *);
    ALERTS[NAL++] = sdup(s, l);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue j_proto(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    return JS_DupValue(ctx, ELPROTO);
}

typedef struct { Node *n; JSValue cb; } Lsn;
static Lsn *LS;
static int NLS, LSCAP;

static int ls_add(Node *n, JSValue cb) {
    GROW(LS, NLS, LSCAP, Lsn);
    LS[NLS].n = n;
    LS[NLS].cb = cb;
    return NLS++;
}

static void ls_clear(Node *n) {
    for (int i = 0; i < NLS; i++)
        if (LS[i].n == n) {
            JS_FreeValue(CTX, LS[i].cb);
            LS[i] = LS[--NLS];
            i--;
        }
}

static int ls_first(Node *n) {
    for (int i = 0; i < NLS; i++)
        if (LS[i].n == n) return i;
    return -1;
}

int oc_find(Node *n) { return ls_first(n); }

void js_click(int i) {
    if (i < 0 || i >= NLS) return;
    Node *n = LS[i].n;
    JSValue *cbs = malloc((size_t)NLS * sizeof *cbs);
    if (!cbs) oom();
    int nc = 0;
    for (int j = 0; j < NLS; j++)
        if (LS[j].n == n) cbs[nc++] = JS_DupValue(CTX, LS[j].cb);
    JSValue g = JS_GetGlobalObject(CTX);
    JSValue mk = JS_GetPropertyStr(CTX, g, "__mkevt");
    JSValue ev = JS_UNDEFINED;
    if (JS_IsFunction(CTX, mk)) {
        JSValue el = mk_el(CTX, n);
        JSValue args = el;
        JSValue r = JS_Call(CTX, mk, JS_UNDEFINED, 1, &args);
        JS_FreeValue(CTX, el);
        if (JS_IsException(r)) {
            js_pexc("<evt>");
            JS_FreeValue(CTX, r);
        } else {
            ev = r;
        }
    }
    JS_FreeValue(CTX, mk);
    JS_FreeValue(CTX, g);
    for (int j = 0; j < nc; j++) {
        JSValue el = mk_el(CTX, n);
        JSValue r = JS_Call(CTX, cbs[j], el, 1, &ev);
        JS_FreeValue(CTX, el);
        if (JS_IsException(r)) js_pexc("<click>");
        JS_FreeValue(CTX, r);
        JS_FreeValue(CTX, cbs[j]);
    }
    JS_FreeValue(CTX, ev);
    free(cbs);
}

static JSValue j_oc(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    ls_clear(n);
    if (!JS_IsNull(av[1]) && !JS_IsUndefined(av[1]))
        ls_add(n, JS_DupValue(ctx, av[1]));
    return JS_UNDEFINED;
}

static JSValue j_og(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    int i = ls_first(n);
    return i >= 0 ? JS_DupValue(ctx, LS[i].cb) : JS_UNDEFINED;
}

static JSValue j_oal(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, av[1])) return JS_UNDEFINED;
    ls_add(n, JS_DupValue(ctx, av[1]));
    return JS_UNDEFINED;
}

static JSValue j_pn(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    return n->parent ? mk_el(ctx, n->parent) : JS_NULL;
}

static JSValue j_cn(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n->nchild; i++)
        JS_SetPropertyUint32(ctx, arr, i, mk_el(ctx, n->child[i]));
    return arr;
}

static JSValue j_ac(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *p = JS_GetOpaque(av[0], CLS), *c = JS_GetOpaque(av[1], CLS);
    if (!p || !c) return JS_EXCEPTION;
    dom_append(p, c);
    return JS_DupValue(ctx, av[1]);
}

static JSValue j_ib(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *p = JS_GetOpaque(av[0], CLS), *c = JS_GetOpaque(av[1], CLS);
    if (!p || !c) return JS_EXCEPTION;
    Node *ref = JS_IsObject(av[2]) ? JS_GetOpaque(av[2], CLS) : 0;
    dom_insert_before(p, c, ref);
    return JS_DupValue(ctx, av[1]);
}

static JSValue j_rc(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *p = JS_GetOpaque(av[0], CLS), *c = JS_GetOpaque(av[1], CLS);
    if (!p || !c || c->parent != p) return JS_EXCEPTION;
    dom_remove(c);
    return JS_DupValue(ctx, av[1]);
}

static void ser_put(char **b, size_t *w, size_t *cap, const char *s, size_t l) {
    if (*w + l + 1 > *cap) {
        *cap = (*cap + l + 1) << 1;
        char *nb = realloc(*b, *cap);
        if (!nb) oom();
        *b = nb;
    }
    memcpy(*b + *w, s, l);
    *w += l;
}

static void ser_node(Node *n, char **b, size_t *w, size_t *cap) {
    if (n->def->f & T_TEXTN) {
        ser_put(b, w, cap, n->text, (size_t)n->tlen);
        return;
    }
    char tmp[300];
    int l = snprintf(tmp, sizeof tmp, "<%s", n->tag);
    ser_put(b, w, cap, tmp, (size_t)l);
    for (int i = 0; i < n->nattr; i++) {
        int al = snprintf(tmp, sizeof tmp, " %s=\"", n->attrs[i].k);
        ser_put(b, w, cap, tmp, (size_t)al);
        ser_put(b, w, cap, n->attrs[i].v, strlen(n->attrs[i].v));
        ser_put(b, w, cap, "\"", 1);
    }
    ser_put(b, w, cap, ">", 1);
    for (int i = 0; i < n->nchild; i++) ser_node(n->child[i], b, w, cap);
    if (!(n->def->f & T_VOID)) {
        l = snprintf(tmp, sizeof tmp, "</%s>", n->tag);
        ser_put(b, w, cap, tmp, (size_t)l);
    }
}

static JSValue j_ihg(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    char *b = 0;
    size_t w = 0, cap = 0;
    for (int i = 0; i < n->nchild; i++) ser_node(n->child[i], &b, &w, &cap);
    if (!b) b = sdup("", 0);
    JSValue r = JS_NewStringLen(ctx, b, w);
    free(b);
    return r;
}

static JSValue j_ihs(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    size_t hl;
    const char *h = JS_ToCStringLen(ctx, &hl, av[1]);
    if (!h) return JS_EXCEPTION;
    char *copy = sdup(h, hl);
    JS_FreeCString(ctx, h);
    n->nchild = 0;
    Node *frag = parse_html(copy);
    for (int i = 0; i < frag->nchild; i++) {
        frag->child[i]->parent = n;
        push_child(n, frag->child[i]);
    }
    return JS_UNDEFINED;
}

static JSValue j_ce(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t tl;
    const char *t = JS_ToCStringLen(ctx, &tl, av[0]);
    if (!t || !tl) return JS_ThrowInternalError(ctx, "bad tag name");
    return mk_el(ctx, node(sdup(t, tl)));
}

static JSValue j_ctn(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t tl;
    const char *t = JS_ToCStringLen(ctx, &tl, av[0]);
    if (!t) return JS_EXCEPTION;
    return mk_el(ctx, text_node(t, tl));
}

static JSValue j_ccm(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    size_t tl;
    const char *t = JS_ToCStringLen(ctx, &tl, av[0]);
    if (!t) return JS_EXCEPTION;
    Node *n = node(sdup("#comment", 8));
    if (tl) {
        Node *tx = text_node(t, tl);
        tx->parent = n;
        push_child(n, tx);
    }
    return mk_el(ctx, n);
}

typedef struct { JSValue cb; double when, iv; int alive; } Tmr;
static Tmr *TM;
static int NTM, TCAP;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int tm_add(JSValue cb, double delay, double iv) {
    GROW(TM, NTM, TCAP, Tmr);
    TM[NTM].cb = cb;
    TM[NTM].when = now_ms() + (delay < 0 ? 0 : delay);
    TM[NTM].iv = iv;
    TM[NTM].alive = 1;
    return NTM++;
}

static double tm_ms(JSContext *ctx, JSValueConst v) {
    double ms = 0;
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) JS_ToFloat64(ctx, &ms, v);
    return ms < 0 ? 0 : ms;
}

static JSValue j_sto(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv;
    if (!JS_IsFunction(ctx, av[0])) return JS_ThrowTypeError(ctx, "not a function");
    return JS_NewInt32(ctx, tm_add(JS_DupValue(ctx, av[0]), tm_ms(ctx, av[1]), 0));
}

static JSValue j_siv(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv;
    if (!JS_IsFunction(ctx, av[0])) return JS_ThrowTypeError(ctx, "not a function");
    double iv = tm_ms(ctx, av[1]);
    if (iv <= 0) iv = 1;
    return JS_NewInt32(ctx, tm_add(JS_DupValue(ctx, av[0]), iv, iv));
}

static JSValue j_raf(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    if (!JS_IsFunction(ctx, av[0])) return JS_ThrowTypeError(ctx, "not a function");
    return JS_NewInt32(ctx, tm_add(JS_DupValue(ctx, av[0]), 16, 0));
}

static JSValue j_ct(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    int32_t id;
    if (JS_ToInt32(ctx, &id, av[0])) return JS_EXCEPTION;
    if (id >= 0 && id < NTM) TM[id].alive = 0;
    return JS_UNDEFINED;
}

int js_pump(void) {
    int total = 0;
    for (int round = 0; round < 64; round++) {
        double now = now_ms();
        int fired = 0;
        for (int i = 0; i < NTM; i++) {
            if (!TM[i].alive || TM[i].when > now) continue;
            if (TM[i].iv > 0) {
                TM[i].when += TM[i].iv;
                if (TM[i].when <= now) TM[i].when = now + TM[i].iv;
            } else {
                TM[i].alive = 0;
            }
            JSValue cb = JS_DupValue(CTX, TM[i].cb);
            JSValue r = JS_Call(CTX, cb, JS_UNDEFINED, 0, 0);
            if (JS_IsException(r)) js_pexc("<timer>");
            JS_FreeValue(CTX, r);
            JS_FreeValue(CTX, cb);
            fired = 1;
        }
        JSContext *c1;
        while (JS_ExecutePendingJob(RT, &c1) > 0) {}
        total += fired;
        if (!fired) break;
    }
    return total;
}

double js_next_wait(void) {
    double now = now_ms(), best = -1;
    for (int i = 0; i < NTM; i++) {
        if (!TM[i].alive) continue;
        double w = TM[i].when - now;
        if (best < 0 || w < best) best = w;
    }
    if (best < 0) return -1;
    return best;
}

static const char BOOT[] =
    "(function(){"
    "var P=_proto();"
    "var cc=function(k){return String(k).replace(/[A-Z]/g,"
    "function(c){return '-'+c.toLowerCase()})};"
    "Object.defineProperty(P,'textContent',"
    "{get:function(){return _tg(this)},set:function(v){_ts(this,String(v))}});"
    "Object.defineProperty(P,'tagName',{get:function(){return _tn(this)}});"
    "Object.defineProperty(P,'onclick',"
    "{set:function(v){_oc(this,v)},get:function(){return _og(this)}});"
    "Object.defineProperty(P,'style',{get:function(){var el=this;return new Proxy(el,{"
    "get:function(t,k){if(typeof k=='symbol')return undefined;"
    "if(k==='setProperty')return function(k,v,p){"
    "_ss(el,cc(String(k)),v===undefined?'':String(v))};"
    "if(k==='removeProperty')return function(k){_ss(el,cc(String(k)),'')};"
    "if(k==='getPropertyValue')return function(k){return _sg(el,cc(String(k)))};"
    "if(k==='getPropertyPriority')return function(){return ''};"
    "if(k==='cssText'){var out='',i=0;var attrs=el.attributes;"
    "for(i=0;i<attrs.length;i++)if(attrs[i].name==='style')out=attrs[i].value;return out}"
    "return _sg(el,cc(k))},"
    "set:function(t,k,v){if(typeof k!='symbol'){"
    "if(k==='cssText'){"
    "var parts=String(v).split(';');"
    "for(var j=0;j<parts.length;j++){var pp=parts[j].indexOf(':');"
    "if(pp>0)_ss(el,cc(parts[j].slice(0,pp).trim()),parts[j].slice(pp+1).trim())}"
    "}else _ss(el,cc(k),String(v))}return true}})}});"
    "Object.defineProperty(P,'parentNode',{get:function(){return _pn(this)}});"
    "Object.defineProperty(P,'parentElement',{get:function(){return _pn(this)}});"
    "Object.defineProperty(P,'childNodes',{get:function(){return _cn(this)}});"
    "Object.defineProperty(P,'className',"
    "{get:function(){var v=this.getAttribute('class');return v==null?'':v},"
    "set:function(v){this.setAttribute('class',String(v))}});"
    "Object.defineProperty(P,'id',"
    "{get:function(){var v=this.getAttribute('id');return v==null?'':v},"
    "set:function(v){this.setAttribute('id',String(v))}});"
    "Object.defineProperty(P,'innerHTML',"
    "{get:function(){return _ihg(this)},set:function(v){_ihs(this,String(v))}});"
    "P.appendChild=function(c){return _ac(this,c)};"
    "P.insertBefore=function(c,r){return _ib(this,c,r===undefined||r===null?null:r)};"
    "P.removeChild=function(c){return _rc(this,c)};"
    "P.addEventListener=function(t,f){if(t=='click')_oal(this,f)};"
    "P.removeEventListener=function(){};"
    "Object.defineProperty(P,'nodeType',{get:function(){return _nt(this)}});"
    "Object.defineProperty(P,'data',"
    "{get:function(){return _tg(this)},set:function(v){_ts(this,String(v))}});"
    "Object.defineProperty(P,'nodeValue',"
    "{get:function(){return _tg(this)},set:function(v){_ts(this,String(v))}});"
    "P.removeAttribute=function(k){_ra(this,k)};"
    "P.hasAttribute=function(k){return _ha(this,k)};"
    "Object.defineProperty(P,'firstChild',{get:function(){return _fc(this)}});"
    "Object.defineProperty(P,'nextSibling',{get:function(){return _ns(this)}});"
    "Object.defineProperty(P,'nextElementSibling',{get:function(){return _nes(this)}});"
    "Object.defineProperty(P,'attributes',{get:function(){return _attrs(this)}});"
    "Object.defineProperty(P,'content',{get:function(){return _content(this)}});"
    "P.cloneNode=function(d){return _clone(this,d===undefined?1:d)};"
    "P.remove=function(){if(this.parentNode)this.parentNode.removeChild(this)};"
    "Object.defineProperty(P,'classList',{get:function(){var el=this;return{"
    "contains:function(c){var v=el.getAttribute('class');"
    "return v!=null&&(' '+v+' ').indexOf(' '+c+' ')>=0},"
    "add:function(){for(var i=0;i<arguments.length;i++){var c=String(arguments[i]);"
    "var v=el.getAttribute('class')||'';"
    "if((' '+v+' ').indexOf(' '+c+' ')<0)el.setAttribute('class',(v?v+' ':'')+c)}},"
    "remove:function(){for(var i=0;i<arguments.length;i++){var c=String(arguments[i]);"
    "var v=el.getAttribute('class');if(v==null)continue;"
    "var toks=v.split(/\\s+/),out=[];"
    "for(var j=0;j<toks.length;j++)if(toks[j]&&toks[j]!=c)out.push(toks[j]);"
    "el.setAttribute('class',out.join(' '))}},"
    "toggle:function(c){if(el.classList.contains(c))el.classList.remove(c);"
    "else el.classList.add(c)"
    "}}}});"
    "globalThis.console={log:function(){var a=[];"
    "for(var i=0;i<arguments.length;i++)a.push(String(arguments[i]));"
    "_log(a.join(' '))}};"
    "globalThis.alert=function(m){_alert(String(m))};"
    "globalThis.document={querySelector:function(s){return _qs(s)},"
    "querySelectorAll:function(s){return _qsa(s)}};"
    "Object.defineProperty(globalThis.document,'body',{get:function(){return _body()}});"
    "globalThis.window=globalThis;"
    "globalThis.navigator={userAgent:'peek'};"
    "globalThis.HTMLTemplateElement={};"
    "globalThis.HTMLTemplateElement[Symbol.hasInstance]=function(i){"
    "return i!=null&&i.tagName==='TEMPLATE'};"
    "globalThis.SVGElement={};"
    "globalThis.SVGElement[Symbol.hasInstance]=function(){return false};"
    "globalThis.MathMLElement={};"
    "globalThis.MathMLElement[Symbol.hasInstance]=function(){return false};"
    "globalThis.Element={};"
    "globalThis.Element[Symbol.hasInstance]=function(i){"
    "return i!=null&&i.nodeType===1};"
    "globalThis.Text=function(s){return _ctn(s===undefined?'':String(s))};"
    "globalThis.Comment=function(s){return _ccm(s===undefined?'':String(s))};"
    "globalThis.queueMicrotask=function(f){Promise.resolve().then(f)};"
    "globalThis.setTimeout=function(f,ms){return _sto(f,ms===undefined?0:ms)};"
    "globalThis.clearTimeout=function(i){_ct(i)};"
    "globalThis.setInterval=function(f,ms){return _siv(f,ms===undefined?0:ms)};"
    "globalThis.clearInterval=function(i){_ct(i)};"
    "globalThis.requestAnimationFrame=function(f){return _raf(f)};"
    "globalThis.document.createElement=function(t){return _ce(String(t))};"
    "globalThis.document.createElementNS=function(ns,t){return _ce(String(t))};"
    "globalThis.__mkevt=function(el){return {target:el,currentTarget:el,"
    "type:'click',_vts:Date.now(),preventDefault:function(){},"
    "stopPropagation:function(){},stopImmediatePropagation:function(){}}};"
    "globalThis.document.createTextNode=function(s){return _ctn(String(s))};"
    "globalThis.document.createComment=function(s){return _ccm(String(s))};"
    "})()";

void js_pexc(const char *where) {
    JSValue e = JS_GetException(CTX);
    const char *m = JS_ToCString(CTX, e);
    fprintf(stderr, "js %s: %s\n", where, m ? m : "(exception)");
    if (m) JS_FreeCString(CTX, m);
    JSValue st = JS_GetPropertyStr(CTX, e, "stack");
    const char *s = JS_ToCString(CTX, st);
    if (s) fprintf(stderr, "%s\n", s);
    if (s) JS_FreeCString(CTX, s);
    JS_FreeValue(CTX, st);
    JS_FreeValue(CTX, e);
}

static JSValue j_merr(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    const char *m = JS_ToCString(ctx, av[0]);
    fprintf(stderr, "js module: %s\n", m ? m : "(rejection)");
    if (m) JS_FreeCString(ctx, m);
    JSValue st = JS_GetPropertyStr(ctx, av[0], "stack");
    const char *s = JS_ToCString(ctx, st);
    if (s) fprintf(stderr, "%s\n", s);
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, st);
    return JS_UNDEFINED;
}

static char BASE[600] = ".";

void js_set_base(const char *dir) {
    snprintf(BASE, sizeof BASE, "%s", dir);
}

static char *read_file(const char *path, size_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n < 0) { fclose(f); return 0; }
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); oom(); }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    b[got] = 0;
    *out = got;
    return b;
}

static char *js_path(const char *src) {
    if (src[0] == '/') return sdup(src, strlen(src));
    size_t bl = strlen(BASE), sl = strlen(src);
    char *p = malloc(bl + sl + 2);
    if (!p) oom();
    memcpy(p, BASE, bl);
    p[bl] = '/';
    memcpy(p + bl + 1, src, sl + 1);
    return p;
}

static JSModuleDef *js_module_loader(JSContext *ctx, const char *name, void *opaque) {
    (void)opaque;
    size_t len;
    char *own = 0;
    const char *path = name;
    char *cand = js_path(name);
    FILE *f = fopen(cand, "rb");
    if (f) { fclose(f); path = cand; own = cand; }
    else free(cand);
    char *code = read_file(path, &len);
    free(own);
    if (!code) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s'", name);
        return 0;
    }
    JSValue func = JS_Eval(ctx, code, len, path,
                           JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(code);
    if (JS_IsException(func)) return 0;
    JSModuleDef *m = JS_VALUE_GET_PTR(func);
    JS_FreeValue(ctx, func);
    return m;
}

static void eval_script(Node *n) {
    char *src = attr_get(n, "src");
    char *ty = attr_get(n, "type");
    int ismod = ty && !strcmp(ty, "module");
    char *code;
    size_t clen;
    char fn[640];
    if (src) {
        char *path = js_path(src);
        code = read_file(path, &clen);
        free(path);
        if (!code) {
            fprintf(stderr, "peek: cannot load script '%s'\n", src);
            return;
        }
        snprintf(fn, sizeof fn, "<%s>", src);
    } else {
        if (!n->nchild || !(n->child[0]->def->f & T_TEXTN) || !n->child[0]->tlen)
            return;
        Node *t = n->child[0];
        code = sdup(t->text, (size_t)t->tlen);
        clen = (size_t)t->tlen;
        static int sn;
        snprintf(fn, sizeof fn, "<script#%d>", ++sn);
    }
    if (ismod) {
        JSValue func = JS_Eval(CTX, code, clen, fn,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(func)) {
            js_pexc(fn);
            free(code);
            return;
        }
        if (JS_ResolveModule(CTX, func) < 0) {
            js_pexc(fn);
            JS_FreeValue(CTX, func);
            free(code);
            return;
        }
        JSValue res = JS_EvalFunction(CTX, func);
        if (JS_IsException(res)) {
            js_pexc(fn);
        } else if (JS_IsObject(res)) {
            JSValue then = JS_GetPropertyStr(CTX, res, "then");
            if (JS_IsFunction(CTX, then)) {
                JSValue g = JS_GetGlobalObject(CTX);
                JSValue herr = JS_GetPropertyStr(CTX, g, "__merr");
                if (JS_IsFunction(CTX, herr)) {
                    JSValue args[2] = { JS_UNDEFINED, herr };
                    JSValue r2 = JS_Call(CTX, then, res, 2, args);
                    if (JS_IsException(r2)) js_pexc(fn);
                    JS_FreeValue(CTX, r2);
                }
                JS_FreeValue(CTX, herr);
                JS_FreeValue(CTX, g);
            }
            JS_FreeValue(CTX, then);
        }
        JS_FreeValue(CTX, res);
        JS_FreeValue(CTX, res);
    } else {
        JSValue r = JS_Eval(CTX, code, clen, fn, JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) js_pexc(fn);
        JS_FreeValue(CTX, r);
    }
    free(code);
}

static JSValue j_nt(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    if (n->def->f & T_TEXTN) return JS_NewInt32(ctx, 3);
    if (!strcmp(n->tag, "#comment")) return JS_NewInt32(ctx, 8);
    if (!strcmp(n->tag, "#fragment")) return JS_NewInt32(ctx, 11);
    return JS_NewInt32(ctx, 1);
}

static JSValue j_ra(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[1]);
    if (!k) return JS_EXCEPTION;
    for (int i = 0; i < n->nattr; i++)
        if (!strcmp(n->attrs[i].k, k)) {
            memmove(n->attrs + i, n->attrs + i + 1,
                    (size_t)(n->nattr - i - 1) * sizeof(Attr));
            n->nattr--;
            break;
        }
    JS_FreeCString(ctx, k);
    return JS_UNDEFINED;
}

static JSValue j_ha(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    const char *k = JS_ToCString(ctx, av[1]);
    if (!k) return JS_EXCEPTION;
    char *v = attr_get(n, k);
    JS_FreeCString(ctx, k);
    return JS_NewBool(ctx, v != 0);
}

static JSValue j_fc(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    return n->nchild ? mk_el(ctx, n->child[0]) : JS_NULL;
}

static JSValue j_ns(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    Node *p = n->parent;
    if (!p) return JS_NULL;
    for (int i = 0; i < p->nchild; i++)
        if (p->child[i] == n) {
            if (i + 1 < p->nchild) return mk_el(ctx, p->child[i + 1]);
            return JS_NULL;
        }
    return JS_NULL;
}

static JSValue j_nes(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    Node *p = n->parent;
    if (!p) return JS_NULL;
    int i = 0;
    while (i < p->nchild && p->child[i] != n) i++;
    for (i++; i < p->nchild; i++) {
        Node *s = p->child[i];
        if (!(s->def->f & T_TEXTN) && strcmp(s->tag, "#comment")) return mk_el(ctx, s);
    }
    return JS_NULL;
}

static JSValue j_attrs(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n->nattr; i++) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, n->attrs[i].k));
        JS_SetPropertyStr(ctx, o, "value", JS_NewString(ctx, n->attrs[i].v));
        JS_SetPropertyUint32(ctx, arr, i, o);
    }
    return arr;
}

static JSValue j_content(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac; (void)av;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    if (!n->frag) {
        Node *f = node(sdup("#fragment", 9));
        while (n->nchild) dom_append(f, n->child[0]);
        n->frag = f;
    }
    return mk_el(ctx, n->frag);
}

static Node *clone_node(Node *n, int deep) {
    if (n->def->f & T_TEXTN) return text_node(n->text, (size_t)n->tlen);
    Node *c = node(sdup(n->tag, strlen(n->tag)));
    for (int i = 0; i < n->nattr; i++)
        attr_set(c, n->attrs[i].k, n->attrs[i].v);
    if (deep)
        for (int i = 0; i < n->nchild; i++) {
            Node *k = clone_node(n->child[i], 1);
            k->parent = c;
            push_child(c, k);
        }
    return c;
}

static JSValue j_clone(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    int32_t deep = 0;
    if (ac > 1 && !JS_IsUndefined(av[1])) JS_ToInt32(ctx, &deep, av[1]);
    return mk_el(ctx, clone_node(n, deep));
}

void run_scripts(Node *n) {
    if (n->tagpk == K6('s', 'c', 'r', 'i', 'p', 't') && n->taglen == 6) {
        eval_script(n);
        return;
    }
    for (int i = 0; i < n->nchild; i++) run_scripts(n->child[i]);
}

JSValue mk_el(JSContext *ctx, Node *n) {
    JSValue o = JS_NewObjectProtoClass(ctx, ELPROTO, CLS);
    JS_SetOpaque(o, n);
    return o;
}

void js_init(void) {
    RT = JS_NewRuntime();
    JS_SetModuleLoaderFunc(RT, 0, js_module_loader, 0);
    CTX = JS_NewContext(RT);
    JS_NewClassID(&CLS);
    static const JSClassDef ELDEF = {.class_name = "Element"};
    JS_NewClass(RT, CLS, &ELDEF);
    ELPROTO = JS_NewObject(CTX);
    JS_SetClassProto(CTX, CLS, ELPROTO);
    JS_SetPropertyStr(CTX, ELPROTO, "getAttribute",
        JS_NewCFunction(CTX, j_ga, "getAttribute", 1));
    JS_SetPropertyStr(CTX, ELPROTO, "setAttribute",
        JS_NewCFunction(CTX, j_sa, "setAttribute", 2));
    JSValue g = JS_GetGlobalObject(CTX);
    JS_SetPropertyStr(CTX, g, "_qs", JS_NewCFunction(CTX, j_qs, "_qs", 1));
    JS_SetPropertyStr(CTX, g, "_qsa", JS_NewCFunction(CTX, j_qsa, "_qsa", 1));
    JS_SetPropertyStr(CTX, g, "_tg", JS_NewCFunction(CTX, j_tg, "_tg", 1));
    JS_SetPropertyStr(CTX, g, "_ts", JS_NewCFunction(CTX, j_ts, "_ts", 2));
    JS_SetPropertyStr(CTX, g, "_sg", JS_NewCFunction(CTX, j_sg, "_sg", 2));
    JS_SetPropertyStr(CTX, g, "_ss", JS_NewCFunction(CTX, j_ss, "_ss", 3));
    JS_SetPropertyStr(CTX, g, "_tn", JS_NewCFunction(CTX, j_tn, "_tn", 1));
    JS_SetPropertyStr(CTX, g, "_log", JS_NewCFunction(CTX, j_log, "_log", 1));
    JS_SetPropertyStr(CTX, g, "_alert", JS_NewCFunction(CTX, j_alert, "_alert", 1));
    JS_SetPropertyStr(CTX, g, "_oc", JS_NewCFunction(CTX, j_oc, "_oc", 2));
    JS_SetPropertyStr(CTX, g, "_og", JS_NewCFunction(CTX, j_og, "_og", 1));
    JS_SetPropertyStr(CTX, g, "_oal", JS_NewCFunction(CTX, j_oal, "_oal", 2));
    JS_SetPropertyStr(CTX, g, "_pn", JS_NewCFunction(CTX, j_pn, "_pn", 1));
    JS_SetPropertyStr(CTX, g, "_cn", JS_NewCFunction(CTX, j_cn, "_cn", 1));
    JS_SetPropertyStr(CTX, g, "_ac", JS_NewCFunction(CTX, j_ac, "_ac", 2));
    JS_SetPropertyStr(CTX, g, "_ib", JS_NewCFunction(CTX, j_ib, "_ib", 3));
    JS_SetPropertyStr(CTX, g, "_rc", JS_NewCFunction(CTX, j_rc, "_rc", 2));
    JS_SetPropertyStr(CTX, g, "_ihg", JS_NewCFunction(CTX, j_ihg, "_ihg", 1));
    JS_SetPropertyStr(CTX, g, "_ihs", JS_NewCFunction(CTX, j_ihs, "_ihs", 2));
    JS_SetPropertyStr(CTX, g, "_ce", JS_NewCFunction(CTX, j_ce, "_ce", 1));
    JS_SetPropertyStr(CTX, g, "_ctn", JS_NewCFunction(CTX, j_ctn, "_ctn", 1));
    JS_SetPropertyStr(CTX, g, "_ccm", JS_NewCFunction(CTX, j_ccm, "_ccm", 1));
    JS_SetPropertyStr(CTX, g, "_sto", JS_NewCFunction(CTX, j_sto, "_sto", 2));
    JS_SetPropertyStr(CTX, g, "_siv", JS_NewCFunction(CTX, j_siv, "_siv", 2));
    JS_SetPropertyStr(CTX, g, "_raf", JS_NewCFunction(CTX, j_raf, "_raf", 1));
    JS_SetPropertyStr(CTX, g, "_ct", JS_NewCFunction(CTX, j_ct, "_ct", 1));
    JS_SetPropertyStr(CTX, g, "_nt", JS_NewCFunction(CTX, j_nt, "_nt", 1));
    JS_SetPropertyStr(CTX, g, "__merr", JS_NewCFunction(CTX, j_merr, "__merr", 1));
    JS_SetPropertyStr(CTX, g, "_ra", JS_NewCFunction(CTX, j_ra, "_ra", 2));
    JS_SetPropertyStr(CTX, g, "_ha", JS_NewCFunction(CTX, j_ha, "_ha", 2));
    JS_SetPropertyStr(CTX, g, "_fc", JS_NewCFunction(CTX, j_fc, "_fc", 1));
    JS_SetPropertyStr(CTX, g, "_ns", JS_NewCFunction(CTX, j_ns, "_ns", 1));
    JS_SetPropertyStr(CTX, g, "_nes", JS_NewCFunction(CTX, j_nes, "_nes", 1));
    JS_SetPropertyStr(CTX, g, "_attrs", JS_NewCFunction(CTX, j_attrs, "_attrs", 1));
    JS_SetPropertyStr(CTX, g, "_content", JS_NewCFunction(CTX, j_content, "_content", 1));
    JS_SetPropertyStr(CTX, g, "_clone", JS_NewCFunction(CTX, j_clone, "_clone", 2));
    JS_SetPropertyStr(CTX, g, "_body", JS_NewCFunction(CTX, j_body, "_body", 0));
    JS_SetPropertyStr(CTX, g, "_proto", JS_NewCFunction(CTX, j_proto, "_proto", 0));
    JS_FreeValue(CTX, g);
    JSValue r = JS_Eval(CTX, BOOT, sizeof BOOT - 1, "<boot>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) js_pexc("<boot>");
    JS_FreeValue(CTX, r);
}

void js_done(void) {
    JS_FreeContext(CTX);
    JS_FreeRuntime(RT);
}
