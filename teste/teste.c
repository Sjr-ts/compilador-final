
int soma(int a, int b);

/* ===== 1. ERROS LEXICOS: 3 ===== */

void teste_lexico(void) {
    @;       // L1
    \;       // L2
    `;       // L3
}

/* ===== 2. ERROS SINTATICOS: 10 ===== */

int sintatico_01(void) {
    int a = 10
    return a;
}

int sintatico_02(void) {
    int a = ;
    return 0;
}

int sintatico_03(void) {
    if (1 > 0 {
        return 1;
    }
    return 0;
}

int sintatico_04(void) {
    int total = 0;
    for (int i = 0; i < 3 i++) {
        total += i;
    }
    return total;
}

int sintatico_05(void) {
    int a = (2 + 3;
    return a;
}

int sintatico_06(int a, , int b) {
    return a + b;
}

int sintatico_07(void) {
    int a = 1
    int b = 2;
    return a + b;
}

int sintatico_08(void) {
    else {
        return 0;
    }
    return 1;
}

int sintatico_09(void) {
    int v[3] = {1, 2, 3};
    return v[1;
}

int sintatico_10(void) {
    return soma(1 2);
}

/* ===== 3. ERROS SEMANTICOS: 3 ===== */

int semantico_01(void) {
    return variavel_inexistente;
}

int semantico_02(void) {
    int numero = "texto";
    return numero;
}

int semantico_03(void) {
    return soma(10);
}

/* ===== 4. OTIMIZACAO: 5 OPORTUNIDADES ===== */

int teste_otimizacao(int x, int y) {

    // O1 - Constant Folding
    int constante = 10 * 2;

    // O2 - Constant Propagation
    const int fator = 4;

    // O3 - Common Subexpression Elimination
    int soma1 = x + y;
    int soma2 = x + y;

    // O4 - Dead Code Elimination
    int descartado = x * 99;

    // O5 - Strength Reduction
    int acumulador = 0;

    for (int i = 0; i < 10; i++) {
        acumulador += i * 4;
    }

    return constante + fator * x
           + soma1 + soma2 + acumulador;
}

/* ===== FUNCAO AUXILIAR ===== */

int soma(int a, int b) {
    return a + b;
}

/* ===== PROGRAMA PRINCIPAL ===== */

int main(void) {
    return teste_otimizacao(5, 10);
}
