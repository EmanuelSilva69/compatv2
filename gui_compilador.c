/*
 * =====================================================================================
 *  gui_compilador.c  -  Compilador/Analisador Sintatico com Recuperacao em Modo Panico
 *  Interface grafica em C99 puro (arquivo unico) sobre a biblioteca RAYLIB 5.5
 *  Visual: IDE escura sobria (paleta #18181b/#1e1e2e/#27272a, sem neon), estilo
 *          VS Code / Linear: barra superior com acoes + velocidade da animacao,
 *          editor + fita de tokens (moldura do scanner com mola) no centro, consola
 *          em 4 abas (Problemas/Consola/Gramatica/Conjuntos), inspetor lateral
 *          colapsavel e rolavel com justificativa didatica, rodape com "Token X de Y".
 *          Animacoes por lerp/mola amortecida com GetFrameTime: chips de token com
 *          elevacao, tic de confirmacao, tremor no erro, tachado animado, entradas
 *          com fade/slide e destaque luminoso da regra/conjunto ativo.
 *          Lexico: identificador [A-Za-z][A-Za-z0-9]* (var1 e um so token).
 *          Sintaxe: anti-cascata - 1 mensagem de erro por match; depois de um
 *          erro o parser varre a frente sem mensagens ate ao proximo match.
 * =====================================================================================
 *
 *  COMPILACAO
 *  ----------
 *  Linux:
 *      gcc gui_compilador.c -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -o gui_compilador
 *      (libs locais:  gcc gui_compilador.c -I. -L. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -o gui_compilador)
 *
 *  macOS:
 *      gcc gui_compilador.c -I. -L. -lraylib -lm -framework Cocoa -framework OpenGL \
 *          -framework IOKit -framework CoreVideo -o gui_compilador
 *
 *  Windows (MinGW-w64, GCC 64 bits - as libs raylib deste diretorio sao x86-64):
 *      gcc gui_compilador.c -lraylib -lopengl32 -lgdi32 -lwinmm -o gui_compilador.exe
 *      com as libs locais:
 *          gcc gui_compilador.c -I. -L. -lraylib -lopengl32 -lgdi32 -lwinmm -o gui_compilador.exe
 *      ex.:  C:\Strawberry\c\bin\gcc.exe   ou   C:\msys64\ucrt64\bin\gcc.exe
 *      (o gcc i686 de C:\mingw64\bin nao serve: as libs raylib da pasta sao de 64 bits)
 *
 *  MODOS DE TESTE SEM JANELA (logica + verificacao de vazamento de memoria):
 *      gui_compilador --selftest          roda os 5 exemplos e mostra o heap vivo
 *      gui_compilador --smoke 3           abre a janela, analisa e fecha sozinho (3 s)
 *
 *  AJUDAS DE VERIFICACAO VISUAL:
 *      --input "<linha>"                  linha de partida (padrao: ultimo exemplo)
 *      --shot <ficheiro.png>              guarda um PNG quando a analise termina
 *      --tab <0|1|2|3>                    Problemas/Consola/Gramatica/Conjuntos
 *      --no-insp                          arranca com o inspetor lateral recolhido
 *
 *  ATALHOS
 *  -------
 *      F5  Executar / Pausar   F10  Passo a passo   F6  Passo anterior
 *      F8  Resetar   ESC  Sair
 *      ENTER no campo de entrada tambem executa a analise
 *
 *  ARQUITETURA (4 blocos claros)
 *  -----------------------------
 *      SECAO 1 : tipos e dados - tokens, fita de eventos, conjuntos PRIMEIROS/
 *                SEQUENCIA (tabelas formais) e estruturas partilhadas
 *      SECAO 2 : motor - lexer + parser descendente recursivo com modo panico
 *                (independente da Raylib; produz uma FITA DE EVENTOS deterministica)
 *      SECAO 3 : maquina de estados de animacao (View + Player) que consome os
 *                eventos e alimenta a justificativa didatica em tempo real
 *      SECAO 4 : interface grafica Raylib (desenho, widgets, maquina de estados UI)
 *
 *  MEMORIA (correcao do vazamento)
 *  -------------------------------
 *      Todo heap passa por xmalloc/xrealloc/xfree com contadores de blocos e bytes.
 *      A analise e libertada em cada re-analise, no reset e no fim do programa;
 *      as fontes sao descarregadas com UnloadFont antes de CloseWindow().
 *      --selftest e a saida normal imprimem "[mem] ... blocos" (tem de ser 0).
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>

#include "raylib.h"

/* rlgl.h nao acompanha o projeto: prototipo da funcao que descarrega o lote de
   vertices (necessario antes de TakeScreenshot, que le o framebuffer diretamente) */
extern void rlDrawRenderBatchActive(void);

#ifndef Clamp
#define Clamp(value, min, max) ((value) < (min) ? (min) : ((value) > (max) ? (max) : (value)))
#endif

/* ===================================================================================== */
/*  PARAMETROS GERAIS                                                                   */
/* ===================================================================================== */

#define MAX_LINE     512
#define MAX_TOKENS   300
#define TOK_TEXT      64
#define MAX_MSG       32
#define MSG_LEN      240
#define MAX_LOG       64
#define LOG_LEN      240
#define WRAP_LEN     200
#define WRAP_MAX       6

/* ===================================================================================== */
/*  SECAO 1 - TIPOS E DADOS COMPARTILHADOS                                              */
/*  (tokens, fita de eventos, conjuntos PRIMEIROS/SEQUENCIA, heap rastreado - sem Raylib) */
/* ===================================================================================== */

/* ---------- rastreador de heap: qualquer vazamento fica evidente nos contadores ----- */
typedef union { long double ld; size_t sz; } MemHdr;

static long g_mem_blocks = 0;
static long g_mem_bytes  = 0;
static long g_mem_peak   = 0;

static void *xmalloc(size_t n)
{
    MemHdr *h = (MemHdr *)malloc(sizeof(MemHdr) + n);
    if (!h) { fprintf(stderr, "[fatal] sem memoria para %lu bytes\n", (unsigned long)n); exit(EXIT_FAILURE); }
    h->sz = n;
    g_mem_blocks++;
    g_mem_bytes += (long)n;
    if (g_mem_bytes > g_mem_peak) g_mem_peak = g_mem_bytes;
    return (void *)((char *)h + sizeof(MemHdr));
}

static void *xrealloc(void *p, size_t n)
{
    MemHdr *h, *h2;
    long old;
    if (!p) return xmalloc(n);
    h  = (MemHdr *)((char *)p - sizeof(MemHdr));
    old = (long)h->sz;
    h2 = (MemHdr *)realloc(h, sizeof(MemHdr) + n);
    if (!h2) { fprintf(stderr, "[fatal] falha ao realocar %lu bytes\n", (unsigned long)n); exit(EXIT_FAILURE); }
    h2->sz = n;
    g_mem_bytes += (long)n - old;
    if (g_mem_bytes > g_mem_peak) g_mem_peak = g_mem_bytes;
    return (void *)((char *)h2 + sizeof(MemHdr));
}

static void xfree(void *p)
{
    MemHdr *h;
    if (!p) return;
    h = (MemHdr *)((char *)p - sizeof(MemHdr));
    g_mem_blocks--;
    g_mem_bytes -= (long)h->sz;
    free(h);
}

/* ---------- tokens ------------------------------------------------------------------ */
typedef enum {
    T_EOF = 0, T_ID, T_NUM, T_READ, T_WRITE, T_ASSIGN, T_PLUS, T_STAR, T_LEXERR, T_COUNT
} TokType;

typedef enum {
    ST_PENDING = 0, ST_SCANNED, ST_OK, ST_ERR, ST_DISCARD, ST_LEXERR
} TokStatus;

typedef struct {
    TokType type;
    char    text[TOK_TEXT];
    int     col;
} Token;

static const char *type_name(TokType t)
{
    switch (t) {
    case T_ID:     return "ID";
    case T_NUM:    return "num";
    case T_READ:   return "read";
    case T_WRITE:  return "write";
    case T_ASSIGN: return ":=";
    case T_PLUS:   return "+";
    case T_STAR:   return "*";
    case T_EOF:    return "$";
    default:       return "erro lexico";
    }
}

static const char *status_name(int st)
{
    switch (st) {
    case ST_SCANNED: return "lido";
    case ST_OK:      return "aceite";
    case ST_ERR:     return "erro";
    case ST_DISCARD: return "descartado";
    case ST_LEXERR:  return "erro lexico";
    default:         return "pendente";
    }
}

/* ---------- fita de eventos (trace deterministico do parser) ------------------------- */
typedef enum {
    EV_START = 0,   /* inicio da etapa lexica                                     */
    EV_LEX,         /* scanner leu o token i                                      */
    EV_STARTPARSE,  /* inicio da etapa sintatica                                  */
    EV_RULE,        /* regra sintatica ativa (i) + esperado + conjunto de sequencia*/
    EV_MATCH,       /* token i aceite (match)                                     */
    EV_ERROR,       /* falha no token i -> mensagem no padrao exigido             */
    EV_DISCARD,     /* token i descartado pelo modo panico                        */
    EV_SYNC,        /* sincronizacao encontrada                                   */
    EV_END          /* analise concluida                                          */
} EvType;

typedef struct {
    unsigned char type;
    short         tok;        /* indice do token (-1 = nao aplicavel) */
    char          text[200];
    char          aux[132];
    char          aux2[132];
} Ev;

typedef struct {
    char   source[MAX_LINE + 4];
    int    srclen;
    Token  tok[MAX_TOKENS];
    int    ntok;
    Ev    *ev;
    int    nev, evcap;
    int    errors;
    char   msgs[MAX_MSG][MSG_LEN];
    int    nmsgs;
} Analysis;

/* ---------- conjuntos PRIMEIROS / SEQUENCIA (tabelas do trabalho) -------------------- */
#define SM(t)            (1u << (t))
#define PRIM_STMT        (SM(T_READ) | SM(T_WRITE) | SM(T_ID))   /* PRIMEIROS(<instrucao>) */
#define SEQ_READ         SM(T_ID)                                /* SEQUENCIA(read)        */
#define SEQ_WRITE        SM(T_ID)                                /* SEQUENCIA(write)       */
#define SEQ_ASSIGN       (SM(T_ID) | SM(T_NUM))                  /* SEQUENCIA(:=)          */
#define SEQ_ID           (SM(T_PLUS) | SM(T_STAR) | SM(T_ASSIGN) | SM(T_EOF))
#define SEQ_NUM          (SM(T_PLUS) | SM(T_STAR) | SM(T_EOF))
#define SEQ_PLUS         (SM(T_ID) | SM(T_NUM))
#define SEQ_STAR         (SM(T_ID) | SM(T_NUM))
#define SEQ_NONE         0u
/* recuperacao padrao: conjunto de sequencia + PRIMEIROS de nova linha + fim ($) */
#define RECOV(s)         ((s) | PRIM_STMT | SM(T_EOF))

/* ---------- regras da GLC ------------------------------------------------------------ */
static const char RULE_STMT[]   = "<instrução> -> <read> | <write> | <atribuição>";
static const char RULE_READ[]   = "<read> -> read identificador";
static const char RULE_WRITE[]  = "<write> -> write identificador";
static const char RULE_ASSIGN[] = "<atribuição> -> identificador := <exp aritmética>";
static const char RULE_EXPR[]   = "<exp aritmética> -> <operando> {<op aritmético> <operando>}";
static const char RULE_OPER[]   = "<operando> -> identificador | num";
static const char RULE_ARITH[]  = "<op aritmético> -> + | *";
static const char MSG_OK[]      = "declaração sintaticamente correta";

/* ---------- mensagens "Esperado ..." ------------------------------------------------- */
static const char EXP_STMT[]      = "ID ou palavra reservada (\"read\" ou \"write\")";
static const char EXP_ID[]        = "ID";
static const char EXP_ASSIGN[]    = ":=";
static const char EXP_OPER[]      = "ID ou num";
static const char EXP_AFTER_ID[]  = "+, *, :=, $";
static const char EXP_AFTER_NUM[] = "+, *, $";

/* ---------- rotulos dos conjuntos de sequencia --------------------------------------- */
static const char SEQ_STMT_D[]    = "PRIMEIROS(<instrução>) = { read, write, identificador }";
static const char SEQ_READ_D[]    = "SEQUÊNCIA(read) = { identificador }";
static const char SEQ_WRITE_D[]   = "SEQUÊNCIA(write) = { identificador }";
static const char SEQ_ASSIGN_D[]  = "SEQUÊNCIA(:=) = { identificador, num }";
static const char SEQ_ID_D[]      = "SEQUÊNCIA(identificador) = { +, *, :=, $ }";
static const char SEQ_NUM_D[]     = "SEQUÊNCIA(num) = { +, *, $ }";
static const char SEQ_PLUS_D[]    = "SEQUÊNCIA(+) = { identificador, num }";
static const char SEQ_STAR_D[]    = "SEQUÊNCIA(*) = { identificador, num }";

/* ---------- tabelas formais (painel didatico PRIMEIROS / SEQUENCIA) ------------------ */
typedef struct {
    const char *name;   /* rotulo formal da linha, ex.: "SEQUÊNCIA(:=)"   */
    unsigned    mask;   /* mascara de tokens da tabela                    */
    const char *desc;   /* representacao  { ... }                         */
    const char *note;   /* frase didatica curta                           */
} SetRow;

static const SetRow PRIM_ROWS[] = {
    { "PRIMEIROS(<read>)",       SM(T_READ),  "{ read }",
      "todo <read> comeca no terminal read" },
    { "PRIMEIROS(<write>)",      SM(T_WRITE), "{ write }",
      "todo <write> comeca no terminal write" },
    { "PRIMEIROS(<atribuição>)", SM(T_ID),    "{ identificador }",
      "toda atribuicao comeca no identificador" },
    { "PRIMEIROS(<instrução>)",  PRIM_STMT,   "{ read, write, identificador }",
      "uniao dos PRIMEIROS das tres producoes" }
};
#define N_PRIM_ROWS ((int)(sizeof PRIM_ROWS / sizeof PRIM_ROWS[0]))

static const SetRow SEQ_ROWS[] = {
    { "SEQUÊNCIA(read)",         SEQ_READ,   "{ identificador }",
      "depois de <read> so pode vir um identificador" },
    { "SEQUÊNCIA(write)",        SEQ_WRITE,  "{ identificador }",
      "depois de <write> so pode vir um identificador" },
    { "SEQUÊNCIA(:=)",           SEQ_ASSIGN, "{ identificador, num }",
      "depois de := espera-se um operando" },
    { "SEQUÊNCIA(identificador)", SEQ_ID,    "{ +, *, :=, $ }",
      "fim da instrucao ou continuacao da expressao" },
    { "SEQUÊNCIA(num)",          SEQ_NUM,    "{ +, *, $ }",
      "continuacao aritmetica ou fim de instrucao" },
    { "SEQUÊNCIA(+)",            SEQ_PLUS,   "{ identificador, num }",
      "depois de + espera-se outro operando" },
    { "SEQUÊNCIA(*)",            SEQ_STAR,   "{ identificador, num }",
      "depois de * espera-se outro operando" }
};
#define N_SEQ_ROWS ((int)(sizeof SEQ_ROWS / sizeof SEQ_ROWS[0]))

/* estado do construtor de eventos */
static Analysis *gA  = NULL;
static int        gPp = 0;
static int        g_armado = 1;  /* anti-cascata: 1 = desde o ultimo match (pode
                                    emitir erro); 0 = varrer a frente sem mensagens
                                    ate ao proximo match (slides p1, p10)        */

/* descreve uma mascara:  { ID, read, write, $ } */
static void mask_names(unsigned m, char *dst, size_t cap)
{
    static const TokType ord[] = { T_ID, T_NUM, T_READ, T_WRITE, T_ASSIGN, T_PLUS, T_STAR, T_EOF };
    static const char *nms[]   = { "ID", "num", "read", "write", ":=", "+", "*", "$" };
    size_t k = 0;
    int i, n = 0, total = 0;

    if (cap == 0) return;
    dst[0] = '\0';
    for (i = 0; i < 8; i++) if (m & SM(ord[i])) total++;
    for (i = 0; i < 8; i++) {
        int w;
        if (!(m & SM(ord[i]))) continue;
        n++;
        w = snprintf(dst + k, (k < cap) ? cap - k : 0, "%s%s",
                     (n == 1) ? "" : ", ", nms[i]);
        if (w < 0) break;
        k += (size_t)w;
        if (k >= cap) { dst[cap - 1] = '\0'; break; }
    }
    if (total == 0) snprintf(dst, cap, "(vazio)");
}

