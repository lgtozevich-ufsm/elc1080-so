// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
#include "so.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "tabpag.h"
#include "fifo.h"

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

// CONSTANTES E TIPOS {{{1
// intervalo entre interrupções do relógio
#define INTERVALO_INTERRUPCAO 50
#define QUANTUM 5
#define ESCALONADOR 2 // 0 para prioridade, 1 para round-robin, 2 para simples
#define TEMPO_DISCO 5

#define N_QUADROS 100

#define TROCA_FIFO 0
#define TROCA_SEGUNDA_CHANCE 1

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// no t2, foi adicionado o 'processo' aos argumentos dessas funções
// carrega o programa na memória virtual de um processo; retorna end. inicial
static int so_carrega_programa(so_t *self, processo_t *processo, char *nome_do_executavel);
// copia para str da memória do processo, até copiar um 0 (retorna true) ou tam bytes
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam], int end_virt, processo_t *processo);

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void escreve_memoria_fisica(so_t *self)
{
  FILE *file = fopen("memoria_fisica.txt", "w");
  if (!file)
  {
    perror("Erro ao abrir arquivo memoria_fisica.txt");
    return;
  }

  fprintf(file, "Memória Física:\n");
  for (int i = 0; i < N_QUADROS * TAM_PAGINA; i++)
  {
    int dado;
    if (mem_le(self->mem, i, &dado) == ERR_OK)
    {
      if (i % 10 == 0)
      {
        fprintf(file, "\n------------- QUADRO RAM i: %d --------------\n", i / 10);
      }
      fprintf(file, "Endereço %d: %d\n", i, dado);
    }
    else
    {
      fprintf(file, "Endereço %d: erro de leitura\n", i);
    }
  }

  fclose(file);
}

static void escreve_memoria_secundaria(so_t *self)
{
  FILE *file = fopen("memoria_secundaria.txt", "w");
  if (!file)
  {
    perror("Erro ao abrir arquivo memoria_secundaria.txt");
    return;
  }

  fprintf(file, "Memória Secundária:\n");
  for (int i = 0; i < N_QUADROS * TAM_PAGINA; i++)
  {
    int dado;
    if (mem_le(self->mem_secundaria, i, &dado) == ERR_OK)
    {
      if (i % 10 == 0)
      {
        fprintf(file, "\n------------- QUADRO DISCO i: %d --------------\n", i);
      }
      fprintf(file, "Endereço %d: %d\n", i, dado);
    }
    else
    {
      fprintf(file, "Endereço %d: erro de leitura\n", i);
    }
  }

  fclose(file);
}

// --- TEMPO ---
int tempo_atual(so_t *self)
{
  int hora_atual = 0;

  if (es_le(self->es, D_RELOGIO_INSTRUCOES, &hora_atual) != ERR_OK)
  {
    console_printf("SO: erro na leitura do relógio");
    self->erro_interno = true;
    return 0;
  }

  return hora_atual;
}

char *pega_nome_estado(estado_processo_t estado)
{
  switch (estado)
  {
  case ESTADO_PRONTO:
    return "PRONTO";
  case ESTADO_EXECUTANDO:
    return "EXECUTANDO";
  case ESTADO_BLOQUEADO:
    return "BLOQUEADO";
  case ESTADO_MORTO:
    return "MORTO";
  default:
    return "NÃO TRATADO";
  }
}

// funçoes para impressao e calculo das metricas
static void so_imprime_metricas(so_t *self)
{
  console_printf("MÉTRICAS DO SO (quantum: %d, intervalo: %d):\n ", QUANTUM, INTERVALO_INTERRUPCAO);
  console_printf("| %-26s | %-10s |\n", "MÉTRICA", "VALOR");
  console_printf("|---------------------------|------------|\n");
  console_printf("| NÚMERO DE PROCESSOS       | %-10d |\n", self->n_procs);
  console_printf("| TEMPO TOTAL DE EXECUÇÃO   | %-10d |\n", self->metricas.tempo_total_execucao);
  console_printf("| TEMPO TOTAL OCIOSO        | %-10d |\n", self->metricas.tempo_total_ocioso);
  console_printf("| NÚMERO DE PREEMPÇÕES      | %-10d |\n", self->metricas.num_preempcoes);

  console_printf("\nINTERRUPÇÕES:\n");
  console_printf("| %-5s | %-10s |\n", "IRQ", "VEZES");
  console_printf("|-------|------------|\n");
  for (int i = 0; i < QTD_IRQ; i++)
  {
    console_printf("| %-5d | %-10d |\n", i, self->metricas.num_interrupcoes[i]);
  }

  console_printf("\nMÉTRICAS DOS PROCESSOS (num quadros: %d):\n ", N_QUADROS);
  for (int i = 0; i < self->n_procs; i++)
  {
    processo_t *proc = self->processos[i];

    console_printf("PROCESSO %d\n ", proc->pid);
    console_printf("| %-23s | %-10s |\n", "MÉTRICA", "VALOR");
    console_printf("|------------------------|------------|\n");
    console_printf("| PREEMPÇÕES             | %-10d |\n", proc->metricas.qtd_preempcoes);
    console_printf("| TEMPO DE RESPOSTA      | %-10d |\n", proc->metricas.tempo_resposta);
    console_printf("| TEMPO DE RETORNO       | %-10d |\n", proc->metricas.tempo_retorno);
    console_printf("| PAGE FAULTS            | %-10d |\n", proc->metricas.qtd_page_fault);

    console_printf("\nMÉTRICAS POR ESTADO DO PROCESSO %d:\n\n ", proc->pid);
    console_printf("| %-10s | %-10s | %-12s |\n", "ESTADO", "VEZES", "TEMPO TOTAL");
    console_printf("|------------|------------|--------------|\n");

    for (int j = 0; j < ESTADO_N; j++)
    {
      console_printf("| %-10s | %-10d | %-12d |\n", pega_nome_estado(j), proc->metricas.estados[j].qtd, proc->metricas.estados[j].tempo_total);
    }

    console_printf("\n");
  }
}

static bool preemptiu(processo_t *self, estado_processo_t estado)
{
  return self->estado == ESTADO_EXECUTANDO && estado == ESTADO_PRONTO;
}

