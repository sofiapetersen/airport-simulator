#define _GNU_SOURCE  // Para pthread_timedjoin_np
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>  // Para ETIMEDOUT

// ========== CÓDIGOS ANSI PARA CORES ==========
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define UNDERLINE   "\033[4m"
#define BLINK       "\033[5m"

// Cores básicas
#define BLACK       "\033[30m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"

// Cores brilhantes
#define BRIGHT_BLACK    "\033[90m"
#define BRIGHT_RED      "\033[91m"
#define BRIGHT_GREEN    "\033[92m"
#define BRIGHT_YELLOW   "\033[93m"
#define BRIGHT_BLUE     "\033[94m"
#define BRIGHT_MAGENTA  "\033[95m"
#define BRIGHT_CYAN     "\033[96m"
#define BRIGHT_WHITE    "\033[97m"

// Cores de fundo
#define BG_RED      "\033[41m"
#define BG_GREEN    "\033[42m"
#define BG_YELLOW   "\033[43m"
#define BG_BLUE     "\033[44m"

// ========== CORES ESPECÍFICAS DO SISTEMA ==========
#define COR_DOMESTICO       BRIGHT_BLUE
#define COR_INTERNACIONAL   BRIGHT_MAGENTA
#define COR_POUSO          BRIGHT_GREEN
#define COR_DECOLAGEM      BRIGHT_CYAN
#define COR_DESEMBARQUE    YELLOW
#define COR_ALERTA         BOLD YELLOW BG_RED
#define COR_CRASH          BOLD BRIGHT_RED BLINK
#define COR_SUCESSO        BRIGHT_GREEN
#define COR_RECURSOS       CYAN
#define COR_DEADLOCK       BOLD RED BG_YELLOW
#define COR_TITULO         BOLD BRIGHT_WHITE
#define COR_SUBTITULO      BOLD CYAN
#define COR_TEMPO          BRIGHT_BLACK
#define COR_CRIADO         GREEN
#define COR_FINALIZADO     BRIGHT_GREEN

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
    int thread_ativa; // Flag para indicar se a thread está ativa
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
const char* obter_cor_por_operacao(const char* msg);
const char* obter_cor_tipo_aviao(tipo_voo_t tipo);
void imprimir_cabecalho();
void forcar_finalizacao_threads(); // Nova função

int main() {
    imprimir_cabecalho();

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
    
    printf("\n" COR_TITULO "═══ FIM DA SIMULAÇÃO - AGUARDANDO THREADS FINALIZAREM ═══" RESET "\n");
    
    // Aguarda thread criadora e monitor
    pthread_join(thread_criador, NULL);
    pthread_join(thread_monitor, NULL);
    
    // Força a finalização das threads bloqueadas
    forcar_finalizacao_threads();
    
    // Aguarda todas as threads de aviões terminarem com timeout
    for (int i = 0; i < contador_avioes; i++) {
        if (avioes[i].thread_ativa && avioes[i].estado != CRASHED) {
            // Tenta aguardar a thread por 5 segundos
            void* retval;
            struct timespec timeout;
            if (clock_gettime(CLOCK_REALTIME, &timeout) == 0) {
                timeout.tv_sec += 5; // 5 segundos de timeout
                
                int result = pthread_timedjoin_np(avioes[i].thread, &retval, &timeout);
                if (result == ETIMEDOUT) {
                    printf(COR_ALERTA "⚠ Thread do avião %d não finalizou a tempo - forçando cancelamento" RESET "\n", avioes[i].id);
                    pthread_cancel(avioes[i].thread);
                    pthread_join(avioes[i].thread, NULL); // Aguarda o cancelamento
                }
            } else {
                // Se clock_gettime falhar, usa join normal
                pthread_join(avioes[i].thread, NULL);
            }
        }
    }
    
    finalizar_recursos();
    
    printf(COR_TITULO "═══ SIMULAÇÃO FINALIZADA COM SUCESSO ═══" RESET "\n");
    return 0;
}