/* ===================================================================================== */
/*  SECAO 2 - MOTOR: LEXICO + PARSE DESCENDENTE RECURSIVO COM MODO PANICO               */
/*  (nao usa Raylib - produz a fita de eventos; testavel com --selftest)                */
/* ===================================================================================== */

static void ev_push(EvType ty, int tok, const char *text, const char *aux, const char *aux2)
{
    Analysis *a = gA;
    Ev *e;
    if (!a) return;
    if (a->nev == a->evcap) {
        a->evcap = a->evcap ? a->evcap * 2 : 160;
        a->ev = (Ev *)xrealloc(a->ev, (size_t)a->evcap * sizeof(Ev));
    }
    e = &a->ev[a->nev++];
    memset(e, 0, sizeof(*e));
    e->type = (unsigned char)ty;
    e->tok  = (short)tok;
    snprintf(e->text, sizeof e->text, "%s", text ? text : "");
    snprintf(e->aux,  sizeof e->aux,  "%s", aux  ? aux  : "");
    snprintf(e->aux2, sizeof e->aux2, "%s", aux2 ? aux2 : "");
}

static TokType peek(void)
{
    if (!gA || gPp < 0 || gPp >= gA->ntok) return T_EOF;
    return gA->tok[gPp].type;
}

static const char *peek_text(void)
{
    if (!gA || gPp < 0 || gPp >= gA->ntok) return "$";
    return gA->tok[gPp].text;
}

/* ---------- LEXICO ------------------------------------------------------------------ */
static void tokenize(Analysis *a)
{
    const char *s = a->source;
    int i = 0, n = 0, has_end = 0;

    a->ntok = 0;
    while (s[i] != '\0' && n < MAX_TOKENS - 1) {
        unsigned char c = (unsigned char)s[i];
        Token *t = &a->tok[n];
        int start, len;

        if (isspace(c)) { i++; continue; }          /* espaco, \t, \r, \n */
        t->col = i;
        t->text[0] = '\0';

        if (c == '$') {                              /* fim de comando/linha */
            t->type = T_EOF;
            strcpy(t->text, "$");
            has_end = 1;
            i++;
            n++;
            break;
        }

        start = i;
        if (isalpha(c)) {                            /* [a-zA-Z][a-zA-Z0-9]* */
            while (s[i] != '\0' && isalnum((unsigned char)s[i])) i++;
            len = i - start;
            if (len >= TOK_TEXT) len = TOK_TEXT - 1;
            memcpy(t->text, s + start, (size_t)len);
            t->text[len] = '\0';
            if (strcmp(t->text, "read") == 0)       t->type = T_READ;
            else if (strcmp(t->text, "write") == 0) t->type = T_WRITE;
            else                                    t->type = T_ID;
        } else if (isdigit(c)) {                     /* [0-9]+ */
            while (s[i] != '\0' && isdigit((unsigned char)s[i])) i++;
            len = i - start;
            if (len >= TOK_TEXT) len = TOK_TEXT - 1;
            memcpy(t->text, s + start, (size_t)len);
            t->text[len] = '\0';
            t->type = T_NUM;
        } else if (c == ':' && s[i + 1] == '=') {
            i += 2;
            t->type = T_ASSIGN;
            strcpy(t->text, ":=");
        } else if (c == '+') {
            i++;
            t->type = T_PLUS;
            strcpy(t->text, "+");
        } else if (c == '*') {
            i++;
            t->type = T_STAR;
            strcpy(t->text, "*");
        } else {                                     /* caractere invalido */
            i++;
            t->type = T_LEXERR;
            t->text[0] = (char)c;
            t->text[1] = '\0';
        }
        n++;
    }

    if (!has_end && n < MAX_TOKENS) {                /* sintetico "$" */
        Token *t = &a->tok[n];
        t->type = T_EOF;
        strcpy(t->text, "$");
        t->col = a->srclen;
        n++;
    }
    a->ntok = n;
}

/* linha de contexto didatico: de que conjunto parte a decisao do modo panico        */
static void set_look(unsigned seq, const char *seq_disp, char *dst, size_t cap)
{
    if (seq == SEQ_NONE || !seq_disp || !seq_disp[0])
        snprintf(dst, cap, "%s", seq_disp ? seq_disp : "-");
    else
        snprintf(dst, cap, "%s ou PRIMEIROS(<instrução>)", seq_disp);
}

/* ---------- MODO PANICO -------------------------------------------------------------- */
static void vis_match(const char *rule, const char *expected, const char *seq_disp);
/* descarta tokens ate um simbolo de SEQUENCIA, um PRIMEIRO de nova linha ou "$".
   Se a marca esperada (exp_mask) for encontrada durante a varredura, faz MATCH
   (recupera a producao) em vez de sincronizar — e.g. var.:=temp: o ":=" e a
   marca esperada, faz MATCH e continua a atribuicao (1 erro, nao 2). */
static int panic_sync(unsigned seq, const char *seq_disp, unsigned exp_mask,
                      const char *rule, const char *expected)
{
    unsigned rec = RECOV(seq) | exp_mask;
    char disp[132], buf[200], look[200];
    int guard = 0;

    mask_names(rec, disp, sizeof disp);
    set_look(seq, seq_disp, look, sizeof look);

    while (peek() != T_EOF && !(rec & SM(peek())) && guard++ < MAX_TOKENS) {
        snprintf(buf, sizeof buf, "modo pânico: descartando \"%s\" - à procura de sincronização",
                 peek_text());
        ev_push(EV_DISCARD, gPp, buf, look, disp);
        gPp++;
    }

    /* marca esperada encontrada: MATCH e a producao continua */
    if (peek() != T_EOF && (exp_mask & SM(peek()))) {
        vis_match(rule, expected, seq_disp);
        return 1;
    }

    if (peek() == T_EOF)
        snprintf(buf, sizeof buf, "modo pânico: sincronizado em fim de linha ($)");
    else
        snprintf(buf, sizeof buf, "modo pânico: sincronizado em \"%s\"", peek_text());
    ev_push(EV_SYNC, gPp, buf, look, disp);
    return 0;
}

/* registo de erro no padrao exigido:  Erro "token". Esperado <esperado>               */
/* anti-cascata: so emite msg/conta se desde o ultimo match; caso contrario apenas     */
/* varre a frente (panic_sync) ate haver match, sem mensagens secundarias.
   exp_mask: tokens que, se encontrados na varredura, devem fazer MATCH (recuperar).  */
static int report_error(const char *expected, const char *rule,
                        unsigned seq, const char *seq_disp, unsigned exp_mask)
{
    char msg[MSG_LEN], rec_disp[132], aux2[132];

    if (!gA || gPp < 0 || gPp >= gA->ntok) return 0;

    if (g_armado) {
        snprintf(msg, sizeof msg, "Erro \"%s\". Esperado %s", gA->tok[gPp].text, expected);
        mask_names(RECOV(seq), rec_disp, sizeof rec_disp);
        snprintf(aux2, sizeof aux2, "%.70s  |  rec: %.50s", seq_disp, rec_disp);

        ev_push(EV_RULE, gPp, rule, expected, aux2);
        ev_push(EV_ERROR, gPp, msg, rule, seq_disp);

        gA->errors++;
        if (gA->nmsgs < MAX_MSG) {
            snprintf(gA->msgs[gA->nmsgs], MSG_LEN, "%s", msg);
            gA->nmsgs++;
        }
        g_armado = 0;                       /* desarma ate ao proximo match */
    }
    return panic_sync(seq, seq_disp, exp_mask, rule, expected);
}

/* consome o token atual com animacao (regra + match) */
static void vis_match(const char *rule, const char *expected, const char *seq_disp)
{
    int i = gPp;
    char msg[200];

    if (!gA || i < 0 || i >= gA->ntok) return;
    ev_push(EV_RULE, i, rule, expected, seq_disp);
    snprintf(msg, sizeof msg, "match de \"%s\" aceite [%s]", gA->tok[i].text, rule);
    ev_push(EV_MATCH, i, msg, gA->tok[i].text, rule);
    gPp++;
    g_armado = 1;                           /* match reabre a emissao de erros */
}

/* ---------- PRODUCAO <operando> ------------------------------------------------------ */
static int parse_operando(unsigned seq, const char *seq_disp, TokType *last)
{
    if (peek() == T_ID || peek() == T_NUM) {
        *last = peek();
        vis_match(RULE_OPER, EXP_OPER, seq_disp);
        return 1;
    }
    /* se o modo pânico encontrar um ID/num, faz MATCH e o operando recupera */
    if (report_error(EXP_OPER, RULE_OPER, seq, seq_disp, SM(T_ID) | SM(T_NUM)))
        return 1;
    return 0;
}

/* ---------- PRODUCAO <exp aritmetica> ------------------------------------------------ */
static void parse_expressao(void)
{
    TokType last = T_LEXERR;

    if (!parse_operando(SEQ_ASSIGN, SEQ_ASSIGN_D, &last)) return;

    while (peek() == T_PLUS || peek() == T_STAR) {
        TokType op = peek();
        const char *sd = (op == T_PLUS) ? SEQ_PLUS_D : SEQ_STAR_D;
        unsigned   sm = (op == T_PLUS) ? SEQ_PLUS    : SEQ_STAR;
        vis_match(RULE_ARITH, EXP_OPER, sd);
        if (!parse_operando(sm, sd, &last)) return;
    }

    /* follow do ultimo operando: tabelas SEQUENCIA(identificador) e SEQUENCIA(num) */
    if (last == T_ID && !(SEQ_ID & SM(peek())))
        report_error(EXP_AFTER_ID, RULE_EXPR, SEQ_ID, SEQ_ID_D, 0);
    else if (last == T_NUM && !(SEQ_NUM & SM(peek())))
        report_error(EXP_AFTER_NUM, RULE_EXPR, SEQ_NUM, SEQ_NUM_D, 0);
}

/* ---------- PRODUCAO <read> ---------------------------------------------------------- */
static void parser_read(void)
{
    vis_match(RULE_READ, EXP_ID, SEQ_READ_D);
    if (peek() == T_ID) vis_match(RULE_READ, EXP_ID, SEQ_READ_D);
    else report_error(EXP_ID, RULE_READ, SEQ_READ, SEQ_READ_D, SM(T_ID));
}

/* ---------- PRODUCAO <write> --------------------------------------------------------- */
static void parser_write(void)
{
    vis_match(RULE_WRITE, EXP_ID, SEQ_WRITE_D);
    if (peek() == T_ID) vis_match(RULE_WRITE, EXP_ID, SEQ_WRITE_D);
    else report_error(EXP_ID, RULE_WRITE, SEQ_WRITE, SEQ_WRITE_D, SM(T_ID));
}

/* ---------- PRODUCAO <atribuicao> ---------------------------------------------------- */
static void parser_atribuicao(void)
{
    vis_match(RULE_ASSIGN, EXP_ID, SEQ_ID_D);

    if (peek() != T_ASSIGN) {
        /* 1. Emite o erro no ponto onde esperava ':=' */
        if (g_armado) {
            char msg[MSG_LEN];
            snprintf(msg, sizeof msg, "Erro \"%s\". Esperado %s", peek_text(), EXP_ASSIGN);
            ev_push(EV_RULE, gPp, RULE_ASSIGN, EXP_ASSIGN, ":= ou fim de linha");
            ev_push(EV_ERROR, gPp, msg, RULE_ASSIGN, ":=");
            gA->errors++;
            if (gA->nmsgs < MAX_MSG) {
                snprintf(gA->msgs[gA->nmsgs++], MSG_LEN, "%s", msg);
            }
            g_armado = 0;
        }

        /* 2. Modo pânico do slide 6: varre à frente até a marca esperada ':=' ou fim de linha '$' */
        while (peek() != T_EOF && peek() != T_ASSIGN) {
            char buf[200];
            snprintf(buf, sizeof buf, "modo pânico: descartando \"%s\" - à procura de \":=\"", peek_text());
            ev_push(EV_DISCARD, gPp, buf, "marca esperada \":=\"", ":=");
            gPp++;
        }

        if (peek() == T_ASSIGN) {
            ev_push(EV_SYNC, gPp, "modo pânico: sincronizado em \":=\"", "marca esperada \":=\"", ":=");
        } else {
            return; /* Se atingiu o final da linha ($) sem ':=', encerra */
        }
    }

    /* 3. Faz o match no ':=' recuperado e continua analisando a expressão */
    vis_match(RULE_ASSIGN, EXP_ASSIGN, SEQ_ASSIGN_D);
    parse_expressao();
}
/* ---------- PRODUCAO <instrucao> ----------------------------------------------------- */
static void parser_instrucao(void)
{
    switch (peek()) {
    case T_EOF:   return;
    case T_READ:  parser_read();       return;
    case T_WRITE: parser_write();      return;
    case T_ID:    parser_atribuicao(); return;
    default:      report_error(EXP_STMT, RULE_STMT, SEQ_NONE, SEQ_STMT_D, 0); return;
    }
}

/* ---------- geracao da fita de eventos ----------------------------------------------- */
static void build_events(Analysis *a)
{
    char buf[600], aux[32];
    int i, guard = 0;

    gA  = a;
    gPp = 0;
    g_armado = 1;                           /* anti-cascata: comecar armado */

    snprintf(buf, sizeof buf, "Etapa 1 - tokenização (scanner) de \"%s\"",
             a->source[0] ? a->source : "(linha vazia)");
    ev_push(EV_START, -1, buf, "", "");

    for (i = 0; i < a->ntok; i++) {
        snprintf(buf, sizeof buf, "scanner: token %d/%d  \"%s\"  [%s]",
                 i + 1, a->ntok, a->tok[i].text, type_name(a->tok[i].type));
        ev_push(EV_LEX, i, buf, "", "");
    }

    ev_push(EV_STARTPARSE, -1,
                 "Etapa 2 - análise sintática descendente recursiva (modo pânico armado)", "", "");

    while (peek() != T_EOF && guard++ < MAX_TOKENS * 4) {
        int before = gPp;
        parser_instrucao();
        if (gPp == before && peek() != T_EOF) {          /* rede de seguranca */
            report_error(EXP_STMT, RULE_STMT, SEQ_NONE, SEQ_STMT_D, 0);
            if (gPp == before) {
                snprintf(buf, sizeof buf, "descartando \"%s\" - sem producao aplicavel",
                         peek_text());
                ev_push(EV_DISCARD, gPp, buf, SEQ_STMT_D, "");
                gPp++;
            }
        }
    }

    if (a->errors == 0) {
        snprintf(buf, sizeof buf, "%s", MSG_OK);
        strcpy(aux, "OK");
    } else {
        snprintf(buf, sizeof buf, "análise concluída - %d erro(s) detectado(s)", a->errors);
        strcpy(aux, "ERROS");
    }
    ev_push(EV_END, -1, buf, aux, "");

    gA  = NULL;
    gPp = 0;
}

static Analysis *analysis_build(const char *src)
{
    Analysis *a = (Analysis *)xmalloc(sizeof(Analysis));
    memset(a, 0, sizeof(*a));
    snprintf(a->source, sizeof a->source, "%s", src ? src : "");
    a->srclen = (int)strlen(a->source);
    tokenize(a);
    build_events(a);
    return a;
}

static void analysis_free(Analysis *a)
{
    if (!a) return;
    xfree(a->ev);      /* fita de eventos */
    xfree(a);          /* tokens + mensagens */
}

/* ===================================================================================== */
/*  SECAO 3 - MAQUINA DE ESTADOS DE ANIMACAO (consome a fita de eventos)                */
/*  (tambem independente da Raylib)                                                     */
/* ===================================================================================== */

