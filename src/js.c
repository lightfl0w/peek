#include "peek.h"

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
    Node *t = node(N_TEXT);
    t->parent = n;
    t->text = sdup(v, vl);
    t->tlen = (int)vl;
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

static Node **OCN;
static JSValue *OCV;
static int NOC, OCCAP;

int oc_find(Node *n) {
    for (int i = 0; i < NOC; i++) if (OCN[i] == n) return i;
    return -1;
}

void js_click(int i) {
    if (i < 0 || i >= NOC || JS_IsUndefined(OCV[i])) return;
    JSValue el = mk_el(CTX, OCN[i]);
    JSValue r = JS_Call(CTX, OCV[i], el, 0, 0);
    JS_FreeValue(CTX, el);
    if (JS_IsException(r)) js_pexc("<click>");
    JS_FreeValue(CTX, r);
}

static JSValue j_oc(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    int i = oc_find(n);
    if (i < 0) {
        if (NOC >= OCCAP) {
            OCCAP = OCCAP ? OCCAP << 1 : 8;
            OCN = realloc(OCN, (size_t)OCCAP * sizeof *OCN);
            OCV = realloc(OCV, (size_t)OCCAP * sizeof *OCV);
            if (!OCN || !OCV) oom();
        }
        i = NOC++;
        OCN[i] = n;
        OCV[i] = JS_UNDEFINED;
    }
    JS_FreeValue(ctx, OCV[i]);
    OCV[i] = JS_DupValue(ctx, av[1]);
    return JS_UNDEFINED;
}

static JSValue j_og(JSContext *ctx, JSValueConst thisv, int ac, JSValueConst *av) {
    (void)thisv; (void)ac;
    Node *n = JS_GetOpaque(av[0], CLS);
    if (!n) return JS_EXCEPTION;
    int i = oc_find(n);
    return i >= 0 ? JS_DupValue(ctx, OCV[i]) : JS_UNDEFINED;
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
    "globalThis.console={log:function(){var a=[];"
    "for(var i=0;i<arguments.length;i++)a.push(String(arguments[i]));"
    "_log(a.join(' '))}};"
    "globalThis.alert=function(m){_alert(String(m))};"
    "globalThis.document={querySelector:function(s){return _qs(s)},"
    "querySelectorAll:function(s){return _qsa(s)}};"
    "Object.defineProperty(globalThis.document,'body',{get:function(){return _body()}})"
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
