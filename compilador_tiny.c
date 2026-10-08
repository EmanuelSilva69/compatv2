#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAX_LINE     512
#define MAX_TOKENS   128
#define TOK_TEXT      64
#define MAX_ERRORS    64

#define MS_STEP      75
#define MS_CHAR      40
#define MS_SETTLE    85
#define MS_PULSE     80
#define PULSES        6

#define C_RESET   "\033[0m"
#define C_DIM     "\033[2m"
#define C_WHITE   "\033[37m"
#define C_NEON    "\033[1;32m"
#define C_RED     "\033[1;31m"
#define C_RED_D   "\033[31m"
#define C_CYAN    "\033[1;36m"
#define C_YELLOW  "\033[1;33m"
#define C_GREY    "\033[90m"

#define FW0 "\033[1;30;47m"
#define FW1 "\033[30;47m"
#define FG0 "\033[1;30;42m"
#define FG1 "\033[30;42m"

#define CUR_ON   "\033[?25h"
#define CUR_OFF  "\033[?25l"
#define CLEAR    "\033[2J\033[H"

typedef enum {
    T_EOF = 0,
    T_ID,
    T_NUM,
    T_READ,
    T_WRITE,
    T_ASSIGN,
    T_PLUS,
    T_STAR,
    T_LEXERR,
    T_COUNT
} TokType;

typedef enum {
    ST_PENDING = 0,
    ST_OK,
    ST_ERR,
    ST_DISCARD
} TokStatus;

typedef struct {
    TokType  type;
    char     text[TOK_TEXT];
    int      col;
    TokStatus status;
} Token;

typedef struct {
    char    source[MAX_LINE];
    int     srclen;
    Token   tok[MAX_TOKENS];
    int     ntok;
    int     lex_progress;
    int     focus;
    int     stage;
    int     box_on;
    int     box_bright;
    char    box_msg[320];
    char    box_rule[220];
    char    box_sync[220];
    char    status[320];
    int     errors;
    int     done;
    char    errmsg[MAX_ERRORS][320];
    int     nerrmsg;
} Ui;

typedef unsigned SyncMask;

static Ui g;
static int g_delay_scale = 1;
static int g_vt_ok = 0;
static int pp = 0;

static const char RULE_STMT[]   = "<instrucao> -> <read> | <write> | <atribuição>";
static const char RULE_READ[]   = "<read> -> read identificador";
static const char RULE_WRITE[]  = "<write> -> write identificador";
static const char RULE_ASSIGN[] = "<atribuição> -> identificador := <exp aritmética>";
static const char RULE_EXPR[]   = "<exp aritmética> -> <operando> {<op aritmético> <operando>}";
static const char RULE_OPER[]   = "<operando> -> identificador | num";
static const char RULE_ARITH[]  = "<op aritmético> -> + | *";
static const char MSG_OK[]      = "declaração sintaticamente correta";

static const char EXP_STMT[]   = "ID ou palavra reservada (\"read\" ou \"write\")";
static const char EXP_ID[]     = "ID";
static const char EXP_ASSIGN[] = ":=";
static const char EXP_OPER[]   = "ID ou num";
static const char EXP_AFTER_ID[]   = "+, *, :=, ID, read, write ou $";
static const char EXP_AFTER_NUM[]  = "+, *, ID, read, write ou $";

#define SM(type)     (1u << (type))
#define SYNC_STMT         (SM(T_ID) | SM(T_READ) | SM(T_WRITE) | SM(T_EOF))
#define SYNC_AFTER_RW     (SM(T_ID) | SM(T_READ) | SM(T_WRITE) | SM(T_EOF))
#define SYNC_AFTER_ASSIGN (SM(T_ASSIGN) | SM(T_ID) | SM(T_NUM) | SM(T_READ) | SM(T_WRITE) | SM(T_EOF))
#define SYNC_OPERAND      (SM(T_ID) | SM(T_NUM) | SM(T_READ) | SM(T_WRITE) | SM(T_EOF))
#define SYNC_OPER_ID      (SM(T_PLUS) | SM(T_STAR) | SM(T_ASSIGN) | SM(T_ID) | SM(T_READ) | SM(T_WRITE) | SM(T_EOF))
#define SYNC_OPER_NUM     (SM(T_PLUS) | SM(T_STAR) | SM(T_ID) | SM(T_READ) | SM(T_WRITE) | SM(T_EOF))

static void delay_ms(int ms)
{
    if (ms <= 0 || g_delay_scale <= 0)
        return;
#ifdef _WIN32
    Sleep((DWORD)(ms * g_delay_scale));
#else
    usleep((useconds_t)(ms * g_delay_scale) * 1000u);
#endif
}

