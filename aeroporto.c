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
#define COR_STARVATION     BOLD YELLOW

// configuracoes do aeroporto
#define NUM_PISTAS 3
#define NUM_PORTOES 5
#define MAX_TORRE_OPERACOES 2
#define TEMPO_SIMULACAO 150  // 5 minutos = 300 segundos
#define ALERTA_CRITICO 60    
#define TEMPO_CRASH 90       
#define MAX_AVIOES 100       

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
    int tempo_total_operacao; // Para estatísticas
} aviao_t;

typedef struct {
    int avioes_criados;
    int avioes_finalizados_sucesso;
    int avioes_crashed;
    int pousos_realizados;
    int decolagens_realizadas;
    int desembarques_realizados;
    int voos_domesticos_total;
    int voos_internacionais_total;
    int voos_domesticos_finalizados;
    int voos_internacionais_finalizados;
    int voos_domesticos_crashed;
    int voos_internacionais_crashed;
    int alertas_criticos_emitidos;
    int casos_starvation_detectados;
    int possiveis_deadlocks_detectados;
    double tempo_medio_ciclo_completo;
    double tempo_maximo_espera;
    int recursos_maximos_utilizados_pistas;
    int recursos_maximos_utilizados_portoes;
    int recursos_maximos_utilizados_torre;
} estatisticas_simulacao_t;

estatisticas_simulacao_t stats = {0};

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
int criacao_avioes_ativa = 1; // Controla apenas a criação de novos aviões
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
void atualizar_estatisticas(aviao_t* aviao, const char* evento);
void imprimir_relatorio_final();
void aguardar_threads_finalizarem();
void imprimir_resumo_avioes();
const char* obter_nome_estado(estado_aviao_t estado);

int main() {
    imprimir_cabecalho();

    srand(time(NULL));
    inicio_simulacao = time(NULL);
    
    inicializar_recursos();
    
    // Criar thread para gerar aviões
    pthread_t thread_criador;
    pthread_create(&thread_criador, NULL, (void*)criar_avioes, NULL);
    
    // Thread para monitoramento de deadlocks a cada 15 segundos
    pthread_t thread_monitor;
    pthread_create(&thread_monitor, NULL, (void*)detectar_deadlock, NULL);
    
    // Aguarda o tempo de simulação
    sleep(TEMPO_SIMULACAO);
    
    // Para apenas a criação de novos aviões
    criacao_avioes_ativa = 0;
    
    printf("\n" COR_TITULO "═══ TEMPO DE SIMULAÇÃO ENCERRADO - PARANDO CRIAÇÃO DE NOVOS AVIÕES ═══" RESET "\n");
    printf(COR_SUBTITULO "Aguardando threads ativas finalizarem suas operações..." RESET "\n\n");
    
    // Aguarda thread criadora e monitor
    pthread_join(thread_criador, NULL);
    pthread_join(thread_monitor, NULL);
    
    // Aguarda todas as threads de aviões terminarem naturalmente
    aguardar_threads_finalizarem();
    
    finalizar_recursos();
    imprimir_resumo_avioes();
    imprimir_relatorio_final();
    
    printf(COR_TITULO "═══ SIMULAÇÃO FINALIZADA COM SUCESSO ═══" RESET "\n");
    return 0;
}

void aguardar_threads_finalizarem() {
    printf(COR_TITULO "═══ AGUARDANDO THREADS FINALIZAREM SUAS OPERAÇÕES ═══" RESET "\n");
    
    int threads_ativas = 0;
    do {
        threads_ativas = 0;
        for (int i = 0; i < contador_avioes; i++) {
            if (avioes[i].thread_ativa && 
                avioes[i].estado != FINALIZADO && 
                avioes[i].estado != CRASHED) {
                threads_ativas++;
            }
        }
        
        if (threads_ativas > 0) {
            printf(COR_SUBTITULO "Threads ainda ativas: %d" RESET "\n", threads_ativas);
            sleep(5); // Verifica a cada 5 segundos
        }
    } while (threads_ativas > 0);
    
    // Agora faz join em todas as threads
    for (int i = 0; i < contador_avioes; i++) {
        if (avioes[i].thread_ativa) {
            pthread_join(avioes[i].thread, NULL);
        }
    }
    
    printf(COR_SUCESSO "✓ Todas as threads finalizaram suas operações" RESET "\n\n");
}

void imprimir_resumo_avioes() {
    printf(COR_TITULO "═══ RESUMO FINAL DO ESTADO DOS AVIÕES ═══" RESET "\n\n");
    
    int avioes_por_estado[8] = {0}; // Para cada estado
    
    for (int i = 0; i < contador_avioes; i++) {
        avioes_por_estado[avioes[i].estado]++;
    }
    
    printf(COR_SUBTITULO "CONTAGEM POR ESTADO:" RESET "\n");
    if (avioes_por_estado[FINALIZADO] > 0)
        printf(COR_FINALIZADO "  ✓ Finalizados com sucesso: %d aviões" RESET "\n", avioes_por_estado[FINALIZADO]);
    if (avioes_por_estado[CRASHED] > 0)
        printf(COR_CRASH "  ✗ Crashed (timeout): %d aviões" RESET "\n", avioes_por_estado[CRASHED]);
    if (avioes_por_estado[ESPERANDO_POUSO] > 0)
        printf(COR_ALERTA "   Ainda esperando pouso: %d aviões" RESET "\n", avioes_por_estado[ESPERANDO_POUSO]);
    if (avioes_por_estado[ESPERANDO_DESEMBARQUE] > 0)
        printf(COR_ALERTA "   Ainda esperando desembarque: %d aviões" RESET "\n", avioes_por_estado[ESPERANDO_DESEMBARQUE]);
    if (avioes_por_estado[ESPERANDO_DECOLAGEM] > 0)
        printf(COR_ALERTA "   Ainda esperando decolagem: %d aviões" RESET "\n", avioes_por_estado[ESPERANDO_DECOLAGEM]);
    
    printf("\n" COR_SUBTITULO "DETALHES INDIVIDUAIS:" RESET "\n");
    for (int i = 0; i < contador_avioes; i++) {
        const char* cor_tipo = obter_cor_tipo_aviao(avioes[i].tipo);
        const char* tipo_str = (avioes[i].tipo == VOO_DOMESTICO) ? "DOM" : "INT";
        const char* cor_estado = (avioes[i].estado == FINALIZADO) ? COR_FINALIZADO : 
                                (avioes[i].estado == CRASHED) ? COR_CRASH : COR_ALERTA;
        
        double tempo_total = tempo_decorrido(avioes[i].tempo_criacao);
        
        printf("  Avião %s%d (%s)%s: %s%s%s (%.1fs total)\n", 
               cor_tipo, avioes[i].id, tipo_str, RESET,
               cor_estado, obter_nome_estado(avioes[i].estado), RESET,
               tempo_total);
    }
    printf("\n");
}