typedef struct {
    int    ev_next;                       /* proximo evento a aplicar */
    int    focus;                         /* token sob holofote (-1)  */
    int    tok_status[MAX_TOKENS];
    float  tok_t[MAX_TOKENS];             /* s desde a ultima troca de estado */
    char   rule[200], expected[132], sync[200], status[240];
    float  rule_flash;
    char   banner[MSG_LEN];
    float  banner_hold;                   /* s que o banner de erro continua visivel */
    char   log[MAX_LOG][LOG_LEN];
    float  log_t[MAX_LOG];                /* s desde a entrada de cada linha */
    int    log_kind[MAX_LOG];
    int    nlog, log_first, log_follow;
    int    errors, done, ok;
    int    stage;                         /* 0 idle | 1 lexico | 2 sintatico | 3 fim */
    /* --- didatica em tempo real (PRIMEIROS / SEQUENCIA / modo panico) --- */
    char   didactic_head[220];            /* contexto, ex.: "Buscando tokens em ..." */
    char   didactic[400];                 /* justificativa do ultimo passo          */
    float  didactic_t;                    /* s desde a ultima atualizacao (fade)     */
    float  sync_flash;                    /* >0: destaque do instante de sincronizar */
    char   status_prev[240];              /* deteta troca de status (fade/slide)     */
    float  status_t;                      /* s desde a ultima troca de status        */
} View;

typedef struct {
    int    playing;
    double acc;
    float  speed;
} Player;

static int ev_delay_ms(EvType t)
{
    switch (t) {
    case EV_START:      return 420;
    case EV_LEX:        return 150;
    case EV_STARTPARSE: return 460;
    case EV_RULE:       return 220;
    case EV_MATCH:      return 290;
    case EV_ERROR:      return 660;
    case EV_DISCARD:    return 250;
    case EV_SYNC:       return 430;
    case EV_END:        return 520;
    default:            return 200;
    }
}

static void log_add(View *v, int kind, const char *line)
{
    if (v->nlog >= MAX_LOG) {                      /* descarta os mais antigos */
        memmove(&v->log[0], &v->log[4], (size_t)(MAX_LOG - 4) * LOG_LEN);
        memmove(&v->log_kind[0], &v->log_kind[4], (size_t)(MAX_LOG - 4) * sizeof(int));
        memmove(&v->log_t[0], &v->log_t[4], (size_t)(MAX_LOG - 4) * sizeof(float));
        v->nlog -= 4;
    }
    snprintf(v->log[v->nlog], LOG_LEN, "%s", line);
    v->log_kind[v->nlog] = kind;
    v->log_t[v->nlog] = 0.0f;                     /* entra com fade/slide */
    v->nlog++;
    v->log_follow = 1;
}

static void view_reset(View *v, const Analysis *a)
{
    int i;
    (void)a;
    memset(v, 0, sizeof(*v));
    v->focus = -1;
    for (i = 0; i < MAX_TOKENS; i++) v->tok_t[i] = 1.0e6f;
    snprintf(v->status, sizeof v->status,
             "aguardando análise - clique em ANALISAR (F5) ou PASSO (F10)");
    snprintf(v->rule,     sizeof v->rule,     "-");
    snprintf(v->expected, sizeof v->expected, "-");
    snprintf(v->sync,     sizeof v->sync,     "-");
    snprintf(v->didactic_head, sizeof v->didactic_head, "-");
    snprintf(v->didactic, sizeof v->didactic,
             "sem decisão ainda - execute a análise (F5) ou avance passo a passo (F10)");
    v->log_follow = 1;
}

/* atualiza a justificativa didatica em tempo real (sem Raylib) */
static void set_didactic(View *v, const char *head, const char *text)
{
    snprintf(v->didactic_head, sizeof v->didactic_head, "%s",
             (head && head[0]) ? head : "-");
    snprintf(v->didactic, sizeof v->didactic, "%s", text ? text : "");
    v->didactic_t = 0.0f;
}

static void view_apply(View *v, const Analysis *a, const Ev *e)
{
    char buf[LOG_LEN];
    int i;

    switch ((EvType)e->type) {
    case EV_START:
        v->stage = 1;
        snprintf(v->status, sizeof v->status, "%s", e->text);
        log_add(v, 0, e->text);
        break;

    case EV_LEX:
        if (e->tok < 0 || e->tok >= a->ntok) break;
        v->focus = e->tok;
        v->tok_t[e->tok] = 0.0f;
        v->tok_status[e->tok] = (a->tok[e->tok].type == T_LEXERR) ? ST_LEXERR : ST_SCANNED;
        snprintf(v->status, sizeof v->status, "%s", e->text);
        break;

    case EV_STARTPARSE:
        v->stage = 2;
        snprintf(v->status, sizeof v->status, "%s", e->text);
        log_add(v, 0, e->text);
        break;

    case EV_RULE:
        if (e->tok >= 0 && e->tok < a->ntok) v->focus = e->tok;
        snprintf(v->rule,     sizeof v->rule,     "%s", e->text);
        snprintf(v->expected, sizeof v->expected, "%s", e->aux[0]  ? e->aux  : "-");
        snprintf(v->sync,     sizeof v->sync,     "%s", e->aux2[0] ? e->aux2 : "-");
        v->rule_flash = 1.0f;
        snprintf(v->status, sizeof v->status, "aplicando %s", e->text);
        {
            char h[220], t[400];
            snprintf(h, sizeof h, "%s", e->aux2[0] ? e->aux2 : "-");
            snprintf(t, sizeof t, "Aplicando %s - esperado %s.",
                     e->text, e->aux[0] ? e->aux : "-");
            set_didactic(v, h, t);
        }
        break;

    case EV_MATCH:
        if (e->tok < 0 || e->tok >= a->ntok) break;
        v->focus = e->tok;
        v->tok_t[e->tok] = 0.0f;
        if (v->tok_status[e->tok] != ST_ERR)
            v->tok_status[e->tok] = ST_OK;
        snprintf(v->status, sizeof v->status, "%s", e->text);
        break;

    case EV_ERROR:
        if (e->tok >= 0 && e->tok < a->ntok) {
            v->focus = e->tok;
            v->tok_t[e->tok] = 0.0f;
            v->tok_status[e->tok] = ST_ERR;
        }
        v->errors++;
        snprintf(v->banner, sizeof v->banner, "%s", e->text);
        v->banner_hold = 2.6f;
        snprintf(v->status, sizeof v->status,
                 "FALHA sintática - modo pânico ativado (sincronização: %s)",
                 e->aux2[0] ? e->aux2 : "?");
        snprintf(buf, sizeof buf, "%s", e->text);
        log_add(v, 1, buf);
        {
            char h[220];
            snprintf(h, sizeof h, "Buscando tokens em %s",
                     e->aux2[0] ? e->aux2 : "-");
            set_didactic(v, h, buf);
        }
        break;

    case EV_DISCARD:
        if (e->tok < 0 || e->tok >= a->ntok) break;
        v->focus = e->tok;
        v->tok_t[e->tok] = 0.0f;
        v->tok_status[e->tok] = ST_DISCARD;
        snprintf(v->status, sizeof v->status, "%s", e->text);
        {
            char h[220], t[400];
            snprintf(h, sizeof h, "Buscando tokens em %s",
                     e->aux[0] ? e->aux : "-");
            snprintf(t, sizeof t,
                     "O token \"%s\" foi descartado porque não pertence ao conjunto "
                     "de continuação válida nem ao início de uma nova instrução.",
                     a->tok[e->tok].text);
            set_didactic(v, h, t);
        }
        break;

    case EV_SYNC:
        if (e->tok >= 0 && e->tok < a->ntok) v->focus = e->tok;
        snprintf(v->status, sizeof v->status, "%s", e->text);
        snprintf(v->sync, sizeof v->sync, "%s", e->aux2[0] ? e->aux2 : v->sync);
        v->sync_flash = 1.0f;
        log_add(v, 2, e->text);
        {
            char h[220], t[400];
            snprintf(h, sizeof h, "Buscando tokens em %s",
                     e->aux[0] ? e->aux : "-");
            snprintf(t, sizeof t, "Sincronização encontrada em \"%s\" - retomada da análise.",
                     (e->tok >= 0 && e->tok < a->ntok) ? a->tok[e->tok].text : "$");
            set_didactic(v, h, t);
        }
        break;

    case EV_END:
        v->stage = 3;
        v->done  = 1;
        v->ok    = (v->errors == 0);
        v->focus = (a->ntok > 0) ? a->ntok - 1 : -1;
        if (v->ok) {                     /* linha valida: tudo o que ficou por marcar passa a verde */
            for (i = 0; i < a->ntok; i++)
                if (v->tok_status[i] == ST_SCANNED || v->tok_status[i] == ST_PENDING) {
                    v->tok_status[i] = ST_OK;
                    v->tok_t[i] = 0.0f;
                }
        }
        snprintf(v->status, sizeof v->status, "%s", e->text);
        log_add(v, v->ok ? 3 : 1, e->text);
        break;
    }
}

/* movimento reduzido (WCAG): espelho por frame de App.reduced_motion.
   anim_k() devolve o multiplicador das animacoes visuais (1 normal, 4 reduzido);
   o relogio do player (acc) nao e escalado - so transicoes, nunca o ritmo. */
static int   g_reduced = 0;
static float anim_k(void) { return g_reduced ? 4.0f : 1.0f; }

static void view_tick(View *v, const Analysis *a, float dt)
{
    float adt = dt * anim_k();   /* idades de animacao; banner_hold usa dt puro */
    int i;
    for (i = 0; i < a->ntok; i++)
        if (v->tok_t[i] < 1.0e5f) v->tok_t[i] += adt;
    for (i = 0; i < v->nlog; i++)
        if (v->log_t[i] < 1.0e5f) v->log_t[i] += adt;
    if (v->rule_flash > 0.0f) v->rule_flash -= adt * 1.6f;
    if (v->banner_hold  > 0.0f) v->banner_hold -= dt;
    if (v->sync_flash   > 0.0f) v->sync_flash  -= adt * 1.1f;
    v->didactic_t += adt;
    /* fade/slide do status: reinicia quando o texto muda */
    if (strcmp(v->status, v->status_prev) != 0) {
        snprintf(v->status_prev, sizeof v->status_prev, "%s", v->status);
        v->status_t = 0.0f;
    }
    v->status_t += adt;
}

/* aplica um unico evento; devolve 0 quando a fita terminou */
static int player_apply_next(Analysis *a, View *v)
{
    if (v->ev_next >= a->nev) return 0;
    view_apply(v, a, &a->ev[v->ev_next]);
    v->ev_next++;
    if (v->done) return 0;
    return 1;
}

static void player_update(Analysis *a, View *v, Player *p, float dt)
{
    int guard = 0;
    if (!p->playing) return;
    if (v->ev_next >= a->nev) { p->playing = 0; return; }

    p->acc += (double)dt * 1000.0 * (double)p->speed;

    while (p->playing && v->ev_next < a->nev && guard++ < 64) {
        int need = ev_delay_ms((EvType)a->ev[v->ev_next].type);
        if (p->acc < (double)need) break;
        p->acc -= (double)need;
        if (!player_apply_next(a, v)) { p->playing = 0; break; }
    }
}

/* ===================================================================================== */
/*  SECAO 4 - INTERFACE GRAFICA (RAYLIB)                                                */
/* ===================================================================================== */

/* ---------- cores (paleta sobria, estilo IDE) ---------------------------------------- */
#define COL_BG        (Color){ 24, 24, 27, 255 }    /* #18181b - fundo            */
#define COL_BG2       (Color){ 30, 30, 46, 255 }    /* #1e1e2e - barras           */
#define COL_PANEL     (Color){ 39, 39, 42, 255 }    /* #27272a - paineis          */
#define COL_PANEL_IN  (Color){ 38, 38, 54, 255 }    /* #262636 - interior         */
#define COL_BORDER    (Color){ 63, 63, 70, 255 }    /* #3f3f46 - linhas 1px       */
#define COL_TEXT      (Color){ 228, 228, 231, 255 } /* #e4e4e7 - texto            */
#define COL_DIM       (Color){ 161, 161, 170, 255 } /* #a1a1aa - rotulos           */
#define COL_MUTE      (Color){ 152, 152, 161, 255 } /* #9898a1 - texto inativo     */
#define COL_GREEN     (Color){ 34, 197, 94, 255 }   /* #22c55e - sucesso          */
#define COL_GREEN_D   (Color){ 22, 163, 74, 255 }   /* #16a34a - sucesso (borda)  */
#define COL_RED       (Color){ 239, 68, 68, 255 }   /* #ef4444 - erro             */
#define COL_RED_D     (Color){ 220, 38, 38, 255 }   /* #dc2626 - erro (borda)     */
#define COL_RED_T     (Color){ 248, 113, 113, 255 } /* #f87171 - erro (texto)     */
#define COL_FOCUS     (Color){ 100, 116, 139, 255 } /* #64748b - foco/acao        */
#define COL_FOCUS_D   (Color){ 71, 85, 105, 255 }   /* #475569 - foco (borda)     */
#define COL_HOVER     (Color){ 48, 48, 56, 255 }    /* hover generico             */
#define COL_FILLOK    (Color){ 24, 43, 34, 255 }    /* fundo subtle de aceite     */
#define COL_FILLERR   (Color){ 58, 26, 26, 255 }    /* fundo subtle de erro       */

/* ---------- fontes ------------------------------------------------------------------ */
enum { F_12 = 0, F_14, F_16, F_20, F_26, F_34, F_44, F_COUNT };

static Font   g_font[F_COUNT];
static bool   g_font_owned[F_COUNT];
static int    g_font_cp[224];           /* 32..255: cobre acentos (c, a, e, ...) */
static float  g_t   = 0.0f;             /* relogio global de animacao */
static float  g_dt  = 0.0f;

static void fonts_load(void)
{
    static const int sizes[F_COUNT] = { 12, 14, 16, 20, 26, 34, 44 };
    static const char *paths[] = {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/consolas.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        NULL
    };
    Font def = GetFontDefault();
    const char *path = NULL;
    int i, n = 0;

    for (i = 32; i < 256; i++) g_font_cp[n++] = i;   /* codepoints latinos */

    for (i = 0; paths[i]; i++)
        if (FileExists(paths[i])) { path = paths[i]; break; }

    for (i = 0; i < F_COUNT; i++) {
        g_font[i] = def;
        g_font_owned[i] = false;
        if (path) {
            Font f = LoadFontEx(path, sizes[i], g_font_cp, n);
            if (f.texture.id != 0) { g_font[i] = f; g_font_owned[i] = true; }
        }
    }
}

static void fonts_unload(void)
{
    int i;
    for (i = 0; i < F_COUNT; i++)
        if (g_font_owned[i]) { UnloadFont(g_font[i]); g_font_owned[i] = false; }
}

/* ---------- primitivas de texto / brilho -------------------------------------------- */
static float text_w(int fi, const char *s, float sz)
{
    return MeasureTextEx(g_font[fi], s, sz, 1.0f).x;
}

static void text_at(int fi, const char *s, float x, float y, float sz, float sp, Color c)
{
    DrawTextEx(g_font[fi], s, (Vector2){ x, y }, sz, sp, c);
}

static void text_mid(int fi, const char *s, float cx, float cy, float sz, Color c)
{
    float w = text_w(fi, s, sz);
    text_at(fi, s, cx - w * 0.5f, cy - sz * 0.55f, sz, 1.0f, c);
}