static void platform_init(void)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        if (SetConsoleMode(h, mode | 0x0004UL))
            g_vt_ok = 1;
    }
    SetConsoleOutputCP(65001);
#endif
}

static void cls(void)
{
#ifdef _WIN32
    if (!g_vt_ok) {
        system("cls");
        return;
    }
#endif
    fputs(CLEAR, stdout);
}

static int disp_width(const char *s)
{
    int w = 0;
    while (*s) {
        if (((unsigned char)*s & 0xC0) != 0x80)
            w++;
        s++;
    }
    return w;
}

static void box_pad(const char *s, int width, const char *color, const char *border)
{
    int i;
    int w;
    printf("%s  |%s %s", border, color, s);
    w = disp_width(s);
    for (i = w; i < width; i++)
        putchar(' ');
    printf(" %s|%s\n", border, C_RESET);
}

static void box_line(int width, const char *border, const char *l, const char *r)
{
    int i;
    printf("%s  %s", border, l);
    for (i = 0; i < width + 2; i++)
        putchar('-');
    printf("%s%s\n", r, C_RESET);
}

static void mask_names(SyncMask m, char *dst, size_t cap)
{
    static const TokType ord[] = { T_ID, T_NUM, T_READ, T_WRITE, T_ASSIGN, T_PLUS, T_STAR, T_EOF };
    static const char *nms[]   = { "ID", "num", "read", "write", ":=", "+", "*", "$" };
    int i;
    int total = 0;
    int left;
    size_t k = 0;

    dst[0] = '\0';
    for (i = 0; i < 8; i++)
        if (m & SM(ord[i]))
            total++;
    left = total;
    for (i = 0; i < 8; i++) {
        const char *sep;
        int n;
        if (!(m & SM(ord[i])))
            continue;
        left--;
        if (total == 1)
            sep = "";
        else if (left > 0)
            sep = (k == 0) ? "" : ", ";
        else
            sep = (k == 0) ? "" : " ou ";
        if (k + 1 >= cap)
            break;
        n = snprintf(dst + k, cap - k, "%s%s", sep, nms[i]);
        if (n < 0)
            break;
        k += (size_t)n;
        if (k >= cap)
            k = cap - 1;
    }
}

static TokType peek_type(void)
{
    if (pp < 0 || pp >= g.ntok)
        return T_EOF;
    return g.tok[pp].type;
}

static const char *token_color(int i)
{
    if (i >= g.lex_progress)
        return C_DIM;
    if (i == g.focus) {
        if (g.tok[i].status == ST_OK)
            return g.stage ? FG1 : FG0;
        return g.stage ? FW1 : FW0;
    }
    switch (g.tok[i].status) {
    case ST_OK:
        return C_NEON;
    case ST_ERR:
    case ST_DISCARD:
        return C_RED;
    default:
        return C_WHITE;
    }
}

static void render_tape(void)
{
    int i = 0;
    int t = 0;
    int len = g.srclen;

    while (i < len) {
        if (t < g.ntok && i == g.tok[t].col) {
            int end = g.tok[t].col + (int)strlen(g.tok[t].text);
            fputs(token_color(t), stdout);
            while (i < end && i < len) {
                putchar(g.source[i]);
                i++;
            }
            fputs(C_RESET, stdout);
            t++;
        } else {
            if (g.source[i] == '\t')
                fputs("    ", stdout);
            else
                putchar(g.source[i]);
            i++;
        }
    }
    if (t < g.ntok && g.tok[t].col >= len) {
        fputs(token_color(t), stdout);
        fputc('$', stdout);
        fputs(C_RESET, stdout);
    }
}

static void render_box(void)
{
    const char *border = g.box_bright ? C_RED : C_RED_D;
    const char *msgc   = g.box_bright ? C_RED : C_RED_D;

    box_line(68, border, "+", "+");
    box_pad(g.box_msg, 68, msgc, border);
    printf("%s  |%s %-68s %s|%s\n", border, C_YELLOW, g.box_rule, border, C_RESET);
    printf("%s  | Sincronizacao aceitavel: ", border);
    fputs(g.box_sync, stdout);
    {
        int w = disp_width("Sincronizacao aceitavel: ") + disp_width(g.box_sync);
        int i;
        for (i = w; i < 68; i++)
            putchar(' ');
    }
    printf(" %s|%s\n", border, C_RESET);
    box_line(68, border, "+", "+");
}