const char* obter_nome_estado(estado_aviao_t estado) {
    switch (estado) {
        case ESPERANDO_POUSO: return "Esperando Pouso";
        case POUSANDO: return "Pousando";
        case ESPERANDO_DESEMBARQUE: return "Esperando Desembarque";
        case DESEMBARCANDO: return "Desembarcando";
        case ESPERANDO_DECOLAGEM: return "Esperando Decolagem";
        case DECOLANDO: return "Decolando";
        case FINALIZADO: return "Finalizado";
        case CRASHED: return "Crashed";
        default: return "Estado Desconhecido";
    }
}

void imprimir_cabecalho() {
    printf("\n");
    printf(COR_TITULO "╔══════════════════════════════════════════════════════════════╗" RESET "\n");
    printf(COR_TITULO "║" RESET COR_SUBTITULO "        SIMULAÇÃO DE CONTROLE DE TRÁFEGO AÉREO                 " RESET COR_TITULO "║" RESET "\n");
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
    while (criacao_avioes_ativa && contador_avioes < MAX_AVIOES) {
        pthread_mutex_lock(&mutex_aviao);
        
        aviao_t* novo_aviao = &avioes[contador_avioes];
        novo_aviao->id = contador_avioes + 1;
        novo_aviao->tipo = (rand() % 2 == 0) ? VOO_DOMESTICO : VOO_INTERNACIONAL;
        novo_aviao->estado = ESPERANDO_POUSO;
        novo_aviao->tempo_criacao = time(NULL);
        novo_aviao->tempo_inicio_espera = time(NULL);
        novo_aviao->alerta_critico = 0;
        novo_aviao->crashed = 0;
        novo_aviao->thread_ativa = 1;
        
        int result = pthread_create(&novo_aviao->thread, NULL, aviao_thread, (void*)novo_aviao);
        if (result != 0) {
            printf(RED "✗ Erro ao criar thread do avião %d" RESET "\n", novo_aviao->id);
            novo_aviao->thread_ativa = 0;
            pthread_mutex_unlock(&mutex_aviao);
            continue;
        }
        
        imprimir_status("AVIÃO CRIADO E ENTRANDO NO ESPAÇO AÉREO", novo_aviao);
        atualizar_estatisticas(novo_aviao, "CRIADO");
        contador_avioes++;
        
        pthread_mutex_unlock(&mutex_aviao);
        
        // Intervalo randômico entre criações (1-5 segundos)
        sleep(rand() % 5 + 1);
    }
    
    printf(COR_TITULO "═══ CRIAÇÃO DE NOVOS AVIÕES FINALIZADA ═══" RESET "\n");
}