/* interpolacao de cores (transicoes suaves com GetFrameTime) */
static Color mixc(Color a, Color b, float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return (Color){ (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
                    (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
                    (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t),
                    (unsigned char)((float)a.a + ((float)b.a - (float)a.a) * t) };
}

/* sublinhado ondulado marcador de erro (por baixo do texto) */
static void wavy_underline(float x, float y, float w, Color c)
{
    float px, py;
    int i, n = (int)(w / 3.0f);
    if (n < 2) n = 2;
    px = x;
    py = y;
    for (i = 1; i <= n; i++) {
        float nx = x + w * ((float)i / (float)n);
        float ny = y + sinf((float)i * 1.35f) * 1.6f;
        DrawLineEx((Vector2){ px, py }, (Vector2){ nx, ny }, 1.4f, c);
        px = nx;
        py = ny;
    }
}


/* quebra de texto simples (palavras) em ate WRAP_MAX linhas */
static int wrap_text(int fi, const char *s, float sz, float maxw,
                     char out[WRAP_MAX][WRAP_LEN])
{
    char line[WRAP_LEN * 2 + 2], word[WRAP_LEN], test[WRAP_LEN * 2 + 2];
    int lines = 0;
    const char *p = s ? s : "";

    line[0] = '\0';
    if (!*p) { out[0][0] = '\0'; return 1; }

    while (*p && lines < WRAP_MAX) {
        int wl = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && wl < WRAP_LEN - 1) { word[wl++] = *p++; }
        word[wl] = '\0';
        if (wl == 0) break;

        if (line[0]) snprintf(test, sizeof test, "%.*s %.*s",
                              WRAP_LEN - 1, line, WRAP_LEN - 1, word);
        else         snprintf(test, sizeof test, "%s", word);

        if (text_w(fi, test, sz) <= maxw || !line[0]) {
            snprintf(line, sizeof line, "%s", test);
        } else {
            snprintf(out[lines++], WRAP_LEN, "%.*s", WRAP_LEN - 1, line);
            snprintf(line, sizeof line, "%s", word);
        }
    }
    if (line[0] && lines < WRAP_MAX) snprintf(out[lines++], WRAP_LEN, "%.*s", WRAP_LEN - 1, line);
    if (lines == 0) { out[0][0] = '\0'; lines = 1; }
    return lines;
}

/* ---------- painel (chato, borda 1px, titulo discreto) ------------------------------ */
static void panel_frame(Rectangle r, const char *title, Color accent)
{
    DrawRectangleRec(r, COL_PANEL);
    DrawRectangleLinesEx(r, 1.0f, COL_BORDER);
    if (title && title[0]) {
        DrawRectangle((int)(r.x + 10.0f), (int)(r.y + 11.0f), 2, 12, Fade(accent, 0.95f));
        text_at(F_12, title, r.x + 18.0f, r.y + 9.0f, 12.0f, 2.0f, COL_DIM);
    }
}

/* ---------- icones vetoriais (nao dependem de glifos) -------------------------------- */
typedef enum { ICON_NONE = 0, ICON_PLAY, ICON_PAUSE, ICON_STEP, ICON_STEPBACK,
               ICON_RESET, ICON_WAVE, ICON_STILL } UiIcon;

static void draw_icon(UiIcon ic, Rectangle r, Color c)
{
    float cx = r.x + r.width * 0.5f;
    float cy = r.y + r.height * 0.5f;
    float s  = r.height * 0.34f;

    switch (ic) {
    case ICON_PLAY: {
        Vector2 a = { cx - s * 0.55f, cy - s };
        Vector2 b = { cx - s * 0.55f, cy + s };
        Vector2 d = { cx + s * 0.95f, cy };
        DrawTriangle(a, b, d, c);
        break;
    }
    case ICON_PAUSE:
        DrawRectangle((int)(cx - s * 0.75f), (int)(cy - s), (int)(s * 0.55f), (int)(2 * s), c);
        DrawRectangle((int)(cx + s * 0.20f), (int)(cy - s), (int)(s * 0.55f), (int)(2 * s), c);
        break;
    case ICON_STEP: {
        Vector2 a = { cx - s * 0.95f, cy - s };
        Vector2 b = { cx - s * 0.95f, cy + s };
        Vector2 d = { cx + s * 0.25f, cy };
        DrawTriangle(a, b, d, c);
        DrawRectangle((int)(cx + s * 0.60f), (int)(cy - s), (int)(s * 0.40f), (int)(2 * s), c);
        break;
    }
    case ICON_STEPBACK: {   /* barra + seta triangular para a esquerda */
        DrawRectangle((int)(cx - s * 0.95f), (int)(cy - s), (int)(s * 0.40f), (int)(2 * s), c);
        Vector2 a = { cx + s * 0.95f, cy - s };
        Vector2 b = { cx + s * 0.95f, cy + s };
        Vector2 d = { cx - s * 0.25f, cy };
        DrawTriangle(b, a, d, c); /* invertido para sentido anti-horario */
        break;
    }
    case ICON_RESET:
        DrawRing((Vector2){ cx, cy }, s * 0.62f, s, -110.0f, 150.0f, 28, c);
        DrawRing((Vector2){ cx, cy }, s * 0.62f, s, 150.0f, 175.0f, 6, Fade(c, 0.0f));
        {
            float ang = 150.0f * DEG2RAD;
            Vector2 p = { cx + cosf(ang) * s * 0.81f, cy + sinf(ang) * s * 0.81f };
            Vector2 t1 = { p.x + s * 0.55f, p.y - s * 0.42f };
            Vector2 t2 = { p.x + s * 0.55f, p.y + s * 0.18f };
            DrawTriangle(p, t1, t2, c);
        }
        break;
    case ICON_WAVE: {
        int row, seg;
        for (row = 0; row < 2; row++) {
            float yy = cy + (row ? s * 0.50f : -s * 0.50f);
            Vector2 p[5];
            for (seg = 0; seg < 5; seg++) {
                p[seg].x = cx - s * 0.95f + (s * 0.475f) * (float)seg;
                p[seg].y = yy + (((seg % 2) == 0) ? -s * 0.32f : s * 0.32f);
            }
            for (seg = 0; seg < 4; seg++)
                DrawLineEx(p[seg], p[seg + 1], 1.6f, c);
        }
        break;
    }
    case ICON_STILL: {
        int row;
        for (row = 0; row < 2; row++) {
            float yy = cy + (row ? s * 0.50f : -s * 0.50f);
            DrawLineEx((Vector2){ cx - s * 0.95f, yy },
                       (Vector2){ cx + s * 0.95f, yy }, 1.6f, c);
        }
        break;
    }
    default:
        break;
    }
}

/* ---------- botao (chato, sem brilho) ------------------------------------------------ */
static bool ui_button(Rectangle r, const char *label, Color accent, bool primary,
                      bool enabled, UiIcon icon)
{
    Vector2 m = GetMousePosition();
    bool hover  = enabled && CheckCollisionPointRec(m, r);
    bool press  = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float dy    = press ? 1.0f : 0.0f;
    Color base, bord, txt;
    Rectangle b;
    float fs = 14.0f;
    float lw, total, tx;

    if (!enabled)      { base = COL_PANEL_IN;  bord = COL_BORDER; txt = COL_MUTE; }
    else if (primary)  { base = COL_FOCUS_D; bord = hover ? COL_DIM : COL_FOCUS; txt = COL_TEXT; }
    else if (hover)    { base = COL_HOVER;     bord = (Color){ 82, 82, 92, 255 }; txt = COL_TEXT; }
    else               { base = COL_PANEL_IN;  bord = COL_BORDER; txt = COL_TEXT; }

    b = (Rectangle){ r.x, r.y + dy, r.width, r.height };
    DrawRectangleRounded(b, 0.18f, 4, base);
    DrawRectangleRoundedLinesEx(b, 0.18f, 4, 1.0f, Fade(bord, enabled ? 1.0f : 0.7f));

    lw = text_w(F_14, label, fs);
    total = lw + ((icon != ICON_NONE) ? 24.0f : 0.0f);
    tx = b.x + (b.width - total) * 0.5f;
    if (icon != ICON_NONE) {
        draw_icon(icon, (Rectangle){ tx, b.y + (b.height - 16.0f) * 0.5f, 16.0f, 16.0f },
                  enabled ? txt : COL_MUTE);
        tx += 24.0f;
    }
    text_at(F_14, label, tx, b.y + (b.height - fs) * 0.5f - 1.0f, fs, 1.0f, txt);

    /* pista de acao discreta (1px) para o botao em destaque */
    if (enabled && primary)
        DrawRectangle((int)b.x, (int)b.y + 4, 2, (int)(b.height - 8.0f), accent);

    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* ---------- controlo segmentado (velocidade 0.5x / 1x / 2x) ------------------------- */
static int ui_segmented(Rectangle r, const char **labels, int n, int sel, float *pillx)
{
    Vector2 m = GetMousePosition();
    float segw = r.width / (float)n;
    float target, pw;
    int i, hit = -1;
    Color pill;

    if (n <= 0) return -1;
    sel = Clamp(sel, 0, n - 1);

    DrawRectangleRounded(r, 0.30f, 4, COL_PANEL_IN);
    DrawRectangleRoundedLinesEx(r, 0.30f, 4, 1.0f, COL_BORDER);

    /* pula deslizante (mola suave entre as opcoes) */
    target = r.x + segw * ((float)sel + 0.5f);
    if (*pillx <= 0.0f) *pillx = target;
    else *pillx += (target - *pillx) * Clamp(g_dt * 14.0f * anim_k(), 0.0f, 1.0f);
    pw = segw - 6.0f;
    pill = mixc(COL_HOVER, (Color){ 62, 62, 72, 255 }, 0.6f);
    {
        Rectangle pr = { *pillx - pw * 0.5f, r.y + 3.0f, pw, r.height - 6.0f };
        DrawRectangleRounded(pr, 0.30f, 4, pill);
        DrawRectangleRoundedLinesEx(pr, 0.30f, 4, 1.0f, (Color){ 82, 82, 92, 255 });
    }

    for (i = 0; i < n; i++) {
        Rectangle s = { r.x + segw * (float)i, r.y, segw, r.height };
        bool hov = CheckCollisionPointRec(m, s);
        Color c = (i == sel) ? COL_TEXT : (hov ? COL_DIM : COL_MUTE);
        text_mid(F_12, labels[i], s.x + s.width * 0.5f,
                 s.y + s.height * 0.5f + 1.0f, 12.0f, c);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) hit = i;
    }
    return hit;
}

/* ---------- estado da aplicacao ------------------------------------------------------ */
typedef struct {
    Analysis *an;
    View       view;
    Player     player;
    char       input[MAX_LINE];
    bool       tfocus;
    int        caret;
    float      txoff;        /* deslocamento horizontal do texto no campo */
    float      ribbon_off;
    int        con_tab;      /* 0 problemas | 1 consola | 2 gramatica | 3 conjuntos */
    float      tab_x;        /* sublinhado de aba (animado) */
    int        spd_idx;      /* velocidade: 0 = 0.5x | 1 = 1x | 2 = 2x */
    float      spd_pill;     /* posicao da pula segmentada (animada) */
    int        sel_set;      /* linha selecionada na aba Conjuntos (-1 = nenhuma) */
    float      sel_t;        /* s desde a selecao (fade) */
    bool       reduced_motion; /* movimento reduzido: animacoes 4x, tremor 0 */
    float      row_hl[N_PRIM_ROWS + N_SEQ_ROWS];  /* luz da linha ativa (animada) */
    float      sets_t;       /* s desde a abertura da aba Conjuntos (entrada) */
    float      scan_x;       /* moldura do scanner: x alvo/animado (mola) */
    float      scan_w;       /* largura animada da moldura */
    float      scan_v;       /* velocidade da mola do scanner */
    float      scan_on;      /* 0..1 presenca da moldura */
    float      chip_s[MAX_TOKENS];  /* escala elastica de cada chip */
    float      res_t;        /* s desde o fim da analise (stagger das mensagens) */
    float      insp_off;     /* scroll vertical do inspetor */
    bool       insp_open;    /* inspetor lateral expandido */
    float      insp_w;       /* largura animada do inspetor */
    float      prog_vis;     /* barra de progresso interpolada */
    float      smoke;        /* >0: fecha a janela apos N segundos */
} App;

static const char *EXAMPLES[5] = {
    "read 20",
    "1 read var",
    "var.:=temp",
    "var := 25-10",
    "x := a + b * 2"
};

/* ---------- layout ------------------------------------------------------------------ */
typedef struct {
    int W, H;
    Rectangle top, foot;                 /* barra superior / rodape */
    Rectangle editor, ribbon, conso;     /* centro: entrada, fita, consola */
    Rectangle insp;                      /* inspetor lateral */
} Layout;

#define LW_TOP    54.0f
#define LW_FOOT   26.0f
#define LW_M      12.0f
#define LW_GAP    10.0f
#define LW_RWOPEN 300.0f
#define LW_RWSHUT 34.0f

static Layout layout_compute(const App *ap)
{
    Layout L;
    int y0, y1, h;
    float cw;

    memset(&L, 0, sizeof L);
    L.W = GetScreenWidth();
    L.H = GetScreenHeight();

    L.top  = (Rectangle){ 0.0f, 0.0f, (float)L.W, LW_TOP };
    L.foot = (Rectangle){ 0.0f, (float)L.H - LW_FOOT, (float)L.W, LW_FOOT };

    y0 = (int)(LW_TOP + LW_GAP);                       /* 64 */
    y1 = L.H - (int)(LW_FOOT + LW_GAP);
    h  = y1 - y0;

    L.insp = (Rectangle){ (float)L.W - LW_M - ap->insp_w, (float)y0,
                          ap->insp_w, (float)h };
    cw = L.insp.x - LW_M - LW_GAP - LW_M;              /* largura central */

    L.editor = (Rectangle){ LW_M, (float)y0, cw, 108.0f };
    L.ribbon = (Rectangle){ LW_M, (float)y0 + 118.0f, cw, 158.0f };
    L.conso  = (Rectangle){ LW_M, (float)y0 + 286.0f, cw,
                            (float)y0 + h - ((float)y0 + 286.0f) };

    return L;
}

/* ---------- fundo (chato, sem grelha/feixes) ---------------------------------------- */
static void draw_background(void)
{
    ClearBackground(COL_BG);
    DrawRectangleGradientEx((Rectangle){ 0.0f, 0.0f, (float)GetScreenWidth(),
                                         (float)GetScreenHeight() },
                            (Color){ 26, 26, 30, 255 }, (Color){ 26, 26, 30, 255 },
                            (Color){ 21, 21, 24, 255 }, (Color){ 21, 21, 24, 255 });
}

/* ---------- acoes partilhadas (botoes + atalhos de teclado) -------------------------- */
static void act_play(App *ap)
{
    if (ap->view.done || ap->view.ev_next >= ap->an->nev) {
        view_reset(&ap->view, ap->an);
        ap->player.acc = 0.0;
    }
    ap->player.playing = !ap->player.playing;
}

static void act_step(App *ap)
{
    ap->player.playing = 0;
    if (ap->view.done || ap->view.ev_next >= ap->an->nev) {
        view_reset(&ap->view, ap->an);
        ap->player.acc = 0.0;
    }
    player_apply_next(ap->an, &ap->view);
}

static void act_reset(App *ap)
{
    view_reset(&ap->view, ap->an);
    ap->player.playing = 0;
    ap->player.acc = 0.0;
    ap->ribbon_off = 0.0f;
}

/* passo anterior: replay deterministico do prefixo ev[0..ev_next-2] (design D1);
   inativo quando ev_next == 0 */
static void act_step_back(App *ap)
{
    View *v = &ap->view;
    int i, keep;

    ap->player.playing = 0;
    ap->player.acc = 0.0;
    if (v->ev_next <= 0) return;

    keep = v->ev_next - 1;
    view_reset(v, ap->an);
    for (i = 0; i < keep; i++)
        view_apply(v, ap->an, &ap->an->ev[i]);
    v->ev_next = keep;

    /* timers de apresentacao assentados: sem fades a meio (D1); o banner de
       erro nao reaparece por ter sido reaplicado */
    snprintf(v->status_prev, sizeof v->status_prev, "%s", v->status);
    v->didactic_t  = 1.0e6f;
    v->status_t    = 1.0e6f;
    v->banner_hold = 0.0f;
}

/* ---------- barra superior: titulo + botoes de acao --------------------------------- */
static void draw_topbar(App *ap, const Layout *L, bool empty)
{
    Rectangle bar = L->top;
    const float bh = 30.0f, by = (LW_TOP - bh) * 0.5f;
    float x, w;
    char lbl[48];

    DrawRectangleRec(bar, COL_BG2);
    DrawLineEx((Vector2){ 0.0f, LW_TOP - 1.0f }, (Vector2){ (float)L->W, LW_TOP - 1.0f },
               1.0f, COL_BORDER);

    /* titulo + subtitulo (subtitulo so cabe em janelas >= 1240 px: o cluster
       de controlos da direita ocupa ~736 px e cresceu com o botao de movimento) */
    text_at(F_20, "Compilador TINY", 16.0f, 7.0f, 20.0f, 1.0f, COL_TEXT);
    if (L->W >= 1240)
        text_at(F_12, "análise descendente recursiva  ·  modo pânico  ·  GLC-Tiny",
                17.0f, 32.0f, 12.0f, 1.0f, COL_DIM);

    /* botoes alinhados a direita */
    snprintf(lbl, sizeof lbl, ap->player.playing ? "Pausar [F5]" : "Executar [F5]");
    w = text_w(F_14, lbl, 14.0f) + 24.0f + 26.0f;
    x = (float)L->W - LW_M - w;
    {
        Rectangle bR = { x, by, w, bh };
        if (ui_button(bR, lbl, COL_GREEN, !ap->player.playing, !empty,
                      ap->player.playing ? ICON_PAUSE : ICON_PLAY))
            act_play(ap);
    }

    snprintf(lbl, sizeof lbl, "Passo a Passo [F10]");
    w = text_w(F_14, lbl, 14.0f) + 24.0f + 26.0f;
    x -= w + 8.0f;
    {
        Rectangle bS = { x, by, w, bh };
        if (ui_button(bS, lbl, COL_FOCUS, false, !empty, ICON_STEP))
            act_step(ap);
    }

    /* passo anterior: so icone (seta para a esquerda); inativo no inicio */
    {
        Rectangle bB = { x - 8.0f - 50.0f, by, 50.0f, bh };
        x = bB.x;
        if (ui_button(bB, "", COL_FOCUS, false, !empty && ap->view.ev_next > 0,
                      ICON_STEPBACK))
            act_step_back(ap);
    }

    snprintf(lbl, sizeof lbl, "Resetar [F8]");
    w = text_w(F_14, lbl, 14.0f) + 24.0f + 26.0f;
    x -= w + 8.0f;
    {
        Rectangle bRs = { x, by, w, bh };
        if (ui_button(bRs, lbl, COL_DIM, false, true, ICON_RESET))
            act_reset(ap);
    }

    /* movimento reduzido (WCAG): so icone (onda = normal, linhas = reduzido) */
    {
        Rectangle bM = { x - 8.0f - 50.0f, by, 50.0f, bh };
        x = bM.x;
        if (ui_button(bM, "", COL_FOCUS, false, true,
                      ap->reduced_motion ? ICON_STILL : ICON_WAVE))
            ap->reduced_motion = !ap->reduced_motion;
    }

    /* velocidade da animacao: controlo segmentado com pula deslizante */
    {
        static const char *spd_lbl[3] = { "0.5x", "1x", "2x" };
        static const float spd_val[3] = { 0.5f, 1.0f, 2.0f };
        Rectangle bSeg = { x - 8.0f - 126.0f, by, 126.0f, bh };
        int hit;
        x = bSeg.x;
        hit = ui_segmented(bSeg, spd_lbl, 3, ap->spd_idx, &ap->spd_pill);
        if (hit >= 0) {
            ap->spd_idx = hit;
            ap->player.speed = spd_val[hit];
        }
    }
}

/* ---------- campo de texto interativo ------------------------------------------------ */
static int ui_textbox(App *ap, Rectangle r)
{
    Vector2 m = GetMousePosition();
    float fs = 16.0f;
    float pad = 12.0f;
    float avail = r.width - pad * 2.0f - 4.0f;
    Color bord = ap->tfocus ? COL_FOCUS : COL_BORDER;
    int submit = 0;
    int i, c;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(m, r)) {
            ap->tfocus = true;
            /* posiciona o cursor pelo clique */
            {
                int best = ap->input[0] ? (int)strlen(ap->input) : 0;
                float bx = r.x + pad - ap->txoff;
                for (i = 0; ap->input[i]; i++) {
                    float cw = text_w(F_16, (char[]){ ap->input[i], 0 }, fs);
                    if (m.x < bx + cw * 0.5f) { best = i; break; }
                    bx += cw;
                    best = i + 1;
                }
                ap->caret = Clamp(best, 0, (int)strlen(ap->input));
            }
        } else {
            ap->tfocus = false;
        }
    }

    /* fundo + borda */
    DrawRectangleRounded(r, 0.12f, 4, COL_PANEL_IN);
    DrawRectangleRoundedLinesEx(r, 0.12f, 4, ap->tfocus ? 2.0f : 1.0f, Fade(bord, 1.0f));

    if (ap->tfocus) {
        int len = (int)strlen(ap->input);
        int back;

        /* teclas de edicao */
        if (IsKeyPressed(KEY_LEFT)  || IsKeyPressedRepeat(KEY_LEFT))  ap->caret--;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) ap->caret++;
        if (IsKeyPressed(KEY_HOME)  || (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_A)))
            ap->caret = 0;
        if (IsKeyPressed(KEY_END))
            ap->caret = len;
        if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
            if (ap->caret < len) { memmove(ap->input + ap->caret, ap->input + ap->caret + 1,
                                           strlen(ap->input + ap->caret + 1) + 1); }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (ap->caret > 0) {
                memmove(ap->input + ap->caret - 1, ap->input + ap->caret,
                        strlen(ap->input + ap->caret) + 1);
                ap->caret--;
            }
        }

        /* clipboard */
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
            if (IsKeyPressed(KEY_C)) SetClipboardText(ap->input);
            if (IsKeyPressed(KEY_X)) { SetClipboardText(ap->input); ap->input[0] = '\0'; ap->caret = 0; }
            if (IsKeyPressed(KEY_V)) {
                const char *clip = GetClipboardText();
                for (i = 0; clip && clip[i]; i++) {
                    unsigned char ch = (unsigned char)clip[i];
                    int n = (int)strlen(ap->input);
                    if (ch < 32 || ch > 126) continue;
                    if (n >= MAX_LINE - 1) break;
                    memmove(ap->input + ap->caret + 1, ap->input + ap->caret,
                            strlen(ap->input + ap->caret) + 1);
                    ap->input[ap->caret++] = (char)ch;
                }
            }
        }

        /* caracteres imprimiveis */
        while ((c = GetCharPressed()) != 0) {
            int n = (int)strlen(ap->input);
            if (c < 32 || c > 126) continue;
            if (n >= MAX_LINE - 1) break;
            memmove(ap->input + ap->caret + 1, ap->input + ap->caret,
                    strlen(ap->input + ap->caret) + 1);
            ap->input[ap->caret++] = (char)c;
        }

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) submit = 1;

        ap->caret = Clamp(ap->caret, 0, (int)strlen(ap->input));
        len = (int)strlen(ap->input);

        /* manter o cursor visivel */
        {
            float cx = 0.0f;
            for (back = 0; back < ap->caret; back++) {
                char tmp[2] = { ap->input[back], 0 };
                cx += text_w(F_16, tmp, fs);
            }
            if (cx - ap->txoff > avail) ap->txoff = cx - avail;
            if (cx - ap->txoff < 0.0f)  ap->txoff = cx;
            if (ap->txoff < 0.0f) ap->txoff = 0.0f;
        }
    }

    /* texto */
    BeginScissorMode((int)(r.x + pad), (int)r.y, (int)(r.width - pad), (int)r.height);
    text_at(F_16, ap->input[0] ? ap->input : "digite aqui a linha do programa...",
            r.x + pad - ap->txoff, r.y + (r.height - fs) * 0.5f - 1.0f, fs, 1.0f,
            ap->input[0] ? COL_TEXT : COL_MUTE);
    if (ap->tfocus && fmodf(g_t, 1.0f) < 0.55f) {
        float cx = 0.0f;
        for (i = 0; i < ap->caret; i++) {
            char tmp[2] = { ap->input[i], 0 };
            cx += text_w(F_16, tmp, fs);
        }
        {
            float cxx = r.x + pad + cx - ap->txoff;
            DrawRectangle((int)cxx, (int)(r.y + 9.0f), 2, (int)(r.height - 18.0f),
                          Fade(COL_TEXT, 0.85f));
        }
    }
    EndScissorMode();

    return submit;
}

