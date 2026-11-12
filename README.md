# Compilador Didático — Trabalho Final de Compiladores

Compilador didático escrito em **C (C99)**, em arquivo único (`main.c`, 1447
linhas), sem bibliotecas externas. Ele analisa um subconjunto de C e emite
diagnósticos em **três fases** (léxico, sintático e semântico) além de
**oportunidades de otimização**, continuando a análise até o fim do arquivo
mesmo depois de encontrar erros.

---

## 1. Estrutura do projeto

```
compilador-final-main/
├── main.c                        # fonte do compilador (tudo em um arquivo)
├── main.exe                      # executável gerado
├── Makefile                      # build via make (Linux/macOS)
├── compile_commands.json         # metadados de compilação (ferramentas)
├── README.md                     # este arquivo
│
├── 01-arquivo-analisado.txt      # descrição do arquivo de teste e do resultado
├── 02-evidencia-execucao.txt     # evidência da execução (comando + saída)
├── 03-descricao-etapas.txt       # como cada etapa foi implementada
├── evidencia_saida.txt           # saída bruta gerada pelo compilador
│
└── teste/
    ├── codigo_teste_compilador.c # arquivo analisado (erros + otimizações)
    ├── simples.c                 # código com erro sintático grave
    ├── teste.c                   # código com várias funções
    ├── target.c                  # código limpo (sem erros)
    └── b.txt                     # arquivo não-C (teste de entrada inválida)
```

---

## 2. Compilação

### Linux / macOS (com make)

```bash
make build      # apenas compila -> gera o executável "main"
make            # compila e executa (roda de forma interativa)
make clean      # remove main e main.o
```

### Linux / macOS (sem make)

```bash
gcc -std=c17 -Wall -Wextra -pedantic -g -o main main.c
```

### Windows (MinGW / TDM-GCC)

```bat
gcc -std=c99 -Wall -Wextra -pedantic -o main.exe main.c
```

> **Observação:** o `Makefile` usa `-std=c17`. Versões antigas do GCC
> (ex.: TDM-GCC 4.9.2) não reconhecem `-std=c17`; nesses casos utilize
> `-std=c99` ou `-std=c11`. O código não usa recursos exclusivos de C17.

O build foi verificado com **zero warnings** em `gcc -Wall -Wextra -pedantic`.

---

## 3. Execução

### Forma 1 — passando o arquivo como argumento (recomendada)

```bash
./main.exe teste/codigo_teste_compilador.c      # Windows
./main  teste/codigo_teste_compilador.c         # Linux/macOS
```

No Windows, o caminho também pode ser escrito com barra invertida:

```bat
main.exe teste\codigo_teste_compilador.c
```

### Forma 2 — modo interativo

```bash
./main.exe
```

O programa pergunta `Digite o caminho do arquivo .c:`, lê do teclado e,
ao final, exibe `Pressione ENTER para sair...`.

### Forma 3 — só o nome do arquivo (busca automática)

```bash
./main.exe codigo_teste_compilador.c
```

O compilador procura o arquivo no caminho informado, na pasta atual, na
subpasta `teste/` e em até 4 níveis acima da pasta do executável.

### Opções de linha de comando

| Opção       | Efeito                                              |
|-------------|-----------------------------------------------------|
| `arquivo.c` | arquivo a analisar (pode vir como 1º argumento)     |
| `--pause`   | espera ENTER antes de sair                          |
| `--help`/`-h` | mostra o resumo de uso e sai (código 0)           |

### Códigos de retorno

| Código | Significado                                  |
|--------|----------------------------------------------|
| `0`    | análise concluída **sem** erros              |
| `1`    | análise concluída **com** diagnósticos       |
| `2`    | arquivo não encontrado / argumento inválido  |

---

## 4. Evidência da execução

Comando utilizado:

```bat
main.exe teste\codigo_teste_compilador.c > evidencia_saida.txt 2>&1
```

Resultado (resumo — saída integral em `02-evidencia-execucao.txt` e
`evidencia_saida.txt`):

```
Erros lexicos:       1
Erros sintaticos:    14
Erros semanticos:    2
Total de erros:      17
Otimizacoes sugeridas: 6
Estado: analise concluida com diagnosticos.
```

Principais diagnósticos detectados em `teste/codigo_teste_compilador.c`:

- **Léxico (1):** linha 21 — caractere não reconhecido `@`.
- **Sintáticos (14):** linha 18 — falta de `;`; linha 19 — ordem inválida
  `int = valor 20;`; linhas 15, 21 e 24 — recuperação de erro.
- **Semânticos (2):** linha 14 — redeclaração de `idade`;
  linha 16 — `int nome = "Maria"` (tipos incompatíveis).
- **Otimizações (6):** constant folding nas linhas 10, 11, 18 e 25;
  simplificação algébrica (`x + 0`) nas linhas 12 e 26.

O relatório ainda imprime o código-fonte numerado e marca a coluna de
cada erro com `^`.

---

## 5. Descrição das etapas (resumo)

A descrição detalhada, com números de linha do código-fonte, está em
**`03-descricao-etapas.txt`**.

1. **Léxico — `lex_source()` (`main.c:223`)**  
   Varredura manual da fonte: comentários (`//`, `/* */`), diretivas `#`,
   identificadores/palavras-chave, literais numéricos (decimal, hex,
   float, expoente, sufixos), strings/caracteres com validação de escape
   e operadores de 1 e 2 caracteres. Símbolos inválidos (como `@`) viram
   token `T_INVALID` e geram erro — a análise **não** para.

2. **Sintática — parser descendente recursivo (`main.c:413`)**  
   Primitivas `peek/match/expect`, recuperação de erros com
   `synchronize()` e `accept_semicolon()`, expressões por precedência
   (`parse_expression` com tabela de precedência), declarações, funções,
   `if/else`, `while`, `for`, `do`, `return`, `break/continue`, `struct`.
   `switch/case` é reportado como não suportado.

3. **Semântica — integrada ao parser (`main.c:442` em diante)**  
   Tabela de símbolos com escopos (lista ligada por profundidade):
   redeclaração no mesmo escopo, uso de variável não declarada,
   compatibilidade de tipos em inicialização/atribuição/argumentos,
   aridade de chamadas, operandos numéricos/inteiros, divisão por zero
   em tempo de compilação, `lvalue`, constantes imutáveis, tipos de
   `return`, `break/continue` fora de laço. O tipo `TY_ERROR` evita
   erros em cascata.

4. **Otimizações (`main.c:124`, `:523`, `:715`)**  
   Constant folding (com checagem de overflow/divisão por zero/shift),
   simplificação algébrica (`x+0`, `x*1`...), propagação de constantes,
   eliminação de subexpressão comum, eliminação de atribuição
   sobrescrita, código morto e redução de força em laços.

5. **Relatório e main (`main.c:1283`, `:1416`)**  
   Leitura do arquivo (com remoção de BOM), resolução de caminho,
   impressão do fonte numerado, seções por fase com marcador `^`,
   resumo com contadores e código de retorno 0/1/2.

---

## 6. Arquivos de entrega

| Arquivo                     | Conteúdo                                              |
|-----------------------------|-------------------------------------------------------|
| `README.md`                 | instruções de compilação e execução (este arquivo)    |
| `01-arquivo-analisado.txt`  | o arquivo `codigo_teste_compilador.c` e seu resultado |
| `02-evidencia-execucao.txt` | evidência da execução (comandos + saída completa)     |
| `03-descricao-etapas.txt`   | como cada etapa foi implementada                      |
| `evidencia_saida.txt`       | saída bruta do compilador para o arquivo de teste     |
