#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

// configuracoes do aeroporto
#define NUM_PISTAS 3
#define NUM_PORTOES 5
#define MAX_TORRE_OPERACOES 2
#define TEMPO_SIMULACAO 100  
#define ALERTA_CRITICO 60    
#define TEMPO_CRASH 90       
#define MAX_AVIOES 50        

// estados dos avioes
typedef enum {
    ESPERANDO_POUSO,
    POUSANDO,
    ESPERANDO_DESEMBARQUE,
    DESEMBARCANDO,
    ESPERANDO_DECOLAGEM,
    DECOLANDO,
    FINALIZADO,
    CRASHED
} estado_aviao_t;

// tipos de voo
typedef enum {
    VOO_DOMESTICO,
    VOO_INTERNACIONAL
} tipo_voo_t;

// estrutura do aviao
typedef struct {
    int id;
    tipo_voo_t tipo;
    estado_aviao_t estado;
    time_t tempo_criacao;
    time_t tempo_inicio_espera;
    int alerta_critico;
    int crashed;
    pthread_t thread;
} aviao_t;

// recursos do aeroporto
sem_t sem_pistas;
sem_t sem_portoes;
sem_t sem_torre;

// mutex para controle
pthread_mutex_t mutex_output = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_estatisticas = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_aviao = PTHREAD_MUTEX_INITIALIZER;

// variaveis globais
aviao_t avioes[MAX_AVIOES];
int contador_avioes = 0;
int simulacao_ativa = 1;
time_t inicio_simulacao;

// contadores de recursos em uso
int pistas_em_uso = 0;
int portoes_em_uso = 0;
int torre_operacoes_ativas = 0;

// estatisticas
// int total_pousos_sucesso = 0;
// int total_decolagens_sucesso = 0;
// int total_crashes = 0;
// int total_starvations = 0;
// int total_deadlocks_detectados = 0;
// int voos_domesticos_criados = 0;
// int voos_internacionais_criados = 0;
// int voos_domesticos_finalizados = 0;
// int voos_internacionais_finalizados = 0;

// Protótipos das funções
void* aviao_thread(void* arg);
void pouso(aviao_t* aviao);
void desembarque(aviao_t* aviao);
void decolagem(aviao_t* aviao);
void verificar_timeout(aviao_t* aviao);
void imprimir_status(const char* msg, aviao_t* aviao);
void imprimir_status_recursos(const char* operacao, aviao_t* aviao);
void inicializar_recursos();
void finalizar_recursos();
void criar_avioes();
void detectar_deadlock();
void imprimir_estado_recursos();
double tempo_decorrido(time_t inicio);

int main() {
    printf("=== SIMULAÇÃO DE CONTROLE DE TRÁFEGO AÉREO - WSL ===\n");
    printf("Recursos: %d pistas, %d portões, %d operações simultâneas na torre\n",
           NUM_PISTAS, NUM_PORTOES, MAX_TORRE_OPERACOES);
    printf("Tempo de simulação: %d segundos\n\n", TEMPO_SIMULACAO);

    srand(time(NULL));
    inicio_simulacao = time(NULL);
    
    inicializar_recursos();
    
    // Criar thread para gerar aviões
    pthread_t thread_criador;
    pthread_create(&thread_criador, NULL, (void*)criar_avioes, NULL);
    
    // Thread para monitoramento de deadlocks a cada 10 segundos
    pthread_t thread_monitor;
    pthread_create(&thread_monitor, NULL, (void*)detectar_deadlock, NULL);
    
    // Aguarda o fim da simulação
    sleep(TEMPO_SIMULACAO);
    simulacao_ativa = 0;
    
    printf("\n=== FIM DA SIMULAÇÃO - AGUARDANDO THREADS FINALIZAREM ===\n");
    
    // Aguarda thread criadora e monitor
    pthread_join(thread_criador, NULL);
    pthread_join(thread_monitor, NULL);
    
    // Aguarda todas as threads de aviões terminarem
    for (int i = 0; i < contador_avioes; i++) {
        if (avioes[i].estado != CRASHED) {
            pthread_join(avioes[i].thread, NULL);
        }
    }
    finalizar_recursos();
    
    return 0;
}

