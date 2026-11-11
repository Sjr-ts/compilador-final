#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAME 96
#define MAX_TEXT 256
#define MAX_PARAMS 32
#define MAX_ERRORS 4096
#define MAX_OPTS 4096

typedef enum {
    T_EOF, T_ID, T_INT_LITERAL, T_FLOAT_LITERAL, T_CHAR_LITERAL, T_STRING_LITERAL,
    K_INT, K_FLOAT, K_DOUBLE, K_CHAR, K_VOID, K_CONST, K_STRUCT,
    K_RETURN, K_IF, K_ELSE, K_FOR, K_WHILE, K_DO, K_BREAK, K_CONTINUE,
    K_SWITCH, K_CASE, K_DEFAULT, K_SIZEOF,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_SEMI, T_COMMA, T_DOT, T_QUESTION, T_COLON,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_INC, T_DEC, T_ASSIGN, T_ADD_ASSIGN, T_SUB_ASSIGN, T_MUL_ASSIGN, T_DIV_ASSIGN,
    T_MOD_ASSIGN, T_AND_ASSIGN, T_OR_ASSIGN, T_XOR_ASSIGN, T_SHL_ASSIGN, T_SHR_ASSIGN,
    T_SHL, T_SHR,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE, T_AND, T_OR, T_NOT, T_BITAND, T_BITOR,
    T_XOR, T_TILDE, T_ARROW, T_INVALID
} TokenKind;
typedef struct {
    TokenKind kind; char text[MAX_TEXT]; int line; int col;
} Token;
typedef struct {
    Token *data; size_t count; size_t cap;
} TokenVec;
typedef enum { TY_ERROR, TY_VOID, TY_INT, TY_FLOAT, TY_CHAR, TY_STRING, TY_STRUCT } TypeKind;
typedef struct {
    TypeKind kind; char tag[MAX_NAME]; int is_array;
} Type;
typedef struct Symbol Symbol;
struct Symbol {
    char name[MAX_NAME]; Type type; int line; int depth; int is_function; int is_const; int has_const;
    double const_value; int initialized; int reads; int has_last_assignment; int value_read_since_assignment;
    int last_assignment_line; char last_assignment[MAX_TEXT]; int defined; int variadic; int param_count;
    Type params[MAX_PARAMS]; Symbol *next;
};
typedef struct {
    int line; int col; char phase[20]; char message[MAX_TEXT];
} Diagnostic;
typedef struct {
    int line; char kind[48]; char before[MAX_TEXT]; char after[MAX_TEXT];
} Optimization;
typedef struct {
    Type type; Symbol *symbol; int is_lvalue; int is_constant; double value; int syntax_bad; char repr[MAX_TEXT];
} Expr;
typedef struct {
    TokenVec tokens; size_t pos; Symbol *symbols; int scope_depth; Symbol *current_function; Type return_type;
    int loop_depth; char loop_induction[MAX_NAME]; Diagnostic errors[MAX_ERRORS]; int error_count; int lex_count;
    int syntax_count; int semantic_count; Optimization opts[MAX_OPTS]; int opt_count; char seen_expr[512][MAX_TEXT];
    int seen_expr_count; char propagated[MAX_PARAMS * 8][MAX_NAME]; int propagated_count; int had_lex_error;
    int had_syntax_error;
} Compiler;
static void copy_text(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
static void format_text(char *dst, size_t cap, const char *fmt, ...) {
    if (!cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
}
static Type make_type(TypeKind kind) {
    Type t;
    memset(&t, 0, sizeof(t));
    t.kind = kind;
    return t;
}
static int type_equal(Type a, Type b) {
    if (a.kind != b.kind) return 0;
    if (a.kind == TY_STRUCT && strcmp(a.tag, b.tag) != 0) return 0;
    return a.is_array == b.is_array;
}
static int type_assignable(Type dst, Type src) {
    if (dst.kind == TY_ERROR || src.kind == TY_ERROR) return 1;
    if (dst.kind == TY_STRING && src.kind == TY_CHAR && src.is_array) return 1;
    if (type_equal(dst, src)) return 1;
    if (!dst.is_array && !src.is_array && dst.kind == TY_FLOAT && (src.kind == TY_INT || src.kind == TY_CHAR)) return 1;
    if (!dst.is_array && !src.is_array && dst.kind == TY_INT && src.kind == TY_CHAR) return 1;
    return 0;
}
static const char *type_name(Type t) {
    static char buffers[4][MAX_NAME];
    static unsigned index = 0;
    char *out = buffers[(index++) % 4];
    switch (t.kind) {
        case TY_VOID: snprintf(out, MAX_NAME, "void"); break;
        case TY_INT: snprintf(out, MAX_NAME, "int"); break;
        case TY_FLOAT: snprintf(out, MAX_NAME, "float"); break;
        case TY_CHAR: snprintf(out, MAX_NAME, "char"); break;
        case TY_STRING: snprintf(out, MAX_NAME, "string"); break;
        case TY_STRUCT: snprintf(out, MAX_NAME, "%s", t.tag); break;
        case TY_ERROR: snprintf(out, MAX_NAME, "tipo invalido"); break;
        default: snprintf(out, MAX_NAME, "desconhecido"); break;
    }
    return out;
}
static void diagnostic(Compiler *c, int line, int col, const char *phase, const char *fmt, ...) {
    if (c->error_count >= MAX_ERRORS) return;
    Diagnostic *d = &c->errors[c->error_count++];
    d->line = line;
    d->col = col;
    snprintf(d->phase, sizeof(d->phase), "%s", phase);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->message, sizeof(d->message), fmt, ap);
    va_end(ap);
    if (strcmp(phase, "LEXICO") == 0) { c->lex_count++; c->had_lex_error = 1; }
    else if (strcmp(phase, "SINTATICO") == 0) { c->syntax_count++; c->had_syntax_error = 1; }
    else if (strcmp(phase, "SEMANTICO") == 0) c->semantic_count++;
}
static void optimization(Compiler *c, int line, const char *kind, const char *before, const char *after) {
    if (c->opt_count >= MAX_OPTS) return;
    Optimization *o = &c->opts[c->opt_count++];
    o->line = line;
    snprintf(o->kind, sizeof(o->kind), "%s", kind);
    snprintf(o->before, sizeof(o->before), "%s", before);
    snprintf(o->after, sizeof(o->after), "%s", after);
}
static void remove_overwrite_suggestions(Compiler *c, const char *name) {
    char prefix[MAX_NAME + 4];
    snprintf(prefix, sizeof(prefix), "%s =", name);
    for (int i = 0; i < c->opt_count;) {
        Optimization *o = &c->opts[i];
        if (strcmp(o->kind, "eliminacao de atribuicao sobrescrita") == 0 &&
            strncmp(o->before, prefix, strlen(prefix)) == 0) {
            for (int j = i; j + 1 < c->opt_count; ++j) c->opts[j] = c->opts[j + 1];
            --c->opt_count;
        } else {
            ++i;
        }
    }
}
static void record_assignment(Compiler *c, Symbol *s, Token op,
                               const char *rhs, int simple_assignment) {
    char current[MAX_TEXT];
    if (!s) return;
    if (simple_assignment && s->has_last_assignment &&
        !s->value_read_since_assignment) {
        char after[MAX_TEXT];
        format_text(after, sizeof(after),
                    "remover a atribuicao anterior de '%s' (linha %d), pois o valor foi sobrescrito antes de ser usado",
                    s->name, s->last_assignment_line);
        optimization(c, op.line, "eliminacao de atribuicao sobrescrita",
                     s->last_assignment, after);
    }
    format_text(current, sizeof(current), "%s %s %s", s->name,
                op.text[0] ? op.text : "=", rhs);
    copy_text(s->last_assignment, sizeof(s->last_assignment), current);
    s->last_assignment_line = op.line;
    s->has_last_assignment = 1;
    s->value_read_since_assignment = 0;
    s->initialized = 1;
}
static void token_push(TokenVec *v, Token t) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 256; Token *p = (Token *)realloc(v->data, next * sizeof(Token));
        if (!p) { fprintf(stderr, "Memoria insuficiente.\n"); exit(2); }
        v->data = p;
        v->cap = next;
    }
    v->data[v->count++] = t;
}
static Token make_token(TokenKind kind, const char *src, size_t len, int line, int col) {
    Token t;
    t.kind = kind;
    size_t n = len < MAX_TEXT - 1 ? len : MAX_TEXT - 1;
    memcpy(t.text, src, n);
    t.text[n] = '\0';
    t.line = line;
    t.col = col;
    return t;
}
static TokenKind keyword_kind(const char *s) {
    struct Pair { const char *s; TokenKind k; } words[] = {
        {"int",K_INT},{"float",K_FLOAT},{"double",K_DOUBLE},{"char",K_CHAR},{"void",K_VOID},
        {"const",K_CONST},{"struct",K_STRUCT},{"return",K_RETURN},{"if",K_IF},{"else",K_ELSE},
        {"for",K_FOR},{"while",K_WHILE},{"do",K_DO},{"break",K_BREAK},{"continue",K_CONTINUE},
        {"switch",K_SWITCH},{"case",K_CASE},{"default",K_DEFAULT},{"sizeof",K_SIZEOF}
    };
    size_t n = sizeof(words) / sizeof(words[0]);
    for (size_t i = 0; i < n; i++) if (strcmp(s, words[i].s) == 0) return words[i].k;
    return T_ID;
}
static int is_ident_start(int c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_part(int c) { return isalnum((unsigned char)c) || c == '_'; }
static int decode_char_literal(const char *text) {
    const unsigned char *p=(const unsigned char *)text;
    if (p[1]!='\\') return p[1];
    switch (p[2]) {
        case 'a': return '\a'; case 'b': return '\b'; case 'f': return '\f';
        case 'n': return '\n'; case 'r': return '\r'; case 't': return '\t';
        case 'v': return '\v'; case '\\': return '\\'; case '\'': return '\'';
        case '"': return '"'; case '?': return '?';
        case 'x': {
            char *end=NULL; long value=strtol((const char *)(p+3),&end,16);
            return end==(char *)(p+3)?0:(int)(unsigned char)value;
        }
        default:
            if (p[2]>='0' && p[2]<='7') {
                int value=0, count=0;
                while (count<3 && p[2+count]>='0' && p[2+count]<='7') {
                    value=value*8+(p[2+count]-'0');
                    ++count;
                }
                return value & 0xFF;
            }
            return p[2];
    }
}
static void lex_source(Compiler *c, const char *src) {
    size_t i = 0; int line = 1, col = 1, line_start = 1;
    while (src[i]) {
        char ch = src[i];
        if (line_start && (ch == ' ' || ch == '\t' || ch == '\r')) { i++; col++; continue; }
        if (line_start && ch == '#') {
            while (src[i] && src[i] != '\n') { i++; col++; }
            continue;
        }
        line_start = 0;
        if (ch == '\n') { i++; line++; col = 1; line_start = 1; continue; }
        if (isspace((unsigned char)ch)) { i++; col++; continue; }
        if (ch == '/' && src[i + 1] == '/') {
            while (src[i] && src[i] != '\n') { i++; col++; }
            continue;
        }
        if (ch == '/' && src[i + 1] == '*') {
            int start_line = line, start_col = col;
            i += 2; col += 2;
            while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') { i++; line++; col = 1; line_start = 1; }
                else { i++; col++; }
            }
            if (src[i]) { i += 2; col += 2; }
            else diagnostic(c, start_line, start_col, "LEXICO", "comentario de bloco nao foi fechado");
            continue;
        }
        int start_line = line, start_col = col; size_t start = i;
        if (is_ident_start(ch)) {
            i++; col++;
            while (is_ident_part(src[i])) { i++; col++; }
            Token t = make_token(T_ID, src + start, i - start, start_line, start_col);
            t.kind = keyword_kind(t.text);
            token_push(&c->tokens, t);
            continue;
        }
        if (isdigit((unsigned char)ch) || (ch=='.' && isdigit((unsigned char)src[i+1]))) {
            int is_float = 0; int malformed = 0; int has_hex_exponent = 0;
            if (ch=='0' && (src[i+1]=='x' || src[i+1]=='X')) {
                i+=2; col+=2;
                while (isxdigit((unsigned char)src[i])) { i++; col++; }
                if (src[i]=='.') {
                    is_float=1; i++; col++;
                    while (isxdigit((unsigned char)src[i])) { i++; col++; }
                }
                if (src[i]=='p' || src[i]=='P') {
                    size_t exp_pos=i, j=i+1;
                    if (src[j]=='+' || src[j]=='-') j++;
                    if (isdigit((unsigned char)src[j])) {
                        is_float=1; has_hex_exponent=1; i=j+1; col+=(int)(i-exp_pos);
                        while (isdigit((unsigned char)src[i])) { i++; col++; }
                    } else {
                        diagnostic(c,start_line,start_col,"LEXICO","expoente incompleto em literal hexadecimal");
                        i=j; col+=(int)(i-exp_pos); malformed=1;
                    }
                }
                if (is_float && !has_hex_exponent) {
                    diagnostic(c,start_line,start_col,"LEXICO","literal hexadecimal de ponto flutuante exige expoente p/P");
                    malformed=1;
                }
                if (i==start+2) {
                    diagnostic(c,start_line,start_col,"LEXICO","literal hexadecimal sem digitos");
                    while (isalnum((unsigned char)src[i])) { i++; col++; }
                    token_push(&c->tokens,make_token(T_INVALID,src+start,i-start,start_line,start_col));
                    continue;
                }
            } else {
                if (ch=='.') { is_float=1; i++; col++; }
                while (isdigit((unsigned char)src[i])) { i++; col++; }
                if (src[i]=='.' && !is_float) {
                    is_float=1; i++; col++;
                    while (isdigit((unsigned char)src[i])) { i++; col++; }
                }
                if (src[i]=='e' || src[i]=='E') {
                    size_t exp_pos=i, j=i+1;
                    if (src[j]=='+' || src[j]=='-') j++;
                    if (isdigit((unsigned char)src[j])) {
                        is_float=1; i=j+1; col+=(int)(i-exp_pos);
                        while (isdigit((unsigned char)src[i])) { i++; col++; }
                    } else {
                        diagnostic(c,start_line,start_col,"LEXICO","expoente incompleto em literal numerico");
                        i=j; col+=(int)(i-exp_pos); malformed=1;
                    }
                }
            }
            if (is_float) {
                if (src[i]=='f' || src[i]=='F' || src[i]=='l' || src[i]=='L') { i++; col++; }
            } else {
                while (src[i]=='u' || src[i]=='U' || src[i]=='l' || src[i]=='L') { i++; col++; }
            }
            if (!is_float && src[start]=='0' && src[start+1]>='0' && src[start+1]<='9' &&
                !(src[start+1]=='x' || src[start+1]=='X')) {
                for (size_t k=start+1;k<i;k++) {
                    if (src[k]=='8' || src[k]=='9') {
                        diagnostic(c,start_line,start_col,"LEXICO","digito invalido em literal octal");
                        malformed=1;
                        break;
                    }
                }
            }
            if (is_ident_start((unsigned char)src[i])) {
                diagnostic(c,start_line,start_col,"LEXICO","sufixo ou caractere invalido em literal numerico");
                while (is_ident_part((unsigned char)src[i])) { i++; col++; }
                malformed=1;
            }
            if (malformed) {
                token_push(&c->tokens,make_token(T_INVALID,src+start,i-start,start_line,start_col));
                continue;
            }
            token_push(&c->tokens, make_token(is_float ? T_FLOAT_LITERAL : T_INT_LITERAL, src + start, i - start, start_line, start_col));
            continue;
        }
        if (ch == '"' || ch == '\'') {
            char quote = ch;
            i++; col++;
            int closed = 0;
            while (src[i]) {
                if (src[i] == '\\' && src[i + 1]) {
                    if (src[i + 1] == '\n') {
                        i += 2;
                        line++;
                        col = 1;
                        line_start = 0;
                        continue;
                    }
                    if (!strchr("abfnrtv\\'\"?01234567xXuU", src[i + 1]))
                        diagnostic(c,line,col,"LEXICO","sequencia de escape invalida '\\%c'",src[i + 1]);
                    i += 2;
                    col += 2;
                    continue;
                }
                if (src[i] == quote) { i++; col++; closed = 1; break; }
                if (src[i] == '\n') break;
                i++;
                col++;
            }
            if (!closed) {
                diagnostic(c,start_line,start_col,"LEXICO","literal %s nao foi fechado",quote=='"' ? "string" : "caractere");
                token_push(&c->tokens,make_token(T_INVALID,src+start,i-start,start_line,start_col));
            } else if (quote=='\'' && i-start<=2) {
                diagnostic(c,start_line,start_col,"LEXICO","literal de caractere vazio");
                token_push(&c->tokens,make_token(T_INVALID,src+start,i-start,start_line,start_col));
            } else {
                token_push(&c->tokens,make_token(quote=='"' ? T_STRING_LITERAL : T_CHAR_LITERAL,
                                                  src+start,i-start,start_line,start_col));
            }
            continue;
        }
        struct Op { const char *s; TokenKind k; } ops[] = {
            {"<<=",T_SHL_ASSIGN},{">>=",T_SHR_ASSIGN},
            {"++",T_INC},{"--",T_DEC},{"+=",T_ADD_ASSIGN},{"-=",T_SUB_ASSIGN},
            {"*=",T_MUL_ASSIGN},{"/=",T_DIV_ASSIGN},{"%=",T_MOD_ASSIGN},
            {"&=",T_AND_ASSIGN},{"|=",T_OR_ASSIGN},{"^=",T_XOR_ASSIGN},
            {"==",T_EQ},{"!=",T_NE},{"<=",T_LE},{">=",T_GE},
            {"&&",T_AND},{"||",T_OR},{"->",T_ARROW},{"<<",T_SHL},{">>",T_SHR}
        };
        int matched = 0;
        for (size_t oi = 0; oi < sizeof(ops)/sizeof(ops[0]); oi++) {
            size_t n = strlen(ops[oi].s);
            if (strncmp(src + i, ops[oi].s, n) == 0) {
                token_push(&c->tokens, make_token(ops[oi].k, src + i, n, line, col));
                i += n; col += (int)n; matched = 1; break;
            }
        }
        if (matched) continue;
        TokenKind kind = T_INVALID;
        switch (ch) {
            case '(': kind=T_LPAREN; break; case ')': kind=T_RPAREN; break;
            case '{': kind=T_LBRACE; break; case '}': kind=T_RBRACE; break;
            case '[': kind=T_LBRACKET; break; case ']': kind=T_RBRACKET; break;
            case ';': kind=T_SEMI; break; case ',': kind=T_COMMA; break;
            case '.': kind=T_DOT; break; case '?': kind=T_QUESTION; break; case ':': kind=T_COLON; break;
            case '+': kind=T_PLUS; break; case '-': kind=T_MINUS; break; case '*': kind=T_STAR; break;
            case '/': kind=T_SLASH; break; case '%': kind=T_PERCENT; break; case '=': kind=T_ASSIGN; break;
            case '<': kind=T_LT; break; case '>': kind=T_GT; break; case '!': kind=T_NOT; break;
            case '&': kind=T_BITAND; break; case '|': kind=T_BITOR; break; case '^': kind=T_XOR; break;
            case '~': kind=T_TILDE; break;
        }
        if (kind == T_INVALID || ch == '@' || ch == '$' || ch == '`' || ch == '\\') {
            diagnostic(c, line, col, "LEXICO", "caractere nao reconhecido '%c'", ch);
            token_push(&c->tokens,make_token(T_INVALID,src+i,1,line,col));
            i++; col++;
        } else {
            token_push(&c->tokens, make_token(kind, src + i, 1, line, col));
            i++; col++;
        }
    }
    Token eof = make_token(T_EOF, "<fim>", 5, line, col);
    token_push(&c->tokens, eof);
}
static Token *peek(Compiler *c) { return &c->tokens.data[c->pos < c->tokens.count ? c->pos : c->tokens.count - 1]; }
static Token *previous(Compiler *c) { return &c->tokens.data[c->pos ? c->pos - 1 : 0]; }
static int check(Compiler *c, TokenKind k) { return peek(c)->kind == k; }
static Token advance_token(Compiler *c) { if (!check(c, T_EOF)) c->pos++; return *previous(c); }
static int match(Compiler *c, TokenKind k) { if (!check(c,k)) return 0; advance_token(c); return 1; }
static int expect(Compiler *c, TokenKind k, const char *what) {
    if (match(c,k)) return 1;
    Token *t = peek(c);
    diagnostic(c, t->line, t->col, "SINTATICO", "esperado %s antes de '%s'", what, t->text);
    return 0;
}
static void scope_enter(Compiler *c) { c->scope_depth++; }
static void scope_leave(Compiler *c, int report_dead) {
    Symbol *s = c->symbols;
    while (s && s->depth == c->scope_depth) {
        Symbol *next = s->next;
        if (report_dead && !s->is_function && !s->is_const && s->initialized && s->reads == 0) {
            remove_overwrite_suggestions(c, s->name);
            char before[MAX_TEXT], after[MAX_TEXT];
            snprintf(before, sizeof(before), "declaracao de %s", s->name);
            snprintf(after, sizeof(after), "remover declaracao e inicializacao de %s", s->name);
            optimization(c, s->line, "eliminacao de codigo morto", before, after);
        }
        free(s);
        s = next;
    }
    c->symbols = s;
    if (c->scope_depth > 0) c->scope_depth--;
}
static Symbol *find_symbol(Compiler *c, const char *name) {
    for (Symbol *s = c->symbols; s; s = s->next) if (strcmp(s->name,name)==0) return s;
    return NULL;
}
static Symbol *find_global(Compiler *c, const char *name) {
    for (Symbol *s = c->symbols; s; s = s->next) if (s->depth == 0 && strcmp(s->name,name)==0) return s;
    return NULL;
}
static Symbol *add_symbol(Compiler *c, const char *name, Type type, int line, int is_function) {
    Symbol *s = (Symbol *)calloc(1, sizeof(Symbol));
    if (!s) { fprintf(stderr,"Memoria insuficiente.\n"); exit(2); }
    snprintf(s->name,sizeof(s->name),"%s",name);
    s->type = type;
    s->line = line;
    s->depth = c->scope_depth;
    s->is_function = is_function;
    s->next = c->symbols;
    c->symbols = s;
    return s;
}
static int is_type_start(TokenKind k) {
    return k==K_INT || k==K_FLOAT || k==K_DOUBLE || k==K_CHAR || k==K_VOID || k==K_STRUCT || k==K_CONST;
}
static int is_statement_start(TokenKind k) {
    return is_type_start(k) || k==K_RETURN || k==K_IF || k==K_FOR || k==K_WHILE || k==K_DO ||
           k==K_BREAK || k==K_CONTINUE || k==T_LBRACE || k==T_SEMI || k==T_ID || k==T_INT_LITERAL ||
           k==T_FLOAT_LITERAL || k==T_CHAR_LITERAL || k==T_STRING_LITERAL || k==T_LPAREN || k==T_INC || k==T_DEC;
}
static void synchronize(Compiler *c) {
    while (!check(c,T_EOF)) {
        if (previous(c)->kind == T_SEMI) return;
        if (check(c,T_RBRACE) || is_statement_start(peek(c)->kind)) return;
        advance_token(c);
    }
}
static int accept_semicolon(Compiler *c) {
    if (match(c,T_SEMI)) return 1;
    Token *t = peek(c); Token *prev = previous(c); int error_line = prev->line;
    int error_col = prev->col + (int)strlen(prev->text);
    if (t->line == prev->line) {
        error_line = t->line;
        error_col = t->col;
    }
    diagnostic(c,error_line,error_col,"SINTATICO",
               "esperado ';' ao final da instrucao (apos '%s')",prev->text);
    if (!is_statement_start(t->kind) && t->kind != T_RBRACE && t->kind != T_EOF) synchronize(c);
    match(c,T_SEMI);
    return 0;
}
static int expr_precedence(TokenKind k) {
    switch(k) {
        case T_ASSIGN: case T_ADD_ASSIGN: case T_SUB_ASSIGN: case T_MUL_ASSIGN: case T_DIV_ASSIGN:
        case T_MOD_ASSIGN: case T_AND_ASSIGN: case T_OR_ASSIGN: case T_XOR_ASSIGN:
        case T_SHL_ASSIGN: case T_SHR_ASSIGN: return 1;
        case T_OR: return 2;
        case T_AND: return 3;
        case T_BITOR: return 4;
        case T_XOR: return 5;
        case T_BITAND: return 6;
        case T_EQ: case T_NE: return 7;
        case T_LT: case T_LE: case T_GT: case T_GE: return 8;
        case T_SHL: case T_SHR: return 9;
        case T_PLUS: case T_MINUS: return 10;
        case T_STAR: case T_SLASH: case T_PERCENT: return 11;
        default: return 0;
    }
}
static const char *operator_text(TokenKind k) {
    switch(k) {
        case T_PLUS:return "+"; case T_MINUS:return "-"; case T_STAR:return "*"; case T_SLASH:return "/"; case T_PERCENT:return "%";
        case T_EQ:return "=="; case T_NE:return "!="; case T_LT:return "<"; case T_LE:return "<="; case T_GT:return ">"; case T_GE:return ">=";
        case T_AND:return "&&"; case T_OR:return "||"; case T_ASSIGN:return "="; case T_ADD_ASSIGN:return "+=";
        case T_SUB_ASSIGN:return "-="; case T_MUL_ASSIGN:return "*="; case T_DIV_ASSIGN:return "/=";
        case T_MOD_ASSIGN:return "%="; case T_AND_ASSIGN:return "&="; case T_OR_ASSIGN:return "|=";
        case T_XOR_ASSIGN:return "^="; case T_SHL_ASSIGN:return "<<="; case T_SHR_ASSIGN:return ">>=";
        case T_BITAND:return "&"; case T_BITOR:return "|"; case T_XOR:return "^";
        case T_SHL:return "<<"; case T_SHR:return ">>";
        default:return "?";
    }
}
static int expr_is_numeric(Type t) { return t.kind==TY_INT || t.kind==TY_FLOAT || t.kind==TY_CHAR; }
static void remember_expression(Compiler *c, int line, const char *repr) {
    if (!repr[0] || c->seen_expr_count >= 512) return;
    for (int i=0;i<c->seen_expr_count;i++) {
        if (strcmp(c->seen_expr[i],repr)==0) {
            char after[MAX_TEXT];
            format_text(after,sizeof(after),"reutilizar o resultado de %s",repr);
            optimization(c,line,"eliminacao de subexpressao comum",repr,after);
            return;
        }
    }
    snprintf(c->seen_expr[c->seen_expr_count++],MAX_TEXT,"%s",repr);
}
static Expr parse_expression(Compiler *c, int min_prec);
static Expr bad_expr(void) {
    Expr e;
    memset(&e,0,sizeof(e));
    e.type=make_type(TY_ERROR);
    e.syntax_bad=1;
    snprintf(e.repr,sizeof(e.repr),"<expressao invalida>");
    return e;
}
static Expr parse_primary(Compiler *c) {
    Token t = *peek(c);
    if (match(c,T_INVALID)) return bad_expr();
    if (match(c,T_INT_LITERAL) || match(c,T_FLOAT_LITERAL)) {
        Expr e; memset(&e,0,sizeof(e));
        e.type=make_type(t.kind==T_FLOAT_LITERAL?TY_FLOAT:TY_INT);
        e.is_constant=1;
        if (t.kind==T_FLOAT_LITERAL) e.value=strtod(t.text,NULL);
        else e.value=(double)strtoll(t.text,NULL,0);
        snprintf(e.repr,sizeof(e.repr),"%s",t.text);
        return e;
    }
    if (match(c,T_CHAR_LITERAL)) {
        Expr e; memset(&e,0,sizeof(e));
        e.type=make_type(TY_CHAR); e.is_constant=1;
        e.value=(double)decode_char_literal(t.text);
        snprintf(e.repr,sizeof(e.repr),"%s",t.text);
        return e;
    }
    if (match(c,T_STRING_LITERAL)) {
        Expr e; memset(&e,0,sizeof(e)); e.type=make_type(TY_STRING);
        snprintf(e.repr,sizeof(e.repr),"%s",t.text); return e;
    }
    if (match(c,T_ID)) {
        Expr e; memset(&e,0,sizeof(e));
        snprintf(e.repr,sizeof(e.repr),"%s",t.text);
        if (match(c,T_LPAREN)) {
            c->seen_expr_count=0;
            Symbol *fn=find_symbol(c,t.text);
            if (!fn || !fn->is_function) {
                if (!fn) diagnostic(c,t.line,t.col,"SEMANTICO","funcao '%s' nao declarada",t.text);
                else diagnostic(c,t.line,t.col,"SEMANTICO","'%s' nao e uma funcao",t.text);
            }
            Type arg_types[MAX_PARAMS]; int argc=0, malformed=0;
            if (!check(c,T_RPAREN)) {
                do {
                    if (argc<MAX_PARAMS) arg_types[argc]=parse_expression(c,2).type;
                    else parse_expression(c,2);
                    argc++;
                    if (check(c,T_RPAREN)) break;
                    if (!match(c,T_COMMA)) {
                        Token *p=peek(c);
                        diagnostic(c,p->line,p->col,"SINTATICO","esperado ',' ou ')' na lista de argumentos");
                        malformed=1;
                        while (!check(c,T_RPAREN) && !check(c,T_SEMI) && !check(c,T_EOF)) advance_token(c);
                        break;
                    }
                } while (!check(c,T_EOF));
            }
            expect(c,T_RPAREN,"')'");
            if (fn && fn->is_function && !malformed) {
                if ((!fn->variadic && argc!=fn->param_count) ||
                    (fn->variadic && argc<fn->param_count)) {
                    if (fn->variadic)
                        diagnostic(c,t.line,t.col,"SEMANTICO","funcao '%s' espera pelo menos %d argumento(s), recebeu %d",t.text,fn->param_count,argc);
                    else
                        diagnostic(c,t.line,t.col,"SEMANTICO","funcao '%s' espera %d argumento(s), recebeu %d",t.text,fn->param_count,argc);
                }
                int checked=argc<fn->param_count?argc:fn->param_count;
                for(int i=0;i<checked && i<MAX_PARAMS;i++) if(!type_assignable(fn->params[i],arg_types[i]))
                    diagnostic(c,t.line,t.col,"SEMANTICO","argumento %d de '%s' tem tipo %s; esperado %s",i+1,t.text,type_name(arg_types[i]),type_name(fn->params[i]));
                e.type=fn->type;
            } else if (fn && fn->is_function) e.type=fn->type;
            else e.type=make_type(TY_ERROR);
            format_text(e.repr,sizeof(e.repr),"%s()",t.text);
            return e;
        }
        Symbol *s=find_symbol(c,t.text);
        if (!s) {
            diagnostic(c,t.line,t.col,"SEMANTICO","identificador '%s' nao declarado",t.text);
            e.type=make_type(TY_ERROR);
        } else if (s->is_function) {
            diagnostic(c,t.line,t.col,"SEMANTICO","funcao '%s' usada sem chamada",t.text);
            e.type=s->type;
        } else {
            e.type=s->type; e.symbol=s; e.is_lvalue=1;
            if (!check(c, T_ASSIGN)) {
                s->reads++;
                s->value_read_since_assignment=1;
            }
            if (s->is_const && s->has_const) {
                e.is_constant=1; e.value=s->const_value;
                int known=0;
                for(int i=0;i<c->propagated_count;i++) if(strcmp(c->propagated[i],s->name)==0) known=1;
                if(!known && c->propagated_count<MAX_PARAMS*8) {
                    char after[MAX_TEXT], before[MAX_TEXT];
                    snprintf(before,sizeof(before),"uso de %s",s->name);
                    if((int)s->const_value==s->const_value) snprintf(after,sizeof(after),"substituir %s por %d",s->name,(int)s->const_value);
                    else snprintf(after,sizeof(after),"substituir %s por %.6g",s->name,s->const_value);
                    optimization(c,t.line,"propagacao de constantes",before,after);
                    snprintf(c->propagated[c->propagated_count++],MAX_NAME,"%s",s->name);
                }
            }
        }
        return e;
    }
    if (match(c,T_LPAREN)) {
        Expr e=parse_expression(c,1);
        if (!expect(c,T_RPAREN,"')'")) e.syntax_bad=1;
        char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"(%s)",e.repr); copy_text(e.repr,sizeof(e.repr),tmp);
        return e;
    }
    if (match(c,K_SIZEOF)) {
        Expr inner;
        if (match(c,T_LPAREN)) { inner=parse_expression(c,1); expect(c,T_RPAREN,"')'"); }
        else inner=parse_primary(c);
        (void)inner;
        Expr e; memset(&e,0,sizeof(e)); e.type=make_type(TY_INT); e.is_constant=1; e.value=4; snprintf(e.repr,sizeof(e.repr),"sizeof"); return e;
    }
    diagnostic(c,t.line,t.col,"SINTATICO","expressao esperada antes de '%s'",t.text);
    if (!check(c,T_EOF) && !check(c,T_SEMI) && !check(c,T_COMMA) && !check(c,T_RPAREN) && !check(c,T_RBRACE)) advance_token(c);
    return bad_expr();
}
static Expr parse_unary(Compiler *c) {
    Token t=*peek(c);
    if (match(c,T_PLUS)||match(c,T_MINUS)||match(c,T_NOT)||match(c,T_TILDE)||match(c,T_BITAND)||match(c,T_STAR)||match(c,T_INC)||match(c,T_DEC)) {
        Expr e=parse_unary(c);
        if ((t.kind==T_INC||t.kind==T_DEC) && !e.is_lvalue)
            diagnostic(c,t.line,t.col,"SEMANTICO","operador '%s' exige uma variavel modificavel",t.text);
        if ((t.kind==T_INC||t.kind==T_DEC) && e.symbol && e.symbol->is_const)
            diagnostic(c,t.line,t.col,"SEMANTICO","nao e permitido alterar a constante '%s'",e.symbol->name);
        if ((t.kind==T_PLUS||t.kind==T_MINUS||t.kind==T_NOT) && !expr_is_numeric(e.type) && e.type.kind!=TY_ERROR)
            diagnostic(c,t.line,t.col,"SEMANTICO","operador '%s' exige operando numerico",t.text);
        if (t.kind==T_TILDE && e.type.kind!=TY_INT && e.type.kind!=TY_CHAR && e.type.kind!=TY_ERROR)
            diagnostic(c,t.line,t.col,"SEMANTICO","operador '~' exige operando inteiro");
        if (t.kind==T_MINUS && e.is_constant) e.value=-e.value;
        if (t.kind==T_NOT && e.is_constant) e.value=(e.value==0.0)?1.0:0.0;
        if (t.kind==T_TILDE && e.is_constant && e.value>(double)INT_MIN && e.value<(double)INT_MAX && (double)(int)e.value==e.value)
            e.value=~(int)e.value;
        if ((t.kind==T_INC||t.kind==T_DEC) && e.symbol) {
            e.symbol->initialized=1;
            e.symbol->has_const=0;
            e.symbol->value_read_since_assignment=0;
            c->seen_expr_count=0;
        }
        if (t.kind==T_NOT || t.kind==T_TILDE || ((t.kind==T_PLUS||t.kind==T_MINUS) && e.type.kind==TY_CHAR))
            e.type=make_type(TY_INT);
        char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"%s%s",t.text,e.repr); copy_text(e.repr,sizeof(e.repr),tmp);
        return e;
    }
    Expr e=parse_primary(c);
    for (;;) {
        if (match(c,T_LBRACKET)) {
            Expr idx=parse_expression(c,1);
            if (!expr_is_numeric(idx.type) && idx.type.kind!=TY_ERROR)
                diagnostic(c,previous(c)->line,previous(c)->col,"SEMANTICO","indice de vetor deve ser numerico");
            expect(c,T_RBRACKET,"']'");
            if (!e.type.is_array && e.type.kind!=TY_ERROR)
                diagnostic(c,t.line,t.col,"SEMANTICO","indice aplicado a objeto que nao e vetor");
            e.type.is_array=0; e.is_lvalue=1;
            char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"%s[%s]",e.repr,idx.repr); copy_text(e.repr,sizeof(e.repr),tmp);
            continue;
        }
        if (match(c,T_INC)||match(c,T_DEC)) {
            Token op=*previous(c);
            if (!e.is_lvalue) diagnostic(c,op.line,op.col,"SEMANTICO","operador '%s' exige uma variavel modificavel",op.text);
            if (e.symbol && e.symbol->is_const)
                diagnostic(c,op.line,op.col,"SEMANTICO","nao e permitido alterar a constante '%s'",e.symbol->name);
            if (e.symbol) {
                e.symbol->initialized=1;
                e.symbol->has_const=0;
                e.symbol->value_read_since_assignment=0;
            }
            c->seen_expr_count=0;
            char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"%s%s",e.repr,op.text); copy_text(e.repr,sizeof(e.repr),tmp);
            continue;
        }
        break;
    }
    return e;
}
static Expr parse_expression(Compiler *c, int min_prec) {
    Expr left=parse_unary(c);
    while (expr_precedence(peek(c)->kind)>=min_prec) {
        Token op=advance_token(c); int prec=expr_precedence(op.kind); int right_assoc=prec==1;
        Expr right=parse_expression(c,prec+(right_assoc?0:1));
        if (prec==1) {
            if (!left.is_lvalue && left.type.kind!=TY_ERROR)
                diagnostic(c,op.line,op.col,"SEMANTICO","lado esquerdo de atribuicao nao e modificavel");
            if (op.kind!=T_ASSIGN &&
                ((!expr_is_numeric(left.type) && left.type.kind!=TY_ERROR) ||
                 (!expr_is_numeric(right.type) && right.type.kind!=TY_ERROR)))
                diagnostic(c,op.line,op.col,"SEMANTICO","atribuicao composta '%s' exige operandos numericos",op.text);
            if ((op.kind==T_MOD_ASSIGN || op.kind==T_AND_ASSIGN || op.kind==T_OR_ASSIGN ||
                 op.kind==T_XOR_ASSIGN || op.kind==T_SHL_ASSIGN || op.kind==T_SHR_ASSIGN) &&
                (left.type.kind==TY_FLOAT || right.type.kind==TY_FLOAT))
                diagnostic(c,op.line,op.col,"SEMANTICO","atribuicao composta '%s' exige operandos inteiros",op.text);
            if (left.symbol && left.symbol->is_const)
                diagnostic(c,op.line,op.col,"SEMANTICO","nao e permitido alterar a constante '%s'",left.symbol->name);
            if (!type_assignable(left.type,right.type) && left.type.kind!=TY_ERROR && right.type.kind!=TY_ERROR)
                diagnostic(c,op.line,op.col,"SEMANTICO","atribuicao incompativel: %s recebe %s",type_name(left.type),type_name(right.type));
            c->seen_expr_count=0;
            if (left.symbol) {
                left.symbol->initialized=1;
                left.symbol->has_const=0;
                if (strcmp(left.repr, left.symbol->name) == 0) {
                    record_assignment(c, left.symbol, op, right.repr,
                                      op.kind == T_ASSIGN);
                }
            }
            left.is_constant=0; left.type=left.type.kind==TY_ERROR?right.type:left.type;
        } else {
            char left_symbol[MAX_NAME]="", right_symbol[MAX_NAME]="";
            if(left.symbol) snprintf(left_symbol,sizeof(left_symbol),"%s",left.symbol->name);
            if(right.symbol) snprintf(right_symbol,sizeof(right_symbol),"%s",right.symbol->name);
            Type operand_left_type=left.type, operand_right_type=right.type;
            char operand_left_repr[MAX_TEXT], operand_right_repr[MAX_TEXT];
            copy_text(operand_left_repr, sizeof(operand_left_repr), left.repr);
            copy_text(operand_right_repr, sizeof(operand_right_repr), right.repr);
            int is_arithmetic = op.kind==T_PLUS || op.kind==T_MINUS || op.kind==T_STAR ||
                                op.kind==T_SLASH || op.kind==T_PERCENT;
            int is_bitwise = op.kind==T_BITAND || op.kind==T_BITOR || op.kind==T_XOR ||
                             op.kind==T_SHL || op.kind==T_SHR;
            if (is_arithmetic &&
                ((!expr_is_numeric(operand_left_type) && operand_left_type.kind!=TY_ERROR) ||
                 (!expr_is_numeric(operand_right_type) && operand_right_type.kind!=TY_ERROR)))
                diagnostic(c,op.line,op.col,"SEMANTICO","operador '%s' exige operandos numericos",op.text);
            if ((op.kind==T_PERCENT || is_bitwise) &&
                (operand_left_type.kind==TY_FLOAT || operand_right_type.kind==TY_FLOAT))
                diagnostic(c,op.line,op.col,"SEMANTICO","operador '%s' exige operandos inteiros",op.text);
            if ((op.kind==T_SLASH || op.kind==T_PERCENT) && right.is_constant && right.value==0.0 &&
                (op.kind==T_PERCENT || (operand_left_type.kind!=TY_FLOAT && operand_right_type.kind!=TY_FLOAT)))
                diagnostic(c,op.line,op.col,"SEMANTICO","divisao por zero em expressao inteira");
            if (op.kind==T_EQ||op.kind==T_NE||op.kind==T_LT||op.kind==T_LE||op.kind==T_GT||op.kind==T_GE||op.kind==T_AND||op.kind==T_OR || is_bitwise)
                left.type=make_type(TY_INT);
            else if (operand_left_type.kind==TY_FLOAT||operand_right_type.kind==TY_FLOAT)
                left.type=make_type(TY_FLOAT);
            else if (is_arithmetic)
                left.type=make_type(TY_INT);
            else if (left.type.kind==TY_ERROR) left.type=right.type;
            left.is_lvalue=0; left.symbol=NULL;
            if (left.is_constant && right.is_constant) {
                double a=left.value,b=right.value,r=0; int valid=1;
                switch(op.kind) {
                    case T_PLUS:r=a+b;break; case T_MINUS:r=a-b;break; case T_STAR:r=a*b;break;
                    case T_SLASH:
                        if(b==0) valid=0;
                        else if (operand_left_type.kind!=TY_FLOAT && operand_right_type.kind!=TY_FLOAT &&
                                 a>(double)INT_MIN && a<(double)INT_MAX &&
                                 b>(double)INT_MIN && b<(double)INT_MAX)
                            r=(int)a/(int)b;
                        else r=a/b;
                        break;
                    case T_PERCENT:
                        if(b==0 || operand_left_type.kind==TY_FLOAT || operand_right_type.kind==TY_FLOAT ||
                           a<=(double)INT_MIN || a>=(double)INT_MAX ||
                           b<=(double)INT_MIN || b>=(double)INT_MAX ||
                           (double)(int)a!=a || (double)(int)b!=b) valid=0;
                        else r=(int)a%(int)b;
                        break;
                    case T_EQ:r=a==b;break; case T_NE:r=a!=b;break; case T_LT:r=a<b;break;
                    case T_LE:r=a<=b;break; case T_GT:r=a>b;break; case T_GE:r=a>=b;break;
                    case T_AND:r=(a!=0)&&(b!=0);break; case T_OR:r=(a!=0)||(b!=0);break;
                    case T_BITAND: case T_BITOR: case T_XOR:
                        if (a<=(double)INT_MIN || a>=(double)INT_MAX ||
                            b<=(double)INT_MIN || b>=(double)INT_MAX ||
                            (double)(int)a!=a || (double)(int)b!=b) valid=0;
                        else if (op.kind==T_BITAND) r=(int)a & (int)b;
                        else if (op.kind==T_BITOR) r=(int)a | (int)b;
                        else r=(int)a ^ (int)b;
                        break;
                    case T_SHL:
                        if (a<0 || a>=(double)INT_MAX || b<0 || b>30 ||
                            (double)(int)a!=a || (double)(int)b!=b ||
                            (int)a > (INT_MAX >> (int)b)) valid=0;
                        else r=(int)((unsigned int)(int)a << (unsigned int)(int)b);
                        break;
                    case T_SHR:
                        if (a<=(double)INT_MIN || a>=(double)INT_MAX || b<0 || b>30 ||
                            (double)(int)a!=a || (double)(int)b!=b) valid=0;
                        else r=(int)a >> (int)b;
                        break;
                    default:valid=0;break;
                }
                if(valid && (is_arithmetic || is_bitwise)) {
                    char before[MAX_TEXT],after[MAX_TEXT];
                    format_text(before,sizeof(before),"%s %s %s",operand_left_repr,operator_text(op.kind),operand_right_repr);
                    if(r>(double)INT_MIN && r<(double)INT_MAX && (double)(int)r==r)
                        snprintf(after,sizeof(after),"%d",(int)r);
                    else snprintf(after,sizeof(after),"%.6g",r);
                    optimization(c,op.line,"constant folding",before,after);
                    left.value=r;
                } else left.value=r;
                left.is_constant=valid;
            } else left.is_constant=0;
            if (!(left.is_constant && right.is_constant)) {
                const char *replacement=NULL;
                if (op.kind==T_PLUS && right.is_constant && right.value==0.0) replacement=left.repr;
                else if (op.kind==T_PLUS && left.is_constant && left.value==0.0) replacement=right.repr;
                else if (op.kind==T_MINUS && right.is_constant && right.value==0.0) replacement=left.repr;
                else if (op.kind==T_STAR && right.is_constant && right.value==1.0) replacement=left.repr;
                else if (op.kind==T_STAR && left.is_constant && left.value==1.0) replacement=right.repr;
                else if (op.kind==T_SLASH && right.is_constant && right.value==1.0) replacement=left.repr;
                if (replacement) {
                    char before[MAX_TEXT];
                    format_text(before,sizeof(before),"%s %s %s",operand_left_repr,operator_text(op.kind),operand_right_repr);
                    optimization(c,op.line,"simplificacao algebrica",before,replacement);
                }
            }
            if(op.kind==T_STAR && c->loop_depth>0 && c->loop_induction[0]) {
                int induction=((strcmp(left_symbol,c->loop_induction)==0 && right.is_constant) ||
                              (strcmp(right_symbol,c->loop_induction)==0 && left.is_constant));
                if(induction) {
                    char before[MAX_TEXT],after[MAX_TEXT];
                    snprintf(before,sizeof(before),"multiplicacao de %s por constante dentro do laco",c->loop_induction);
                    snprintf(after,sizeof(after),"substituir por atualizacao incremental do produto");
                    optimization(c,op.line,"reducao de forca",before,after);
                }
            }
            char repr[MAX_TEXT];
            format_text(repr,sizeof(repr),"(%s %s %s)",left.repr,operator_text(op.kind),right.repr);
            if (!left.is_constant && !right.is_constant && (op.kind==T_PLUS||op.kind==T_MINUS||op.kind==T_STAR||op.kind==T_SLASH))
                remember_expression(c,op.line,repr);
            snprintf(left.repr,sizeof(left.repr),"%s",repr);
        }
    }
    return left;
}
static int parse_type(Compiler *c, Type *out, int *is_const) {
    *is_const=0;
    if (match(c,K_CONST)) *is_const=1;
    Token t=*peek(c);
    switch(t.kind) {
        case K_INT: advance_token(c); *out=make_type(TY_INT); return 1;
        case K_FLOAT: case K_DOUBLE: advance_token(c); *out=make_type(TY_FLOAT); return 1;
        case K_CHAR: advance_token(c); *out=make_type(TY_CHAR); return 1;
        case K_VOID: advance_token(c); *out=make_type(TY_VOID); return 1;
        case K_STRUCT:
            advance_token(c);
            if (!check(c,T_ID)) { diagnostic(c,peek(c)->line,peek(c)->col,"SINTATICO","esperado nome apos struct"); *out=make_type(TY_ERROR); return 0; }
            *out=make_type(TY_STRUCT);
            Token tag_token=advance_token(c); copy_text(out->tag,sizeof(out->tag),tag_token.text);
            return 1;
        default: return 0;
    }
}
static void parse_declaration(Compiler *c, int consume_semi); static void parse_statement(Compiler *c);
static void parse_compound(Compiler *c, int new_scope);
static void check_initializer(Compiler *c, Symbol *s, Expr e, int line) {
    if (!s || e.syntax_bad || e.type.kind==TY_ERROR) return;
    s->initialized=1;
    Type target=s->type;
    target.is_array=0;
    if (!type_assignable(target,e.type) &&
        !(s->type.kind==TY_CHAR && s->type.is_array && e.type.kind==TY_STRING) &&
        e.type.kind!=TY_ERROR)
        diagnostic(c,line,1,"SEMANTICO","variavel '%s' do tipo %s recebe valor do tipo %s",s->name,type_name(s->type),type_name(e.type));
    if (s->is_const && e.is_constant) { s->has_const=1; s->const_value=e.value; }
    if (strcmp(e.repr, "<expressao invalida>") != 0) {
        Token assignment_token = make_token(T_ASSIGN, "=", 1, line, 1);
        record_assignment(c, s, assignment_token, e.repr, 0);
    }
}
static void parse_initializer(Compiler *c, Symbol *s, int line) {
    if (match(c,T_LBRACE)) {
        while (!check(c,T_RBRACE) && !check(c,T_EOF)) {
            if (check(c,T_COMMA)) {
                Token *p=peek(c);
                diagnostic(c,p->line,p->col,"SINTATICO","elemento ausente na inicializacao do vetor");
                advance_token(c);
                continue;
            }
            Expr e=parse_expression(c,2);
            if (s) {
                Type element_type=s->type;
                element_type.is_array=0;
                if (!type_assignable(element_type,e.type) && e.type.kind!=TY_ERROR)
                    diagnostic(c,line,1,"SEMANTICO","elemento de inicializacao incompativel com '%s'",s->name);
            }
            if (!match(c,T_COMMA)) break;
        }
        expect(c,T_RBRACE,"'}'");
        if(s) s->initialized=1;
    } else {
        Expr e=parse_expression(c,2);
        check_initializer(c,s,e,line);
    }
}
static void parse_declaration(Compiler *c, int consume_semi) {
    int is_const=0; Type base;
    if(!parse_type(c,&base,&is_const)) {
        Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","tipo de dado esperado");
        if(!check(c,T_EOF)) advance_token(c);
        return;
    }
    for(;;) {
        Token name=*peek(c);
        if(!match(c,T_ID)) {
            diagnostic(c,name.line,name.col,"SINTATICO","identificador esperado na declaracao");
            while(!check(c,T_SEMI)&&!check(c,T_RPAREN)&&!check(c,T_EOF)&&!check(c,T_RBRACE)) advance_token(c);
            break;
        }
        Type var_type=base;
        if(base.kind==TY_VOID)
            diagnostic(c,name.line,name.col,"SEMANTICO","variavel '%s' nao pode ter tipo void",name.text);
        if(match(c,T_LBRACKET)) {
            var_type.is_array=1;
            if(!check(c,T_RBRACKET)) parse_expression(c,1);
            expect(c,T_RBRACKET,"']'");
        }
        Symbol *old=NULL;
        for(Symbol *q=c->symbols;q && q->depth==c->scope_depth;q=q->next) if(strcmp(q->name,name.text)==0) old=q;
        if(old) diagnostic(c,name.line,name.col,"SEMANTICO","identificador '%s' ja declarado neste escopo",name.text);
        Symbol temporary; Symbol *s;
        if (old) {
            memset(&temporary, 0, sizeof(temporary));
            copy_text(temporary.name, sizeof(temporary.name), name.text);
            temporary.type = var_type;
            temporary.is_const = is_const;
            s = &temporary;
        } else {
            s = add_symbol(c,name.text,var_type,name.line,0);
            s->is_const=is_const;
        }
        if(match(c,T_ASSIGN)) parse_initializer(c,s,name.line);
        if(!match(c,T_COMMA)) break;
    }
    if(consume_semi) accept_semicolon(c);
}
static void parse_parameter_list(Compiler *c, Type *types, char names[][MAX_NAME], int *count, int *bad) {
    *count=0; *bad=0;
    if(check(c,K_VOID) && c->pos+1<c->tokens.count && c->tokens.data[c->pos+1].kind==T_RPAREN) { advance_token(c); return; }
    if(check(c,T_RPAREN)) return;
    for(;;) {
        int is_const=0; Type t;
        if(!parse_type(c,&t,&is_const)) {
            Token *p=peek(c);
            diagnostic(c,p->line,p->col,"SINTATICO","tipo esperado na lista de parametros");
            *bad=1;
            if(match(c,T_COMMA)) continue;
            while(!check(c,T_RPAREN)&&!check(c,T_EOF)&&!check(c,T_COMMA)) advance_token(c);
            if(match(c,T_COMMA)) continue;
            break;
        }
        Token n=*peek(c);
        if (check(c,T_ID)) {
            advance_token(c);
            if (*count<MAX_PARAMS) {
                types[*count]=t;
                copy_text(names[*count],MAX_NAME,n.text);
                (*count)++;
            }
        } else if (check(c,T_COMMA) || check(c,T_RPAREN)) {
            if (*count<MAX_PARAMS) {
                types[*count]=t;
                names[*count][0]='\0';
                (*count)++;
            }
        } else {
            diagnostic(c,n.line,n.col,"SINTATICO","nome do parametro esperado");
            *bad=1;
        }
        if(!match(c,T_COMMA)) break;
        if(check(c,T_COMMA)) { Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","parametro ausente entre virgulas"); *bad=1; advance_token(c); }
    }
}
static void parse_function(Compiler *c, Type ret, Token name, int is_const) {
    (void)is_const;
    Type params[MAX_PARAMS]; char names[MAX_PARAMS][MAX_NAME]; int count=0,bad=0;
    parse_parameter_list(c,params,names,&count,&bad);
    expect(c,T_RPAREN,"')'");
    Symbol *fn=find_global(c,name.text); int name_conflict=0; int signature_ok=1;
    if(fn && !fn->is_function) {
        diagnostic(c,name.line,name.col,"SEMANTICO","'%s' ja foi declarado como variavel",name.text);
        fn=NULL;
        name_conflict=1;
    }
    if(!fn && !name_conflict) {
        fn=add_symbol(c,name.text,ret,name.line,1);
        fn->param_count=count;
    } else if(fn) {
        int signature_conflict = fn->param_count != count || !type_equal(fn->type,ret);
        for (int i=0; !signature_conflict && i<count && i<MAX_PARAMS; ++i)
            if (!type_equal(fn->params[i], params[i])) signature_conflict = 1;
        if (signature_conflict) {
            diagnostic(c,name.line,name.col,"SEMANTICO","declaracao conflitante da funcao '%s'",name.text);
            signature_ok=0;
        }
    }
    if(fn && signature_ok) {
        fn->param_count=count;
        for(int i=0;i<count && i<MAX_PARAMS;i++) fn->params[i]=params[i];
    }
    if(match(c,T_SEMI)) return;
    for (int i=0;i<count;i++) {
        if (!names[i][0])
            diagnostic(c,name.line,name.col,"SINTATICO","parametro %d da definicao de '%s' precisa de nome",i+1,name.text);
    }
    if(!check(c,T_LBRACE)) {
        Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","esperado ';' ou corpo da funcao");
        while(!check(c,T_LBRACE)&&!check(c,T_SEMI)&&!check(c,T_EOF)) advance_token(c);
        if(match(c,T_SEMI)) return;
    }
    if(fn && fn->defined) diagnostic(c,name.line,name.col,"SEMANTICO","funcao '%s' definida mais de uma vez",name.text);
    if(fn) fn->defined=1;
    Symbol *old_func=c->current_function; Type old_ret=c->return_type;
    c->current_function=fn; c->return_type=ret;
    c->seen_expr_count=0; c->propagated_count=0; c->loop_induction[0]='\0'; c->loop_depth=0;
    scope_enter(c);
    for(int i=0;i<count;i++) {
        if(names[i][0]) {
            Symbol *p=find_symbol(c,names[i]);
            if(p && p->depth==c->scope_depth) diagnostic(c,name.line,name.col,"SEMANTICO","parametro '%s' duplicado",names[i]);
            else add_symbol(c,names[i],params[i],name.line,0);
        }
    }
    parse_compound(c,1);
    scope_leave(c,0);
    c->current_function=old_func; c->return_type=old_ret;
}
static void parse_struct_definition(Compiler *c) {
    Token kw=advance_token(c); Token tag=*peek(c);
    if(!match(c,T_ID)) { diagnostic(c,tag.line,tag.col,"SINTATICO","nome da estrutura esperado"); return; }
    if(match(c,T_LBRACE)) {
        while(!check(c,T_RBRACE)&&!check(c,T_EOF)) {
            int is_const=0; Type field;
            if(!parse_type(c,&field,&is_const)) { Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","tipo esperado no campo da estrutura"); advance_token(c); continue; }
            if(!match(c,T_ID)) { Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","nome do campo esperado"); synchronize(c); continue; }
            while(match(c,T_COMMA)) { if(!match(c,T_ID)) { diagnostic(c,peek(c)->line,peek(c)->col,"SINTATICO","nome do campo esperado"); break; } }
            accept_semicolon(c);
        }
        expect(c,T_RBRACE,"'}'");
        accept_semicolon(c);
    } else {
        c->pos--;
        (void)kw;
    }
}
static void parse_external(Compiler *c) {
    if(check(c,T_INVALID)) { advance_token(c); return; }
    if(check(c,K_STRUCT) && c->pos+2<c->tokens.count && c->tokens.data[c->pos+2].kind==T_LBRACE) {
        parse_struct_definition(c); return;
    }
    size_t declaration_start=c->pos; int is_const=0; Type base;
    if(!parse_type(c,&base,&is_const)) {
        Token t=advance_token(c);
        diagnostic(c,t.line,t.col,"SINTATICO","declaracao externa inesperada: '%s'",t.text);
        synchronize(c); return;
    }
    Token name=*peek(c);
    if(!match(c,T_ID)) {
        diagnostic(c,name.line,name.col,"SINTATICO","identificador esperado depois do tipo");
        synchronize(c); return;
    }
    if(match(c,T_LPAREN)) { parse_function(c,base,name,is_const); return; }
    c->pos=declaration_start;
    parse_declaration(c,1);
}
static void parse_compound(Compiler *c, int new_scope) {
    if(!expect(c,T_LBRACE,"'{'")) return;
    if(new_scope) scope_enter(c);
    while(!check(c,T_RBRACE)&&!check(c,T_EOF)) {
        size_t before=c->pos;
        parse_statement(c);
        if(c->pos==before && !check(c,T_EOF)) advance_token(c);
    }
    expect(c,T_RBRACE,"'}'");
    if(new_scope) scope_leave(c,1);
}
static void parse_if(Compiler *c) {
    advance_token(c);
    expect(c,T_LPAREN,"'('");
    Expr cond=parse_expression(c,1);
    if(cond.type.kind!=TY_ERROR && !expr_is_numeric(cond.type)) diagnostic(c,previous(c)->line,previous(c)->col,"SEMANTICO","condicao do if deve ser escalar");
    expect(c,T_RPAREN,"')'");
    c->seen_expr_count=0;
    parse_statement(c);
    c->seen_expr_count=0;
    if(match(c,K_ELSE)) {
        parse_statement(c);
        c->seen_expr_count=0;
    }
}
static void parse_while(Compiler *c) {
    advance_token(c);
    expect(c,T_LPAREN,"'('");
    Expr cond=parse_expression(c,1);
    if (cond.type.kind!=TY_ERROR && !expr_is_numeric(cond.type))
        diagnostic(c,previous(c)->line,previous(c)->col,"SEMANTICO","condicao do while deve ser escalar");
    expect(c,T_RPAREN,"')'");
    c->loop_depth++;
    c->seen_expr_count=0;
    parse_statement(c);
    c->loop_depth--;
    c->seen_expr_count=0;
}
static void parse_for(Compiler *c) {
    advance_token(c); expect(c,T_LPAREN,"'('");
    scope_enter(c);
    char previous_induction[MAX_NAME];
    snprintf(previous_induction,sizeof(previous_induction),"%s",c->loop_induction);
    if(is_type_start(peek(c)->kind)) {
        parse_declaration(c,0);
        if(c->symbols && c->symbols->depth==c->scope_depth && !c->symbols->is_function)
            snprintf(c->loop_induction,sizeof(c->loop_induction),"%s",c->symbols->name);
    } else if(!check(c,T_SEMI)) parse_expression(c,1);
    expect(c,T_SEMI,"';' no cabecalho do for");
    if(!check(c,T_SEMI)) {
        Expr condition=parse_expression(c,1);
        if (condition.type.kind!=TY_ERROR && !expr_is_numeric(condition.type))
            diagnostic(c,previous(c)->line,previous(c)->col,"SEMANTICO","condicao do for deve ser escalar");
    }
    expect(c,T_SEMI,"segundo ';' no cabecalho do for");
    if(!check(c,T_RPAREN)) parse_expression(c,1);
    expect(c,T_RPAREN,"')'");
    c->loop_depth++;
    c->seen_expr_count=0;
    parse_statement(c);
    c->loop_depth--;
    c->seen_expr_count=0;
    snprintf(c->loop_induction,sizeof(c->loop_induction),"%s",previous_induction);
    scope_leave(c,1);
}
static void parse_statement(Compiler *c) {
    if(check(c,T_INVALID)) { advance_token(c); return; }
    if(check(c,T_LBRACE)) { parse_compound(c,1); return; }
    if(is_type_start(peek(c)->kind)) { parse_declaration(c,1); return; }
    if(check(c,K_IF)) { parse_if(c); return; }
    if(check(c,K_WHILE)) { parse_while(c); return; }
    if(check(c,K_FOR)) { parse_for(c); return; }
    if(match(c,K_DO)) {
        c->loop_depth++;
        c->seen_expr_count=0;
        parse_statement(c);
        c->loop_depth--;
        c->seen_expr_count=0;
        if(match(c,K_WHILE)) {
            expect(c,T_LPAREN,"'('");
            Expr cond=parse_expression(c,1);
            if (cond.type.kind!=TY_ERROR && !expr_is_numeric(cond.type))
                diagnostic(c,previous(c)->line,previous(c)->col,"SEMANTICO","condicao do while deve ser escalar");
            expect(c,T_RPAREN,"')'");
            accept_semicolon(c);
        } else diagnostic(c,peek(c)->line,peek(c)->col,"SINTATICO","esperado while depois do bloco do");
        return;
    }
    if(match(c,K_RETURN)) {
        Token start=*previous(c);
        if(!c->current_function)
            diagnostic(c,start.line,start.col,"SEMANTICO","return usado fora de uma funcao");
        if(!check(c,T_SEMI)) {
            Expr e=parse_expression(c,1);
            if(c->current_function && c->return_type.kind==TY_VOID && e.type.kind!=TY_ERROR)
                diagnostic(c,start.line,start.col,"SEMANTICO","funcao void nao pode retornar um valor");
            else if(c->current_function && c->return_type.kind!=TY_VOID && !type_assignable(c->return_type,e.type) && e.type.kind!=TY_ERROR)
                diagnostic(c,start.line,start.col,"SEMANTICO","retorno do tipo %s incompativel com %s",type_name(e.type),type_name(c->return_type));
        } else if(c->current_function && c->return_type.kind!=TY_VOID)
            diagnostic(c,start.line,start.col,"SEMANTICO","funcao deve retornar um valor do tipo %s",type_name(c->return_type));
        accept_semicolon(c); return;
    }
    if(match(c,K_BREAK)||match(c,K_CONTINUE)) {
        Token t=*previous(c);
        if(!c->loop_depth) diagnostic(c,t.line,t.col,"SEMANTICO","'%s' usado fora de um laco",t.text);
        accept_semicolon(c); return;
    }
    if(match(c,T_SEMI)) return;
    if(check(c,K_ELSE)) {
        Token t=advance_token(c); diagnostic(c,t.line,t.col,"SINTATICO","'else' sem um 'if' correspondente");
        if(check(c,T_LBRACE)) parse_statement(c);
        return;
    }
    if(check(c,K_CASE)||check(c,K_DEFAULT)||check(c,K_SWITCH)) {
        Token t=advance_token(c); diagnostic(c,t.line,t.col,"SINTATICO","construcao '%s' nao suportada neste subconjunto",t.text); synchronize(c); return;
    }
    Expr e=parse_expression(c,1);
    (void)e;
    accept_semicolon(c);
}
static void add_builtin(Compiler *c, const char *name, Type ret, int variadic,
                        int param_count, const Type *params) {
    Symbol *s=add_symbol(c,name,ret,0,1);
    s->variadic=variadic;
    s->defined=1;
    s->param_count=param_count;
    for (int i=0;i<param_count && i<MAX_PARAMS;i++) s->params[i]=params[i];
}
static void parse_program(Compiler *c) {
    Type one_string[1], one_int[1];
    one_string[0]=make_type(TY_STRING);
    one_int[0]=make_type(TY_INT);
    add_builtin(c,"printf",make_type(TY_INT),1,1,one_string);
    add_builtin(c,"scanf",make_type(TY_INT),1,1,one_string);
    add_builtin(c,"puts",make_type(TY_INT),0,1,one_string);
    add_builtin(c,"putchar",make_type(TY_INT),0,1,one_int);
    add_builtin(c,"getchar",make_type(TY_INT),0,0,NULL);
    add_builtin(c,"strlen",make_type(TY_INT),0,1,one_string);
    {
        Symbol *null_symbol=add_symbol(c,"NULL",make_type(TY_INT),0,0);
        null_symbol->is_const=1; null_symbol->has_const=1; null_symbol->const_value=0;
        null_symbol->initialized=1;
        Symbol *eof_symbol=add_symbol(c,"EOF",make_type(TY_INT),0,0);
        eof_symbol->is_const=1; eof_symbol->has_const=1; eof_symbol->const_value=-1;
        eof_symbol->initialized=1;
    }
    while(!check(c,T_EOF)) {
        size_t before=c->pos;
        parse_external(c);
        if(c->pos==before && !check(c,T_EOF)) advance_token(c);
    }
}
static char *read_file(const char *path) {
    FILE *f=fopen(path,"rb");
    if(!f) return NULL;
    if(fseek(f,0,SEEK_END)!=0) { fclose(f); return NULL; }
    long n=ftell(f);
    if(n<0) { fclose(f); return NULL; }
    rewind(f);
    char *buf=(char *)malloc((size_t)n+1);
    if(!buf) { fclose(f); return NULL; }
    size_t got=fread(buf,1,(size_t)n,f); fclose(f); buf[got]='\0';
    if (got>=3 && (unsigned char)buf[0]==0xEF &&
        (unsigned char)buf[1]==0xBB && (unsigned char)buf[2]==0xBF) {
        memmove(buf,buf+3,got-2);
    }
    return buf;
}
static void print_source(const char *source) {
    int line=1; const char *p=source;
    while(*p) {
        const char *end=strchr(p,'\n'); size_t n=end?(size_t)(end-p):strlen(p);
        if (n && p[n-1]=='\r') --n;
        printf("%4d | %.*s\n",line++,(int)n,p);
        if(!end) break;
        p=end+1;
    }
}
static void print_diagnostic_excerpt(const char *source, int target_line, int target_col) {
    const char *start=source; int line=1;
    while (*start && line<target_line) {
        if (*start=='\n') ++line;
        ++start;
    }
    if (line!=target_line) return;
    const char *end=strchr(start,'\n'); size_t n=end?(size_t)(end-start):strlen(start);
    if (n && start[n-1]=='\r') --n;
    printf("       %4d | %.*s\n            | ",target_line,(int)n,start);
    for (int i=1;i<target_col && i<240;++i) putchar(' ');
    puts("^");
}
static void print_report(Compiler *c, const char *filename, const char *source) {
    printf("\n============================================================\nANALISE DO ARQUIVO: %s\n============================================================\n",filename);
    print_source(source);
    const char *phases[]={"LEXICO","SINTATICO","SEMANTICO"};
    const char *titles[]={"ANALISE LEXICA","ANALISE SINTATICA","ANALISE SEMANTICA"};
    for(int p=0;p<3;p++) {
        printf("\n[%s]\n",titles[p]); int found=0;
        for(int i=0;i<c->error_count;i++) if(strcmp(c->errors[i].phase,phases[p])==0) {
            printf("Linha %d, coluna %d | ERRO %s: %s\n",
                   c->errors[i].line,c->errors[i].col,c->errors[i].phase,c->errors[i].message);
            print_diagnostic_excerpt(source,c->errors[i].line,c->errors[i].col);
            found++;
        }
        if(!found) printf("Nenhum problema encontrado.\n");
    }
    printf("\n[OTIMIZACAO]\n");
    if(!c->opt_count) printf("Nenhuma oportunidade identificada.\n");
    for(int i=0;i<c->opt_count;i++) {
        Optimization *o=&c->opts[i];
        printf("Linha %d | %s\n  Antes: %s\n  Sugestao: %s\n",o->line,o->kind,o->before,o->after);
    }
    printf("\n============================================================\nRESUMO\n============================================================\n");
    printf("Erros lexicos:       %d\n",c->lex_count);
    printf("Erros sintaticos:    %d\n",c->syntax_count);
    printf("Erros semanticos:    %d\n",c->semantic_count);
    printf("Total de erros:      %d\n",c->lex_count+c->syntax_count+c->semantic_count);
    printf("Otimizacoes sugeridas: %d\n",c->opt_count);
    if(c->lex_count+c->syntax_count+c->semantic_count==0) printf("Estado: analise concluida sem erros detectados.\n");
    else printf("Estado: analise concluida com diagnosticos.\n");
}
static int path_is_absolute(const char *path) {
    if (!path || !path[0]) return 0;
    return path[0] == '/' || path[0] == '\\' ||
           (isalpha((unsigned char)path[0]) && path[1] == ':');
}
static const char *path_basename(const char *path) {
    const char *base = path;
    if (!path) return "";
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}
static void get_program_directory(char *out, size_t out_cap, const char *argv0) {
    char executable[4096];
    char *slash1, *slash2, *slash;
    if (!out_cap) return;
    copy_text(executable, sizeof(executable), argv0 && argv0[0] ? argv0 : ".");
    slash1 = strrchr(executable, '/');
    slash2 = strrchr(executable, '\\');
    slash = slash1;
    if (!slash || (slash2 && slash2 > slash)) slash = slash2;
    if (!slash) {
        copy_text(out, out_cap, ".");
        return;
    }
    if (slash == executable || (slash == executable + 2 && executable[1] == ':'))
        slash[1] = '\0';
    else
        *slash = '\0';
    copy_text(out, out_cap, executable);
}
static int make_path(char *out, size_t cap, const char *base, const char *relative) {
    int n;
    if (!base || !base[0] || strcmp(base, ".") == 0)
        n = snprintf(out, cap, "%s", relative);
    else
        n = snprintf(out, cap, "%s/%s", base, relative);
    return n >= 0 && (size_t)n < cap;
}
static char *try_candidate(const char *candidate, char *resolved, size_t resolved_cap) {
    char *content = read_file(candidate);
    if (content) copy_text(resolved, resolved_cap, candidate);
    return content;
}
static char *resolve_source_file(const char *filename, const char *program_dir,
                                 char *resolved, size_t resolved_cap) {
    char candidate[4096], root[4096], test_dir[4096];
    char *source;
    source = try_candidate(filename, resolved, resolved_cap);
    if (source) return source;
    if (path_is_absolute(filename)) return NULL;
    if (make_path(candidate, sizeof(candidate), "teste", filename)) {
        source = try_candidate(candidate, resolved, resolved_cap);
        if (source) return source;
    }
    copy_text(root, sizeof(root), program_dir && program_dir[0] ? program_dir : ".");
    for (int level = 0; level <= 4; ++level) {
        if (make_path(candidate, sizeof(candidate), root, filename)) {
            source = try_candidate(candidate, resolved, resolved_cap);
            if (source) return source;
        }
        if (make_path(test_dir, sizeof(test_dir), root, "teste")) {
            if (make_path(candidate, sizeof(candidate), test_dir, filename)) {
                source = try_candidate(candidate, resolved, resolved_cap);
                if (source) return source;
            }
            if (strcmp(path_basename(filename), filename) != 0 &&
                make_path(candidate, sizeof(candidate), test_dir, path_basename(filename))) {
                source = try_candidate(candidate, resolved, resolved_cap);
                if (source) return source;
            }
        }
        if (level < 4) {
            char parent[4096];
            if (!make_path(parent, sizeof(parent), root, "..")) break;
            copy_text(root, sizeof(root), parent);
        }
    }
    return NULL;
}
static int run_file(const char *filename, const char *program_dir) {
    char resolved[4096]; char *source = resolve_source_file(filename, program_dir, resolved, sizeof(resolved));
    if(!source) {
        fprintf(stderr,
            "Nao foi possivel abrir '%s'. Verifique o caminho informado, a pasta atual, "
            "a pasta do projeto e a subpasta teste/.\n", filename);
        return 2;
    }
    static Compiler c;
    memset(&c,0,sizeof(c));
    lex_source(&c,source);
    parse_program(&c);
    print_report(&c,resolved,source);
    for(Symbol *s=c.symbols;s;) { Symbol *n=s->next; free(s); s=n; }
    free(c.tokens.data); free(source);
    return c.lex_count+c.syntax_count+c.semantic_count ? 1 : 0;
}
static void wait_for_enter(void) {
    int ch;
    printf("\nPressione ENTER para sair...");
    fflush(stdout);
    while ((ch = getchar()) != '\n' && ch != EOF) { }
}
int main(int argc, char **argv) {
    char filename[1024], program_dir[4096]; const char *input_path=NULL; int pause_at_end=0; int rc; int i;
    get_program_directory(program_dir, sizeof(program_dir), argv[0]);
    for (i=1;i<argc;i++) {
        if (strcmp(argv[i],"--pause")==0) pause_at_end=1;
        else if (strcmp(argv[i],"--help")==0 || strcmp(argv[i],"-h")==0) {
            printf("Uso: %s [--pause] arquivo.c\n",argv[0]);
            printf("Sem arquivo, o programa solicita o caminho interativamente.\n");
            printf("Busca tambem na pasta do programa e na subpasta teste/.\n");
            return 0;
        } else if (!input_path) input_path=argv[i];
        else {
            fprintf(stderr,"Argumento inesperado: %s\n",argv[i]);
            return 2;
        }
    }
    if (!input_path) {
        printf("Digite o caminho do arquivo .c: ");
        fflush(stdout);
        if (!fgets(filename,sizeof(filename),stdin)) return 2;
        filename[strcspn(filename,"\r\n")]='\0';
        if (!filename[0]) {
            fprintf(stderr,"Nenhum arquivo foi informado.\n");
            return 2;
        }
        input_path=filename;
        pause_at_end=1;
    }
    rc=run_file(input_path, program_dir);
    if (pause_at_end) wait_for_enter();
    return rc;
}