static void proc_muda_estado(processo_t *self, estado_processo_t estado)
{
  if (preemptiu(self, estado))
  {
    self->metricas.qtd_preempcoes++;
  }
  console_printf("Processo PID: %d, estado: %s -> %s\n", self->pid, pega_nome_estado(self->estado), pega_nome_estado(estado));

  self->metricas.estados[estado].qtd++;
  self->estado = estado;
}

static void processo_atualiza_metricas(processo_t *self, int dif_tempo)
{
  if (self->estado != ESTADO_MORTO)
  {
    self->metricas.tempo_retorno += dif_tempo;
  }
  self->metricas.estados[self->estado].tempo_total += dif_tempo;
  self->metricas.tempo_resposta = self->metricas.estados[ESTADO_PRONTO].tempo_total;
  self->metricas.tempo_resposta /= self->metricas.estados[ESTADO_PRONTO].qtd;

  console_printf("Processo PID: %d, Tempo: %d, Estado: %s\n", self->pid, self->metricas.estados[self->estado].tempo_total, pega_nome_estado(self->estado));
}

static void so_atualiza_metricas(so_t *self, int dif_tempo)
{
  self->metricas.tempo_total_execucao += dif_tempo;
  if (self->processo_corrente == NULL)
    self->metricas.tempo_total_ocioso += dif_tempo;
  for (int i = 0; i < self->n_procs; i++)
    processo_atualiza_metricas(self->processos[i], dif_tempo);
}

// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// função auxiliar para obter o terminal correspondente ao PID
static int so_obtem_terminal(int pid);

// CRIAÇÃO {{{1

static void inicializa_metricas(so_t *self)
{
  self->metricas.tempo_total_execucao = 0;
  self->metricas.tempo_total_ocioso = 0;
  self->metricas.num_preempcoes = 0;

  for (int i = 0; i < QTD_IRQ; i++)
  {
    self->metricas.num_interrupcoes[i] = 0;
  }
}

static void inicializa_fila_prontos(so_t *self)
{
  self->fila_prontos = malloc(sizeof(fila_t));
  if (self->fila_prontos == NULL)
  {
    console_printf("SO: erro ao alocar memória para a fila de processos prontos");
    free(self);
    return;
  }
  self->fila_prontos->inicio = NULL;
  self->fila_prontos->fim = NULL;
}

static void inicializa_cpu(so_t *self)
{
  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor,
  //   salva seu estado à partir do endereço 0, e desvia para o endereço
  //   IRQ_END_TRATADOR
  // colocamos no endereço IRQ_END_TRATADOR o programa de tratamento
  //   de interrupção (escrito em asm). esse programa deve conter a
  //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
  //   foi definido acima)
  int ender = so_carrega_programa(self, NENHUM_PROCESSO, "trata_int.maq");
  if (ender != IRQ_END_TRATADOR)
  {
    console_printf("SO: problema na carga do programa de tratamento de interrupção");
    self->erro_interno = true;
  }

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK)
  {
    console_printf("SO: problema na programação do timer");
    self->erro_interno = true;
  }
}

so_t *so_cria(cpu_t *cpu, mem_t *mem, mem_t *mem_sec, mmu_t *mmu, es_t *es, console_t *console)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL)
    return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->mem_secundaria = mem_sec;
  self->mmu = mmu;
  self->es = es;
  self->console = console;
  self->erro_interno = false;
  self->processo_corrente = NULL;
  self->pid_atual = 1;
  self->quantum_proc = QUANTUM;
  self->n_procs = 0;
  self->r_agora = -1;

  inicializa_fila_prontos(self);
  inicializa_metricas(self);
  inicializa_cpu(self);

  // inicializa a tabela de páginas global, e entrega ela para a MMU
  // t2: com processos, essa tabela não existiria, teria uma por processo, que
  //     deve ser colocada na MMU quando o processo é despachado para execução
  // define o primeiro quadro livre de memória como o seguinte àquele que
  //   contém o endereço 99 (as 100 primeiras posições de memória (pelo menos)
  //   não vão ser usadas por programas de usuário)
  // t2: o controle de memória livre deve ser mais aprimorado que isso
  self->quadro_livre = 0;
  self->quadro_livre_primaria = 99 / TAM_PAGINA + 1;

  self->fifo = fifo_cria();

  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);

  for (int i = 0; i < self->n_procs; i++)
  {
    free(self->processos[i]);
  }
  free(self->processos);

  no_fila_t *no_atual = self->fila_prontos->inicio;
  while (no_atual != NULL)
  {
    no_fila_t *no_proximo = no_atual->proximo;
    free(no_atual);
    no_atual = no_proximo;
  }
  free(self->fila_prontos);

  free(self);
}

// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self, int escalonador);
static void so_escalona_simples(so_t *self);
static void so_escalona_round_robin(so_t *self);
static void so_escalona_prioridade(so_t *self);
static int so_despacha(so_t *self);

static int so_para(so_t *self)
{
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_TIMER, 0);
  e2 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0);
  if (e1 != ERR_OK || e2 != ERR_OK)
  {
    console_printf("SO: nao consigo desligar o timer!!");
    self->erro_interno = true;
  }

  so_imprime_metricas(self);

  return 1;
}

static bool algo_pra_fazer(so_t *self)
{
  for (int i = 0; i < self->n_procs; i++)
  {
    if (self->processos[i]->estado != ESTADO_MORTO)
    {
      return true;
    }
  }
  return false;
}

static void so_calc_metricas(so_t *self)
{
  int r_anterior = self->r_agora;

  if (es_le(self->es, D_RELOGIO_INSTRUCOES, &self->r_agora) != ERR_OK)
  {
    console_printf("SO: erro na leitura do relógio");
    return;
  }

  if (r_anterior == -1)
  {
    return;
  }

  int dif_tempo = self->r_agora - r_anterior;

  so_atualiza_metricas(self, dif_tempo);
}