/* ---------- centro superior: entrada de codigo + exemplos --------------------------- */
static void draw_editor(App *ap, const Layout *L, bool empty)
{
    Rectangle p = L->editor;
    char buf[96];
    float x;
    int i;

    panel_frame(p, "ENTRADA DE CÓDIGO", COL_FOCUS);

    snprintf(buf, sizeof buf, "%d/%d caracteres", (int)strlen(ap->input), MAX_LINE - 1);
    text_at(F_12, buf, p.x + p.width - 12.0f - text_w(F_12, buf, 12.0f), p.y + 9.0f,
            12.0f, 1.0f, COL_DIM);

    /* campo de texto */
    {
        Rectangle box = { p.x + 12.0f, p.y + 30.0f, p.width - 24.0f, 38.0f };
        if (ui_textbox(ap, box) && !empty) {           /* ENTER = analisar */
            if (ap->view.done || ap->view.ev_next >= ap->an->nev) {
                view_reset(&ap->view, ap->an);
                ap->player.acc = 0.0;
            }
            ap->player.playing = 1;
        }
    }

    /* exemplos do trabalho (linha compacta) */
    text_at(F_12, "exemplos:", p.x + 12.0f, p.y + 78.0f, 12.0f, 1.0f, COL_DIM);
    x = p.x + 12.0f + text_w(F_12, "exemplos:", 12.0f) + 8.0f;
    for (i = 0; i < 5; i++) {
        float bw = text_w(F_12, EXAMPLES[i], 12.0f) + 18.0f;
        Rectangle b = { x, p.y + 72.0f, bw, 24.0f };
        if (ui_button(b, EXAMPLES[i], COL_FOCUS, false, true, ICON_NONE)) {
            snprintf(ap->input, sizeof ap->input, "%s", EXAMPLES[i]);
            ap->caret = (int)strlen(ap->input);
            ap->txoff = 0.0f;
        }
        x += bw + 6.0f;
    }
}

/* ---------- cartao de token da fita -------------------------------------------------- */
static void draw_token_card(const Token *t, int st, Rectangle r, float age, bool focused)
{
    Color fill = (Color){ 30, 30, 36, 255 }, border = COL_BORDER;
    Color txtc = COL_DIM, labelc = COL_DIM;
    Color nfill = fill, nborder = border, ntxt = txtc, nlabel = labelc;
    float k;
    float shake = 0.0f;
    Rectangle d;
    char lbl[24];

    switch (st) {
    case ST_OK:
        fill   = COL_FILLOK;
        border = COL_GREEN_D;
        txtc   = COL_TEXT;
        labelc = COL_GREEN;
        break;
    case ST_ERR:
        fill   = COL_FILLERR;
        border = COL_RED;
        txtc   = (Color){ 254, 205, 205, 255 };
        labelc = COL_RED_T;
        break;
    case ST_DISCARD:
        fill   = (Color){ 42, 42, 46, 255 };
        border = COL_MUTE;
        txtc   = COL_MUTE;
        labelc = COL_MUTE;
        break;
    case ST_LEXERR:
        fill   = COL_FILLERR;
        border = COL_RED_D;
        txtc   = (Color){ 254, 205, 205, 255 };
        labelc = COL_RED_T;
        break;
    case ST_SCANNED:
        fill   = (Color){ 35, 41, 54, 255 };
        border = COL_FOCUS;
        txtc   = COL_TEXT;
        labelc = COL_DIM;
        break;
    default:
        break;
    }

    /* transicao suave: do estado neutro para o novo estado */
    k = 1.0f - expf(-7.0f * Clamp(age, 0.0f, 10.0f));
    fill   = mixc(nfill,   fill,   k);
    border = mixc(nborder, border, k);
    txtc   = mixc(ntxt,    txtc,   k);
    labelc = mixc(nlabel,  labelc, k);

    /* tremor curto (2-3 px) imediatamente apos erro/descarte */
    if ((st == ST_ERR || st == ST_LEXERR || st == ST_DISCARD) && age < 0.45f && !g_reduced)
        shake = sinf(age * 62.0f) * 3.0f * expf(-age * 8.0f);

    d = (Rectangle){ r.x + shake, r.y, r.width, r.height };

    /* foco: anel discreto por fora + fundo ligeiramente mais claro */
    if (focused) {
        fill = mixc(fill, (Color){ 47, 47, 56, 255 }, 0.55f);
    }

    DrawRectangleRounded(d, 0.14f, 4, fill);
    DrawRectangleRoundedLinesEx(d, 0.14f, 4, (st == ST_ERR || st == ST_LEXERR) ? 2.0f : 1.0f,
                                border);
    if (focused) {
        Rectangle ring = { d.x - 2.0f, d.y - 2.0f, d.width + 4.0f, d.height + 4.0f };
        DrawRectangleRoundedLinesEx(ring, 0.16f, 4, 1.0f, COL_FOCUS);
    }

    snprintf(lbl, sizeof lbl, "%s", type_name(t->type));
    if (text_w(F_12, lbl, 12.0f) > d.width - 14.0f) snprintf(lbl, sizeof lbl, "ERR");
    text_at(F_12, lbl, d.x + 8.0f, d.y + 6.0f, 12.0f, 1.0f, labelc);

    /* "lido": chevron duplo vetorial (sinal nao-cromatico do estado) */
    if (st == ST_SCANNED) {
        float gx = d.x + d.width - 14.0f, gy = d.y + 7.0f;
        DrawLineEx((Vector2){ gx,          gy },
                   (Vector2){ gx + 4.0f,   gy + 3.0f }, 1.4f, labelc);
        DrawLineEx((Vector2){ gx + 4.0f,   gy + 3.0f },
                   (Vector2){ gx,          gy + 6.0f }, 1.4f, labelc);
        DrawLineEx((Vector2){ gx + 5.0f,   gy },
                   (Vector2){ gx + 9.0f,   gy + 3.0f }, 1.4f, labelc);
        DrawLineEx((Vector2){ gx + 9.0f,   gy + 3.0f },
                   (Vector2){ gx + 5.0f,   gy + 6.0f }, 1.4f, labelc);
    }

    {
        float fs = (text_w(F_16, t->text, 16.0f) > d.width - 14.0f) ? 13.0f : 16.0f;
        float tw = text_w(F_16, t->text, fs);
        float tx = d.x + d.width * 0.5f - tw * 0.5f;
        float ty = d.y + d.height * 0.56f;
        text_at(F_16, t->text, tx, ty, fs, 1.0f, txtc);

        /* marcadores de estado: ondulado (erro) e tachado animado (descartado) */
        if (st == ST_ERR || st == ST_LEXERR)
            wavy_underline(tx, ty + fs + 4.0f, tw, Fade(COL_RED, Clamp(k, 0.0f, 1.0f)));
        if (st == ST_DISCARD) {
            float p1 = Clamp((age - 0.05f) * 3.5f, 0.0f, 1.0f);
            DrawLineEx((Vector2){ tx, ty + fs * 0.55f },
                       (Vector2){ tx + tw * p1, ty + fs * 0.55f },
                       1.4f, mixc(nborder, border, k));
        }
    }

    /* tic de confirmacao subtil quando o match assenta (fade p/ verde) */
    if (st == ST_OK && k > 0.55f) {
        float p = Clamp((k - 0.55f) / 0.45f, 0.0f, 1.0f);
        Vector2 a1 = { d.x + d.width - 16.0f, d.y + 8.0f };
        Vector2 b1 = { d.x + d.width - 12.0f, d.y + 12.5f };
        Vector2 c1 = { d.x + d.width - 5.0f,  d.y + 5.5f };
        Vector2 pe = (p < 0.5f)
            ? (Vector2){ a1.x + (b1.x - a1.x) * (p / 0.5f),
                         a1.y + (b1.y - a1.y) * (p / 0.5f) }
            : b1;
        DrawLineEx(a1, pe, 1.8f, Fade(COL_GREEN, Clamp(k, 0.0f, 1.0f)));
        if (p >= 0.5f) {
            float q = (p - 0.5f) / 0.5f;
            Vector2 pe2 = { b1.x + (c1.x - b1.x) * q, b1.y + (c1.y - b1.y) * q };
            DrawLineEx(b1, pe2, 1.8f, Fade(COL_GREEN, Clamp(k, 0.0f, 1.0f)));
        }
    }
}