void inicializar_recursos() {
    if (sem_init(&sem_pistas, 0, NUM_PISTAS) != 0) {
        perror("Erro ao inicializar semáforo de pistas");
        exit(1);
    }
    if (sem_init(&sem_portoes, 0, NUM_PORTOES) != 0) {
        perror("Erro ao inicializar semáforo de portões");
        exit(1);
    }
    if (sem_init(&sem_torre, 0, MAX_TORRE_OPERACOES) != 0) {
        perror("Erro ao inicializar semáforo de torre");
        exit(1);
    }
    printf("Recursos inicializados com sucesso!\n");
}

void finalizar_recursos() {
    sem_destroy(&sem_pistas);
    sem_destroy(&sem_portoes);
    sem_destroy(&sem_torre);
    pthread_mutex_destroy(&mutex_output);
    pthread_mutex_destroy(&mutex_estatisticas);
    pthread_mutex_destroy(&mutex_aviao);
}

void criar_avioes() {
    while (simulacao_ativa && contador_avioes < MAX_AVIOES) {
        pthread_mutex_lock(&mutex_aviao);
        
        aviao_t* novo_aviao = &avioes[contador_avioes];
        novo_aviao->id = contador_avioes + 1;
        novo_aviao->tipo = (rand() % 2 == 0) ? VOO_DOMESTICO : VOO_INTERNACIONAL;
        novo_aviao->estado = ESPERANDO_POUSO;
        novo_aviao->tempo_criacao = time(NULL);
        novo_aviao->tempo_inicio_espera = time(NULL);
        novo_aviao->alerta_critico = 0;
        novo_aviao->crashed = 0;
        
        int result = pthread_create(&novo_aviao->thread, NULL, aviao_thread, (void*)novo_aviao);
        if (result != 0) {
            printf("Erro ao criar thread do avião %d\n", novo_aviao->id);
            pthread_mutex_unlock(&mutex_aviao);
            continue;
        }
        
        imprimir_status("CRIADO", novo_aviao);
        contador_avioes++;
        
        pthread_mutex_unlock(&mutex_aviao);
        
        // Intervalo menor para teste (1-3 segundos)
        sleep(rand() % 3 + 1);
    }
}

void* aviao_thread(void* arg) {
    aviao_t* aviao = (aviao_t*)arg;
    
    while (aviao->estado != FINALIZADO && aviao->estado != CRASHED && simulacao_ativa) {
        verificar_timeout(aviao);
        
        if (aviao->crashed) {
            aviao->estado = CRASHED;
            break;
        }
        
        switch (aviao->estado) {
            case ESPERANDO_POUSO:
                pouso(aviao);
                break;
            case ESPERANDO_DESEMBARQUE:
                desembarque(aviao);
                break;
            case ESPERANDO_DECOLAGEM:
                decolagem(aviao);
                break;
            default:
                break;
        }
        
        sleep(1); // Pequena pausa
    }
    
    return NULL;
}

void pouso(aviao_t* aviao) {
    imprimir_status("SOLICITANDO RECURSOS PARA POUSO", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Pista → Torre
        imprimir_status("Aguardando PISTA (prioridade internacional)", aviao);
        sem_wait(&sem_pistas);
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        sem_wait(&sem_torre);
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Pista
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        sem_wait(&sem_torre);
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando PISTA", aviao);
        sem_wait(&sem_pistas);
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
    }
    
    aviao->estado = POUSANDO;
    aviao->tempo_inicio_espera = time(NULL);
    imprimir_status("EXECUTANDO POUSO", aviao);
    
    // Simula tempo de pouso
    sleep(rand() % 2 + 1);
    
    // Libera recursos do pouso
    pthread_mutex_lock(&mutex_estatisticas);
    pistas_em_uso--;
    torre_operacoes_ativas--;
    pthread_mutex_unlock(&mutex_estatisticas);
    
    sem_post(&sem_pistas);
    sem_post(&sem_torre);
    imprimir_status_recursos("PISTA e TORRE LIBERADAS", aviao);
    
    aviao->estado = ESPERANDO_DESEMBARQUE;
    pthread_mutex_lock(&mutex_estatisticas);
    pthread_mutex_unlock(&mutex_estatisticas);
    
    imprimir_status("POUSO CONCLUÍDO COM SUCESSO", aviao);
}