static int so_trata_interrupcao(void *argC, int reg_A)
{
  console_printf("SO: tratando interrupção");
  so_t *self = argC;
  irq_t irq = reg_A;

  self->metricas.num_interrupcoes[irq]++;

  // esse print polui bastante, recomendo tirar quando estiver com mais confiança
  // console_printf("SO: recebi IRQ %d (%s)", irq, QTD_IRQome(irq));

  // salva o estado da cpu no descritor do processo que foi interrompido
  console_printf("SO: salvando estado da CPU");
  so_salva_estado_da_cpu(self);

  // console_printf("SO: calculando métricas");
  so_calc_metricas(self);

  // console_printf("SO: tratando IRQ %d", irq);
  so_trata_irq(self, irq);

  // faz o processamento independente da interrupção
  // console_printf("SO: processando");
  so_trata_pendencias(self);

  // escolhe o próximo processo a executar
  so_escalona(self, ESCALONADOR);

  // recupera o estado do processo escolhido
  if (algo_pra_fazer(self))
  {
    console_printf("SO: despachando");
    return so_despacha(self);
  }
  else
  {
    console_printf("SO: nada a fazer");
    return so_para(self);
  }
}

static void so_salva_estado_da_cpu(so_t *self)
{
  if (self->processo_corrente == NULL)
  {
    return; // Se não há processo corrente, nada a fazer
  }

  // lê o PC da CPU (endereço onde o programa interrompido será retomado)
  mem_le(self->mem, IRQ_END_PC, &self->processo_corrente->pc);

  // lê os valores dos registradores de propósito geral e salva no processo
  mem_le(self->mem, IRQ_END_A, &self->processo_corrente->reg[0]);
  mem_le(self->mem, IRQ_END_X, &self->processo_corrente->reg[1]);
  mem_le(self->mem, IRQ_END_complemento, &self->processo_corrente->complemento);
  mem_le(self->mem, IRQ_END_erro, &self->processo_corrente->erro);
  mem_le(self->mem, IRQ_END_modo, &self->processo_corrente->modo);
}

static int calcula_dispositivo(int disp, int terminal)
{
  return disp + terminal * 4;
}

static void insere_na_fila_prontos(so_t *self, processo_t *proc)
{
  no_fila_t *novo_no = malloc(sizeof(no_fila_t));
  if (novo_no == NULL)
  {
    console_printf("SO: erro ao alocar memória para a fila de processos prontos");
    self->erro_interno = true;
    return;
  }
  novo_no->processo = proc;
  novo_no->proximo = NULL;

  if (self->fila_prontos->fim == NULL)
  {
    self->fila_prontos->inicio = novo_no;
  }
  else
  {
    self->fila_prontos->fim->proximo = novo_no;
  }
  self->fila_prontos->fim = novo_no;
}

static bool disco_disponivel(so_t *self, int tempo_pedido)
{
  int tempo_atual_disco = tempo_atual(self);

  if (tempo_atual_disco >= self->hora_disco_livre)
  {
    // Disco está livre, atualiza próximo horário livre
    self->hora_disco_livre = tempo_atual_disco + tempo_pedido;
    return true;
  }
  else
  {
    // Disco ocupado, soma o tempo pedido ao tempo que já vai levar
    self->hora_disco_livre += tempo_pedido;
    return false;
  }
}

static void so_trata_pendencias(so_t *self)
{
  for (int i = 0; self->processos[i] != NULL; i++)
  {
    processo_t *proc = self->processos[i];

    if (proc->estado == ESTADO_BLOQUEADO)
    {
      int terminal = so_obtem_terminal(proc->pid);
      int dispositivo_teclado = calcula_dispositivo(D_TERM_A_TECLADO_OK, terminal);
      int dispositivo_tela = calcula_dispositivo(D_TERM_A_TELA_OK, terminal);

      int estado_teclado, estado_tela;

      switch (proc->motivo_bloqueio)
      {
      case R_BLOQ_LEITURA:
        // se o dispositivo de teclado está pronto, le
        if (es_le(self->es, dispositivo_teclado, &estado_teclado) == ERR_OK && estado_teclado != 0)
        {
          proc_muda_estado(proc, ESTADO_PRONTO);
        }
        break;
      case R_BLOQ_ESCRITA:
        // se o dispositivo de tela está pronto, escreve
        if (es_le(self->es, dispositivo_tela, &estado_tela) == ERR_OK && estado_tela != 0)
        {
          if (es_escreve(self->es, calcula_dispositivo(D_TERM_A_TELA, terminal), proc->dado_pendente) == ERR_OK)
          {
            proc_muda_estado(proc, ESTADO_PRONTO);
          }
        }
        break;
      case R_BLOQ_ESPERA_PROC:
        // verifica se o processo esperado já morreu
        for (int j = 0; self->processos[j] != NULL; j++)
        {
          if (self->processos[j]->pid == proc->reg[0] && self->processos[j]->estado == ESTADO_MORTO)
          {
            proc_muda_estado(proc, ESTADO_PRONTO);
            break;
          }
        }
        break;
      case R_BLOQ_ESPERA_DISCO:
        if (proc->hora_desbloqueio < tempo_atual(self))
        {
          proc_muda_estado(proc, ESTADO_PRONTO);
        }
        break;
      default:
        break;
      }

      if (proc->estado == ESTADO_PRONTO)
      {
        insere_na_fila_prontos(self, proc);
        console_printf("SO: processo %d desbloqueado e inserido na fila de prontos", proc->pid);
      }
    }
  }
}
static void atualiza_estado_processo_corrente(so_t *self)
{
  if (self->processo_corrente != NULL)
  {
    console_printf("SO: escalonando, processo corrente %d, estado %s", self->processo_corrente->pid, pega_nome_estado(self->processo_corrente->estado));
    if (self->processo_corrente->estado == ESTADO_PRONTO)
      self->processo_corrente->estado = ESTADO_EXECUTANDO;
  }
}

static void atualiza_prioridade(so_t *self)
{
  if (self->processo_corrente != NULL)
  {
    self->processo_corrente->prioridade = ((self->processo_corrente->prioridade + (QUANTUM - self->quantum_proc) / (float)QUANTUM)) / 2;
  }
}

static void so_escalona(so_t *self, int escalonador)
{

  atualiza_estado_processo_corrente(self);
  atualiza_prioridade(self);

  switch (escalonador)
  {
  case 0:
    so_escalona_prioridade(self);
    break;
  case 1:
    so_escalona_round_robin(self);
    break;
  case 2:
    so_escalona_simples(self);
    break;
  default:
    console_printf("SO: escalonador não reconhecido");
    self->erro_interno = true;
  }
  if (self->processo_corrente != NULL)
    console_printf("SO: escalonado, processo corrente %d, estado %s", self->processo_corrente->pid, pega_nome_estado(self->processo_corrente->estado));
}

