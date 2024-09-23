#include "random.h"
#include "console.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <assert.h>

typedef struct random_t
{
    int value;
} random_t;

random_t *random_cria(void)
{
    random_t *self;
    self = malloc(sizeof(random_t));
    assert(self != NULL);
    self->value = 0;
    return self;
    srand(time(NULL));
}

void random_destroi(random_t *r)
{
    free(r);
}

err_t random_leitura(void *r, int id, int *pvalor)
{

    random_t *self = r;
 
    self->value = rand() % 100 + 1;
    err_t err = ERR_OK;
    *pvalor = self->value;
    return err;
}