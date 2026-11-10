/* ARQUIVO DE TESTE - TRABALHO FINAL DE COMPILADORES
   O compilador didatico dos alunos deve ler este arquivo,
   identificar os problemas e continuar a analise ate o final.
*/

int idade = 25;
float salario = 3500.50;
char letra = 'A';

int resultado = 10 + 20;
int dobro = 5 * 2;
int neutro = resultado + 0;

int idade = 30;          // ERRO SEMANTICO: redeclaracao
quantidade = 8;          // ERRO SEMANTICO: variavel nao declarada
int nome = "Maria";      // ERRO SEMANTICO: tipos incompativeis

int total = 10 + 5       // ERRO SINTATICO: falta ;
int = valor 20;          // ERRO SINTATICO: ordem invalida

int codigo = 10 @ 2;     // ERRO LEXICO: simbolo @ nao reconhecido

int x = 10;
x = 20;                  // OPORTUNIDADE: atribuicao anterior pode ser codigo morto
int y = 4 * 5;           // OPORTUNIDADE: constant folding -> 20
int z = y + 0;           // OPORTUNIDADE: simplificacao algebrica -> y
