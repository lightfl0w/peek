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
    for (int j = 0; j < nc; j++) {
        JSValue el = mk_el(CTX, n);
        JSValue r = JS_Call(CTX, cbs[j], el, 0, 0);
        JS_FreeValue(CTX, el);
        if (JS_IsException(r)) js_pexc("<click>");
        JS_FreeValue(CTX, r);
        JS_FreeValue(CTX, cbs[j]);
    }
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
    "get:function(t,k){return typeof k=='symbol'?undefined:_sg(el,cc(k))},"
    "set:function(t,k,v){if(typeof k!='symbol')_ss(el,cc(k),String(v));return true}})}});"
    "Object.defineProperty(P,'parentNode',{get:function(){return _pn(this)}});"
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
    "globalThis.queueMicrotask=function(f){Promise.resolve().then(f)};"
    "globalThis.setTimeout=function(f,ms){return _sto(f,ms===undefined?0:ms)};"
    "globalThis.clearTimeout=function(i){_ct(i)};"
    "globalThis.setInterval=function(f,ms){return _siv(f,ms===undefined?0:ms)};"
    "globalThis.clearInterval=function(i){_ct(i)};"
    "globalThis.requestAnimationFrame=function(f){return _raf(f)};"
    "globalThis.document.createElement=function(t){return _ce(String(t))};"
    "globalThis.document.createTextNode=function(s){return _ctn(String(s))};"
    "globalThis.document.createComment=function(s){return _ccm(String(s))};"
    "})()";

void js_pexc(const char *where) {
    JSValue e = JS_GetException(CTX);
    const char *m = JS_ToCString(CTX, e);
    fprintf(stderr, "js %s: %s\n", where, m ? m : "(exception)");
    if (m) JS_FreeCString(CTX, m);
    JS_FreeValue(CTX, e);
}

void run_scripts(Node *n) {
    if (n->tagpk == K6('s', 'c', 'r', 'i', 'p', 't') && n->taglen == 6 && n->nchild &&
        (n->child[0]->def->f & T_TEXTN) && n->child[0]->tlen) {
        Node *t = n->child[0];
        static int sn;
        char fn[24];
        snprintf(fn, sizeof fn, "<script#%d>", ++sn);
        char *code = sdup(t->text, (size_t)t->tlen);
        JSValue r = JS_Eval(CTX, code, t->tlen, fn, JS_EVAL_TYPE_GLOBAL);
        free(code);
        if (JS_IsException(r)) js_pexc(fn);
        JS_FreeValue(CTX, r);
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