void forcar_finalizacao_threads() {
    printf(COR_TITULO "═══ FORÇANDO LIBERAÇÃO DE RECURSOS PARA FINALIZAÇÃO ═══" RESET "\n");
    
    // Libera todos os semáforos para desbloquear threads em espera
    int pistas_disponiveis, portoes_disponiveis, torre_disponivel;
    
    // Verifica quantos recursos estão disponíveis
    sem_getvalue(&sem_pistas, &pistas_disponiveis);
    sem_getvalue(&sem_portoes, &portoes_disponiveis);
    sem_getvalue(&sem_torre, &torre_disponivel);
    
    // Libera recursos suficientes para desbloquear todas as threads
    for (int i = 0; i < (NUM_PISTAS - pistas_disponiveis + 10); i++) {
        sem_post(&sem_pistas);
    }
    
    for (int i = 0; i < (NUM_PORTOES - portoes_disponiveis + 10); i++) {
        sem_post(&sem_portoes);
    }
    
    for (int i = 0; i < (MAX_TORRE_OPERACOES - torre_disponivel + 10); i++) {
        sem_post(&sem_torre);
    }
    
    printf(COR_SUCESSO "✓ Recursos liberados para permitir finalização das threads" RESET "\n");
    
    // Aguarda um pouco para as threads processarem
    sleep(2);
}

void imprimir_cabecalho() {
    printf("\n");
    printf(COR_TITULO "╔══════════════════════════════════════════════════════════════╗" RESET "\n");
    printf(COR_TITULO "║" RESET COR_SUBTITULO "        SIMULAÇÃO DE CONTROLE DE TRÁFEGO AÉREO       " RESET COR_TITULO "║" RESET "\n");
    printf(COR_TITULO "╚══════════════════════════════════════════════════════════════╝" RESET "\n");
    printf(COR_RECURSOS "Recursos: " RESET "%d pistas, %d portões, %d operações simultâneas na torre\n",
           NUM_PISTAS, NUM_PORTOES, MAX_TORRE_OPERACOES);
    printf(COR_RECURSOS "Tempo de simulação: " RESET "%d segundos\n", TEMPO_SIMULACAO);
    printf(COR_RECURSOS "Legenda: " RESET COR_DOMESTICO "DOM" RESET " = Doméstico | " COR_INTERNACIONAL "INT" RESET " = Internacional\n\n");
}

void inicializar_recursos() {
    if (sem_init(&sem_pistas, 0, NUM_PISTAS) != 0) {
        perror(RED "Erro ao inicializar semáforo de pistas" RESET);
        exit(1);
    }
    if (sem_init(&sem_portoes, 0, NUM_PORTOES) != 0) {
        perror(RED "Erro ao inicializar semáforo de portões" RESET);
        exit(1);
    }
    if (sem_init(&sem_torre, 0, MAX_TORRE_OPERACOES) != 0) {
        perror(RED "Erro ao inicializar semáforo de torre" RESET);
        exit(1);
    }
    printf(COR_SUCESSO "✓ Recursos inicializados com sucesso!" RESET "\n\n");
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
        novo_aviao->thread_ativa = 1; // Marca thread como ativa
        
        int result = pthread_create(&novo_aviao->thread, NULL, aviao_thread, (void*)novo_aviao);
        if (result != 0) {
            printf(RED "✗ Erro ao criar thread do avião %d" RESET "\n", novo_aviao->id);
            novo_aviao->thread_ativa = 0;
            pthread_mutex_unlock(&mutex_aviao);
            continue;
        }
        
        imprimir_status("AVIÃO CRIADO E ENTRANDO NO ESPAÇO AÉREO", novo_aviao);
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
    
    // Se a simulação terminou, força finalização
    if (!simulacao_ativa && aviao->estado != FINALIZADO && aviao->estado != CRASHED) {
        aviao->estado = FINALIZADO;
        imprimir_status("FORÇADO A FINALIZAR - FIM DA SIMULAÇÃO", aviao);
    }
    
    aviao->thread_ativa = 0; // Marca thread como inativa
    return NULL;
}