static void so_executa_proc(so_t *self, processo_t *proc)
{
  if (self->processo_corrente != NULL && proc != NULL)
    console_printf("--SO: processo %d, estado %s, processo_so %d, estado %s", proc->pid, pega_nome_estado(proc->estado), self->processo_corrente->pid, pega_nome_estado(self->processo_corrente->estado));

  if (
      self->processo_corrente != NULL &&
      self->processo_corrente != proc &&
      self->processo_corrente->estado == ESTADO_EXECUTANDO)
  {
    proc_muda_estado(self->processo_corrente, ESTADO_PRONTO);
    self->metricas.num_preempcoes++;
    console_printf("SO: processo %d preempedido", self->processo_corrente->pid);
  }

  if (proc != NULL && proc->estado != ESTADO_EXECUTANDO)
  {
    console_printf("SO: processo %d executando", proc->pid);
    proc_muda_estado(proc, ESTADO_EXECUTANDO);
  }

  self->processo_corrente = proc;
  self->quantum_proc = QUANTUM;
}

static bool ha_processos_em_estado(so_t *self, int estado)
{
  for (int i = 0; self->processos[i] != NULL; i++)
  {
    if (self->processos[i]->estado == estado)
    {
      return true;
    }
  }
  return false;
}

static processo_t *busca_primeiro_processo_pronto(so_t *self)
{
  for (int i = 0; self->processos[i] != NULL; i++)
  {
    if (self->processos[i]->estado == ESTADO_PRONTO)
    {
      return self->processos[i];
    }
  }
  return NULL;
}

static void so_escalona_simples(so_t *self)
{
  if (self->processo_corrente != NULL && self->processo_corrente->estado == ESTADO_EXECUTANDO)
  {
    return;
  }

  processo_t *proximo = busca_primeiro_processo_pronto(self);
  if (proximo != NULL)
  {
    so_executa_proc(self, proximo);
    return;
  }

  if (ha_processos_em_estado(self, ESTADO_BLOQUEADO))
  {
    self->processo_corrente = NULL;
  }
  else
  {
    console_printf("SO: todos os processos finalizaram, CPU parando");
    self->erro_interno = true;
  }
}

static processo_t *remove_primeiro_processo_fila(fila_t *fila)
{
  if (fila->inicio == NULL)
  {
    return NULL;
  }

  no_fila_t *no = fila->inicio;
  processo_t *proc = no->processo;
  fila->inicio = no->proximo;
  if (fila->inicio == NULL)
  {
    fila->fim = NULL;
  }
  free(no);
  return proc;
}

static void so_escalona_round_robin(so_t *self)
{
  // se o processo corrente está executando e ainda tem quantum, ele continua executando
  if (self->processo_corrente != NULL && self->processo_corrente->estado == ESTADO_EXECUTANDO && self->quantum_proc > 0)
  {
    return;
  }

  // se o processo corrente terminou seu quantum, coloca no final da fila
  if (self->processo_corrente != NULL && self->processo_corrente->estado == ESTADO_EXECUTANDO)
  {
    insere_na_fila_prontos(self, self->processo_corrente);
  }

  // pega o próximo processo da fila de prontos
  if (self->fila_prontos->inicio != NULL)
  {
    processo_t *proximo = remove_primeiro_processo_fila(self->fila_prontos);
    so_executa_proc(self, proximo);
  }
  else
  {
    self->processo_corrente = NULL;
  }
}

static no_fila_t *busca_processo_maior_prioridade(fila_t *fila)
{
  no_fila_t *no_anterior = NULL;
  no_fila_t *no_maior_prioridade = fila->inicio;
  no_fila_t *no_maior_prioridade_anterior = NULL;
  no_fila_t *no_atual = fila->inicio;

  while (no_atual != NULL)
  {
    if (no_atual->processo->prioridade < no_maior_prioridade->processo->prioridade)
    {
      no_maior_prioridade = no_atual;
      no_maior_prioridade_anterior = no_anterior;
    }
    no_anterior = no_atual;
    no_atual = no_atual->proximo;
  }

  if (no_maior_prioridade_anterior != NULL)
  {
    no_maior_prioridade_anterior->proximo = no_maior_prioridade->proximo;
  }
  else
  {
    fila->inicio = no_maior_prioridade->proximo;
  }

  if (no_maior_prioridade == fila->fim)
  {
    fila->fim = no_maior_prioridade_anterior;
  }

  return no_maior_prioridade;
}

static void so_escalona_prioridade(so_t *self)
{
  if (self->processo_corrente != NULL)
    console_printf("Processo Corrente: %d, estado %s", self->processo_corrente->pid, pega_nome_estado(self->processo_corrente->estado));

  if (self->processo_corrente != NULL && self->processo_corrente->estado == ESTADO_EXECUTANDO && self->quantum_proc > 0)
  {
    return;
  }

  if (self->processo_corrente != NULL && self->processo_corrente->estado == ESTADO_EXECUTANDO)
  {
    insere_na_fila_prontos(self, self->processo_corrente);
  }

  if (self->fila_prontos->inicio != NULL)
  {
    no_fila_t *no_maior_prioridade = busca_processo_maior_prioridade(self->fila_prontos);

    if (self->processo_corrente != NULL && no_maior_prioridade->processo != NULL)
    {
      console_printf("SO: processo %d de maior prioridade, estado %s", no_maior_prioridade->processo->pid, pega_nome_estado(no_maior_prioridade->processo->estado));
      console_printf("SO: processo_so %d , estado %s", self->processo_corrente->pid, pega_nome_estado(self->processo_corrente->estado));
    }

    so_executa_proc(self, no_maior_prioridade->processo);
    free(no_maior_prioridade);
  }
  else
  {
    self->processo_corrente = NULL;
  }
}