void* aviao_thread(void* arg) {
    aviao_t* aviao = (aviao_t*)arg;
    
    while (aviao->estado != FINALIZADO && aviao->estado != CRASHED) {
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
    
    aviao->thread_ativa = 0;
    return NULL;
}

// Substitua as funções pouso(), desembarque() e decolagem() pelas versões corrigidas:

void pouso(aviao_t* aviao) {
    imprimir_status("SOLICITANDO RECURSOS PARA POUSO", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Pista → Torre
        imprimir_status("Aguardando PISTA (prioridade internacional)", aviao);
        
        // Verificar timeout enquanto aguarda pista
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5; // Timeout de 5 segundos para verificação
        
        while (sem_timedwait(&sem_pistas, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    return;
                }
                // Renovar timeout para próxima tentativa
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                // Outro erro - sair
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        if (pistas_em_uso > stats.recursos_maximos_utilizados_pistas) {
            stats.recursos_maximos_utilizados_pistas = pistas_em_uso;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        
        // Verificar timeout enquanto aguarda torre
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_torre, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    // Liberar pista antes de sair
                    pthread_mutex_lock(&mutex_estatisticas);
                    pistas_em_uso--;
                    pthread_mutex_unlock(&mutex_estatisticas);
                    sem_post(&sem_pistas);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                // Outro erro - liberar pista e sair
                pthread_mutex_lock(&mutex_estatisticas);
                pistas_em_uso--;
                pthread_mutex_unlock(&mutex_estatisticas);
                sem_post(&sem_pistas);
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        if (torre_operacoes_ativas > stats.recursos_maximos_utilizados_torre) {
            stats.recursos_maximos_utilizados_torre = torre_operacoes_ativas;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Pista
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_torre, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        if (torre_operacoes_ativas > stats.recursos_maximos_utilizados_torre) {
            stats.recursos_maximos_utilizados_torre = torre_operacoes_ativas;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando PISTA", aviao);
        
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_pistas, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    // Liberar torre antes de sair
                    pthread_mutex_lock(&mutex_estatisticas);
                    torre_operacoes_ativas--;
                    pthread_mutex_unlock(&mutex_estatisticas);
                    sem_post(&sem_torre);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                pthread_mutex_lock(&mutex_estatisticas);
                torre_operacoes_ativas--;
                pthread_mutex_unlock(&mutex_estatisticas);
                sem_post(&sem_torre);
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        if (pistas_em_uso > stats.recursos_maximos_utilizados_pistas) {
            stats.recursos_maximos_utilizados_pistas = pistas_em_uso;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
    }
    
    aviao->estado = POUSANDO;
    imprimir_status("EXECUTANDO POUSO", aviao);
    
    // Simula tempo de pouso - verificar timeout durante execução
    for (int i = 0; i < (rand() % 3 + 2); i++) {
        sleep(1);
        verificar_timeout(aviao);
        if (aviao->crashed) {
            aviao->estado = CRASHED;
            // Liberar recursos antes de sair
            pthread_mutex_lock(&mutex_estatisticas);
            pistas_em_uso--;
            torre_operacoes_ativas--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_pistas);
            sem_post(&sem_torre);
            return;
        }
    }
    
    // Libera recursos do pouso
    pthread_mutex_lock(&mutex_estatisticas);
    pistas_em_uso--;
    torre_operacoes_ativas--;
    pthread_mutex_unlock(&mutex_estatisticas);
    
    sem_post(&sem_pistas);
    sem_post(&sem_torre);
    imprimir_status_recursos("PISTA e TORRE LIBERADAS", aviao);
    
    aviao->estado = ESPERANDO_DESEMBARQUE;
    aviao->tempo_inicio_espera = time(NULL);
    aviao->alerta_critico = 0;
    imprimir_status("POUSO CONCLUÍDO COM SUCESSO", aviao);
    atualizar_estatisticas(aviao, "POUSO_CONCLUIDO");
}

void desembarque(aviao_t* aviao) {
    imprimir_status("SOLICITANDO RECURSOS PARA DESEMBARQUE", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Portão → Torre
        imprimir_status("Aguardando PORTÃO DE EMBARQUE (prioridade internacional)", aviao);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_portoes, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso++;
        if (portoes_em_uso > stats.recursos_maximos_utilizados_portoes) {
            stats.recursos_maximos_utilizados_portoes = portoes_em_uso;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PORTÃO ADQUIRIDO", aviao);
        
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_torre, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    pthread_mutex_lock(&mutex_estatisticas);
                    portoes_em_uso--;
                    pthread_mutex_unlock(&mutex_estatisticas);
                    sem_post(&sem_portoes);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                pthread_mutex_lock(&mutex_estatisticas);
                portoes_em_uso--;
                pthread_mutex_unlock(&mutex_estatisticas);
                sem_post(&sem_portoes);
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        if (torre_operacoes_ativas > stats.recursos_maximos_utilizados_torre) {
            stats.recursos_maximos_utilizados_torre = torre_operacoes_ativas;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Portão
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_torre, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        if (torre_operacoes_ativas > stats.recursos_maximos_utilizados_torre) {
            stats.recursos_maximos_utilizados_torre = torre_operacoes_ativas;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando PORTÃO DE EMBARQUE", aviao);
        
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_portoes, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    pthread_mutex_lock(&mutex_estatisticas);
                    torre_operacoes_ativas--;
                    pthread_mutex_unlock(&mutex_estatisticas);
                    sem_post(&sem_torre);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                pthread_mutex_lock(&mutex_estatisticas);
                torre_operacoes_ativas--;
                pthread_mutex_unlock(&mutex_estatisticas);
                sem_post(&sem_torre);
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        portoes_em_uso++;
        if (portoes_em_uso > stats.recursos_maximos_utilizados_portoes) {
            stats.recursos_maximos_utilizados_portoes = portoes_em_uso;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PORTÃO ADQUIRIDO", aviao);
    }
    
    aviao->estado = DESEMBARCANDO;
    imprimir_status("EXECUTANDO DESEMBARQUE DE PASSAGEIROS", aviao);
    
    // Simula tempo de desembarque com verificação de timeout
    for (int i = 0; i < (rand() % 4 + 2); i++) {
        sleep(1);
        verificar_timeout(aviao);
        if (aviao->crashed) {
            aviao->estado = CRASHED;
            pthread_mutex_lock(&mutex_estatisticas);
            portoes_em_uso--;
            torre_operacoes_ativas--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_portoes);
            sem_post(&sem_torre);
            return;
        }
    }
    
    // Libera torre primeiro, mas mantém portão
    pthread_mutex_lock(&mutex_estatisticas);
    torre_operacoes_ativas--;
    pthread_mutex_unlock(&mutex_estatisticas);
    
    sem_post(&sem_torre);
    imprimir_status_recursos("TORRE LIBERADA (portão mantido)", aviao);
    
    aviao->estado = ESPERANDO_DECOLAGEM;
    aviao->tempo_inicio_espera = time(NULL);
    aviao->alerta_critico = 0;
    imprimir_status("DESEMBARQUE CONCLUÍDO - AGUARDANDO DECOLAGEM", aviao);
    atualizar_estatisticas(aviao, "DESEMBARQUE_CONCLUIDO");
}

void decolagem(aviao_t* aviao) {
    imprimir_status("SOLICITANDO RECURSOS PARA DECOLAGEM", aviao);
    
    if (aviao->tipo == VOO_INTERNACIONAL) {
        // Voo internacional: Pista → Torre (portão já ocupado)
        imprimir_status("Aguardando PISTA (prioridade internacional)", aviao);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_pistas, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        if (pistas_em_uso > stats.recursos_maximos_utilizados_pistas) {
            stats.recursos_maximos_utilizados_pistas = pistas_em_uso;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_torre, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    pthread_mutex_lock(&mutex_estatisticas);
                    pistas_em_uso--;
                    pthread_mutex_unlock(&mutex_estatisticas);
                    sem_post(&sem_pistas);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                pthread_mutex_lock(&mutex_estatisticas);
                pistas_em_uso--;
                pthread_mutex_unlock(&mutex_estatisticas);
                sem_post(&sem_pistas);
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        if (torre_operacoes_ativas > stats.recursos_maximos_utilizados_torre) {
            stats.recursos_maximos_utilizados_torre = torre_operacoes_ativas;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
    } else {
        // Voo doméstico: Torre → Pista (portão já ocupado)
        imprimir_status("Aguardando TORRE DE CONTROLE", aviao);
        
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_torre, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        torre_operacoes_ativas++;
        if (torre_operacoes_ativas > stats.recursos_maximos_utilizados_torre) {
            stats.recursos_maximos_utilizados_torre = torre_operacoes_ativas;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("TORRE ADQUIRIDA", aviao);
        
        imprimir_status("Aguardando PISTA", aviao);
        
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;
        
        while (sem_timedwait(&sem_pistas, &timeout) != 0) {
            if (errno == ETIMEDOUT) {
                verificar_timeout(aviao);
                if (aviao->crashed) {
                    aviao->estado = CRASHED;
                    pthread_mutex_lock(&mutex_estatisticas);
                    torre_operacoes_ativas--;
                    pthread_mutex_unlock(&mutex_estatisticas);
                    sem_post(&sem_torre);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += 5;
            } else {
                pthread_mutex_lock(&mutex_estatisticas);
                torre_operacoes_ativas--;
                pthread_mutex_unlock(&mutex_estatisticas);
                sem_post(&sem_torre);
                return;
            }
        }
        
        pthread_mutex_lock(&mutex_estatisticas);
        pistas_em_uso++;
        if (pistas_em_uso > stats.recursos_maximos_utilizados_pistas) {
            stats.recursos_maximos_utilizados_pistas = pistas_em_uso;
        }
        pthread_mutex_unlock(&mutex_estatisticas);
        imprimir_status_recursos("PISTA ADQUIRIDA", aviao);
    }
    
    aviao->estado = DECOLANDO;
    imprimir_status("EXECUTANDO DECOLAGEM", aviao);
    
    // Simula tempo de decolagem com verificação de timeout
    for (int i = 0; i < (rand() % 3 + 2); i++) {
        sleep(1);
        verificar_timeout(aviao);
        if (aviao->crashed) {
            aviao->estado = CRASHED;
            pthread_mutex_lock(&mutex_estatisticas);
            portoes_em_uso--;
            pistas_em_uso--;
            torre_operacoes_ativas--;
            pthread_mutex_unlock(&mutex_estatisticas);
            sem_post(&sem_portoes);
            sem_post(&sem_pistas);
            sem_post(&sem_torre);
            return;
        }
    }
    
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
    imprimir_status("DECOLAGEM CONCLUÍDA - AVIÃO FINALIZADO", aviao);
    atualizar_estatisticas(aviao, "DECOLAGEM_CONCLUIDA");
    atualizar_estatisticas(aviao, "FINALIZADO");
}

void verificar_timeout(aviao_t* aviao) {
    double tempo_espera = tempo_decorrido(aviao->tempo_inicio_espera);
    
    // ALERTA CRÍTICO após 60 segundos de espera
    if (tempo_espera > ALERTA_CRITICO && !aviao->alerta_critico) {
        aviao->alerta_critico = 1;
        imprimir_status(" ALERTA CRÍTICO - 60s de espera! POSSÍVEL STARVATION!", aviao);
        atualizar_estatisticas(aviao, "ALERTA_CRITICO");
        
        // Analisar se é starvation (especialmente para voos domésticos)
        if (aviao->tipo == VOO_DOMESTICO) {
            int voos_int_ativos = 0;
            int voos_int_usando_recursos = 0;
            
            // Contar voos internacionais ativos e usando recursos
            for (int i = 0; i < contador_avioes; i++) {
                if (avioes[i].tipo == VOO_INTERNACIONAL && 
                    avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
                    voos_int_ativos++;
                    
                    // Verificar se está usando recursos (operando)
                    if (avioes[i].estado == POUSANDO || avioes[i].estado == DESEMBARCANDO || 
                        avioes[i].estado == DECOLANDO) {
                        voos_int_usando_recursos++;
                    }
                }
            }
            
            if (voos_int_ativos > 0) {
                printf(COR_STARVATION "     └─  STARVATION DETECTADA: Voo doméstico %d bloqueado há %.1fs" RESET "\n", 
                       aviao->id, tempo_espera);
                printf(COR_STARVATION "       • Voos internacionais ativos: %d" RESET "\n", voos_int_ativos);
                printf(COR_STARVATION "       • Voos internacionais usando recursos: %d" RESET "\n", voos_int_usando_recursos);
                printf(COR_STARVATION "       • CAUSA: Prioridade dos voos internacionais está impedindo acesso aos recursos" RESET "\n");
                atualizar_estatisticas(aviao, "STARVATION_DETECTADA");
            }
        } else {
            // Mesmo voos internacionais podem sofrer starvation se há muita contenção
            printf(COR_ALERTA "     └─  Voo internacional em alerta - possível contenção de recursos" RESET "\n");
        }
        
        // Mostrar estado atual dos recursos durante o alerta
        printf(COR_RECURSOS "     └─  Estado dos recursos no momento do alerta:" RESET "\n");
        printf(COR_RECURSOS "       • Pistas: %d/%d ocupadas | Portões: %d/%d ocupados | Torre: %d/%d ativa" RESET "\n",
               pistas_em_uso, NUM_PISTAS, portoes_em_uso, NUM_PORTOES, torre_operacoes_ativas, MAX_TORRE_OPERACOES);
    }
    
    // CRASH após 90 segundos de espera
    if (tempo_espera > TEMPO_CRASH && !aviao->crashed) {
        aviao->crashed = 1;
        
        printf(COR_CRASH "\n CRASH SIMULADO - FALHA OPERACIONAL!" RESET "\n");
        imprimir_status(" AVIÃO CRASHOU - 90s de espera! THREAD FINALIZADA!", aviao);
        
        printf(COR_CRASH "     ╔═══════════════════════════════════════════════════════╗" RESET "\n");
        printf(COR_CRASH "     ║  FALHA OPERACIONAL CRÍTICA - AVIÃO %s %03d           ║" RESET "\n", 
               (aviao->tipo == VOO_DOMESTICO) ? "DOM" : "INT", aviao->id);
        printf(COR_CRASH "     ║  Tempo total de espera: %.1f segundos                 ║" RESET "\n", tempo_espera);
        printf(COR_CRASH "     ║  Estado no momento do crash: %-25s ║" RESET "\n", obter_nome_estado(aviao->estado));
        
        // Diagnóstico específico para voos domésticos
        if (aviao->tipo == VOO_DOMESTICO) {
            int voos_int_ativos = 0;
            for (int i = 0; i < contador_avioes; i++) {
                if (avioes[i].tipo == VOO_INTERNACIONAL && 
                    avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
                    voos_int_ativos++;
                }
            }
            
            printf(COR_CRASH "     ║  DIAGNÓSTICO: STARVATION SEVERA                       ║" RESET "\n");
            printf(COR_CRASH "     ║  - Voo doméstico não conseguiu recursos              ║" RESET "\n");
            printf(COR_CRASH "     ║  - Voos internacionais ativos: %-3d                   ║" RESET "\n", voos_int_ativos);
            printf(COR_CRASH "     ║  - CAUSA: Prioridade excessiva dos voos internac.    ║" RESET "\n");
        } else {
            printf(COR_CRASH "     ║  DIAGNÓSTICO: CONTENÇÃO EXTREMA DE RECURSOS          ║" RESET "\n");
            printf(COR_CRASH "     ║  - Mesmo com prioridade, não conseguiu recursos      ║" RESET "\n");
        }
        
        printf(COR_CRASH "     ╚═══════════════════════════════════════════════════════╝" RESET "\n\n");
        
        atualizar_estatisticas(aviao, "CRASHED");
        
        // Log adicional para análise
        printf(COR_TEMPO "[ANÁLISE] " RESET "Recursos no momento do crash: Pistas %d/%d, Portões %d/%d, Torre %d/%d\n",
               pistas_em_uso, NUM_PISTAS, portoes_em_uso, NUM_PORTOES, torre_operacoes_ativas, MAX_TORRE_OPERACOES);
    }
}

const char* obter_cor_por_operacao(const char* msg) {
    if (strstr(msg, "POUSO")) return COR_POUSO;
    if (strstr(msg, "DECOLAGEM")) return COR_DECOLAGEM;
    if (strstr(msg, "DESEMBARQUE")) return COR_DESEMBARQUE;
    if (strstr(msg, "ALERTA")) return COR_ALERTA;
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
    printf(COR_RECURSOS "     └─ Recursos: " RESET "Pistas " BRIGHT_BLUE "%d/%d" RESET " | Portões " BRIGHT_MAGENTA "%d/%d" RESET " | Torre " BRIGHT_GREEN "%d/%d" RESET "\n",
           pistas_em_uso, NUM_PISTAS, portoes_em_uso, NUM_PORTOES, 
           torre_operacoes_ativas, MAX_TORRE_OPERACOES);
    
    pthread_mutex_unlock(&mutex_output);
}

void detectar_deadlock() {
    while (criacao_avioes_ativa || contador_avioes > 0) {
        sleep(30); // Verificar a cada 30 segundos
        
        pthread_mutex_lock(&mutex_output);
        
        // Contar aviões por estado e tempo de espera
        int avioes_esperando_muito = 0;
        int voos_dom_bloqueados = 0;
        int voos_int_bloqueados = 0;
        int threads_ativas = 0;
        int avioes_em_espera_critica = 0;
        
        printf(COR_SUBTITULO "\n═══ MONITORAMENTO DE DEADLOCK/STARVATION ═══" RESET "\n");
        
                for (int i = 0; i < contador_avioes; i++) {
            if (avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
                threads_ativas++;
                double tempo_espera = tempo_decorrido(avioes[i].tempo_inicio_espera);
                
                // Contar aviões com espera problemática (>30s)
                if (tempo_espera > 30) {
                    avioes_esperando_muito++;
                    if (avioes[i].tipo == VOO_DOMESTICO) {
                        voos_dom_bloqueados++;
                    } else {
                        voos_int_bloqueados++;
                    }
                }
                
                // Contar aviões próximos do alerta crítico (>45s)
                if (tempo_espera > 45) {
                    avioes_em_espera_critica++;
                }
            }
        }
        
        printf(COR_RECURSOS "Threads ativas: %d | Esperando >30s: %d | Espera crítica >45s: %d" RESET "\n", 
               threads_ativas, avioes_esperando_muito, avioes_em_espera_critica);
        
        // Se não há mais threads ativas, sair do loop
        if (threads_ativas == 0) {
            printf(COR_SUCESSO "✓ Todas as threads finalizaram - encerrando monitoramento" RESET "\n");
            pthread_mutex_unlock(&mutex_output);
            break;
        }
        
        // Critérios para detectar possível deadlock:
        // 1. Múltiplos aviões esperando muito tempo
        // 2. Recursos aparentemente disponíveis mas aviões não conseguem prosseguir
        // 3. Todos os tipos de aviões afetados (não é só starvation)
        
        int recursos_totalmente_ocupados = (pistas_em_uso == NUM_PISTAS) + 
                                          (portoes_em_uso == NUM_PORTOES) + 
                                          (torre_operacoes_ativas == MAX_TORRE_OPERACOES);
        
        if (avioes_esperando_muito >= 4 || avioes_em_espera_critica >= 2) {
            printf(COR_DEADLOCK "\n POSSÍVEL DEADLOCK DETECTADO!" RESET "\n");
            printf(COR_DEADLOCK "   ╔═══════════════════════════════════════════════════╗" RESET "\n");
            printf(COR_DEADLOCK "   ║  ANÁLISE DE DEADLOCK/STARVATION                   ║" RESET "\n");
            printf(COR_DEADLOCK "   ║  • Aviões esperando >30s: %-3d                    ║" RESET "\n", avioes_esperando_muito);
            printf(COR_DEADLOCK "   ║  • Voos domésticos bloqueados: %-3d               ║" RESET "\n", voos_dom_bloqueados);
            printf(COR_DEADLOCK "   ║  • Voos internacionais bloqueados: %-3d           ║" RESET "\n", voos_int_bloqueados);
            printf(COR_DEADLOCK "   ║  • Recursos totalmente ocupados: %-3d/3           ║" RESET "\n", recursos_totalmente_ocupados);
            
            // Diagnóstico específico
            if (voos_dom_bloqueados > voos_int_bloqueados * 2) {
                printf(COR_DEADLOCK "   ║  DIAGNÓSTICO: STARVATION SEVERA                   ║" RESET "\n");
                printf(COR_DEADLOCK "   ║  - Voos domésticos sendo sistematicamente        ║" RESET "\n");
                printf(COR_DEADLOCK "   ║    prejudicados pela prioridade internacional    ║" RESET "\n");
            } else if (recursos_totalmente_ocupados >= 2) {
                printf(COR_DEADLOCK "   ║  DIAGNÓSTICO: DEADLOCK CLÁSSICO                   ║" RESET "\n");
                printf(COR_DEADLOCK "   ║  - Múltiplos recursos esgotados simultaneamente  ║" RESET "\n");
                printf(COR_DEADLOCK "   ║  - Aviões aguardando recursos uns dos outros     ║" RESET "\n");
            } else {
                printf(COR_DEADLOCK "   ║  DIAGNÓSTICO: CONTENÇÃO EXTREMA                   ║" RESET "\n");
                printf(COR_DEADLOCK "   ║  - Alta demanda por recursos limitados           ║" RESET "\n");
            }
            
            printf(COR_DEADLOCK "   ╚═══════════════════════════════════════════════════╝" RESET "\n");
            
            imprimir_estado_recursos();
            atualizar_estatisticas(NULL, "POSSIVEL_DEADLOCK");
            
            // Mostrar detalhes dos aviões problemáticos
            printf(COR_SUBTITULO "\n AVIÕES EM SITUAÇÃO CRÍTICA:" RESET "\n");
            for (int i = 0; i < contador_avioes; i++) {
                if (avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
                    double tempo_espera = tempo_decorrido(avioes[i].tempo_inicio_espera);
                    if (tempo_espera > 30) {
                        const char* cor_tipo = obter_cor_tipo_aviao(avioes[i].tipo);
                        const char* tipo_str = (avioes[i].tipo == VOO_DOMESTICO) ? "DOM" : "INT";
                        
                        printf("  • Avião %s%d (%s)%s: %s (%.1fs esperando)\n", 
                               cor_tipo, avioes[i].id, tipo_str, RESET,
                               obter_nome_estado(avioes[i].estado), tempo_espera);
                    }
                }
            }
        } else if (voos_dom_bloqueados > 0 && voos_int_bloqueados == 0) {
            // Starvation específica de voos domésticos
            printf(COR_STARVATION "\n STARVATION DE VOOS DOMÉSTICOS DETECTADA!" RESET "\n");
            printf(COR_STARVATION "   • %d voos domésticos esperando >30s" RESET "\n", voos_dom_bloqueados);
            printf(COR_STARVATION "   • 0 voos internacionais com problema similar" RESET "\n");
            printf(COR_STARVATION "   • CAUSA: Priorização excessiva dos voos internacionais" RESET "\n");
        }
        
        pthread_mutex_unlock(&mutex_output);
    }
}

void imprimir_estado_recursos() {
    printf("\n" COR_SUBTITULO "═══ ESTADO ATUAL DOS RECURSOS ═══" RESET "\n");
    printf(COR_RECURSOS "  Pistas: " RESET BRIGHT_BLUE "%d/%d" RESET " em uso\n", pistas_em_uso, NUM_PISTAS);
    printf(COR_RECURSOS "  Portões: " RESET BRIGHT_MAGENTA "%d/%d" RESET " em uso\n", portoes_em_uso, NUM_PORTOES);
    printf(COR_RECURSOS "  Torre: " RESET BRIGHT_GREEN "%d/%d" RESET " operações ativas\n", torre_operacoes_ativas, MAX_TORRE_OPERACOES);
    
    printf("\n" COR_SUBTITULO "═══ AVIÕES ATIVOS POR ESTADO ═══" RESET "\n");
    int estados[8] = {0}; // Para cada estado
    
    for (int i = 0; i < contador_avioes; i++) {
        if (avioes[i].estado != FINALIZADO && avioes[i].estado != CRASHED) {
            estados[avioes[i].estado]++;
        }
    }
    
    if (estados[ESPERANDO_POUSO] > 0) 
        printf(COR_RECURSOS "  Esperando pouso: " RESET "%d\n", estados[ESPERANDO_POUSO]);
    if (estados[POUSANDO] > 0) 
        printf(COR_POUSO "  Pousando: " RESET "%d\n", estados[POUSANDO]);
    if (estados[ESPERANDO_DESEMBARQUE] > 0) 
        printf(COR_RECURSOS "  Esperando desembarque: " RESET "%d\n", estados[ESPERANDO_DESEMBARQUE]);
    if (estados[DESEMBARCANDO] > 0) 
        printf(COR_DESEMBARQUE "  Desembarcando: " RESET "%d\n", estados[DESEMBARCANDO]);
    if (estados[ESPERANDO_DECOLAGEM] > 0) 
        printf(COR_RECURSOS "  Esperando decolagem: " RESET "%d\n", estados[ESPERANDO_DECOLAGEM]);
    if (estados[DECOLANDO] > 0) 
        printf(COR_DECOLAGEM "  Decolando: " RESET "%d\n", estados[DECOLANDO]);
    
    printf("\n");
}

double tempo_decorrido(time_t inicio) {
    return difftime(time(NULL), inicio);
}

void atualizar_estatisticas(aviao_t* aviao, const char* evento) {
    pthread_mutex_lock(&mutex_estatisticas);
    
    if (strcmp(evento, "CRIADO") == 0) {
        stats.avioes_criados++;
        if (aviao->tipo == VOO_DOMESTICO) {
            stats.voos_domesticos_total++;
        } else {
            stats.voos_internacionais_total++;
        }
    }
    else if (strcmp(evento, "POUSO_CONCLUIDO") == 0) {
        stats.pousos_realizados++;
    }
    else if (strcmp(evento, "DESEMBARQUE_CONCLUIDO") == 0) {
        stats.desembarques_realizados++;
    }
    else if (strcmp(evento, "DECOLAGEM_CONCLUIDA") == 0) {
        stats.decolagens_realizadas++;
    }
    else if (strcmp(evento, "FINALIZADO") == 0) {
        stats.avioes_finalizados_sucesso++;
        if (aviao->tipo == VOO_DOMESTICO) {
            stats.voos_domesticos_finalizados++;
        } else {
            stats.voos_internacionais_finalizados++;
        }
        
        // Calcular tempo total do ciclo
        double tempo_ciclo = tempo_decorrido(aviao->tempo_criacao);
        stats.tempo_medio_ciclo_completo = 
            (stats.tempo_medio_ciclo_completo * (stats.avioes_finalizados_sucesso - 1) + tempo_ciclo) 
            / stats.avioes_finalizados_sucesso;
    }
    else if (strcmp(evento, "CRASHED") == 0) {
        stats.avioes_crashed++;
        if (aviao->tipo == VOO_DOMESTICO) {
            stats.voos_domesticos_crashed++;
        } else {
            stats.voos_internacionais_crashed++;
        }
    }
    else if (strcmp(evento, "ALERTA_CRITICO") == 0) {
        stats.alertas_criticos_emitidos++;
        
        // Atualizar tempo máximo de espera
        double tempo_espera = tempo_decorrido(aviao->tempo_inicio_espera);
        if (tempo_espera > stats.tempo_maximo_espera) {
            stats.tempo_maximo_espera = tempo_espera;
        }
    }
    else if (strcmp(evento, "STARVATION_DETECTADA") == 0) {
        stats.casos_starvation_detectados++;
    }
    else if (strcmp(evento, "POSSIVEL_DEADLOCK") == 0) {
        stats.possiveis_deadlocks_detectados++;
    }
    
    pthread_mutex_unlock(&mutex_estatisticas);
}

void imprimir_relatorio_final() {
    printf("\n\n");
    printf(COR_TITULO "╔══════════════════════════════════════════════════════════════╗" RESET "\n");
    printf(COR_TITULO "║" RESET COR_SUBTITULO "                    RELATÓRIO FINAL                     " RESET COR_TITULO "║" RESET "\n");
    printf(COR_TITULO "║" RESET COR_SUBTITULO "              SIMULAÇÃO DE TRÁFEGO AÉREO               " RESET COR_TITULO "║" RESET "\n");
    printf(COR_TITULO "╚══════════════════════════════════════════════════════════════╝" RESET "\n\n");
    
    double tempo_total_simulacao = tempo_decorrido(inicio_simulacao);
    
    // ========== RESUMO GERAL ==========
    printf(COR_TITULO "┌─ RESUMO GERAL DA SIMULAÇÃO ─────────────────────────────────┐" RESET "\n");
    printf(COR_RECURSOS "│ Tempo total de simulação:      " RESET "%.1f segundos               │\n", tempo_total_simulacao);
    printf(COR_RECURSOS "│ Total de aviões criados:       " RESET "%d aviões                   │\n", stats.avioes_criados);
    printf(COR_SUCESSO "│ Aviões finalizados com sucesso: " RESET "%d aviões                   │\n", stats.avioes_finalizados_sucesso);
    printf(COR_CRASH "│ Aviões que crasharam:           " RESET "%d aviões                   │\n", stats.avioes_crashed);
    if (stats.avioes_criados > 0) {
        double taxa_sucesso_geral = (double)stats.avioes_finalizados_sucesso / stats.avioes_criados * 100;
        printf(COR_RECURSOS "│ Taxa de sucesso geral:          " RESET "%.1f%%                      │\n", taxa_sucesso_geral);
    }
    printf(COR_TITULO "└─────────────────────────────────────────────────────────────┘" RESET "\n\n");
    
    // ========== OPERAÇÕES REALIZADAS ==========
    printf(COR_TITULO "┌─ OPERAÇÕES REALIZADAS ──────────────────────────────────────┐" RESET "\n");
    printf(COR_POUSO "│  Pousos realizados:            " RESET "%d operações               │\n", stats.pousos_realizados);
    printf(COR_DESEMBARQUE "│  Desembarques realizados:      " RESET "%d operações               │\n", stats.desembarques_realizados);
    printf(COR_DECOLAGEM "│  Decolagens realizadas:        " RESET "%d operações               │\n", stats.decolagens_realizadas);
    printf(COR_TITULO "└─────────────────────────────────────────────────────────────┘" RESET "\n\n");
    
    // ========== ANÁLISE POR TIPO DE VOO ==========
    printf(COR_TITULO "┌─ ANÁLISE POR TIPO DE VOO ───────────────────────────────────┐" RESET "\n");
    
    // Voos Domésticos
    printf(COR_DOMESTICO "│ VOOS DOMÉSTICOS:                                            │" RESET "\n");
    printf("│   Total criados:        %d aviões                           │\n", stats.voos_domesticos_total);
    printf("│   Finalizados:          %d aviões                           │\n", stats.voos_domesticos_finalizados);
    printf("│   Crashed:              %d aviões                           │\n", stats.voos_domesticos_crashed);
    if (stats.voos_domesticos_total > 0) {
        double taxa_sucesso_dom = (double)stats.voos_domesticos_finalizados / stats.voos_domesticos_total * 100;
        printf("│   Taxa de sucesso:      %.1f%%                             │\n", taxa_sucesso_dom);
    }
    printf("│                                                             │\n");
    
    // Voos Internacionais
    printf(COR_INTERNACIONAL "│ VOOS INTERNACIONAIS:                                        │" RESET "\n");
    printf("│   Total criados:        %d aviões                           │\n", stats.voos_internacionais_total);
    printf("│   Finalizados:          %d aviões                           │\n", stats.voos_internacionais_finalizados);
    printf("│   Crashed:              %d aviões                           │\n", stats.voos_internacionais_crashed);
    if (stats.voos_internacionais_total > 0) {
        double taxa_sucesso_int = (double)stats.voos_internacionais_finalizados / stats.voos_internacionais_total * 100;
        printf("│   Taxa de sucesso:      %.1f%%                             │\n", taxa_sucesso_int);
    }
    printf(COR_TITULO "└─────────────────────────────────────────────────────────────┘" RESET "\n\n");
    
    // ========== PROBLEMAS DETECTADOS ==========
    printf(COR_TITULO "┌─ PROBLEMAS DE CONCORRÊNCIA DETECTADOS ──────────────────────┐" RESET "\n");
    printf(COR_ALERTA "│ ⚠ Alertas críticos emitidos:    " RESET "%d casos                    │\n", stats.alertas_criticos_emitidos);
    printf(COR_STARVATION "│  Casos de starvation:          " RESET "%d casos                    │\n", stats.casos_starvation_detectados);
    printf(COR_DEADLOCK "│  Possíveis deadlocks:          " RESET "%d casos                    │\n", stats.possiveis_deadlocks_detectados);
    if (stats.tempo_maximo_espera > 0) {
        printf(COR_RECURSOS "│  Tempo máximo de espera:       " RESET "%.1f segundos              │\n", stats.tempo_maximo_espera);
    }
    printf(COR_TITULO "└─────────────────────────────────────────────────────────────┘" RESET "\n\n");
    
    // ========== UTILIZAÇÃO DE RECURSOS ==========
    printf(COR_TITULO "┌─ UTILIZAÇÃO MÁXIMA DE RECURSOS ─────────────────────────────┐" RESET "\n");
    printf(COR_RECURSOS "│  Pistas (máximo simultâneo):   " RESET "%d/%d                       │\n", 
           stats.recursos_maximos_utilizados_pistas, NUM_PISTAS);
    printf(COR_RECURSOS "│  Portões (máximo simultâneo):  " RESET "%d/%d                       │\n", 
           stats.recursos_maximos_utilizados_portoes, NUM_PORTOES);
    printf(COR_RECURSOS "│  Torre (máximo simultâneo):    " RESET "%d/%d                       │\n", 
           stats.recursos_maximos_utilizados_torre, MAX_TORRE_OPERACOES);
    printf(COR_TITULO "└─────────────────────────────────────────────────────────────┘" RESET "\n\n");
    
    // ========== MÉTRICAS DE PERFORMANCE ==========
    if (stats.avioes_finalizados_sucesso > 0) {
        printf(COR_TITULO "┌─ MÉTRICAS DE PERFORMANCE ───────────────────────────────────┐" RESET "\n");
        printf(COR_RECURSOS "│  Tempo médio ciclo completo:   " RESET "%.1f segundos              │\n", stats.tempo_medio_ciclo_completo);
        
        double taxa_throughput = (double)stats.avioes_finalizados_sucesso / (tempo_total_simulacao / 60.0);
        printf(COR_RECURSOS "│  Throughput (aviões/minuto):   " RESET "%.2f aviões/min           │\n", taxa_throughput);
        printf(COR_TITULO "└─────────────────────────────────────────────────────────────┘" RESET "\n\n");
    }
    
    // ========== ANÁLISE DE FAIRNESS ==========
    printf(COR_TITULO "┌─ ANÁLISE DE FAIRNESS (EQUIDADE) ────────────────────────────┐" RESET "\n");
    if (stats.voos_domesticos_total > 0 && stats.voos_internacionais_total > 0) {
        double ratio_dom = (double)stats.voos_domesticos_finalizados / stats.voos_domesticos_total;
        double ratio_int = (double)stats.voos_internacionais_finalizados / stats.voos_internacionais_total;
        double fairness_index = (ratio_dom < ratio_int) ? (ratio_dom / ratio_int) : (ratio_int / ratio_dom);
        
        printf(COR_RECURSOS "│ Índice de equidade:              " RESET "%.3f                       │\n", fairness_index);
        printf(COR_RECURSOS "│ (1.0 = perfeitamente justo)                                │\n");
        
        if (fairness_index < 0.8) {
            printf(COR_ALERTA "│ ⚠ SISTEMA COM BAIXA EQUIDADE - Possível priorização      │" RESET "\n");
            printf(COR_ALERTA "│   excessiva de voos internacionais causando starvation   │" RESET "\n");
        } else if (fairness_index > 0.9) {
            printf(COR_SUCESSO "│ ✓ Sistema com boa equidade entre tipos de voo            │" RESET "\n");
        }
    }
    printf(COR_TITULO "└─────────────────────────────────────────────────────────────┘" RESET "\n\n");
    
    printf(COR_TITULO "═══ RELATÓRIO FINAL CONCLUÍDO ═══" RESET "\n\n");
}