/* ---------- fita de tokens ------------------------------------------------------------ */
static void draw_ribbon(App *ap, const Analysis *a, Rectangle p)
{
    static float cx[MAX_TOKENS], cw[MAX_TOKENS];
    int i, n = a->ntok, focus = ap->view.focus;
    float px = p.x + 12.0f, py = p.y + 36.0f;
    float pw = p.width - 24.0f, ph = 74.0f;
    float gap = 8.0f, x = 0.0f, total, maxoff;

    panel_frame(p, "FITA DE TOKENS", COL_FOCUS);

    /* legenda discreta no cabecalho */
    {
        const char *lg[4] = { "aceite", "erro", "descartado", "lido" };
        Color lc[4] = { COL_GREEN, COL_RED, COL_MUTE, COL_FOCUS };
        float lx = p.x + p.width - 12.0f;
        for (i = 3; i >= 0; i--) {
            float lw = text_w(F_12, lg[i], 12.0f);
            lx -= lw;
            text_at(F_12, lg[i], lx, p.y + 9.0f, 12.0f, 1.0f, COL_DIM);
            lx -= 15.0f;
            DrawRectangle((int)lx, (int)(p.y + 11.0f), 9, 9, lc[i]);
            lx -= 16.0f;
        }
    }

    for (i = 0; i < n; i++) {
        float w = text_w(F_16, a->tok[i].text, 16.0f) + 36.0f;
        if (w < 52.0f)  w = 52.0f;
        if (w > 150.0f) w = 150.0f;
        cx[i] = x;
        cw[i] = w;
        x += w + gap;
    }
    total = (n > 0) ? x - gap : 0.0f;
    maxoff = (total > pw) ? total - pw : 0.0f;

    if (focus >= 0 && focus < n) {
        if (cx[focus] - ap->ribbon_off < 0.0f)
            ap->ribbon_off = cx[focus];
        else if (cx[focus] + cw[focus] - ap->ribbon_off > pw)
            ap->ribbon_off = cx[focus] + cw[focus] - pw;
    }
    if (maxoff > 0.0f && CheckCollisionPointRec(GetMousePosition(), p)) {
        float wh = GetMouseWheelMove();
        if (wh != 0.0f) ap->ribbon_off -= wh * 42.0f;
    }
    ap->ribbon_off = Clamp(ap->ribbon_off, 0.0f, maxoff);

    /* escala elastica do chip em foco (elevacao 1.05x) */
    if (n > 0 && ap->chip_s[0] <= 0.0f)
        for (i = 0; i < n; i++) ap->chip_s[i] = 1.0f;
    for (i = 0; i < n; i++) {
        float tgt = (i == focus) ? 1.05f : 1.0f;
        ap->chip_s[i] += (tgt - ap->chip_s[i]) * Clamp(g_dt * 14.0f * anim_k(), 0.0f, 1.0f);
    }

    /* moldura do scanner: mola amortecida que desliza entre os tokens */
    {
        float tgt_x = ap->scan_x, tgt_w = ap->scan_w, tgt_on = 0.0f;
        if (focus >= 0 && focus < n) {
            tgt_x = cx[focus];
            tgt_w = cw[focus];
            tgt_on = 1.0f;
        }
        if (ap->scan_w <= 0.0f) { ap->scan_x = tgt_x; ap->scan_w = tgt_w; }
        ap->scan_v += ((tgt_x - ap->scan_x) * 140.0f - ap->scan_v * 20.0f) * g_dt * anim_k();
        ap->scan_x += ap->scan_v * g_dt * anim_k();
        ap->scan_w += (tgt_w - ap->scan_w) * Clamp(g_dt * 10.0f * anim_k(), 0.0f, 1.0f);
        ap->scan_on += (tgt_on - ap->scan_on) * Clamp(g_dt * 6.0f * anim_k(), 0.0f, 1.0f);
    }

    if (n == 0)
        text_at(F_12, "sem tokens - digite uma linha do programa", px, py + 8.0f,
                12.0f, 1.0f, COL_MUTE);

    BeginScissorMode((int)(px - 4), (int)(py - 14), (int)(pw + 8), (int)(ph + 28));
    for (i = 0; i < n; i++) {
        Rectangle r = { px + cx[i] - ap->ribbon_off, py, cw[i], ph };
        float s;
        if (r.x + r.width < px - 60.0f || r.x > px + pw + 60.0f) continue;
        s = ap->chip_s[i];
        r = (Rectangle){ r.x + r.width * (1.0f - s) * 0.5f,
                         r.y + r.height * (1.0f - s) * 0.5f,
                         r.width * s, r.height * s };
        draw_token_card(&a->tok[i], ap->view.tok_status[i], r,
                        ap->view.tok_t[i], i == focus);
    }

    /* scanner: moldura deslizante com cantos marcados */
    if (ap->scan_on > 0.02f) {
        Rectangle f = { px + ap->scan_x - ap->ribbon_off - 4.0f, py - 4.0f,
                        ap->scan_w + 8.0f, ph + 8.0f };
        float on = ap->scan_on;
        Color fc = Fade(COL_FOCUS, 0.30f + 0.55f * on);
        Color tc = Fade(COL_TEXT, 0.55f * on);
        float t = 7.0f;
        if (f.x + f.width > px - 20.0f && f.x < px + pw + 20.0f) {
            DrawRectangleRoundedLinesEx(f, 0.16f, 4, 1.0f, fc);
            DrawLineEx((Vector2){ f.x, f.y + t }, (Vector2){ f.x, f.y }, 1.5f, tc);
            DrawLineEx((Vector2){ f.x, f.y }, (Vector2){ f.x + t, f.y }, 1.5f, tc);
            DrawLineEx((Vector2){ f.x + f.width - t, f.y },
                       (Vector2){ f.x + f.width, f.y }, 1.5f, tc);
            DrawLineEx((Vector2){ f.x + f.width, f.y },
                       (Vector2){ f.x + f.width, f.y + t }, 1.5f, tc);
            DrawLineEx((Vector2){ f.x, f.y + f.height - t },
                       (Vector2){ f.x, f.y + f.height }, 1.5f, tc);
            DrawLineEx((Vector2){ f.x, f.y + f.height },
                       (Vector2){ f.x + t, f.y + f.height }, 1.5f, tc);
            DrawLineEx((Vector2){ f.x + f.width - t, f.y + f.height },
                       (Vector2){ f.x + f.width, f.y + f.height }, 1.5f, tc);
            DrawLineEx((Vector2){ f.x + f.width, f.y + f.height - t },
                       (Vector2){ f.x + f.width, f.y + f.height }, 1.5f, tc);
        }
    }
    EndScissorMode();

    /* linha original (recortada ao painel) */
    BeginScissorMode((int)p.x + 1, (int)(p.y + p.height - 30), (int)(p.width - 2),
                     28);
    text_at(F_12, "fonte:", px, p.y + p.height - 28.0f, 12.0f, 1.0f, COL_MUTE);
    text_at(F_14, a->source[0] ? a->source : "(vazia)",
            px + text_w(F_12, "fonte:", 12.0f) + 8.0f,
            p.y + p.height - 29.0f, 14.0f, 1.0f, COL_TEXT);
    EndScissorMode();
}

/* ---------- consola inferior: abas Problemas / Consola / Gramática ------------------- */
static void con_problems(App *ap, const Analysis *a, Rectangle p)
{
    char buf[96], head[80];
    char lines[WRAP_MAX][WRAP_LEN];
    int i, k, nl, shown;
    float y;

    if (!ap->view.done) {
        text_at(F_14, "sem problemas até agora - execute a análise (Executar [F5] ou Passo [F10])",
                p.x + 14.0f, p.y + 16.0f, 14.0f, 1.0f, COL_MUTE);
        return;
    }

    if (ap->view.ok) {
        float kk = Clamp(ap->res_t * 3.5f, 0.0f, 1.0f);
        float slide = (1.0f - kk) * 10.0f;
        Vector2 c = { p.x + 24.0f, p.y + 24.0f + slide };
        DrawCircleV(c, 10.0f, Fade(COL_GREEN, 0.16f * kk));
        DrawCircleLinesV(c, 10.0f, Fade(COL_GREEN, kk));
        DrawLineEx((Vector2){ c.x - 4.5f, c.y + 0.5f }, (Vector2){ c.x - 1.0f, c.y + 4.5f },
                   2.0f, Fade(COL_GREEN, kk));
        DrawLineEx((Vector2){ c.x - 1.0f, c.y + 4.5f }, (Vector2){ c.x + 5.5f, c.y - 4.0f },
                   2.0f, Fade(COL_GREEN, kk));
        text_at(F_16, MSG_OK, p.x + 44.0f, p.y + 14.0f + slide, 16.0f, 1.0f,
                Fade(COL_GREEN, kk));
        snprintf(buf, sizeof buf, "0 erros  ·  %d token(s)  ·  %d evento(s)",
                 a ? a->ntok : 0, a ? a->nev : 0);
        text_at(F_12, buf, p.x + 44.0f, p.y + 38.0f + slide, 12.0f, 1.0f,
                Fade(COL_DIM, kk));
        return;
    }

    snprintf(head, sizeof head, "%d erro(s) detectado(s):", ap->view.errors);
    text_at(F_14, head, p.x + 14.0f, p.y + 14.0f, 14.0f, 1.0f,
            Fade(COL_RED_T, Clamp(ap->res_t * 4.0f, 0.0f, 1.0f)));

    y = p.y + 38.0f;
    shown = 0;
    for (i = 0; i < ap->view.errors && a && i < a->nmsgs && shown < 6; i++) {
        /* entrada escalonada: fade + slide vertical por mensagem */
        float kk = Clamp((ap->res_t - (float)i * 0.09f) * 3.5f, 0.0f, 1.0f);
        float slide = (1.0f - kk) * 8.0f;
        nl = wrap_text(F_14, a->msgs[i], 14.0f, p.width - 52.0f, lines);
        /* marca de erro vetorial (nao depende de glifo) */
        DrawLineEx((Vector2){ p.x + 16.0f, y + 2.0f + slide },
                   (Vector2){ p.x + 25.0f, y + 13.0f + slide }, 2.0f, Fade(COL_RED, kk));
        DrawLineEx((Vector2){ p.x + 25.0f, y + 2.0f + slide },
                   (Vector2){ p.x + 16.0f, y + 13.0f + slide }, 2.0f, Fade(COL_RED, kk));
        for (k = 0; k < nl; k++) {
            text_at(F_14, lines[k], p.x + 32.0f, y + k * 17.0f + slide, 14.0f, 1.0f,
                    Fade(COL_TEXT, kk));
        }
        y += nl * 17.0f + 3.0f;
        shown++;
    }
    if (ap->view.errors > shown) {
        snprintf(buf, sizeof buf, "... e mais %d erro(s) - ver aba Consola",
                 ap->view.errors - shown);
        text_at(F_12, buf, p.x + 32.0f, y, 12.0f, 1.0f, COL_DIM);
    }
}

static void con_log(App *ap, Rectangle p)
{
    View *v = &ap->view;
    float ly = p.y + 8.0f;
    float lh = 16.0f;
    int visible = (int)((p.height - 12.0f) / lh);
    int first, maxfirst, i;
    Color c;

    maxfirst = (v->nlog > visible) ? (v->nlog - visible) : 0;
    if (CheckCollisionPointRec(GetMousePosition(), p)) {
        float wh = GetMouseWheelMove();
        if (wh != 0.0f) { v->log_first -= (int)(wh * 2.0f); v->log_follow = 0; }
    }
    if (v->log_follow) v->log_first = maxfirst;
    first = Clamp(v->log_first, 0, maxfirst);
    v->log_first = first;

    if (v->nlog == 0) {
        text_at(F_12, "sem eventos - execute a análise", p.x + 14.0f, ly, 12.0f, 1.0f,
                COL_MUTE);
        return;
    }

    BeginScissorMode((int)p.x + 1, (int)p.y, (int)(p.width - 2), (int)p.height);
    {
        float y = ly;
        for (i = first; i < v->nlog; i++) {
            char lines[WRAP_MAX][WRAP_LEN];
            int nl, k;
            float kk, slide;
            if (y >= p.y + p.height - 10.0f) break;
            kk = Clamp(v->log_t[i] * 5.0f, 0.0f, 1.0f);   /* fade + slide de entrada */
            slide = (1.0f - kk) * 6.0f;
            switch (v->log_kind[i]) {
            case 1:  c = COL_RED_T; break;
            case 2:  c = COL_DIM;   break;
            case 3:  c = COL_GREEN; break;
            default: c = COL_DIM;   break;
            }
            /* realce subtil da linha mais recente */
            if (i == v->nlog - 1 && v->log_t[i] < 1.2f)
                DrawRectangle((int)p.x + 6, (int)(y - 2.0f + slide),
                              (int)(p.width - 14), 15,
                              Fade(COL_PANEL_IN, 0.75f * (1.0f - v->log_t[i] / 1.2f)));
            nl = wrap_text(F_12, v->log[i], 12.0f, p.width - 62.0f, lines);
            if (nl > 2) nl = 2;
            text_at(F_12, (v->log_kind[i] == 1) ? "ERR" : (v->log_kind[i] == 3 ? "OK" : "::"),
                    p.x + 14.0f, y + slide, 12.0f, 1.0f, Fade(c, kk));
            for (k = 0; k < nl; k++)
                text_at(F_12, lines[k], p.x + 46.0f, y + k * 13.0f + slide, 12.0f, 1.0f,
                        Fade((v->log_kind[i] == 0) ? COL_TEXT : c, kk));
            y += (nl > 1 ? 28.0f : 15.0f);
        }
    }
    EndScissorMode();

    if (maxfirst > 0)
        text_at(F_12, "^ rolar", p.x + p.width - 12.0f - text_w(F_12, "^ rolar", 12.0f),
                p.y + p.height - 16.0f, 12.0f, 1.0f, COL_MUTE);
}

static void con_grammar(App *ap, Rectangle p)
{
    static const char *rules[7] = {
        RULE_STMT, RULE_READ, RULE_WRITE, RULE_ASSIGN, RULE_EXPR, RULE_OPER, RULE_ARITH
    };
    static float fade[7] = { 0 };      /* luminosidade da linha ativa (animada) */
    int i;
    float y = p.y + 6.0f;

    for (i = 0; i < 7; i++) {
        bool on = (strcmp(ap->view.rule, rules[i]) == 0);
        Rectangle hl = { p.x + 8.0f, y - 2.0f, p.width - 16.0f, 22.0f };
        float f;
        fade[i] += ((on ? 1.0f : 0.0f) - fade[i]) * Clamp(g_dt * 7.0f * anim_k(), 0.0f, 1.0f);
        f = Clamp(fade[i], 0.0f, 1.0f);
        if (f > 0.01f) {
            DrawRectangleRec(hl, Fade(COL_PANEL_IN, 0.30f + 0.55f * f));
            DrawRectangle((int)hl.x, (int)hl.y, 2, (int)hl.height, Fade(COL_FOCUS, f));
        }
        text_at(F_14, rules[i], p.x + 16.0f, y, 14.0f, 1.0f,
                mixc(COL_DIM, COL_TEXT, f));
        y += 24.0f;
    }
}

/* ---------- linha de conjunto interativa (clicavel, com fade de entrada) ------------- */
static float set_row(App *ap, const SetRow *row, int idx, float x, float y, float w,
                     int order)
{
    Vector2 m = GetMousePosition();
    float nw = text_w(F_12, row->name, 12.0f);
    float vw = text_w(F_12, row->desc, 12.0f);
    int two = (nw + 14.0f + vw > w);
    float h = two ? 32.0f : 19.0f;
    Rectangle r = { x - 6.0f, y - 3.0f, w + 12.0f, h };
    bool hov = CheckCollisionPointRec(m, r);
    bool sel = (ap->sel_set == idx);
    float al = Clamp(ap->sets_t * 3.0f - (float)order * 0.05f, 0.0f, 1.0f);
    bool on = ap->view.didactic_head[0] &&
              strstr(ap->view.didactic_head, row->name) != NULL;
    float fw;
    Color nc, vc;

    if (al <= 0.01f) return h + 3.0f;

    /* luz da linha cujo conjunto esta em uso no momento */
    ap->row_hl[idx] += ((on ? 1.0f : 0.0f) - ap->row_hl[idx]) * Clamp(g_dt * 8.0f * anim_k(), 0.0f, 1.0f);
    fw = Clamp(ap->row_hl[idx], 0.0f, 1.0f) * al;
    if (fw > 0.01f) {
        DrawRectangleRec(r, Fade(COL_PANEL_IN, 0.85f * fw));
        DrawRectangle((int)r.x, (int)(r.y + 2.0f), 2, (int)(r.height - 4.0f),
                      Fade(COL_FOCUS, fw));
    }

    if (sel) {
        float kk = Clamp(ap->sel_t * 6.0f, 0.0f, 1.0f) * al;
        DrawRectangleRec(r, Fade(COL_PANEL_IN, 0.85f * kk));
        DrawRectangle((int)r.x + 3, (int)(r.y + 2.0f), 2, (int)(r.height - 4.0f),
                      Fade(COL_GREEN, kk));
    } else if (hov) {
        DrawRectangleRec(r, Fade(COL_HOVER, 0.55f * al));
    }

    nc = Fade(sel ? COL_TEXT : mixc(hov ? COL_TEXT : COL_DIM, COL_TEXT, fw * 0.7f), al);
    vc = Fade(sel ? COL_TEXT : mixc(COL_DIM, COL_GREEN, fw * 0.8f), al);
    text_at(F_12, row->name, x, y, 12.0f, 1.0f, nc);
    if (two)
        text_at(F_12, row->desc, x + 14.0f, y + 16.0f, 12.0f, 1.0f, vc);
    else
        text_at(F_12, row->desc, x + w - vw, y, 12.0f, 1.0f, vc);

    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        ap->sel_set = idx;
        ap->sel_t = 0.0f;
    }
    return h + 3.0f;
}