static int so_despacha(so_t *self)
{
  if (self->processo_corrente == NULL || self->erro_interno)
  {
    return 1;
  }
  // configura a MMU para usar a tabela de páginas do processo corrente
  mmu_define_tabpag(self->mmu, self->processo_corrente->tabpag);
  // escreve o PC e os registradores do processo corrente nos endereços onde a CPU recupera o estado
  mem_escreve(self->mem, IRQ_END_PC, self->processo_corrente->pc);
  mem_escreve(self->mem, IRQ_END_A, self->processo_corrente->reg[0]);
  mem_escreve(self->mem, IRQ_END_X, self->processo_corrente->reg[1]);
  mem_escreve(self->mem, IRQ_END_complemento, self->processo_corrente->complemento);
  mem_escreve(self->mem, IRQ_END_erro, ERR_OK);
  mem_escreve(self->mem, IRQ_END_modo, self->processo_corrente->modo);

  console_printf("SO: despachando processo %d", self->processo_corrente->pid);
  return 0;
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq)
{
  // verifica o tipo de interrupção que está acontecendo, e atende de acordo
  switch (irq)
  {
  case IRQ_RESET:
    so_trata_irq_reset(self);
    break;
  case IRQ_SISTEMA:
    so_trata_irq_chamada_sistema(self);
    break;
  case IRQ_ERR_CPU:
    so_trata_irq_err_cpu(self);
    break;
  case IRQ_RELOGIO:
    so_trata_irq_relogio(self);
    break;
  default:
    so_trata_irq_desconhecida(self, irq);
  }
}
static processo_t *aloca_processo()
{
  processo_t *proc = malloc(sizeof(processo_t));
  if (proc == NULL)
  {
    console_printf("SO: erro ao alocar memória para o novo processo");
  }
  return proc;
}

static void inicializa_processo(processo_t *proc, int pid, int pc)
{
  proc->pid = pid;
  proc->pc = pc;
  proc->estado = ESTADO_PRONTO;
  proc->reg[0] = 0;
  proc->reg[1] = 0;
  proc->prioridade = 0.5;
  proc->complemento = 0;
  proc->erro = 0;
  proc->modo = usuario;
  proc->sec_inicial = 0;

  // criar a tabela de páginas para o processo
  proc->tabpag = tabpag_cria();
  if (proc->tabpag == NULL)
  {
    console_printf("SO: erro ao criar tabela de páginas para processo %d", pid);
    return;
  }

  proc->metricas.qtd_preempcoes = 0;
  proc->metricas.tempo_retorno = 0;
  proc->metricas.tempo_resposta = 0;
  proc->metricas.qtd_page_fault = 0;
  for (int i = 0; i < ESTADO_N; i++)
  {
    proc->metricas.estados[i].qtd = 0;
    proc->metricas.estados[i].tempo_total = 0;
  }

  proc->metricas.estados[ESTADO_PRONTO].qtd = 1;
}

static processo_t *so_cria_processo(so_t *self, char *nome_do_executavel)
{
  processo_t *proc = aloca_processo();
  if (proc == NULL)
  {
    return NULL;
  }

  int novo_pid = self->pid_atual++;
  inicializa_processo(proc, novo_pid, 0);

  int pc = so_carrega_programa(self, proc, nome_do_executavel);
  console_printf("SO: processo %d criado com PC=%d", novo_pid, pc);
  if (pc == -1)
  {
    console_printf("SO: erro ao carregar o programa '%s'", nome_do_executavel);
    free(proc);
    return NULL;
  }

  proc->pc = pc;

  self->n_procs++;

  return proc;
}

// interrupção gerada uma única vez, quando a CPU inicializa
static void so_trata_irq_reset(so_t *self)
{
  // cria um processo para o init
  processo_t *init_proc = so_cria_processo(self, "init.maq");
  if (init_proc == NULL)
  {
    console_printf("SO: problema na criação do processo init");
    self->erro_interno = true;
    return;
  }

  // adiciona o processo init à lista de processos
  self->processos = malloc(2 * sizeof(processo_t *));
  if (self->processos == NULL)
  {
    console_printf("SO: problema na criação da lista de processos");
    self->erro_interno = true;
    free(init_proc);
    return;
  }
  self->processos[0] = init_proc;
  self->processos[1] = NULL;
  self->processo_corrente = init_proc;

  // altera o PC para o endereço de carga
  mem_escreve(self->mem, IRQ_END_PC, init_proc->pc);
  // passa o processador para modo usuário
  // mem_escreve(self->mem, IRQ_END_modo, usuario);
}

// função auxiliar para verificar se há processos esperando pela morte de outro processo
static void so_verifica_espera(so_t *self, int pid_morto)
{
  for (int i = 0; self->processos[i] != NULL; i++)
  {
    if (self->processos[i]->estado == ESTADO_BLOQUEADO)
    {
      self->processos[i]->estado = ESTADO_PRONTO;
      console_printf("SO: processo %d desbloqueado após a morte do processo %d", self->processos[i]->pid, pid_morto);
    }
  }
}

bool tem_quadro_livre(so_t *self)
{
  return self->quadro_livre_primaria < N_QUADROS;
}

void bloqueia_por_espera_disco(so_t *self)
{
  self->processo_corrente->hora_desbloqueio = 0;
  proc_muda_estado(self->processo_corrente, ESTADO_BLOQUEADO);
  self->processo_corrente->motivo_bloqueio = R_BLOQ_ESPERA_DISCO;

  if (!disco_disponivel(self, TEMPO_DISCO))
  {
    self->processo_corrente->hora_desbloqueio = self->processo_corrente->hora_desbloqueio + TEMPO_DISCO + self->hora_disco_livre;
    return;
  }
  else
  {
    self->processo_corrente->hora_desbloqueio = TEMPO_DISCO + tempo_atual(self);
  }
}

void so_carrega_pag_para_mem_sec(so_t *self, int quadro, pagina_t *pag)
{
  bloqueia_por_espera_disco(self);

  int end_sec = pag->num * TAM_PAGINA + pag->processo->sec_inicial;

  for (int i = 0; i < TAM_PAGINA; i++)
  {
    self->processo_corrente->hora_desbloqueio += TEMPO_DISCO;
    int dado;
    if (mem_le(self->mem, quadro * TAM_PAGINA + i, &dado) != ERR_OK)
    {
      console_printf("SO: erro ao ler da memória principal");
      self->erro_interno = true;
      return;
    }
    console_printf("SO: ESCREVENDO MEM SEC POIS FOI ALTERADA prim[%d]=v[%d]=sec[%d]=%d", quadro * TAM_PAGINA + i, pag->num * TAM_PAGINA + i, end_sec + i, dado);
    if (mem_escreve(self->mem_secundaria, end_sec + i, dado) != ERR_OK)
    {
      console_printf("SO: erro ao escrever na memória secundária");
      self->erro_interno = true;
      return;
    }
  }
}

