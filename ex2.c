produtor-consumidor
/*
 * Múltiplos produtores e consumidores.
 */
//#define __USE_GNU 1 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <semaphore.h>

#define N_PRODUTORES 5
#define N_CONSUMIDORES 5

#define N_ITENS 30 /* Tamanho do buffer */
int buffer[N_ITENS];

/* Itens a serem produzidos por um produtor */
#define PRODUCAO N_ITENS * 3

sem_t pos_vazia;
sem_t pos_ocupada;
sem_t lock_prod;
sem_t lock_cons;

int inicio = 0, final = 0;

void* produtor(void *v) {
  int i;

  for (i = 0; i < PRODUCAO; i++) {
    sem_wait(&pos_vazia);
    sem_wait(&lock_prod);
    printf("Produtor, item = %d.\n", i);     
    final = (final + 1) % N_ITENS;
    buffer[final] = i;
    sem_post(&lock_prod);
    sem_post(&pos_ocupada);
    sleep(random() % 3);  /* Permite que outra thread execute */
  }
  return NULL;
}

void* consumidor(void *v) {
  int i;

  for (i = 0; i < (PRODUCAO * N_PRODUTORES)/N_CONSUMIDORES; i++) {
    sem_wait(&pos_ocupada);
    sem_wait(&lock_cons);
    inicio = (inicio + 1) % N_ITENS;
    printf("Consumidor, item = %d.\n", buffer[inicio]);
    sem_post(&lock_cons);
    sem_post(&pos_vazia);
    sleep(random() % 3);  /* Permite que outra thread execute */  
  }
  return NULL;
}

int main() {

  pthread_t thr_produtor[N_PRODUTORES], thr_consumidor[N_CONSUMIDORES];
  int i;


  sem_init(&pos_vazia, 0, N_ITENS);
  sem_init(&pos_ocupada, 0, 0);
  sem_init(&lock_prod, 0, 1);
  sem_init(&lock_cons, 0, 1);
  
  for (i = 0; i < N_PRODUTORES; i++)
    pthread_create(&thr_produtor[i], NULL, produtor, NULL);

  for (i = 0; i < N_CONSUMIDORES; i++)
    pthread_create(&thr_consumidor[i], NULL, consumidor, NULL);

  for (i = 0; i < N_PRODUTORES; i++)
    pthread_join(thr_produtor[i], NULL); 

  for (i = 0; i < N_CONSUMIDORES; i++)
    pthread_join(thr_consumidor[i], NULL);

  sem_destroy(&pos_vazia);
  sem_destroy(&pos_ocupada);
  sem_destroy(&lock_cons);
  sem_destroy(&lock_prod);
  
  return 0;
}