static void render_result(void)
{
    int i;
    if (!g.done)
        return;
    if (g.errors == 0) {
        box_line(68, C_NEON, "+", "+");
        box_pad(MSG_OK, 68, C_NEON, C_NEON);
        box_line(68, C_NEON, "+", "+");
    } else {
        printf("  %s  Foram detectado(s) %d erro(s):%s\n", C_RED, g.errors, C_RESET);
        for (i = 0; i < g.nerrmsg; i++)
            printf("  %s    %d) %s%s\n", C_RED, i + 1, g.errmsg[i], C_RESET);
    }
}

static void render(void)
{
    fputs(CLEAR, stdout);
    printf("%s==========================================================================%s\n", C_CYAN, C_RESET);
    printf("%s  COMPILADOR TINY  -  analise descendente recursiva / modo panic%s\n", C_CYAN, C_RESET);
    printf("%s==========================================================================%s\n", C_CYAN, C_RESET);
    printf("  Linha : %s%s%s\n", C_YELLOW, g.source[0] ? g.source : "(vazia)", C_RESET);
    fputs("  Fita  : ", stdout);
    render_tape();
    fputc('\n', stdout);
    printf("  %s->%s %s\n", C_GREY, C_RESET, g.status);
    if (g.box_on)
        render_box();
    render_result();
    fputs((g.focus >= 0 && g.stage == 0) ? CUR_ON : CUR_OFF, stdout);
    fflush(stdout);
}

static void tokenize(void)
{
    const char *s = g.source;
    int i = 0;
    int n = 0;
    int has_end = 0;

    g.ntok = 0;
    while (s[i] != '\0' && n < MAX_TOKENS - 1) {
        unsigned char c = (unsigned char)s[i];
        Token *tk = &g.tok[n];
        int start;
        int len;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            i++;
            continue;
        }
        tk->col = i;
        tk->status = ST_PENDING;
        tk->text[0] = '\0';
        if (c == '$') {
            tk->type = T_EOF;
            strcpy(tk->text, "$");
            has_end = 1;
            i++;
            n++;
            break;
        }
        start = i;
        if (isalpha(c)) {
            while (s[i] != '\0' && isalpha((unsigned char)s[i]))
                i++;
            len = i - start;
            if (len >= TOK_TEXT)
                len = TOK_TEXT - 1;
            memcpy(tk->text, s + start, (size_t)len);
            tk->text[len] = '\0';
            if (strcmp(tk->text, "read") == 0)
                tk->type = T_READ;
            else if (strcmp(tk->text, "write") == 0)
                tk->type = T_WRITE;
            else
                tk->type = T_ID;
        } else if (isdigit(c)) {
            while (s[i] != '\0' && isdigit((unsigned char)s[i]))
                i++;
            len = i - start;
            if (len >= TOK_TEXT)
                len = TOK_TEXT - 1;
            memcpy(tk->text, s + start, (size_t)len);
            tk->text[len] = '\0';
            tk->type = T_NUM;
        } else if (c == ':' && s[i + 1] == '=') {
            i += 2;
            tk->type = T_ASSIGN;
            strcpy(tk->text, ":=");
        } else if (c == '+') {
            i++;
            tk->type = T_PLUS;
            strcpy(tk->text, "+");
        } else if (c == '*') {
            i++;
            tk->type = T_STAR;
            strcpy(tk->text, "*");
        } else {
            i++;
            tk->type = T_LEXERR;
            tk->text[0] = (char)c;
            tk->text[1] = '\0';
        }
        n++;
    }
    if (!has_end && n < MAX_TOKENS) {
        Token *tk = &g.tok[n];
        tk->type = T_EOF;
        tk->text[0] = '$';
        tk->text[1] = '\0';
        tk->col = (int)strlen(s);
        tk->status = ST_PENDING;
        n++;
    }
    g.ntok = n;
}

static void panic_mode(SyncMask sync)
{
    int guard = 0;
    char name[220];

    while (peek_type() != T_EOF && !(sync & SM(peek_type())) && guard < MAX_TOKENS) {
        Token *tk = &g.tok[pp];
        guard++;
        if (tk->status == ST_OK || tk->status == ST_PENDING)
            tk->status = ST_DISCARD;
        snprintf(g.status, sizeof g.status,
                 "MODO PANICO: descartar token \"%s\" a procura de sincronizacao",
                 tk->text);
        g.focus = pp;
        g.stage = 0;
        render();
        delay_ms(MS_STEP);
        pp++;
        g.focus = -1;
    }
    mask_names(sync, name, sizeof name);
    if (peek_type() == T_EOF)
        snprintf(g.status, sizeof g.status, "MODO PANICO: sincronizado em fim de linha ($)");
    else
        snprintf(g.status, sizeof g.status, "MODO PANICO: sincronizado em \"%s\" [%s]",
                 g.tok[pp].text, name);
    g.focus = pp;
    g.stage = 0;
    render();
    delay_ms(MS_STEP);
    g.focus = -1;
}