void so_copia_mem_sec_para_prim(so_t *self, int end_sec, int quadro, int pagina)
{
  bloqueia_por_espera_disco(self);
  for (int i = 0; i < TAM_PAGINA; i++)
  {
    int dado;
    if (mem_le(self->mem_secundaria, end_sec + i, &dado) != ERR_OK)
    {
      console_printf("SO: erro ao ler da memória secundária");
      self->erro_interno = true;
      return;
    }
    console_printf("prim[%d]=v[%d]=sec[%d]=%d", quadro * TAM_PAGINA + i, pagina * TAM_PAGINA + i, end_sec + i, dado);
    if (mem_escreve(self->mem, quadro * TAM_PAGINA + i, dado) != ERR_OK)
    {
      console_printf("SO: erro ao escrever na memória principal");
      self->erro_interno = true;
      return;
    }
  }
}

bool pag_alterada(pagina_t *pag)
{
  return tabpag_bit_alteracao(pag->tab_pag, pag->num);
}

int so_tabpag_escolhe_vitima(so_t *self, int algoritmo_escolha_vitima_t)
{
  pagina_t *pag_vitima = malloc(sizeof(pagina_t));
  if (algoritmo_escolha_vitima_t == TROCA_FIFO)
  {

    fifo_pega(self->fifo, pag_vitima);

    if (pag_vitima == NULL)
    {
      console_printf("SO: erro ao escolher vítima");
      self->erro_interno = true;
      return -1;
    }
  }
  else if (algoritmo_escolha_vitima_t == TROCA_SEGUNDA_CHANCE)
  {
    while (true)
    {
      fifo_pega(self->fifo, pag_vitima);

      bool acessada = tabpag_bit_acesso(pag_vitima->tab_pag, pag_vitima->num);
      if (!acessada)
      {
        break;
      }
      else
      {
        tabpag_zera_bit_acesso(pag_vitima->tab_pag, pag_vitima->num);

        fifo_insere_pagina(self->fifo, pag_vitima->num, pag_vitima->quadro_num, pag_vitima->tab_pag, pag_vitima->processo);
      }
    }
  }
  else
  {
    console_printf("SO: algoritmo de substituição de página desconhecido");
    self->erro_interno = true;
    return -1;
  }
  if (pag_alterada(pag_vitima))
  {
    so_carrega_pag_para_mem_sec(self, pag_vitima->quadro_num, pag_vitima);
  }
  int quadro;
  tabpag_traduz(self->processo_corrente->tabpag, pag_vitima->num, &quadro);
  tabpag_invalida_pagina(pag_vitima->tab_pag, pag_vitima->num);

  return pag_vitima->quadro_num;
}

int so_pega_quadro(so_t *self)
{
  if (tem_quadro_livre(self))
  {

    self->quadro_livre_primaria++;
    return self->quadro_livre_primaria - 1;
  }
  else
  {
    // se não houver espaço, escolhe uma página para substituir
    console_printf("SO: Memoria principal encheu escolhendo um quadro para retirar uma pagina.");
    return so_tabpag_escolhe_vitima(self, TROCA_SEGUNDA_CHANCE);
  }
}

bool verifica_segmentation_fault(int complemento, processo_t *processo)
{

  int end_virtual_final = processo->sec_final - processo->sec_inicial;

  if (complemento < 0 || complemento > end_virtual_final)
    return true;

  return false;
}

static void mata_proc_erro_cpu(so_t *self, processo_t *processo)
{
  console_printf("SO: matando processo %d devido a erro na CPU", processo->pid);
  so_chamada_mata_proc(self);
  so_verifica_espera(self, processo->pid);
  self->erro_interno = true;
  return;
}

static void so_trata_pag_ausente(so_t *self)
{
  int end_faltante = self->processo_corrente->complemento;

  if (verifica_segmentation_fault(end_faltante, self->processo_corrente))
  {
    console_printf("SO: SEGMENTATION FAULT");
    mata_proc_erro_cpu(self, self->processo_corrente);
    return;
  }

  int pagina = end_faltante / TAM_PAGINA;

  // verifica se ainda há espaço na memória principal

  int quadro = so_pega_quadro(self);

  // calcula endereço base na memória secundária
  int end_sec = pagina * TAM_PAGINA + self->processo_corrente->sec_inicial;

  // console_printf("SO:sec_inicial=%d", self->processo_corrente->sec_inicial);
  console_printf("SO: Processo corrente %d", self->processo_corrente->pid);

  // copia a página da memória secundária para a principal
  so_copia_mem_sec_para_prim(self, end_sec, quadro, pagina);
  escreve_memoria_fisica(self);

  // marca a página como presente na tabela de páginas
  tabpag_define_quadro(self->processo_corrente->tabpag, pagina, quadro);

  console_printf("SO: Inserindo página: %d quadro: %d tam_tab: %d processo pid: %d", pagina, quadro, self->processo_corrente->tabpag->tam_tab, self->processo_corrente->pid);
  fifo_insere_pagina(self->fifo, pagina, quadro, self->processo_corrente->tabpag, self->processo_corrente);
  print_tabela_paginas(self->processo_corrente->tabpag);
  fifo_imprime(self->fifo);

  console_printf("SO: Página %d carregada no quadro %d da memória principal", pagina, quadro);
}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
  int err_int;
  mem_le(self->mem, IRQ_END_erro, &err_int);
  err_t err = err_int;

  console_printf("SO: erro na CPU: %s", err_nome(err));

  if (err_int == ERR_PAG_AUSENTE)
  {
    console_printf("SO: Foi causada pelo processo: %d", self->processo_corrente->pid);
    self->processo_corrente->metricas.qtd_page_fault++;
    so_trata_pag_ausente(self);
    return;
  }
  if (err_int == ERR_END_INV)
  {
    console_printf("SO: Foi causado pelo processo: %d", self->processo_corrente->pid);
  }

  if (self->processo_corrente != NULL)
  {
    mata_proc_erro_cpu(self, self->processo_corrente);
  }
  else
  {
    console_printf("SO: erro na CPU sem processo corrente");
  }

  self->erro_interno = true;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self)
{
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0); // desliga o sinalizador de interrupção
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
  if (e1 != ERR_OK || e2 != ERR_OK)
  {
    console_printf("SO: problema da reinicialização do timer");
    self->erro_interno = true;
  }
  // decrementa o quantum do processo corrente
  if (self->quantum_proc > 0)
  {
    self->quantum_proc--;
  }
  console_printf("Quantum: %d", self->quantum_proc);
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  self->erro_interno = true;
}