void desembarque(aviao_t* aviao) {
    imprimir_status("SOLICITANDO RECURSOS PARA DESEMBARQUE", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Portão → Torre
        imprimir_status("Aguardando PORTÃO DE EMBARQUE (prioridade internacional)", aviao);
        sem_wait(&sem_portoes);
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PORTÃO ADQUIRIDO", aviao);
        
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        sem_wait(&sem_torre);
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Portão
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        sem_wait(&sem_torre);
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando PORTÃO DE EMBARQUE", aviao);
        sem_wait(&sem_portoes);
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PORTÃO ADQUIRIDO", aviao);
    }
    
    aviao->estado = DESEMBARCANDO;
    aviao->tempo_inicio_espera = time(NULL);
    imprimir_status("EXECUTANDO DESEMBARQUE DE PASSAGEIROS", aviao);
    
    // Simula tempo de desembarque
    sleep(rand() % 3 + 1);
    
    // Libera torre primeiro, mas mantém portão
    pthread_mutex_lock(&mutex_estatisticas);
    torre_operacoes_ativas--;
    pthread_mutex_unlock(&mutex_estatisticas);
    
    sem_post(&sem_torre);
    imprimir_status_recursos("TORRE LIBERADA (portão mantido)", aviao);
    
    aviao->estado = ESPERANDO_DECOLAGEM;
    imprimir_status("DESEMBARQUE CONCLUÍDO - AGUARDANDO DECOLAGEM", aviao);
}

void decolagem(aviao_t* aviao) {
    imprimir_status("SOLICITANDO RECURSOS PARA DECOLAGEM", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Pista → Torre (portão já ocupado)
        imprimir_status("Aguardando PISTA (prioridade internacional)", aviao);
        sem_wait(&sem_pistas);
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        sem_wait(&sem_torre);
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Pista (portão já ocupado)
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        sem_wait(&sem_torre);
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando PISTA", aviao);
        sem_wait(&sem_pistas);
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
    }
    
    aviao->estado = DECOLANDO;
    imprimir_status("EXECUTANDO DECOLAGEM", aviao);
    
    // Simula tempo de decolagem
    sleep(rand() % 2 + 1);
    
    // Libera todos os recursos
    pthread_mutex_lock(&mutex_estatisticas);
    portoes_em_uso--;
    pistas_em_uso--;
    torre_operacoes_ativas--;
    pthread_mutex_unlock(&mutex_estatisticas);
    
    sem_post(&sem_portoes);
    sem_post(&sem_pistas);
    sem_post(&sem_torre);
    imprimir_status_recursos("TODOS OS RECURSOS LIBERADOS", aviao);
    
    aviao->estado = FINALIZADO;
    pthread_mutex_lock(&mutex_estatisticas);
    pthread_mutex_unlock(&mutex_estatisticas);
    
    imprimir_status("DECOLAGEM CONCLUÍDA - AVIÃO FINALIZADO", aviao);
}

void verificar_timeout(aviao_t* aviao) {
    double tempo_espera = tempo_decorrido(aviao->tempo_inicio_espera);
    
    if (tempo_espera > ALERTA_CRITICO && !aviao->alerta_critico) {
        aviao->alerta_critico = 1;
        pthread_mutex_lock(&mutex_estatisticas);
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status("ALERTA CRÍTICO - 40s de espera! POSSÍVEL STARVATION!", aviao);
        
        // Verificar se é voo doméstico sendo prejudicado por internacionais
        if (aviao->tipo == VOO_DOMESTICO) {
            int voos_int_ativos = 0;
            for (int i = 0; i < contador_avioes; i++) {
                if (avioes[i].tipo == VOO_INTERNACIONAL && 
                    avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
                    voos_int_ativos++;
                }
            }
            if (voos_int_ativos > 0) {
                printf("     └─ STARVATION DETECTADA: Voo doméstico aguardando com %d voos internacionais ativos\n", voos_int_ativos);
            }
        }
    }
    
    if (tempo_espera > TEMPO_CRASH && !aviao->crashed) {
        aviao->crashed = 1;
        pthread_mutex_lock(&mutex_estatisticas);
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status("CRASH SIMULADO - 90s de espera! THREAD FINALIZADA!", aviao);
        
        printf("     └─ FALHA OPERACIONAL: Avião %s %d crashou por timeout excessivo!\n", 
               (aviao->tipo == VOO_DOMESTICO) ? "doméstico" : "internacional", aviao->id);
        
        // Se for voo doméstico, pode indicar starvation severa
        if (aviao->tipo == VOO_DOMESTICO) {
            printf("     └─ STARVATION SEVERA: Voo doméstico não conseguiu recursos devido à prioridade internacional!\n");
        }
    }
}