static void report_error(const char *expected, const char *rule, SyncMask sync)
{
    Token *tk;
    int i;

    if (pp < 0 || pp >= g.ntok)
        return;
    tk = &g.tok[pp];
    snprintf(g.box_msg, sizeof g.box_msg, "Erro \"%s\". Esperado %s", tk->text, expected);
    snprintf(g.box_rule, sizeof g.box_rule, "Regra: %s", rule);
    mask_names(sync, g.box_sync, sizeof g.box_sync);

    tk->status = ST_ERR;
    g.errors++;
    if (g.nerrmsg < MAX_ERRORS) {
        snprintf(g.errmsg[g.nerrmsg], sizeof g.errmsg[0], "%s", g.box_msg);
        g.nerrmsg++;
    }

    g.box_on = 1;
    g.box_bright = 1;
    g.focus = pp;
    snprintf(g.status, sizeof g.status,
             "FALHA sintatica no token %d - activando modo panic", pp + 1);

    for (i = 0; i < PULSES; i++) {
        g.box_bright = (i & 1);
        g.stage = (i & 1);
        render();
        delay_ms(MS_PULSE);
    }
    g.box_bright = 1;
    g.stage = 0;
    panic_mode(sync);
    g.focus = -1;
}

static void vis_match(void)
{
    Token *tk;

    if (pp < 0 || pp >= g.ntok)
        return;
    tk = &g.tok[pp];
    if (tk->status != ST_ERR)
        tk->status = ST_OK;
    g.focus = pp;
    g.stage = 0;
    render();
    delay_ms(MS_STEP);
    g.stage = 1;
    render();
    delay_ms(MS_CHAR);
    pp++;
    g.focus = -1;
}

static int parse_operando(TokType *last)
{
    TokType t = peek_type();

    if (t == T_ID || t == T_NUM) {
        *last = t;
        snprintf(g.status, sizeof g.status, "A validar %s", RULE_OPER);
        vis_match();
        return 1;
    }
    report_error(EXP_OPER, RULE_OPER, SYNC_OPERAND);
    return 0;
}

static void parse_expressao(void)
{
    TokType last = T_LEXERR;

    snprintf(g.status, sizeof g.status, "A validar %s", RULE_EXPR);
    if (!parse_operando(&last))
        return;
    while (peek_type() == T_PLUS || peek_type() == T_STAR) {
        snprintf(g.status, sizeof g.status, "A validar %s", RULE_ARITH);
        vis_match();
        if (!parse_operando(&last))
            return;
    }
    if (last == T_ID && !(SYNC_OPER_ID & SM(peek_type())))
        report_error(EXP_AFTER_ID, RULE_EXPR, SYNC_OPER_ID);
    else if (last == T_NUM && !(SYNC_OPER_NUM & SM(peek_type())))
        report_error(EXP_AFTER_NUM, RULE_EXPR, SYNC_OPER_NUM);
}

static void parser_read(void)
{
    snprintf(g.status, sizeof g.status, "A validar %s", RULE_READ);
    vis_match();
    if (peek_type() == T_ID) {
        vis_match();
    } else {
        report_error(EXP_ID, RULE_READ, SYNC_AFTER_RW);
    }
}

static void parser_write(void)
{
    snprintf(g.status, sizeof g.status, "A validar %s", RULE_WRITE);
    vis_match();
    if (peek_type() == T_ID) {
        vis_match();
    } else {
        report_error(EXP_ID, RULE_WRITE, SYNC_AFTER_RW);
    }
}

static void parser_atribuicao(void)
{
    snprintf(g.status, sizeof g.status, "A validar %s", RULE_ASSIGN);
    vis_match();
    if (peek_type() != T_ASSIGN) {
        report_error(EXP_ASSIGN, RULE_ASSIGN, SYNC_AFTER_ASSIGN);
        if (peek_type() != T_ASSIGN)
            return;
    }
    vis_match();
    parse_expressao();
}

static void parser_instrucao(void)
{
    switch (peek_type()) {
    case T_EOF:
        return;
    case T_READ:
        parser_read();
        return;
    case T_WRITE:
        parser_write();
        return;
    case T_ID:
        parser_atribuicao();
        return;
    default:
        report_error(EXP_STMT, RULE_STMT, SYNC_STMT);
        return;
    }
}