/* ---------- aba Conjuntos: painel didatico PRIMEIROS / SEQUENCIA em tempo real -------- */
static void con_sets(App *ap, Rectangle p)
{
    char bufh[240], buft[440];
    char hl[WRAP_MAX][WRAP_LEN], tl[WRAP_MAX][WRAP_LEN];
    float xL = p.x + 14.0f, xR = p.x + p.width * 0.5f + 8.0f;
    float wc = p.width * 0.5f - 28.0f;
    float y0 = p.y + 10.0f, hL, hR, y;
    int i, j, nh, nt;

    text_at(F_12, "PRIMEIROS", xL, y0, 12.0f, 2.0f, COL_DIM);
    text_at(F_12, "SEQUÊNCIA (follow)", xR, y0, 12.0f, 2.0f, COL_DIM);

    y = y0 + 20.0f;
    for (i = 0; i < N_PRIM_ROWS; i++)
        y += set_row(ap, &PRIM_ROWS[i], i, xL, y, wc, i);
    hL = y;

    y = y0 + 20.0f;
    for (i = 0; i < N_SEQ_ROWS; i++)
        y += set_row(ap, &SEQ_ROWS[i], N_PRIM_ROWS + i, xR, y, wc, N_PRIM_ROWS + i);
    hR = y;

    /* --- caixa de detalhe: justificativa didatica do passo atual --- */
    y = ((hL > hR) ? hL : hR) + 4.0f;
    {
        const SetRow *sel = NULL;
        float bx = p.x + 10.0f, bw = p.width - 20.0f;
        float by = y;                    /* y ja e absoluto (linhas acima) */
        float avail = (p.y + p.height - 8.0f) - by;
        float kk = Clamp(ap->view.didactic_t * 5.0f, 0.0f, 1.0f);
        float slide = (1.0f - kk) * 5.0f;
        float flash = Clamp(ap->view.sync_flash, 0.0f, 1.0f);
        float bh, ty;
        int maxt;
        Rectangle box;

        if (avail < 52.0f) return;

        if (ap->sel_set >= 0 && ap->sel_set < N_PRIM_ROWS)
            sel = &PRIM_ROWS[ap->sel_set];
        else if (ap->sel_set >= N_PRIM_ROWS && ap->sel_set < N_PRIM_ROWS + N_SEQ_ROWS)
            sel = &SEQ_ROWS[ap->sel_set - N_PRIM_ROWS];

        snprintf(bufh, sizeof bufh, "%s", ap->view.didactic_head);
        snprintf(buft, sizeof buft, "%s", ap->view.didactic);
        nh = (bufh[0] && strcmp(bufh, "-") != 0) ? wrap_text(F_12, bufh, 12.0f,
                                                             bw - 18.0f, hl) : 0;
        nt = wrap_text(F_12, buft, 12.0f, bw - 18.0f, tl);

        bh = 24.0f + (sel ? 14.0f : 0.0f) + nh * 13.0f + nt * 13.0f + 6.0f;
        maxt = nt;
        while (bh > avail && maxt > 1) { maxt--; bh -= 13.0f; }
        if (bh > avail) bh = avail;
        if (bh < 46.0f) return;

        box = (Rectangle){ bx, by, bw, bh };
        DrawRectangleRec(box, mixc(COL_PANEL_IN, COL_BG2, 0.40f));
        DrawRectangleLinesEx(box, 1.0f, mixc(COL_BORDER, COL_FOCUS, flash * 0.85f));
        DrawRectangle((int)box.x, (int)box.y, 2, (int)box.height,
                      Fade(COL_FOCUS, 0.55f + 0.45f * flash));

        text_at(F_12, sel ? sel->name : "PASSO ATUAL", bx + 9.0f, by + 6.0f,
                12.0f, 2.0f, sel ? COL_TEXT : COL_DIM);
        /* elipses animada enquanto a analise esta a correr */
        if (ap->view.stage == 2 && !ap->view.done) {
            for (i = 0; i < 3; i++) {
                float ph = fmodf(g_t * 2.2f + (float)i * 0.33f, 1.0f);
                float al = (ph < 0.5f) ? ph * 2.0f : (1.0f - ph) * 2.0f;
                DrawCircleV((Vector2){ bx + bw - 44.0f + (float)i * 7.0f, by + 12.0f },
                            1.8f, Fade(COL_FOCUS, al));
            }
        }

        ty = by + 22.0f + slide;
        if (sel) {
            text_at(F_12, sel->note, bx + 9.0f, ty, 12.0f, 1.0f, Fade(COL_DIM, kk));
            ty += 14.0f;
        }
        for (j = 0; j < nh && ty + 13.0f < by + bh; j++) {
            text_at(F_12, hl[j], bx + 9.0f, ty, 12.0f, 1.0f, Fade(COL_TEXT, kk));
            ty += 13.0f;
        }
        for (j = 0; j < maxt && ty + 13.0f < by + bh; j++) {
            text_at(F_12, tl[j], bx + 9.0f, ty, 12.0f, 1.0f, Fade(COL_DIM, kk));
            ty += 13.0f;
        }
    }
}

static void draw_console(App *ap, const Analysis *a, Rectangle p)
{
    static const char *tablbl[4] = { "Problemas", "Consola", "Gramática", "Conjuntos" };
    float tx = p.x + 10.0f, tw, target;
    int i;
    char badge[32];

    DrawRectangleRec(p, COL_PANEL);
    DrawRectangleLinesEx(p, 1.0f, COL_BORDER);

    /* tempo de entrada da aba Conjuntos (reinicia ao sair) */
    if (ap->con_tab == 3) ap->sets_t += g_dt * anim_k();
    else                  ap->sets_t  = 0.0f;

    /* abas */
    for (i = 0; i < 4; i++) {
        Rectangle r = { tx, p.y + 4.0f, text_w(F_12, tablbl[i], 12.0f) + 22.0f, 24.0f };
        bool hov = CheckCollisionPointRec(GetMousePosition(), r);
        if (ap->con_tab == i)
            text_at(F_12, tablbl[i], r.x + 11.0f, r.y + 6.0f, 12.0f, 1.0f, COL_TEXT);
        else
            text_at(F_12, tablbl[i], r.x + 11.0f, r.y + 6.0f, 12.0f, 1.0f,
                    hov ? COL_DIM : COL_MUTE);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ap->con_tab = i;
        tx += r.width + 4.0f;
    }

    /* indicador da aba ativa (animado) */
    target = p.x + 10.0f;
    for (i = 0; i < ap->con_tab; i++)
        target += text_w(F_12, tablbl[i], 12.0f) + 26.0f;
    if (ap->tab_x <= 0.0f) ap->tab_x = target;
    else ap->tab_x += (target - ap->tab_x) * Clamp(g_dt * 12.0f * anim_k(), 0.0f, 1.0f);
    tw = text_w(F_12, tablbl[ap->con_tab], 12.0f) + 22.0f;
    DrawRectangle((int)(ap->tab_x + 4.0f), (int)(p.y + 28.0f), (int)(tw - 8.0f), 2,
                  COL_TEXT);

    DrawLineEx((Vector2){ p.x + 1.0f, p.y + 30.0f },
               (Vector2){ p.x + p.width - 1.0f, p.y + 30.0f }, 1.0f, COL_BORDER);

    /* distintivo de estado (canto direito) */
    if (ap->view.done && ap->view.ok) snprintf(badge, sizeof badge, "OK");
    else if (ap->view.errors > 0)     snprintf(badge, sizeof badge, "%d erro(s)",
                                               ap->view.errors);
    else                              snprintf(badge, sizeof badge, "—");
    {
        Color bc = (ap->view.done && ap->view.ok) ? COL_GREEN
                 : (ap->view.errors > 0 ? COL_RED_T : COL_MUTE);
        text_at(F_12, badge, p.x + p.width - 12.0f - text_w(F_12, badge, 12.0f),
                p.y + 9.0f, 12.0f, 1.0f, bc);
    }

    /* conteudo */
    {
        Rectangle c = { p.x + 1.0f, p.y + 31.0f, p.width - 2.0f, p.height - 32.0f };
        switch (ap->con_tab) {
        case 1:  con_log(ap, c);                     break;
        case 2:  con_grammar(ap, c);                 break;
        case 3:  con_sets(ap, c);                    break;
        default: con_problems(ap, a, c);             break;
        }
    }
}

/* ---------- painel lateral: inspetor (didatico, colapsavel e rolavel) ---------------- */
static void draw_inspector(App *ap, const Analysis *a, Rectangle p)
{
    static float hl[N_PRIM_ROWS + N_SEQ_ROWS];   /* luminosidade da linha ativa */
    char buf[160], lines[WRAP_MAX][WRAP_LEN];
    float x, y, w, top, bot, nmx = 0.0f;
    int i, nl, f = ap->view.focus;
    int nrows = N_PRIM_ROWS + N_SEQ_ROWS;

    DrawRectangleRec(p, COL_PANEL);
    DrawRectangleLinesEx(p, 1.0f, COL_BORDER);

    /* cabecalho + botao de recolho */
    {
        Rectangle b = { p.x + p.width - 30.0f, p.y + 6.0f, 22.0f, 22.0f };
        if (p.width > LW_RWSHUT + 4.0f) {
            text_at(F_12, "INSPETOR", p.x + 12.0f, p.y + 10.0f, 12.0f, 2.0f, COL_DIM);
            if (ui_button(b, "<", COL_DIM, false, true, ICON_NONE)) ap->insp_open = false;
        } else {
            Rectangle br = { p.x + 6.0f, p.y + 6.0f, 22.0f, 22.0f };
            if (ui_button(br, ">", COL_DIM, false, true, ICON_NONE)) ap->insp_open = true;
            return;
        }
    }
    if (p.width < LW_RWSHUT + 4.0f) return;

    x = p.x + 12.0f;
    w = p.width - 24.0f;
    top = p.y + 34.0f;
    bot = p.y + p.height - 6.0f;

    for (i = 0; i < nrows; i++) {
        const SetRow *rw = (i < N_PRIM_ROWS) ? &PRIM_ROWS[i] : &SEQ_ROWS[i - N_PRIM_ROWS];
        float nw = text_w(F_12, rw->name, 12.0f);
        if (nw > nmx) nmx = nw;
    }

    BeginScissorMode((int)p.x + 1, (int)top, (int)(p.width - 2), (int)(bot - top));
    y = (p.y + 36.0f) - ap->insp_off;

    /* --- regra ativa --- */
    text_at(F_12, "REGRA ATIVA", x, y, 12.0f, 2.0f, COL_DIM);
    y += 16.0f;
    {
        float flash = Clamp(ap->view.rule_flash, 0.0f, 1.0f);
        nl = wrap_text(F_14, ap->view.rule, 14.0f, w, lines);
        if (flash > 0.02f) {
            Rectangle hl2 = { x - 6.0f, y - 4.0f, w + 12.0f, nl * 17.0f + 6.0f };
            DrawRectangleRec(hl2, mixc(COL_PANEL, COL_PANEL_IN, 0.30f + 0.55f * flash));
        }
        for (i = 0; i < nl && i < 3; i++)
            text_at(F_14, lines[i], x, y + i * 17.0f, 14.0f, 1.0f, COL_TEXT);
        y += nl * 17.0f + 8.0f;
    }
    DrawLineEx((Vector2){ x, y }, (Vector2){ x + w, y }, 1.0f, COL_BORDER);
    y += 12.0f;

    /* --- lido vs esperado --- */
    text_at(F_12, "LIDO  vs  ESPERADO", x, y, 12.0f, 2.0f, COL_DIM);
    y += 16.0f;
    {
        Rectangle bTok = { x, y, w, 34.0f };
        const char *tok = "-";
        const char *typ = "";

        if (a && f >= 0 && f < a->ntok) {
            tok = a->tok[f].text;
            typ = type_name(a->tok[f].type);
        }
        DrawRectangleRec(bTok, COL_PANEL_IN);
        DrawRectangleLinesEx(bTok, 1.0f, COL_BORDER);
        if (typ[0] && strcmp(typ, tok) != 0)
            text_at(F_12, typ, bTok.x + 7.0f, bTok.y + 4.0f, 12.0f, 1.0f, COL_DIM);
        text_mid(F_16, tok, bTok.x + bTok.width * 0.5f, bTok.y + bTok.height * 0.58f,
                 16.0f, COL_TEXT);
        y += 34.0f + 8.0f;

        text_at(F_12, "ESPERADO", x, y, 12.0f, 2.0f, COL_DIM);
        y += 15.0f;
        nl = wrap_text(F_12, ap->view.expected, 12.0f, w, lines);
        for (i = 0; i < nl && i < 3; i++)
            text_at(F_12, lines[i], x, y + i * 14.0f, 12.0f, 1.0f, COL_TEXT);
        y += nl * 14.0f + 6.0f;

        if (a && f >= 0 && f < a->ntok)
            snprintf(buf, sizeof buf, "indice %d/%d · coluna %d · estado: %.20s",
                     f + 1, a->ntok, a->tok[f].col + 1, status_name(ap->view.tok_status[f]));
        else
            snprintf(buf, sizeof buf, "sem token em análise");
        text_at(F_12, buf, x, y, 12.0f, 1.0f, COL_MUTE);
        y += 20.0f;
    }
    DrawLineEx((Vector2){ x, y }, (Vector2){ x + w, y }, 1.0f, COL_BORDER);
    y += 12.0f;

    /* --- tabelas PRIMEIROS / SEQUENCIA (a linha do passo atual ganha luz) --- */
    text_at(F_12, "TABELAS PRIMEIROS / SEQUÊNCIA", x, y, 12.0f, 2.0f, COL_DIM);
    y += 17.0f;
    for (i = 0; i < nrows; i++) {
        const SetRow *rw = (i < N_PRIM_ROWS) ? &PRIM_ROWS[i] : &SEQ_ROWS[i - N_PRIM_ROWS];
        char val[64];
        float fw, vw;
        bool on = ap->view.didactic_head[0] &&
                  strstr(ap->view.didactic_head, rw->name) != NULL;

        hl[i] += ((on ? 1.0f : 0.0f) - hl[i]) * Clamp(g_dt * 8.0f * anim_k(), 0.0f, 1.0f);
        fw = Clamp(hl[i], 0.0f, 1.0f);
        mask_names(rw->mask, val, sizeof val);
        vw = text_w(F_12, val, 12.0f);

        if (fw > 0.01f) {
            Rectangle bg = { x - 6.0f, y - 3.0f, w + 12.0f, 16.0f };
            DrawRectangleRec(bg, Fade(COL_PANEL_IN, 0.9f * fw));
            DrawRectangle((int)bg.x, (int)bg.y, 2, (int)bg.height, Fade(COL_FOCUS, fw));
        }
        text_at(F_12, rw->name, x, y, 12.0f, 0.0f, mixc(COL_DIM, COL_TEXT, fw));
        text_at(F_12, val, x + w - vw, y, 12.0f, 0.0f, mixc(COL_TEXT, COL_GREEN, fw));
        y += 14.0f;
    }
    y += 6.0f;
    {
        float flash = Clamp(ap->view.sync_flash, 0.0f, 1.0f);
        Rectangle r = { x - 4.0f, y, w + 8.0f, 54.0f };
        DrawRectangleRec(r, COL_PANEL_IN);
        DrawRectangleLinesEx(r, 1.0f, mixc(COL_BORDER, COL_FOCUS, flash * 0.85f));
        text_at(F_12, "EM USO", r.x + 6.0f, r.y + 5.0f, 12.0f, 1.0f,
                mixc(COL_FOCUS, COL_TEXT, flash * 0.6f));
        nl = wrap_text(F_12, ap->view.sync, 12.0f, r.width - 12.0f, lines);
        for (i = 0; i < nl && i < 3; i++)
            text_at(F_12, lines[i], r.x + 6.0f, r.y + 19.0f + i * 13.0f, 12.0f, 1.0f,
                    COL_TEXT);
        y += 54.0f + 12.0f;
    }
    DrawLineEx((Vector2){ x, y }, (Vector2){ x + w, y }, 1.0f, COL_BORDER);
    y += 12.0f;

    /* --- justificativa didatica (fade + slide) --- */
    text_at(F_12, "JUSTIFICATIVA DO PASSO", x, y, 12.0f, 2.0f, COL_DIM);
    y += 17.0f;
    {
        float kk = Clamp(ap->view.didactic_t * 5.0f, 0.0f, 1.0f);
        float slide = (1.0f - kk) * 5.0f;
        float flash = Clamp(ap->view.sync_flash, 0.0f, 1.0f);
        char h2[240], t2[440];
        char lh[WRAP_MAX][WRAP_LEN], lt[WRAP_MAX][WRAP_LEN];
        int nh, nt, j;
        float bh, ty;
        Rectangle box;

        snprintf(h2, sizeof h2, "%s", ap->view.didactic_head);
        snprintf(t2, sizeof t2, "%s", ap->view.didactic);
        nh = (h2[0] && strcmp(h2, "-") != 0) ? wrap_text(F_12, h2, 12.0f, w, lh) : 0;
        nt = wrap_text(F_12, t2, 12.0f, w, lt);
        if (nh > 2) nh = 2;
        if (nt > 4) nt = 4;

        bh = 8.0f + nh * 14.0f + nt * 14.0f + 4.0f;
        box = (Rectangle){ x - 6.0f, y - 4.0f, w + 12.0f, bh };
        DrawRectangleRec(box, mixc(COL_PANEL_IN, COL_BG2, 0.40f));
        DrawRectangleLinesEx(box, 1.0f, mixc(COL_BORDER, COL_FOCUS, flash * 0.85f));
        DrawRectangle((int)box.x, (int)box.y, 2, (int)box.height,
                      Fade(COL_FOCUS, 0.55f + 0.45f * flash));

        ty = y + slide;
        for (j = 0; j < nh; j++) {
            text_at(F_12, lh[j], x, ty, 12.0f, 1.0f, Fade(COL_TEXT, kk));
            ty += 14.0f;
        }
        for (j = 0; j < nt; j++) {
            text_at(F_12, lt[j], x, ty, 12.0f, 1.0f, Fade(COL_DIM, kk));
            ty += 14.0f;
        }
        y += bh + 10.0f;
    }

    /* altura total do conteudo + scroll com barra subtil */
    y += 6.0f;
    EndScissorMode();
    {
        float viewH = bot - (p.y + 36.0f);
        float ch = y + ap->insp_off - (p.y + 36.0f);
        float maxoff = (ch > viewH) ? ch - viewH : 0.0f;

        if (maxoff > 0.0f && CheckCollisionPointRec(GetMousePosition(),
                (Rectangle){ p.x, top, p.width, bot - top })) {
            float wh = GetMouseWheelMove();
            if (wh != 0.0f) ap->insp_off += wh * 34.0f;
        }
        ap->insp_off = Clamp(ap->insp_off, 0.0f, maxoff);

        if (maxoff > 1.0f) {
            float trackH = viewH - 6.0f;
            float thumbH = Clamp(trackH * viewH / ch, 24.0f, trackH);
            float t = (maxoff > 0.0f) ? ap->insp_off / maxoff : 0.0f;
            Rectangle tr = { p.x + p.width - 4.0f, top + 3.0f, 2.0f, trackH };
            Rectangle th = { tr.x, tr.y + (trackH - thumbH) * t, 2.0f, thumbH };
            DrawRectangleRec(tr, Fade(COL_BORDER, 0.55f));
            DrawRectangleRec(th, Fade(COL_DIM, 0.85f));
        }
    }
}