void pouso(aviao_t* aviao) {
    // Verifica se simulação ainda está ativa antes de solicitar recursos
    if (!simulacao_ativa) {
        aviao->estado = FINALIZADO;
        return;
    }
    
    imprimir_status(" SOLICITANDO RECURSOS PARA POUSO", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Pista → Torre
        imprimir_status(" Aguardando PISTA (prioridade internacional)", aviao);
        if (sem_wait(&sem_pistas) != 0 || !simulacao_ativa) {
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" PISTA ADQUIRIDA", aviao);
        
        imprimir_status(" Aguardando TORRE DE CONTROLE", aviao);
        if (sem_wait(&sem_torre) != 0 || !simulacao_ativa) {
            // Libera a pista antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            pistas_em_uso--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_pistas);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Pista
        imprimir_status(" Aguardando TORRE DE CONTROLE", aviao);
        if (sem_wait(&sem_torre) != 0 || !simulacao_ativa) {
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" TORRE ADQUIRIDA", aviao);
        
        imprimir_status(" Aguardando PISTA", aviao);
        if (sem_wait(&sem_pistas) != 0 || !simulacao_ativa) {
            // Libera a torre antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            torre_operacoes_ativas--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_torre);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" PISTA ADQUIRIDA", aviao);
    }
    
    if (!simulacao_ativa) {
        // Libera recursos e finaliza
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso--;
        torre_operacoes_ativas--;
        pthread_mutex_unlock(&mutex_estatisticas);
        sem_post(&sem_pistas);
        sem_post(&sem_torre);
        aviao->estado = FINALIZADO;
        return;
    }
    
    aviao->estado = POUSANDO;
    aviao->tempo_inicio_espera = time(NULL);
    imprimir_status(" EXECUTANDO POUSO", aviao);
    
    // Simula tempo de pouso
    sleep(rand() % 2 + 1);
    
    // Libera recursos do pouso
    pthread_mutex_lock(&mutex_estatisticas);
    pistas_em_uso--;
    torre_operacoes_ativas--;
    pthread_mutex_unlock(&mutex_estatisticas);
    
    sem_post(&sem_pistas);
    sem_post(&sem_torre);
    imprimir_status_recursos(" PISTA e TORRE LIBERADAS", aviao);
    
    aviao->estado = ESPERANDO_DESEMBARQUE;
    imprimir_status(" POUSO CONCLUÍDO COM SUCESSO", aviao);
}

void desembarque(aviao_t* aviao) {
    if (!simulacao_ativa) {
        aviao->estado = FINALIZADO;
        return;
    }
    
    imprimir_status(" SOLICITANDO RECURSOS PARA DESEMBARQUE", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Portão → Torre
        imprimir_status(" Aguardando PORTÃO DE EMBARQUE (prioridade internacional)", aviao);
        if (sem_wait(&sem_portoes) != 0 || !simulacao_ativa) {
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" PORTÃO ADQUIRIDO", aviao);
        
        imprimir_status(" Aguardando TORRE DE CONTROLE", aviao);
        if (sem_wait(&sem_torre) != 0 || !simulacao_ativa) {
            // Libera o portão antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            portoes_em_uso--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_portoes);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Portão
        imprimir_status(" Aguardando TORRE DE CONTROLE", aviao);
        if (sem_wait(&sem_torre) != 0 || !simulacao_ativa) {
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" TORRE ADQUIRIDA", aviao);
        
        imprimir_status(" Aguardando PORTÃO DE EMBARQUE", aviao);
        if (sem_wait(&sem_portoes) != 0 || !simulacao_ativa) {
            // Libera a torre antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            torre_operacoes_ativas--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_torre);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" PORTÃO ADQUIRIDO", aviao);
    }
    
    if (!simulacao_ativa) {
        // Libera recursos e finaliza
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso--;
        torre_operacoes_ativas--;
        pthread_mutex_unlock(&mutex_estatisticas);
        sem_post(&sem_portoes);
        sem_post(&sem_torre);
        aviao->estado = FINALIZADO;
        return;
    }
    
    aviao->estado = DESEMBARCANDO;
    aviao->tempo_inicio_espera = time(NULL);
    imprimir_status(" EXECUTANDO DESEMBARQUE DE PASSAGEIROS", aviao);
    
    // Simula tempo de desembarque
    sleep(rand() % 3 + 1);
    
    // Libera torre primeiro, mas mantém portão
    pthread_mutex_lock(&mutex_estatisticas);
    torre_operacoes_ativas--;
    pthread_mutex_unlock(&mutex_estatisticas);
    
    sem_post(&sem_torre);
    imprimir_status_recursos(" TORRE LIBERADA (portão mantido)", aviao);
    
    aviao->estado = ESPERANDO_DECOLAGEM;
    imprimir_status(" DESEMBARQUE CONCLUÍDO - AGUARDANDO DECOLAGEM", aviao);
}

void decolagem(aviao_t* aviao) {
    if (!simulacao_ativa) {
        // Libera o portão que ainda está em uso
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso--;
        pthread_mutex_unlock(&mutex_estatisticas);
        sem_post(&sem_portoes);
        aviao->estado = FINALIZADO;
        return;
    }
    
    imprimir_status(" SOLICITANDO RECURSOS PARA DECOLAGEM", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Pista → Torre (portão já ocupado)
        imprimir_status(" Aguardando PISTA (prioridade internacional)", aviao);
        if (sem_wait(&sem_pistas) != 0 || !simulacao_ativa) {
            // Libera o portão antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            portoes_em_uso--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_portoes);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" PISTA ADQUIRIDA", aviao);
        
        imprimir_status(" Aguardando TORRE DE CONTROLE", aviao);
        if (sem_wait(&sem_torre) != 0 || !simulacao_ativa) {
            // Libera pista e portão antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            pistas_em_uso--;
            portoes_em_uso--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_pistas);
            sem_post(&sem_portoes);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Pista (portão já ocupado)
        imprimir_status(" Aguardando TORRE DE CONTROLE", aviao);
        if (sem_wait(&sem_torre) != 0 || !simulacao_ativa) {
            // Libera o portão antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            portoes_em_uso--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_portoes);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" TORRE ADQUIRIDA", aviao);
        
        imprimir_status(" Aguardando PISTA", aviao);
        if (sem_wait(&sem_pistas) != 0 || !simulacao_ativa) {
            // Libera torre e portão antes de finalizar
            pthread_mutex_lock(&mutex_estatisticas);
            torre_operacoes_ativas--;
            portoes_em_uso--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_torre);
            sem_post(&sem_portoes);
            aviao->estado = FINALIZADO;
            return;
        }
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos(" PISTA ADQUIRIDA", aviao);
    }
    
    if (!simulacao_ativa) {
        // Libera todos os recursos e finaliza
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso--;
        pistas_em_uso--;
        torre_operacoes_ativas--;
        pthread_mutex_unlock(&mutex_estatisticas);
        sem_post(&sem_portoes);
        sem_post(&sem_pistas);
        sem_post(&sem_torre);
        aviao->estado = FINALIZADO;
        return;
    }
    
    aviao->estado = DECOLANDO;
    imprimir_status(" EXECUTANDO DECOLAGEM", aviao);
    
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
    imprimir_status_recursos(" TODOS OS RECURSOS LIBERADOS", aviao);
    
    aviao->estado = FINALIZADO;
    imprimir_status(" DECOLAGEM CONCLUÍDA - AVIÃO FINALIZADO", aviao);
}

void verificar_timeout(aviao_t* aviao) {
    double tempo_espera = tempo_decorrido(aviao->tempo_inicio_espera);
    
    if (tempo_espera > ALERTA_CRITICO && !aviao->alerta_critico) {
        aviao->alerta_critico = 1;
        imprimir_status(" ALERTA CRÍTICO - 60s de espera! POSSÍVEL STARVATION!", aviao);
        
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
                printf(COR_ALERTA "     └─  STARVATION DETECTADA: Voo doméstico aguardando com %d voos internacionais ativos" RESET "\n", voos_int_ativos);
            }
        }
    }
    
    if (tempo_espera > TEMPO_CRASH && !aviao->crashed) {
        aviao->crashed = 1;
        imprimir_status("💥 CRASH SIMULADO - 90s de espera! THREAD FINALIZADA!", aviao);
        
        printf(COR_CRASH "     └─  FALHA OPERACIONAL: Avião %s %d crashou por timeout excessivo!" RESET "\n", 
               (aviao->tipo == VOO_DOMESTICO) ? "doméstico" : "internacional", aviao->id);
        
        // Se for voo doméstico, pode indicar starvation severa
        if (aviao->tipo == VOO_DOMESTICO) {
            printf(COR_CRASH "     └─  STARVATION SEVERA: Voo doméstico não conseguiu recursos devido à prioridade internacional!" RESET "\n");
        }
    }
}

const char* obter_cor_por_operacao(const char* msg) {
    if (strstr(msg, "POUSO") ) return COR_POUSO;
    if (strstr(msg, "DECOLAGEM")) return COR_DECOLAGEM;
    if (strstr(msg, "DESEMBARQUE")) return COR_DESEMBARQUE;
    if (strstr(msg, "ALERTA") ) return COR_ALERTA;
    if (strstr(msg, "CRASH")) return COR_CRASH;
    if (strstr(msg, "CRIADO")) return COR_CRIADO;
    if (strstr(msg, "FINALIZADO")) return COR_FINALIZADO;
    if (strstr(msg, "SUCESSO")) return COR_SUCESSO;
    if (strstr(msg, "LIBERADA")) return COR_RECURSOS;
    if (strstr(msg, "ADQUIRIDA")) return COR_RECURSOS;
    if (strstr(msg, "Aguardando")) return YELLOW;
    return RESET;
}

const char* obter_cor_tipo_aviao(tipo_voo_t tipo) {
    return (tipo == VOO_DOMESTICO) ? COR_DOMESTICO : COR_INTERNACIONAL;
}

void imprimir_status(const char* msg, aviao_t* aviao) {
    pthread_mutex_lock(&mutex_output);
    
    const char* tipo_str = (aviao->tipo == VOO_DOMESTICO) ? "DOM" : "INT";
    const char* cor_tipo = obter_cor_tipo_aviao(aviao->tipo);
    const char* cor_msg = obter_cor_por_operacao(msg);
    double tempo_sim = tempo_decorrido(inicio_simulacao);
    
    printf(COR_TEMPO "[%.1fs]" RESET " Avião %s%d (%s)%s: %s%s%s\n", 
           tempo_sim, cor_tipo, aviao->id, tipo_str, RESET, cor_msg, msg, RESET);
    
    pthread_mutex_unlock(&mutex_output);
}

void imprimir_status_recursos(const char* operacao, aviao_t* aviao) {
    pthread_mutex_lock(&mutex_output);
    
    const char* tipo_str = (aviao->tipo == VOO_DOMESTICO) ? "DOM" : "INT";
    const char* cor_tipo = obter_cor_tipo_aviao(aviao->tipo);
    const char* cor_operacao = obter_cor_por_operacao(operacao);
    double tempo_sim = tempo_decorrido(inicio_simulacao);
    
    printf(COR_TEMPO "[%.1fs]" RESET " Avião %s%d (%s)%s: %s%s%s\n", 
           tempo_sim, cor_tipo, aviao->id, tipo_str, RESET, cor_operacao, operacao, RESET);
    
    // Mostrar estado atual dos recursos com cores
    printf(COR_RECURSOS "     └─  Recursos: " RESET "Pistas " BRIGHT_BLUE "%d/%d" RESET " | Portões " BRIGHT_MAGENTA "%d/%d" RESET " | Torre " BRIGHT_GREEN "%d/%d" RESET "\n",
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
            printf("\n" COR_DEADLOCK " POSSÍVEL DEADLOCK DETECTADO!" RESET "\n");
            printf(COR_DEADLOCK "   - %d aviões esperando há mais de 20 segundos" RESET "\n", avioes_esperando_muito);
            printf(COR_DEADLOCK "   - Voos domésticos bloqueados: %d" RESET "\n", voos_dom_bloqueados);
            printf(COR_DEADLOCK "   - Voos internacionais bloqueados: %d" RESET "\n", voos_int_bloqueados);
            
            imprimir_estado_recursos();
        }
        
        pthread_mutex_unlock(&mutex_output);
    }
}

void imprimir_estado_recursos() {
    printf("\n" COR_SUBTITULO " ESTADO ATUAL DOS RECURSOS:" RESET "\n");
    printf(COR_RECURSOS "    Pistas: " RESET BRIGHT_BLUE "%d/%d" RESET " em uso\n", pistas_em_uso, NUM_PISTAS);
    printf(COR_RECURSOS "   Portões: " RESET BRIGHT_MAGENTA "%d/%d" RESET " em uso\n", portoes_em_uso, NUM_PORTOES);
    printf(COR_RECURSOS "   Torre: " RESET BRIGHT_GREEN "%d/%d" RESET " operações ativas\n", torre_operacoes_ativas, MAX_TORRE_OPERACOES);
    
    printf("\n" COR_SUBTITULO "  AVIÕES ATIVOS POR ESTADO:" RESET "\n");
    int estados[8] = {0}; // Para cada estado
    
    for (int i = 0; i < contador_avioes; i++) {
        if (avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
            estados[avioes[i].estado]++;
        }
    }
    
    if (estados[ESPERANDO_POUSO] > 0) 
        printf(COR_RECURSOS "   Esperando pouso: " RESET "%d\n", estados[ESPERANDO_POUSO]);
    if (estados[POUSANDO] > 0) 
        printf(COR_POUSO "   Pousando: " RESET "%d\n", estados[POUSANDO]);
    if (estados[ESPERANDO_DESEMBARQUE] > 0) 
        printf(COR_RECURSOS "   Esperando desembarque: " RESET "%d\n", estados[ESPERANDO_DESEMBARQUE]);
    if (estados[DESEMBARCANDO] > 0) 
        printf(COR_DESEMBARQUE "   Desembarcando: " RESET "%d\n", estados[DESEMBARCANDO]);
    if (estados[ESPERANDO_DECOLAGEM] > 0) 
        printf(COR_RECURSOS "   Esperando decolagem: " RESET "%d\n", estados[ESPERANDO_DECOLAGEM]);
    if (estados[DECOLANDO] > 0) 
        printf(COR_DECOLAGEM "   Decolando: " RESET "%d\n", estados[DECOLANDO]);
    
    printf("\n");
}

double tempo_decorrido(time_t inicio) {
    return difftime(time(NULL), inicio);
}