static void analise_sintatica(void)
{
    int guard = 0;

    pp = 0;
    g.lex_progress = g.ntok;
    snprintf(g.status, sizeof g.status, "Etapa 2 - analise sintatica descendente recursiva");
    render();
    delay_ms(MS_STEP);

    while (peek_type() != T_EOF && guard < MAX_TOKENS * 4) {
        int before = pp;
        guard++;
        parser_instrucao();
        if (pp == before && peek_type() != T_EOF) {
            g.tok[pp].status = ST_ERR;
            pp++;
        }
    }
}

static void animacao_lexer(void)
{
    int i;

    g.lex_progress = 0;
    g.focus = -1;
    snprintf(g.status, sizeof g.status, "Etapa 1 - tokenizacao animada (fita sequencial)");
    render();
    delay_ms(MS_STEP);

    for (i = 0; i < g.ntok; i++) {
        g.focus = i;
        g.lex_progress = i;
        g.stage = 0;
        snprintf(g.status, sizeof g.status, "A ler caractere/token %d de %d ...", i + 1, g.ntok);
        render();
        delay_ms(MS_CHAR);
        g.stage = 1;
        render();
        delay_ms(MS_CHAR);
        g.tok[i].status = (g.tok[i].type == T_LEXERR) ? ST_ERR : ST_OK;
        g.lex_progress = i + 1;
        g.focus = -1;
        render();
        delay_ms(MS_SETTLE);
    }
}

static void executar_linha(const char *line)
{
    memset(&g, 0, sizeof g);
    snprintf(g.source, sizeof g.source, "%s", line);
    g.srclen = (int)strlen(g.source);
    g.focus = -1;
    g.stage = 0;

    tokenize();
    animacao_lexer();
    analise_sintatica();

    g.box_on = 0;
    g.focus = -1;
    g.done = 1;
    if (g.errors == 0)
        snprintf(g.status, sizeof g.status, "Analise concluida - linha valida");
    else
        snprintf(g.status, sizeof g.status, "Analise concluida - %d erro(s) detectado(s)", g.errors);
    render();
}

static int ler_linha(char *buf, size_t cap)
{
    size_t n;

    if (fgets(buf, (int)cap, stdin) == NULL)
        return 0;
    n = strlen(buf);
    if (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
    } else {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) { }
    }
    n = strlen(buf);
    if (n >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, n - 3 + 1);
    }
    return 1;
}

static int igual(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void menu(void)
{
    static const char *testes[] = {
        "read 20",
        "1 read var",
        "var.:=temp",
        "var := 25-10",
        "x := a + b * 2"
    };
    char buf[MAX_LINE];
    int i;

    for (;;) {
        cls();
        printf("%s==========================================================================%s\n", C_CYAN, C_RESET);
        printf("%s  COMPILADOR TINY  -  analise descendente recursiva / modo panic%s\n", C_CYAN, C_RESET);
        printf("%s==========================================================================%s\n", C_CYAN, C_RESET);
        printf("%s  Testes pre-definidos:%s\n", C_YELLOW, C_RESET);
        for (i = 0; i < 5; i++)
            printf("    [%d] %s%s%s\n", i + 1, C_WHITE, testes[i], C_RESET);
        printf("%s  [6] Introduzir linha personalizada%s\n", C_YELLOW, C_RESET);
        printf("%s  [0] Sair%s\n", C_YELLOW, C_RESET);
        printf("%s==========================================================================%s\n", C_CYAN, C_RESET);
        printf("  Opcao: ");
        fflush(stdout);

        if (!ler_linha(buf, sizeof buf))
            break;

        if (buf[0] == '\0')
            continue;
        if (buf[0] == '0' && buf[1] == '\0')
            break;
        if (igual(buf, "sair") || igual(buf, "exit"))
            break;

        if (buf[0] >= '1' && buf[0] <= '5' && buf[1] == '\0') {
            executar_linha(testes[buf[0] - '1']);
        } else if (buf[0] == '6' && buf[1] == '\0') {
            printf("%s  Introduza a linha: %s", C_YELLOW, C_RESET);
            fflush(stdout);
            if (!ler_linha(buf, sizeof buf))
                break;
            executar_linha(buf);
        } else {
            printf("%s  Opcao invalida.%s", C_RED, C_RESET);
            fflush(stdout);
            delay_ms(700);
            continue;
        }

        printf("\n%s  Prima ENTER para voltar ao menu...%s", C_GREY, C_RESET);
        fflush(stdout);
        if (!ler_linha(buf, sizeof buf))
            break;
    }
}

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fast") == 0 || strcmp(argv[i], "--sem-animacao") == 0)
            g_delay_scale = 0;
    }

    platform_init();
    menu();
    fputs(CUR_ON, stdout);
    fflush(stdout);
    return 0;
}