/* rotulo didatico do token (nome formal para o rodape) */
static const char *tok_label(TokType t)
{
    if (t == T_ID)     return "identificador";
    if (t == T_LEXERR) return "erro lexico";
    return type_name(t);
}

/* ---------- rodape: estado + indicador unico de progresso ---------------------------- */
static void draw_footer(App *ap, const Analysis *a, Rectangle p)
{
    char right[160];
    int ntok = a ? a->ntok : 0;
    int nev = a ? a->nev : 0;
    int tokn;
    float prog, target, tw, tx;
    float sa = Clamp(ap->view.status_t * 6.0f, 0.0f, 1.0f);   /* fade/slide do status */

    DrawRectangleRec(p, COL_BG2);
    DrawLineEx((Vector2){ 0.0f, p.y }, (Vector2){ p.x + p.width, p.y }, 1.0f, COL_BORDER);

    /* estado atual: entrada suave a cada troca (fade + deslize vertical) */
    BeginScissorMode((int)p.x + 10, (int)p.y + 1, (int)(p.width * 0.56f),
                     (int)p.height - 1);
    text_at(F_12, ap->view.status, p.x + 12.0f,
            p.y + 7.0f + (1.0f - sa) * 5.0f, 12.0f, 1.0f,
            Fade(COL_TEXT, 0.45f + 0.55f * sa));
    EndScissorMode();

    /* indicador: token em foco + nome didatico do tipo em analise */
    if (ap->view.focus >= 0 && ap->view.focus < ntok) tokn = ap->view.focus + 1;
    else if (ap->view.done) tokn = ntok;
    else                    tokn = 0;
    if (ap->view.focus >= 0 && ap->view.focus < ntok)
        snprintf(right, sizeof right, "Token %d de %d   |   Analisando: %s",
                 tokn, ntok, tok_label(a->tok[ap->view.focus].type));
    else if (ap->view.done)
        snprintf(right, sizeof right, "Token %d de %d   |   Analisando: fim de linha ($)",
                 tokn, ntok);
    else
        snprintf(right, sizeof right, "Token %d de %d   |   Analisando: -",
                 tokn, ntok);
    tw = text_w(F_12, right, 12.0f);
    tx = p.x + p.width - 12.0f - tw;
    text_at(F_12, right, tx, p.y + 7.0f, 12.0f, 1.0f, COL_TEXT);

    /* barra interpolada */
    target = (nev > 0) ? (float)ap->view.ev_next / (float)nev : 0.0f;
    ap->prog_vis += (target - ap->prog_vis) * Clamp(g_dt * 8.0f * anim_k(), 0.0f, 1.0f);
    prog = Clamp(ap->prog_vis, 0.0f, 1.0f);
    {
        Rectangle t = { tx - 8.0f - 110.0f, p.y + 11.0f, 110.0f, 4.0f };
        DrawRectangleRec(t, (Color){ 63, 63, 70, 255 });
        if (prog > 0.001f)
            DrawRectangle((int)t.x, (int)t.y, (int)(t.width * prog), (int)t.height,
                          COL_FOCUS);
    }
}

/* ---------- aviso flutuante de erro (usa o banner alimentado pelo EV_ERROR) ----------- */
static void draw_toast(App *ap, const Layout *L)
{
    View *v = &ap->view;
    const float life = 2.6f;
    char lines[WRAP_MAX][WRAP_LEN];
    int nl, i;
    float w = 0.0f, hh, in, out, a, slide;
    Rectangle r;

    if (v->banner_hold <= 0.0f || !v->banner[0]) return;

    nl = wrap_text(F_12, v->banner, 12.0f, 430.0f, lines);
    if (nl > 2) nl = 2;
    for (i = 0; i < nl; i++) {
        float lw = text_w(F_12, lines[i], 12.0f);
        if (lw > w) w = lw;
    }
    w += 34.0f;
    hh = 20.0f + nl * 13.0f;

    in  = Clamp((life - v->banner_hold) / (0.22f / anim_k()), 0.0f, 1.0f); /* entrada */
    out = Clamp(v->banner_hold / (0.35f / anim_k()), 0.0f, 1.0f);          /* saida   */
    a   = in * out;
    slide = (1.0f - in) * 10.0f;

    r = (Rectangle){ (float)L->W - LW_M - w, L->foot.y - 14.0f - hh + slide, w, hh };
    DrawRectangleRec(r, Fade((Color){ 46, 26, 26, 255 }, a));
    DrawRectangleLinesEx(r, 1.0f, Fade(COL_RED, a * 0.9f));
    DrawRectangle((int)r.x, (int)r.y, 2, (int)r.height, Fade(COL_RED, a));
    for (i = 0; i < nl; i++)
        text_at(F_12, lines[i], r.x + 11.0f, r.y + 7.0f + i * 13.0f, 12.0f, 1.0f,
                Fade((Color){ 254, 205, 205, 255 }, a));
}

/* ---------- ciclo de vida da aplicacao ----------------------------------------------- */
static void app_rebuild(App *ap)
{
    if (ap->an && strcmp(ap->an->source, ap->input) == 0) return;   /* inalterado */

    analysis_free(ap->an);          /* liberta a analise anterior (heap rastreado) */
    ap->an = analysis_build(ap->input);
    view_reset(&ap->view, ap->an);
    ap->player.playing = 0;
    ap->player.acc = 0.0;
    ap->ribbon_off = 0.0f;
}

static void report_mem(const char *when)
{
    printf("[mem] %s: %ld byte(s) em %ld bloco(s) vivo(s)  (pico %ld bytes)\n",
           when, g_mem_bytes, g_mem_blocks, g_mem_peak);
    if (g_mem_blocks == 0)
        printf("[mem] OK - sem vazamento de memoria\n");
    else
        printf("[mem] VAZAMENTO: %ld bloco(s) por libertar!\n", g_mem_blocks);
}

/* ---------- selftest (sem janela): logica + memoria ---------------------------------- */
static int run_selftest(void)
{
    static const int expect[5] = { 1, 1, 1, 1, 0 };
    int i, k, fails = 0;

    printf("== selftest: analisador descendente recursivo com modo panic ==\n");

    for (i = 0; i < 5; i++) {
        Analysis *a = analysis_build(EXAMPLES[i]);
        View v;
        int guard = 0, errors;

        view_reset(&v, a);
        while (v.ev_next < a->nev && guard++ < 100000)
            view_apply(&v, a, &a->ev[v.ev_next++]);

        errors = a->errors;
        printf("\n[%d] \"%s\"\n", i + 1, EXAMPLES[i]);
        printf("    tokens : ");
        for (k = 0; k < a->ntok; k++)
            printf("%s(%s) ", a->tok[k].text, type_name(a->tok[k].type));
        printf("\n    estados: ");
        for (k = 0; k < a->ntok; k++)
            printf("%s(%s) ", a->tok[k].text, status_name(v.tok_status[k]));
        printf("\n    erros  : %d (esperado %d)  ->  %s\n",
               errors, expect[i], (errors == expect[i]) ? "OK" : "FALHA");
        for (k = 0; k < a->nmsgs; k++)
            printf("             - %s\n", a->msgs[k]);
        printf("    final  : %s\n", v.status);
        printf("    eventos: %d\n", a->nev);

        if (errors != expect[i]) fails++;
        if (!v.done) { printf("    [!] fita nao terminou\n"); fails++; }

        analysis_free(a);                       /* nada pode sobrar */
        printf("    [mem] apos libertar: %ld bloco(s) / %ld byte(s)\n",
               g_mem_blocks, g_mem_bytes);
        if (g_mem_blocks != 0) fails++;
    }

    printf("\n");
    report_mem("selftest");
    if (g_mem_blocks != 0) fails++;

    printf(fails ? "== SELFTEST: FALHAS (%d) ==\n" : "== SELFTEST: TUDO OK ==\n", fails);
    return fails ? 1 : 0;
}

/* ===================================================================================== */
/*  MAIN                                                                                */
/* ===================================================================================== */
int main(int argc, char **argv)
{
    App ap;
    Layout L;
    const char *mode = NULL;
    const char *shot = NULL;
    const char *input_opt = NULL;
    float shot_at = -1.0f;
    int i, smoke = 0, shot_done = 0, tab_opt = -1, no_insp = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) {
            mode = "selftest";
        } else if (!strcmp(argv[i], "--smoke")) {
            mode = "smoke";
            if (i + 1 < argc) { smoke = atoi(argv[++i]); }
            if (smoke <= 0) smoke = 3;
        } else if (!strcmp(argv[i], "--shot")) {
            if (i + 1 < argc) shot = argv[++i];
        } else if (!strcmp(argv[i], "--input")) {
            if (i + 1 < argc) input_opt = argv[++i];
        } else if (!strcmp(argv[i], "--tab")) {
            if (i + 1 < argc) tab_opt = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--no-insp")) {
            no_insp = 1;
        }
    }

    if (mode && !strcmp(mode, "selftest")) return run_selftest();

    /* ---------- arranque da interface ---------- */
    memset(&ap, 0, sizeof ap);
    ap.player.speed = 1.0f;
    ap.insp_open = true;
    ap.insp_w = LW_RWSHUT;             /* abre com animacao suave */
    ap.con_tab = 0;
    ap.sel_set = -1;                   /* nenhuma linha de conjunto selecionada */
    ap.spd_idx = 1;                    /* 1x */
    if (tab_opt >= 0 && tab_opt <= 3) ap.con_tab = tab_opt;
    if (no_insp) { ap.insp_open = false; ap.insp_w = LW_RWSHUT; }
    snprintf(ap.input, sizeof ap.input, "%s",
             input_opt ? input_opt : EXAMPLES[4]);
    ap.caret = (int)strlen(ap.input);

    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Compilador Tiny - análise sintática com modo pânico");
    SetWindowMinSize(1120, 660);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    fonts_load();
    app_rebuild(&ap);

    if (smoke > 0) {                     /* execucao automatica p/ teste */
        ap.player.speed = 2.0f;
        ap.spd_idx = 2;
        ap.player.playing = 1;
        ap.smoke = smoke;
    }

    while (!WindowShouldClose()) {
        bool empty;

        g_dt = Clamp(GetFrameTime(), 0.0f, 0.05f);
        g_t += g_dt;
        g_reduced = ap.reduced_motion ? 1 : 0;

        /* ---- atalhos globais ---- */
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (ap.tfocus) ap.tfocus = false;
            else break;
        }
        if (IsKeyPressed(KEY_F5))  act_play(&ap);
        if (IsKeyPressed(KEY_F10)) act_step(&ap);
        if (IsKeyPressed(KEY_F6))  act_step_back(&ap);
        if (IsKeyPressed(KEY_F8))  act_reset(&ap);

        if (ap.smoke > 0.0f) {
            ap.smoke -= g_dt;
            if (ap.smoke <= 0.0f) break;
        }

        /* largura animada do inspetor (transicao suave) */
        {
            float tgt = ap.insp_open ? LW_RWOPEN : LW_RWSHUT;
            ap.insp_w += (tgt - ap.insp_w) * Clamp(g_dt * 10.0f * anim_k(), 0.0f, 1.0f);
        }

        empty = (ap.input[0] == '\0');
        L = layout_compute(&ap);

        BeginDrawing();
            draw_background();
            draw_topbar(&ap, &L, empty);
            draw_editor(&ap, &L, empty);

            /* texto alterado no campo -> re-tokeniza e re-analisa */
            if (strcmp(ap.input, ap.an->source) != 0) app_rebuild(&ap);

            player_update(ap.an, &ap.view, &ap.player, g_dt);
            view_tick(&ap.view, ap.an, g_dt);

            /* tempo desde o fim da analise (entrada escalonada das mensagens) */
            if (ap.view.done) { if (ap.res_t < 60.0f) ap.res_t += g_dt * anim_k(); }
            else                   ap.res_t = 0.0f;
            ap.sel_t += g_dt * anim_k();   /* fade da selecao de conjunto */

            draw_ribbon(&ap, ap.an, L.ribbon);
            draw_console(&ap, ap.an, L.conso);
            draw_inspector(&ap, ap.an, L.insp);
            draw_footer(&ap, ap.an, L.foot);
            draw_toast(&ap, &L);

            /* captura de ecrã (util para verificacao visual) */
            if (shot && !shot_done) {
                if (shot_at < 0.0f && (ap.view.done || g_t > 7.0f ||
                    (!ap.player.playing && ap.view.ev_next == 0 && g_t > 1.3f)))
                    shot_at = g_t + 0.35f;
                if (shot_at > 0.0f && g_t >= shot_at) {
                    rlDrawRenderBatchActive();   /* descarrega o lote antes de ler o ecrã */
                    TakeScreenshot(shot);
                    shot_done = 1;
                }
            }
        EndDrawing();
    }

    /* ---------- desmontagem: nada fica por libertar ---------- */
    fonts_unload();
    analysis_free(ap.an);
    ap.an = NULL;
    CloseWindow();

    report_mem("fim do programa");
    return (g_mem_blocks == 0) ? 0 : 1;
}
