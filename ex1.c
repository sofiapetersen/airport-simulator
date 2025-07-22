// Faça um programa que calcule a soma dos elementos de um vetor:

// 1. Seu programa deverá receber dois parâmetros: o tamanho de um vetor (>=100) e um número de threads (>=2).
// 2. O resultado do programa é o somatório dos elementos deste vetor.
// 3. O programa deve criar um vetor do tamanho especificado, iniciá-lo com valores sintéticos (randômicos ou fixos, por exemplo, 1, 2, 3, ...) e 
// reparti-lo entre as threads criadas, conforme o número de threads informado. 
// 4. Por exemplo, se o vetor tiver 100 posições, com 5 threads, então, cada thread irá somar 20 números do vetor.

// Orientação a ser seguida: o somatório deve ser realizado em uma variável compartilhada, sendo utilizado um mutex para coordenar o acesso a ela.
// Obs.: 
// Exemplo de teste: Crie um vetor grande o suficiente para que o programa necessite de, no mínimo, uns 2 segundos de execução (ou troque a operação, não faça uma simples soma). 
// Logo em seguida, experimente aumentar a quantidade de threads (distribuindo a carga de trabalho) e colete o tempo de execução.

// Pergunta a ser respondida: 
// Determine a quantidade de threads ideal para o seu problema (a partir de determinada quantidade, o tempo não irá reduzir, podendo até piorar. Faz sentido? qual motivo?)?

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <pthread.h>
#include <time.h>

long *handle_input();
long *initialize_vector(long size);
void *sum_routine(void *arg);

long *vector;
long vector_size;
int num_threads;
long total_sum = 0; 
pthread_mutex_t mutex;

int main()
{   
    long *inputs_array = handle_input();                                                    // Recebe os inputs do usuário: tamanho do vetor e número de threads
    vector_size = inputs_array[0];                                                           
    num_threads = (int)inputs_array[1];        

    free(inputs_array);                                                                     // Libera a memória alocada para os inputs

    vector = initialize_vector(vector_size);                                                // Inicializa o vetor com valores de 1 a vector_size
    pthread_mutex_init(&mutex, NULL);                                                       // Inicializa o mutex
    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));              // Aloca memória para os identificadores das threads

    for (long i = 0; i < num_threads; i++) {                                                // Cria as threads
        pthread_create(&threads[i], NULL, sum_routine, (void *)i);
    }

    for (int i = 0; i < num_threads; i++) {                                                 // Aguarda a finalização de todas as threads
        pthread_join(threads[i], NULL);
    }
    printf("%ld\n", total_sum);                                                             // Imprime o resultado da soma total

    pthread_mutex_destroy(&mutex);                                                          // Destrói o mutex
    free(vector);                                                                           // Libera a memória alocada para o vetor
    free(threads);                                                                          // Libera a memória alocada para os identificadores das threads

    return 0;

    
}

long *handle_input()
{
    long *inputs = (long *)malloc(2 * sizeof(long));
    if (inputs == NULL) {
        perror("Failed to allocate memory for inputs");
        exit(EXIT_FAILURE);
    }

    long vector_size_temp;
    int num_threads_temp;

    printf("Type the size of the vector (>= 100): ");
    scanf("%ld", &vector_size_temp);
    if (vector_size_temp < 100) {
        printf("The size of the vector must be at least 100.\n");
        free(inputs);
        exit(EXIT_FAILURE);
    }

    printf("Type the number of threads (>= 2): ");
    scanf("%d", &num_threads_temp);
    if (num_threads_temp < 2) {
        printf("The number of threads must be at least 2.\n");
        free(inputs);
        exit(EXIT_FAILURE);
    }

    inputs[0] = vector_size_temp;
    inputs[1] = (long)num_threads_temp;

    return inputs; 
}

long *initialize_vector(long size) {
    long *temp_vector = malloc(size * sizeof(long)); 
    if (temp_vector == NULL) {
        perror("Failed to allocate memory for vector");
        exit(EXIT_FAILURE);
    }

    for (long i = 0; i < size; i++) {
        temp_vector[i] = i ;
    }

    return temp_vector;
}

void *sum_routine(void *arg) {
    long thread_id = (long)arg;                                     // ID da thread
    long local_sum = 0;

    long chunk_size = vector_size / num_threads;                    // Tamanho do chunk para cada thread -> SE VECTOR_SIZE = 100 e NUM_THREADS = 2, então CHUNK_SIZE = 50
    long remaining_items = vector_size % num_threads;               // Itens restantes que não se dividem igualmente entre as threads

    long start_index = thread_id * chunk_size;                      // Index inicial para esta thread -> SE THREAD 0, START_INDEX = 0; SE THREAD 1, START_INDEX = 50
    if (thread_id == num_threads - 1)                               // Se for a última thread, ela pega os itens restantes
    {                                                                       
        chunk_size += remaining_items;                              // A última thread pega o restante dos itens
    }
    long end_index = start_index + chunk_size - 1;                  // Index final para esta thread -> SE THREAD 0, END_INDEX = 49; SE THREAD 1, END_INDEX = 99

    
    for (long i = start_index; i <= end_index; i++) {                // Soma os elementos da sua parte do vetor
        local_sum += vector[i];
    }

    pthread_mutex_lock(&mutex);
    printf("Thread %ld: Calculated local sum from index %ld to %ld: %ld\n", thread_id, start_index, end_index, local_sum);
    total_sum += local_sum;
    pthread_mutex_unlock(&mutex);

    return NULL;
}