static void so_trata_irq_chamada_sistema(so_t *self)
{
  // a identificação da chamada está no registrador A
  // t1: com processos, o reg A tá no descritor do processo corrente
  int id_chamada;
  console_printf("trata_irq_sistema");
  if (mem_le(self->mem, IRQ_END_A, &id_chamada) != ERR_OK)
  {
    console_printf("SO: erro no acesso ao id da chamada de sistema");
    self->erro_interno = true;
    return;
  }
  console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada)
  {
  case SO_LE:
    so_chamada_le(self);
    break;
  case SO_ESCR:
    so_chamada_escr(self);
    break;
  case SO_CRIA_PROC:
    so_chamada_cria_proc(self);
    break;
  case SO_MATA_PROC:
    so_chamada_mata_proc(self);
    break;
  case SO_ESPERA_PROC:
    so_chamada_espera_proc(self);

    break;
  default:
    console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
    // t1: deveria matar o processo
    so_chamada_mata_proc(self);
  }
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
// Função auxiliar para obter o terminal correspondente ao PID
static int so_obtem_terminal(int pid)
{
  return (pid - 1) % 4;
}

static void so_chamada_le(so_t *self)
{
  int terminal = so_obtem_terminal(self->processo_corrente->pid);
  int dispositivo = calcula_dispositivo(D_TERM_A_TECLADO, terminal);
  int dispositivo_ok = calcula_dispositivo(D_TERM_A_TECLADO_OK, terminal);

  int estado;
  if (es_le(self->es, dispositivo_ok, &estado) != ERR_OK)
  {
    console_printf("SO: problema no acesso ao estado do teclado do terminal %d", terminal);
    self->erro_interno = true;
    return;
  }

  if (estado == 0)
  {
    // dispositivo ocupado, bloquear o processo
    proc_muda_estado(self->processo_corrente, ESTADO_BLOQUEADO);
    self->processo_corrente->motivo_bloqueio = R_BLOQ_LEITURA; // definindo motivo do bloqueio
    return;
  }

  int dado;
  if (es_le(self->es, dispositivo, &dado) != ERR_OK)
  {
    console_printf("SO: problema no acesso ao teclado do terminal %d", terminal);
    self->erro_interno = true;
    return;
  }

  mem_escreve(self->mem, IRQ_END_A, dado);
}
/// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{

  int terminal = so_obtem_terminal(self->processo_corrente->pid);

  int dispositivo_tela = calcula_dispositivo(D_TERM_A_TELA, terminal);
  int dispositivo_tela_ok = calcula_dispositivo(D_TERM_A_TELA_OK, terminal);

  // verifica o estado do dispositivo de tela
  int estado;
  if (es_le(self->es, dispositivo_tela_ok, &estado) != ERR_OK)
  {
    console_printf("SO: problema no acesso ao estado da tela do terminal %d", terminal);
    self->erro_interno = true;
    return;
  }

  // se o dispositivo está ocupado, salva o dado pendente e bloqueia o processo
  if (estado == 0)
  {
    if (mem_le(self->mem, IRQ_END_X, &self->processo_corrente->dado_pendente) != ERR_OK)
    {
      console_printf("SO: problema ao ler o valor do registrador X");
      self->erro_interno = true;
      return;
    }
    proc_muda_estado(self->processo_corrente, ESTADO_BLOQUEADO);
    self->processo_corrente->motivo_bloqueio = R_BLOQ_ESCRITA; // definindo motivo do bloqueio
    return;
  }

  // lê o valor do registrador X
  int dado;
  if (mem_le(self->mem, IRQ_END_X, &dado) != ERR_OK)
  {
    console_printf("SO: problema ao ler o valor do registrador X");
    self->erro_interno = true;
    return;
  }

  // escreve o valor no dispositivo de tela
  if (es_escreve(self->es, dispositivo_tela, dado) != ERR_OK)
  {
    console_printf("SO: problema no acesso à tela do terminal %d", terminal);
    return;
  }

  mem_escreve(self->mem, IRQ_END_A, 0);
}

