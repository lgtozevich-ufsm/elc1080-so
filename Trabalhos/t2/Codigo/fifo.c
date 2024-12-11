#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "fifo.h"
#include "err.h"
#include "memoria.h"
#include "tabpag.h"
#include "tela.h"

fifo_t *fifo_cria()
{
    fifo_t *self = malloc(sizeof(*self));
    self->num_pags = 0;
    self->head = NULL;
    self->last = NULL;

    return self;
}

void fifo_destroi(fifo_t *self)
{
    pagina_t *aux = self->head;
    while (aux != NULL)
    {
        pagina_t *next = aux->next;
        free(aux);
        aux = next;
    }
    free(self);
}

static pagina_t *fifo_cria_no(int num, processo_t *processo, int quadro, tabpag_t *tab)
{
    pagina_t *self = malloc(sizeof(pagina_t));
    if (self != NULL)
    {
        self->num = num;
        self->processo = processo;
        self->quadro_num = quadro;
        self->tab_pag = tab;
        self->next = NULL;
        return self;
    }
    else
    {
        console_printf("Erro ao alocar nova página na fifo. \n");
        return NULL;
    }
}

void fifo_insere_pagina(fifo_t *self, int num, int quadro, tabpag_t *tab, processo_t *processo)
{
    pagina_t **last = &self->last;
    pagina_t *novo_no = fifo_cria_no(num, processo, quadro, tab);
    if (*last == NULL)
    {
        *last = novo_no;
        pagina_t **head = &self->head;
        *head = novo_no;
    }
    else
    {
        (*last)->next = novo_no;
        *last = novo_no;
    }
    self->num_pags++;
}

void fifo_retira_pagina(fifo_t *self)
{
    pagina_t **head = &self->head;
    if (*head == NULL)
    {
        console_printf("A fifo ja estah vazia");
        return;
    }

    pagina_t *new_head = (*head)->next;
    free(*head);
    *head = new_head;
    if (*head == NULL)
        self->last = NULL;
    self->num_pags--;
}

void fifo_pega(fifo_t *self, pagina_t *pagina)
{
    if (self->head == NULL)
    {
        console_printf("A fifo está vazia");
        return;
    }

    pagina->num = self->head->num;
    pagina->processo = self->head->processo;
    pagina->quadro_num = self->head->quadro_num;
    pagina->tab_pag = self->head->tab_pag;
    pagina->next = NULL;

    fifo_retira_pagina(self);
}

int fifo_prox_pag_num(fifo_t *self)
{
    return self->head->num;
}

int fifo_prox_pag_quadro(fifo_t *self)
{
    return self->head->quadro_num;
}

tabpag_t *fifo_prox_pag_tab(fifo_t *self)
{
    return self->head->tab_pag;
}

bool fifo_vazia(fifo_t *self)
{
    if (self->head == NULL)
        return true;
    else
        return false;
}

int fifo_num_pags(fifo_t *self)
{
    return self->num_pags;
}

void fifo_imprime(fifo_t *self)
{
    if (self->head == NULL)
        console_printf("SO: FILA DE PÁGINAS VAZIA");
    pagina_t *atual = self->head;
    console_printf("SO: IMPRIMINDO FILA DE PÁGINAS");
    int i=0;
    while (atual != NULL)
    {   
        console_printf("index: %d | pag: %d, quadro: %d proc: %d\n", i, atual->num, atual->quadro_num, atual->processo->pid);
        atual = atual->next;
        i++;
    }
}

void fifo_liberaPags_processo(fifo_t *self, int processo_pid)
{
    pagina_t *atual = self->head;
    pagina_t *anterior = NULL;
    while (atual != NULL)
    {
        if (atual->processo->pid == processo_pid)
        {
            self->num_pags--;
            if (anterior != NULL)
            {
                anterior->next = atual->next;
            }
            else
            {
                self->head = atual->next;
                self->last = atual->next;
            }
            pagina_t *temp = atual;
            atual = atual->next;
            free(temp);
        }
        else
        {
            anterior = atual;
            atual = atual->next;
        }
    }
}