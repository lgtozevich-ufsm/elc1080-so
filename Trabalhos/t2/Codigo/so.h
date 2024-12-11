// so.h
// sistema operacional
// simulador de computador
// so24b

#ifndef SO_H
#define SO_H

#include "memoria.h"
#include "mmu.h"
#include "cpu.h"
#include "es.h"
#include "console.h" // só para uma gambiarra
#include "fifo.h"

#define QTD_IRQ 6 // quantidade de interrupções

typedef struct so_t so_t;

typedef enum
{
    ESTADO_EXECUTANDO,
    ESTADO_PRONTO,
    ESTADO_BLOQUEADO,
    ESTADO_MORTO,
    ESTADO_N
} estado_processo_t;

typedef enum
{
    R_BLOQ_LEITURA,
    R_BLOQ_ESCRITA,
    R_BLOQ_ESPERA_PROC,
    R_BLOQ_ESPERA_DISCO,
    R_PROC_BLOQ,

} motivo_bloq_processo_t;

typedef struct metricas_estado_processo_t metricas_estado_processo_t;
typedef struct processo_metricas_t processo_metricas_t;
typedef struct processo_t processo_t;
typedef struct fifo_t fifo_t;

typedef struct
{
    int tempo_total_execucao;
    int tempo_total_ocioso;
    int num_interrupcoes[QTD_IRQ];
    int num_preempcoes;
} so_metricas_t;

struct metricas_estado_processo_t
{
    int qtd;
    int tempo_total;
};

struct processo_metricas_t
{
    int qtd_preempcoes;

    int tempo_retorno;
    int tempo_resposta;
    int qtd_page_fault;

    metricas_estado_processo_t estados[ESTADO_N];
};

struct processo_t
{
    int pid;
    int pc;
    int erro;
    int complemento;
    int modo;

    estado_processo_t estado;
    motivo_bloq_processo_t motivo_bloqueio;

    int reg[2];

    float prioridade;

    int dado_pendente;

    processo_metricas_t metricas;

    tabpag_t *tabpag;
    int sec_inicial;
    int sec_final;
    int hora_desbloqueio;
};

#define NENHUM_PROCESSO NULL
typedef struct no_fila_t
{
    processo_t *processo;
    struct no_fila_t *proximo;
} no_fila_t;

typedef struct
{
    no_fila_t *inicio;
    no_fila_t *fim;
} fila_t;

struct so_t
{
    cpu_t *cpu;
    mem_t *mem;
    mem_t *mem_secundaria;
    mmu_t *mmu;
    es_t *es;
    console_t *console;
    bool erro_interno;
    processo_t *processo_corrente;
    processo_t **processos;
    fila_t *fila_prontos;

    int quantum_proc;
    int pid_atual;

    so_metricas_t metricas;

    int n_procs;

    int r_agora;

    int quadro_livre;
    int quadro_livre_primaria;

    fifo_t *fifo;

    int hora_disco_livre;
};
so_t *so_cria(cpu_t *cpu, mem_t *mem, mem_t *mem_sec, mmu_t *mmu, es_t *es, console_t *console);
void so_destroi(so_t *self);

// Chamadas de sistema
// Uma chamada de sistema é realizada colocando a identificação da
//   chamada (um dos valores abaixo) no registrador A e executando a
//   instrução CHAMAS, que causa uma interrupção do tipo IRQ_SISTEMA.

// Chamadas para entrada e saída
// Cada processo tem um dispositivo (ou arquivo) corrente de entrada
//   e um de saída. As chamadas de sistema para leitura e escrita são
//   realizadas nesses dispositivos.
// Outras chamadas (não definidas) abrem e fecham arquivos, e definem
//   qual dos arquivos abertos é escolhido para ser o de entrada ou
//   saída correntes.

// lê um caractere do dispositivo de entrada do processo
// retorna em A: o caractere lido ou um código de erro negativo
#define SO_LE 1

// escreve um caractere no dispositivo de saída do processo
// recebe em X o caractere a escrever
// retorna em A: 0 se OK ou um código de erro negativo
#define SO_ESCR 2

// #define SO_ABRE        3
// #define SO_FECHA       4
// #define SO_SEL_LE      5
// #define SO_SEL_ESCR    6

// Chamadas para gerenciamento de processos
// O sistema cria um processo automaticamente na sua inicialização,
//   para executar um programa inicial. Esse processo deve criar
//   outros processos para executar outros programas, se for o caso.
// Existem chamadas de sistema para criar um processo, para matar um
//   processo e para um processo se matar.
// Cada processo é identificado por um número (pid). O processo criado
//   automaticamente tem o pid 1, o segundo processo criado tem o
//   pid 2 etc.

// cria um processo novo, para executar um determinado programa
// os caracteres que compõem o nome do arquivo que contém o programa
//   a ser executado pelo novo processo estão na memória do processo
//   que realiza esta chamada, a partir da posição em X até antes
//   da posição que contém um valor 0.
// retorna em A: pid do processo criado, ou código de erro negativo
#define SO_CRIA_PROC 7

// mata um processo
// recebe em X o pid do processo a matar ou 0 para o processo chamador
// retorna em A: 0 se OK ou um código de erro negativo
#define SO_MATA_PROC 8

// espera um processo terminar
// recebe em X o pid do processo a esperar
// retorna em A: 0 se OK ou um código de erro negativo
// bloqueia o processo chamador até que o processo com o pid informado termine
// retorna sem bloquear, com erro, se não existir processo com esse pid
#define SO_ESPERA_PROC 9

#endif // SO_H