void imprimir_status(const char* msg, aviao_t* aviao) {
    pthread_mutex_lock(&mutex_output);
    
    const char* tipo_str = (aviao->tipo == VOO_DOMESTICO) ? "DOM" : "INT";
    double tempo_sim = tempo_decorrido(inicio_simulacao);
    
    printf("[%.1fs] Avião %d (%s): %s\n", 
           tempo_sim, aviao->id, tipo_str, msg);
    
    pthread_mutex_unlock(&mutex_output);
}

void imprimir_status_recursos(const char* operacao, aviao_t* aviao) {
    pthread_mutex_lock(&mutex_output);
    
    const char* tipo_str = (aviao->tipo == VOO_DOMESTICO) ? "DOM" : "INT";
    double tempo_sim = tempo_decorrido(inicio_simulacao);
    
    printf("[%.1fs] Avião %d (%s): %s\n", 
           tempo_sim, aviao->id, tipo_str, operacao);
    
    // Mostrar estado atual dos recursos
    printf("     └─ Recursos: Pistas %d/%d | Portões %d/%d | Torre %d/%d\n",
           pistas_em_uso, NUM_PISTAS, portoes_em_uso, NUM_PORTOES, 
           torre_operacoes_ativas, MAX_TORRE_OPERACOES);
    
    pthread_mutex_unlock(&mutex_output);
}

void detectar_deadlock() {
    while (simulacao_ativa) {
        sleep(10); // Verificar a cada 10 segundos
        
        if (!simulacao_ativa) break;
        
        pthread_mutex_lock(&mutex_output);
        
        // Contar aviões esperando por muito tempo
        int avioes_esperando_muito = 0;
        int voos_dom_bloqueados = 0;
        int voos_int_bloqueados = 0;
        
        for (int i = 0; i < contador_avioes; i++) {
            if (avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
                double tempo_espera = tempo_decorrido(avioes[i].tempo_inicio_espera);
                if (tempo_espera > 20) { // Mais de 20 segundos esperando
                    avioes_esperando_muito++;
                    if (avioes[i].tipo == VOO_DOMESTICO) {
                        voos_dom_bloqueados++;
                    } else {
                        voos_int_bloqueados++;
                    }
                }
            }
        }
        
        if (avioes_esperando_muito > 3) {
            printf("\n🚨 POSSÍVEL DEADLOCK DETECTADO!\n");
            printf("   - %d aviões esperando há mais de 20 segundos\n", avioes_esperando_muito);
            printf("   - Voos domésticos bloqueados: %d\n", voos_dom_bloqueados);
            printf("   - Voos internacionais bloqueados: %d\n", voos_int_bloqueados);
            
            pthread_mutex_lock(&mutex_estatisticas);
            pthread_mutex_unlock(&mutex_estatisticas);
            
            imprimir_estado_recursos();
        }
        
        pthread_mutex_unlock(&mutex_output);
    }
}

void imprimir_estado_recursos() {
    printf("\nESTADO ATUAL DOS RECURSOS:\n");
    printf("   Pistas: %d/%d em uso\n", pistas_em_uso, NUM_PISTAS);
    printf("   Portões: %d/%d em uso\n", portoes_em_uso, NUM_PORTOES);
    printf("   Torre: %d/%d operações ativas\n", torre_operacoes_ativas, MAX_TORRE_OPERACOES);
    
    printf("\nAVIÕES ATIVOS POR ESTADO:\n");
    int estados[8] = {0}; // Para cada estado
    
    for (int i = 0; i < contador_avioes; i++) {
        if (avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
            estados[avioes[i].estado]++;
        }
    }
    
    if (estados[ESPERANDO_POUSO] > 0) 
        printf("   Esperando pouso: %d\n", estados[ESPERANDO_POUSO]);
    if (estados[POUSANDO] > 0) 
        printf("   Pousando: %d\n", estados[POUSANDO]);
    if (estados[ESPERANDO_DESEMBARQUE] > 0) 
        printf("   Esperando desembarque: %d\n", estados[ESPERANDO_DESEMBARQUE]);
    if (estados[DESEMBARCANDO] > 0) 
        printf("   Desembarcando: %d\n", estados[DESEMBARCANDO]);
    if (estados[ESPERANDO_DECOLAGEM] > 0) 
        printf("   Esperando decolagem: %d\n", estados[ESPERANDO_DECOLAGEM]);
    if (estados[DECOLANDO] > 0) 
        printf("   Decolando: %d\n", estados[DECOLANDO]);
    
    printf("\n");
}

double tempo_decorrido(time_t inicio) {
    return difftime(time(NULL), inicio);
}