static void adiciona_processo_na_lista(so_t *self, processo_t *novo_proc)
{
  int i = 0;
  while (self->processos[i] != NULL)
  {
    i++;
  }
  self->processos = realloc(self->processos, (i + 2) * sizeof(processo_t *));
  if (self->processos == NULL)
  {
    console_printf("SO: erro ao realocar a lista de processos");
    free(novo_proc);
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }
  self->processos[i] = novo_proc;
  self->processos[i + 1] = NULL;
}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self)
{
  // lê o endereço onde está o nome do arquivo do novo processo
  int ender_nome;
  if (mem_le(self->mem, IRQ_END_X, &ender_nome) != ERR_OK)
  {
    console_printf("SO: erro ao acessar o endereço do nome do arquivo");
    self->erro_interno = true;
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  // copia o nome do arquivo da memória do processo
  char nome[100];
  if (!so_copia_str_do_processo(self, 100, nome, ender_nome, self->processo_corrente))
  {
    console_printf("SO: erro ao copiar o nome do arquivo da memória");
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  // cria o novo processo
  processo_t *novo_proc = so_cria_processo(self, nome);

  if (novo_proc == NULL)
  {
    console_printf("SO: erro ao criar o novo processo");
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  // adiciona o novo processo à lista de processos
  adiciona_processo_na_lista(self, novo_proc);

  // adiciona o novo processo à fila de prontos
  insere_na_fila_prontos(self, novo_proc);

  int i = 0;
  while (self->processos[i] != NULL)
  {
    console_printf("Lista de processos: %d", self->processos[i]->pid);
    i++;
  }

  self->processo_corrente->reg[0] = novo_proc->pid;
}
static void remove_processo_da_lista(so_t *self, int pid)
{
  no_fila_t *anterior = NULL;
  no_fila_t *atual = self->fila_prontos->inicio;
  while (atual != NULL)
  {
    if (atual->processo->pid == pid)
    {
      if (anterior == NULL)
      {
        self->fila_prontos->inicio = atual->proximo;
      }
      else
      {
        anterior->proximo = atual->proximo;
      }
      if (atual == self->fila_prontos->fim)
      {
        self->fila_prontos->fim = anterior;
      }
      free(atual);
      break;
    }
    anterior = atual;
    atual = atual->proximo;
  }
}

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
// modificação na função so_chamada_mata_proc para verificar processos esperando
static void so_chamada_mata_proc(so_t *self)
{
  int pid = self->processo_corrente->reg[1];

  console_printf("SO: matando processo com PID %d", pid);

  if (pid == 0)
  {
    pid = self->processo_corrente->pid;
  }

  for (int i = 0; self->processos[i] != NULL; i++)
  {
    if (self->processos[i]->pid == pid)
    {
      proc_muda_estado(self->processos[i], ESTADO_MORTO);
      if (self->processo_corrente->pid == pid)
      {
        self->processo_corrente = NULL;
      }
      mem_escreve(self->mem, IRQ_END_A, 0);

      // remove processo da fila de prontos
      remove_processo_da_lista(self, pid);

      return;
    }
  }

  console_printf("SO: processo com PID %d não encontrado", pid);
  mem_escreve(self->mem, IRQ_END_A, -1);
}

static processo_t *encontra_processo_por_pid(so_t *self, int pid)
{
  for (int i = 0; self->processos[i] != NULL; i++)
  {
    if (self->processos[i]->pid == pid)
    {
      return self->processos[i];
    }
  }
  return NULL;
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
  int pid = self->processo_corrente->reg[1];

  if (pid == self->processo_corrente->pid)
  {
    console_printf("SO: processo não pode esperar por si mesmo");
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  processo_t *proc_esperado = encontra_processo_por_pid(self, pid);

  if (proc_esperado == NULL)
  {
    console_printf("SO espera: processo com PID %d não encontrado", pid);
    mem_escreve(self->mem, IRQ_END_A, -1);
    return;
  }

  if (proc_esperado->estado == ESTADO_MORTO)
  {
    console_printf("SO: processo com PID %d já está morto", pid);
    mem_escreve(self->mem, IRQ_END_A, 0);
    return;
  }

  proc_muda_estado(self->processo_corrente, ESTADO_BLOQUEADO);
  self->processo_corrente->motivo_bloqueio = R_BLOQ_ESPERA_PROC; // definindo motivo do bloqueio
  self->processo_corrente->reg[0] = pid;                         // usado para identificar o processo que está esperando

  mem_escreve(self->mem, IRQ_END_A, 0);
}

// CARGA DE PROGRAMA {{{1

static int so_carrega_programa_na_memoria_fisica(so_t *self, programa_t *programa)
{
  int end_ini = prog_end_carga(programa);
  int end_fim = end_ini + prog_tamanho(programa);

  for (int end = end_ini; end < end_fim; end++)
  {
    if (mem_escreve(self->mem, end, prog_dado(programa, end)) != ERR_OK)
    {
      console_printf("Erro na carga da memória, endereco %d\n", end);
      return -1;
    }
  }
  console_printf("carregado na memória física, %d-%d", end_ini, end_fim);
  return end_ini;
}

static int so_carrega_programa_na_memoria_virtual(so_t *self, programa_t *programa, processo_t *processo)
{
  int end_virt_ini = prog_end_carga(programa);
  int end_virt_fim = end_virt_ini + prog_tamanho(programa) - 1;

  // carrega o programa na memória secundária
  int end_sec = self->quadro_livre * TAM_PAGINA; // usa quadro_livre como offset na mem secundária

  processo->sec_inicial = end_sec;
  processo->sec_final = end_sec + end_virt_fim;

  console_printf("SO: end_sec=%d", end_sec);

  for (int end_virt = end_virt_ini; end_virt <= end_virt_fim; end_virt++)
  {
    if (mem_escreve(self->mem_secundaria, end_sec, prog_dado(programa, end_virt)) != ERR_OK)
    {
      console_printf("Erro na carga da memória secundária, end %d\n", end_sec);
      return -1;
    }
    console_printf("v[%d]=sec[%d]=%d", end_virt, end_sec, prog_dado(programa, end_virt));
    end_sec++;
  }

  // atualiza o próximo quadro livre na memória secundária
  self->quadro_livre += (end_virt_fim - end_virt_ini + TAM_PAGINA) / TAM_PAGINA;

  console_printf("programa carregado na memória secundária, V%d-%d\n",
                 end_virt_ini, end_virt_fim);

  escreve_memoria_secundaria(self);
  return end_virt_ini;
}

// carrega o programa na memória de um processo ou na memória física se NENHUM_PROCESSO
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, processo_t *processo,
                               char *nome_do_executavel)
{
  console_printf("SO: carga de '%s'", nome_do_executavel);

  programa_t *programa = prog_cria(nome_do_executavel);
  if (programa == NULL)
  {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_carga;
  if (processo == NENHUM_PROCESSO)
  {
    end_carga = so_carrega_programa_na_memoria_fisica(self, programa);
  }
  else
  {
    end_carga = so_carrega_programa_na_memoria_virtual(self, programa, processo);
  }

  prog_destroi(programa);
  return end_carga;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do processo para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// O endereço é um endereço virtual de um processo.
static bool so_copia_str_do_processo(so_t *self, int tam, char str[tam],
                                     int end_virt, processo_t *processo)
{
  if (processo == NENHUM_PROCESSO)
  {
    return false;
  }

  mmu_define_tabpag(self->mmu, processo->tabpag);

  for (int indice_str = 0; indice_str < tam; indice_str++)
  {
    int caractere;

    err_t err = mmu_le(self->mmu, end_virt + indice_str, &caractere, usuario);

    if (err == ERR_PAG_AUSENTE)
    {
      // salva o endereço que causou o page fault
      self->processo_corrente->complemento = end_virt + indice_str;
      // trata a falta de página
      so_trata_pag_ausente(self);
      // tenta ler novamente
      if (mmu_le(self->mmu, end_virt + indice_str, &caractere, usuario) != ERR_OK)
      {
        return false;
      }
    }
    else if (err != ERR_OK)
    {
      return false;
    }

    if (caractere < 0 || caractere > 255)
    {
      return false;
    }

    str[indice_str] = caractere;

    if (caractere == 0)
    {
      return true;
    }
  }

  return false;
}

// vim: foldmethod=marker