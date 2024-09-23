#ifndef RANDOM_H
#define RANDOM_H

#include "err.h"

typedef struct random_t random_t;

random_t *random_cria(void);

void random_destroi(random_t *r);

err_t random_leitura(void *r, int id, int *pvalor);

#endif