#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>

#define BG_RESET   "\033[0m"
#define BG_RED     "\033[1;31m"
#define BG_GREEN   "\033[1;32m"
#define BG_YELLOW  "\033[1;33m"
#define BG_BLUE    "\033[1;34m"
#define BG_MAGENTA "\033[1;35m"
#define BG_CYAN    "\033[1;36m"
#define BG_WHITE   "\033[1;37m"

#define RESET          "\033[0m"
#define BOLD           "\033[1m"
#define CYAN           "\033[36m"
#define BRIGHT_WHITE   "\033[97m"
#define BRIGHT_CYAN    "\033[96m"
#define BRIGHT_GREEN   "\033[92m"
#define BG_YELLOW_RED  BOLD BG_YELLOW BG_RED

#define COR_SUCESSO    BRIGHT_GREEN
#define COR_ALERTA     BG_YELLOW_RED
#define COR_RECURSOS   CYAN
#define COR_TITULO     BOLD BRIGHT_WHITE
#define COR_SUBTITULO  BOLD CYAN
#define COR_CONFIG     BOLD BRIGHT_CYAN

// Configurações
int N_PISTAS;
int N_PORTOES;
int N_TORRE;
int TEMPO_SIMULACAO;
#define TEMPO_ALERTA 60
#define TEMPO_CRITICO 90
#define FREQUENCIA_DE_AVIAO 3
#define MAX_AVIOES 50

int avioes_domesticos_ativos = 0;
int avioes_internacionais_ativos = 0;
int internacionais_nao_criticos_esperando = 0;
int criticos_esperando = 0;

typedef enum status { EM_APROXIMACAO ,POUSOU, DESEMBARCOU, DECOLOU, CAIU } status;
typedef enum tipo {DOMESTICO, INTERNACIONAL} tipo;
typedef enum { NENHUM, PISTA, PORTAO, TORRE } recurso_tipo;

typedef struct {
    bool em_use;
    pthread_t thread_id;
} t_recurso;

t_recurso *pistas_rec;
t_recurso *portoes_embarque_rec;
t_recurso *torre_rec;

typedef struct node_log {
    char tag[32];
    char tipo[32];
    char final[32];
    char motivo[32];
    char status_final[32];
    int id;
    int tempo_operacao;
    struct node_log *next;
} node_log;

typedef struct {
    int avioes_sucesso;
    int avioes_falha;
    int falha_por_deadlock;
    int falha_por_timeout;
    int total_voos;
} stats_t;

stats_t stats = {0, 0, 0, 0, 0};
node_log *log_head = NULL;

typedef struct t_node_thread {
    pthread_t thread_id;
    struct t_node_thread *next;
} t_node_thread;

typedef struct t_node_aviao {
    int id;
    pthread_t thread_id;
    tipo tipo;
    int index;
    struct t_node_aviao *next;
    status status;
    recurso_tipo esperando_por;
    time_t tempo_inicio_espera;
    time_t tempo_inicio_operacao;
    bool em_alerta_critico;
} t_node_aviao;

t_node_aviao *head = NULL;


pthread_mutex_t ncurses_mutex;
pthread_mutex_t list_mutex;
pthread_mutex_t alocacao_mutex;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t manager_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t recurso_liberado_cond = PTHREAD_COND_INITIALIZER;


void configurar_simulacao() {
    printf("\n");
    printf(COR_TITULO"CONFIGURAÇÃO DA SIMULAÇÃO DE TRÁFEGO AÉREO\n" RESET COR_TITULO);
    printf(COR_CONFIG "Por favor, configure os recursos do aeroporto:" RESET "\n\n");
    
    printf(COR_RECURSOS "Digite o número de PISTAS " RESET "(recomendado: 2-5): ");
    scanf("%d", &N_PISTAS);

    printf(COR_RECURSOS "Digite o número de PORTÕES " RESET "(recomendado: 3-8): ");
    scanf("%d", &N_PORTOES);

    printf(COR_RECURSOS "Digite o número máximo de TORRES " RESET "(recomendado: 1-3): ");
    scanf("%d", &N_TORRE);

    printf(COR_RECURSOS "Digite o TEMPO DE SIMULAÇÃO em segundos " RESET "(recomendado: 60-300): ");
    scanf("%d", &TEMPO_SIMULACAO);

    
    printf("\n" COR_SUCESSO "✓ Configuração aplicada com sucesso!" RESET "\n");
    printf(COR_CONFIG "═══ RESUMO DA CONFIGURAÇÃO ═══" RESET "\n");
    printf(COR_RECURSOS "  Pistas: " RESET "%d\n", N_PISTAS);
    printf(COR_RECURSOS "  Portões: " RESET "%d\n", N_PORTOES);
    printf(COR_RECURSOS "  Torre (2 operações simultâneas cada torre): " RESET "%d\n", N_TORRE);
    printf(COR_RECURSOS "  Tempo de simulação: " RESET "%d segundos (%.1f minutos)\n", TEMPO_SIMULACAO, TEMPO_SIMULACAO / 60.0);
    printf("\n" COR_SUBTITULO "Pressione ENTER para iniciar a simulação..." RESET);
    getchar(); 
    getchar(); 
    printf("\n");
}


void add_log_entry(const char *tag, int id, const char *tipo, const char *status_final, const char *final, const char *motivo, int tempo_operacao) {
    pthread_mutex_lock(&log_mutex);
    
    node_log *new_entry = malloc(sizeof(node_log));
    snprintf(new_entry->tag, sizeof(new_entry->tag), "%s", tag);
    snprintf(new_entry->tipo, sizeof(new_entry->tipo), "%s", tipo);
    snprintf(new_entry->status_final, sizeof(new_entry->status_final), "%s", status_final);
    snprintf(new_entry->final, sizeof(new_entry->final), "%s", final);
    snprintf(new_entry->motivo, sizeof(new_entry->motivo), "%s", motivo);
    new_entry->id = id;
    new_entry->tempo_operacao = tempo_operacao;
    new_entry->next = log_head;
    log_head = new_entry;

    stats.total_voos++;
    if (strcmp(tag, "SUCESSO") == 0) {
        stats.avioes_sucesso++;
    } else if (strcmp(tag, "ERRO") == 0) {
        stats.avioes_falha++;
        if (strcmp(motivo, "DEADLOCK") == 0) {
            stats.falha_por_deadlock++;
        } else if (strcmp(motivo, "TIMEOUT") == 0) {
            stats.falha_por_timeout++;
        }
    }

    pthread_mutex_unlock(&log_mutex);
}

void print_log() {
    pthread_mutex_lock(&log_mutex);
    
    node_log *current = log_head;
    node_log *free_node;
    
    fprintf(stderr, "\n%s=== LOG DE OPERAÇÕES ===%s\n", BG_CYAN, BG_RESET);
    
    while (current != NULL) {
        const char* cor_tag;
        const char* cor_tipo;
        
        if (strcmp(current->tag, "SUCESSO") == 0) {
            cor_tag = BG_GREEN;
        } else if (strcmp(current->tag, "ERRO") == 0) {
            cor_tag = BG_RED;
        } else {
            cor_tag = BG_YELLOW;
        }
        
        if (strcmp(current->tipo, "DOMESTICO") == 0) {
            cor_tipo = BG_BLUE;
        } else {
            cor_tipo = BG_MAGENTA;
        }
        
        if (current->tempo_operacao > 0) {
            fprintf(stderr,"%s[%s]%s %s[ID: %d]%s %s[Tipo: %s]%s %s[Status: %s]%s [Final: %s] [Motivo: %s] %s[Tempo: %ds]%s\n", 
                   cor_tag, current->tag, BG_RESET,
                   BG_WHITE, current->id, BG_RESET,
                   cor_tipo, current->tipo, BG_RESET,
                   BG_YELLOW, current->status_final, BG_RESET,
                   current->final, current->motivo,
                   BG_CYAN, current->tempo_operacao, BG_RESET);
        } else {
            fprintf(stderr,"%s[%s]%s %s[ID: %d]%s %s[Tipo: %s]%s %s[Status: %s]%s [Final: %s] [Motivo: %s]\n", 
                   cor_tag, current->tag, BG_RESET,
                   BG_WHITE, current->id, BG_RESET,
                   cor_tipo, current->tipo, BG_RESET,
                   BG_YELLOW, current->status_final, BG_RESET,
                   current->final, current->motivo);
        }
        
        free_node = current;
        current = current->next;
        free(free_node);
    }

    fprintf(stderr, "\n%s=== ESTATÍSTICAS ===%s\n", BG_CYAN, BG_RESET);
    fprintf(stderr, "%sTempo de simulação:%s %d segundos\n", BG_WHITE, BG_RESET, TEMPO_SIMULACAO);
    fprintf(stderr, "%sTotal de voos:%s %d\n", BG_WHITE, BG_RESET, stats.total_voos);
    fprintf(stderr, "%sAviões com sucesso:%s %s%d%s\n", BG_WHITE, BG_RESET, BG_GREEN, stats.avioes_sucesso, BG_RESET);
    fprintf(stderr, "%sAviões com falha:%s %s%d%s\n", BG_WHITE, BG_RESET, BG_RED, stats.avioes_falha, BG_RESET);
    fprintf(stderr, "%sFalhas por deadlock:%s %s%d%s\n", BG_WHITE, BG_RESET, BG_RED, stats.falha_por_deadlock, BG_RESET);
    fprintf(stderr, "%sFalhas por timeout:%s %s%d%s\n", BG_WHITE, BG_RESET, BG_RED, stats.falha_por_timeout, BG_RESET);

    pthread_mutex_unlock(&log_mutex);
}

void update_list_index() {
    pthread_mutex_lock(&list_mutex);
    t_node_aviao *current = head;
    int index = 0;
    while (current != NULL) {
        current->index = index++;
        current = current->next;
    }
    pthread_mutex_unlock(&list_mutex);
}

t_node_aviao* add_plane_to_list(int id, int tipo) {
    pthread_mutex_lock(&list_mutex);
    int index = 0;

    t_node_aviao *new_node = malloc(sizeof(t_node_aviao));
    new_node->id = id;
    new_node->tipo = tipo;
    new_node->status = EM_APROXIMACAO;
    new_node->em_alerta_critico = false;
    new_node->tempo_inicio_operacao = time(NULL); 
    
    if (head == NULL) {
        head = new_node;
        new_node->next = NULL;
        new_node->index = index;  
    }else {
        t_node_aviao *current = head;
        while (current->next != NULL) {
            current = current->next;
            index++;
        }
        current->next = new_node;
        new_node->next = NULL;
        new_node->index = index + 1;  
    }

    pthread_mutex_unlock(&list_mutex);
    return new_node;
}

void add_thread_to_list(t_node_thread **head, pthread_t thread_id) {
    
    t_node_thread *new_node = malloc(sizeof(t_node_thread));
    new_node->thread_id = thread_id;
    new_node->next = NULL;

    if (*head == NULL) {
        *head = new_node;
    } else {
        t_node_thread *current = *head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_node;
    }
}

void remove_plane_from_list(int index) {
    pthread_mutex_lock(&list_mutex);

    t_node_aviao *current = head;
    t_node_aviao *previous = NULL;

    while (current != NULL) {
        if (current->index == index) {
            if (previous == NULL) {
                head = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            break;
        }
        previous = current;
        current = current->next;
    }
    pthread_mutex_unlock(&list_mutex);

    update_list_index();
}

int calcular_prioridade(t_node_aviao *aviao) {
    time_t tempo_espera = time(NULL) - aviao->tempo_inicio_espera;
    
    if (tempo_espera >= TEMPO_ALERTA) {
        return 1; // Menor valor = maior prioridade
    }
    
    if (aviao->tipo == DOMESTICO && tempo_espera >= 60) {
        int bonus_tempo = (tempo_espera - 60) / 20; 
        return 2 - bonus_tempo;
    }
    
    if (aviao->tipo == INTERNACIONAL) {
        if (tempo_espera < 60) {
            int bonus_tempo = tempo_espera / 20;
            return 5 - bonus_tempo;
        } else {
            int bonus_tempo = (tempo_espera - 60) / 30;
            return 3 - bonus_tempo;
        }
    } else { 
        int bonus_tempo = tempo_espera / 15;
        return 8 - bonus_tempo;
    }
}

bool solicitar_recurso(t_node_aviao *aviao, recurso_tipo tipo_desejado) {
    pthread_mutex_lock(&manager_mutex);
    
    aviao->tempo_inicio_espera = time(NULL);
    aviao->tempo_inicio_operacao = time(NULL);
    aviao->em_alerta_critico = false;

    while(true) {
        t_recurso *array_recurso = NULL;
        int total_recursos = 0;

        switch(tipo_desejado) {
            case PISTA:
                array_recurso = pistas_rec;
                total_recursos = N_PISTAS;
                break;
            case PORTAO:
                array_recurso = portoes_embarque_rec;
                total_recursos = N_PORTOES;
                break;
            case TORRE:
                array_recurso = torre_rec;
                total_recursos = N_TORRE * 2;
                break;
            default:
                pthread_mutex_unlock(&manager_mutex);
                return true; 
        }

        bool deve_esperar_por_prioridade = false;
        int minha_prioridade = calcular_prioridade(aviao);
        
        time_t tempo_espera_atual = time(NULL) - aviao->tempo_inicio_espera;

        
        pthread_mutex_lock(&list_mutex);
        t_node_aviao *current = head;
        while (current != NULL) {
            if (current != aviao && current->esperando_por == tipo_desejado) {
                int prioridade_outro = calcular_prioridade(current);
                time_t tempo_espera_outro = time(NULL) - current->tempo_inicio_espera;

                if (prioridade_outro < minha_prioridade) { 
                    deve_esperar_por_prioridade = true;
                    break;
                } 
                else if (prioridade_outro == minha_prioridade && tempo_espera_outro > tempo_espera_atual) {
                    deve_esperar_por_prioridade = true;
                    break;
                }
            }
            current = current->next;
        }
        pthread_mutex_unlock(&list_mutex);

        int recurso_idx = -1;
        if(!deve_esperar_por_prioridade) {
            for (int i = 0; i < total_recursos; i++) {
                if(!array_recurso[i].em_use){
                    recurso_idx = i;
                    break;
                }
            }
            
        } 
        if (recurso_idx != -1) {
            array_recurso[recurso_idx].em_use = true;
            array_recurso[recurso_idx].thread_id = aviao->thread_id;
            aviao->esperando_por = NENHUM; 
            pthread_mutex_unlock(&manager_mutex);
            return true;
        }

        aviao->esperando_por = tipo_desejado;

        time_t tempo_espera = time(NULL) - aviao->tempo_inicio_espera;
        if (tempo_espera >= TEMPO_CRITICO) {
            aviao->em_alerta_critico = true;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int wait_result = pthread_cond_timedwait(&recurso_liberado_cond, &manager_mutex, &ts);

        if (wait_result == ETIMEDOUT){
            tempo_espera = time(NULL) - aviao->tempo_inicio_espera;

            if(tempo_espera == 0) aviao->tempo_inicio_espera = time(NULL);

            if (tempo_espera > TEMPO_CRITICO) { 
                aviao->status = CAIU;
                aviao->esperando_por = NENHUM;
                pthread_mutex_unlock(&manager_mutex);
                int tempo_total = (int)(time(NULL) - aviao->tempo_inicio_operacao);
                add_log_entry("ERRO", aviao->id, (aviao->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", "CAIU", "CANCELADO", "TIMEOUT", tempo_total);
                return false;
            }
        }
    }
}



void liberar_recurso(t_node_aviao *aviao, recurso_tipo tipo_a_liberar) {
    pthread_mutex_lock(&manager_mutex);

    t_recurso *array_recurso = NULL;
    int total_recursos = 0;

    switch (tipo_a_liberar) {
        case PISTA:
            array_recurso = pistas_rec;
            total_recursos = N_PISTAS;
            break;
        case PORTAO:
            array_recurso = portoes_embarque_rec;
            total_recursos = N_PORTOES;
            break;
        case TORRE:
            array_recurso = torre_rec;
            total_recursos = N_TORRE * 2;
            break;
        default:
            pthread_mutex_unlock(&manager_mutex);
            return;
    }

    for (int i = 0; i < total_recursos; i++) {
        if (array_recurso[i].em_use && pthread_equal(array_recurso[i].thread_id, aviao->thread_id)) {
            array_recurso[i].em_use = false;
            array_recurso[i].thread_id = 0;
            break;
        }
    }
    
    pthread_cond_broadcast(&recurso_liberado_cond);
    pthread_mutex_unlock(&manager_mutex);
}

void cleanup_liberar_recursos(void *arg){
    t_node_aviao *aviao = (t_node_aviao *)arg;

    for(int i = 0; i < N_PISTAS; i++) {
        if (pistas_rec[i].em_use && pthread_equal(pistas_rec[i].thread_id, aviao->thread_id))
            pistas_rec[i].em_use = false;
    }
    for(int i = 0; i < N_PORTOES; i++) {
        if (portoes_embarque_rec[i].em_use && pthread_equal(portoes_embarque_rec[i].thread_id, aviao->thread_id))
            portoes_embarque_rec[i].em_use = false;
    }
    for(int i = 0; i < N_TORRE * 2; i++) {
        if (torre_rec[i].em_use && pthread_equal(torre_rec[i].thread_id, aviao->thread_id))
            torre_rec[i].em_use = false;
    }

    pthread_cond_broadcast(&recurso_liberado_cond);
    pthread_mutex_unlock(&manager_mutex);
    pthread_mutex_lock(&list_mutex);
    t_node_aviao *current = head;
    t_node_aviao *previous = NULL;
    while (current != NULL) {
        if (current->id == aviao->id) {
            if (previous == NULL) {
                head = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            break;
        }
        previous = current;
        current = current->next;
    }

    pthread_mutex_unlock(&list_mutex);
    update_list_index();
}

void print_aviao_list() {
    pthread_mutex_lock(&list_mutex);
    
    t_node_aviao *current = head;

    while(current != NULL) {
        printf("%d -> ", current->id);
        current = current->next;
    }

    printf("\n");
    pthread_mutex_unlock(&list_mutex);
}

void tabprint(int level){
    for (int i = 0; i < level; i++) {
        printf("\t");
    }
}

bool pouso_domestico(void *arg) {
    t_node_aviao *aviao = (t_node_aviao *)arg;

    if (!solicitar_recurso(aviao, TORRE)) return false;
    if (!solicitar_recurso(aviao, PISTA)) {
        liberar_recurso(aviao, TORRE);
        return false;
    }

    pthread_mutex_lock(&list_mutex);
    aviao->status = POUSOU;
    pthread_mutex_unlock(&list_mutex);

    sleep(2);
    liberar_recurso(aviao, PISTA);
    liberar_recurso(aviao, TORRE);

    if (!solicitar_recurso(aviao, PORTAO)) return false;

    return true;
}

bool desembarque_domestico(void *arg) {
    t_node_aviao *aviao = (t_node_aviao *)arg;

    if (!solicitar_recurso(aviao, TORRE)) {
        liberar_recurso(aviao, PORTAO);
        return false;
    }

    pthread_mutex_lock(&list_mutex);
    aviao->status = DESEMBARCOU;
    pthread_mutex_unlock(&list_mutex);

    sleep(3);
    liberar_recurso(aviao, TORRE);
    sleep(1);
    liberar_recurso(aviao, PORTAO);

    return true;
}

bool decolagem_domestico(void *arg) {
    t_node_aviao *aviao = (t_node_aviao *)arg;

    if (!solicitar_recurso(aviao, TORRE)) return false;
    if (!solicitar_recurso(aviao, PORTAO)) {
        liberar_recurso(aviao, TORRE);
        return false;
    }
    if (!solicitar_recurso(aviao, PISTA)) {
        liberar_recurso(aviao, TORRE);
        liberar_recurso(aviao, PORTAO);
        return false;
    }

    pthread_mutex_lock(&list_mutex);
    aviao->status = DECOLOU;
    pthread_mutex_unlock(&list_mutex);

    sleep(1);
    liberar_recurso(aviao, TORRE);
    liberar_recurso(aviao, PORTAO);
    liberar_recurso(aviao, PISTA);

    int tempo_total = (int)(time(NULL) - aviao->tempo_inicio_operacao);
    add_log_entry("SUCESSO", aviao->id, (aviao->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", "DECOLOU", "CONCLUÍDO", "NONE", tempo_total);

    return true;
}

bool pouso_internacional(void *arg) {
    t_node_aviao *aviao = (t_node_aviao *)arg;

    if (!solicitar_recurso(aviao, PISTA)) return false;
    if (!solicitar_recurso(aviao, TORRE)) {
        liberar_recurso(aviao, PISTA);
        return false;
    }

    pthread_mutex_lock(&list_mutex);
    aviao->status = POUSOU;
    pthread_mutex_unlock(&list_mutex);

    sleep(2);
    liberar_recurso(aviao, PISTA);
    liberar_recurso(aviao, TORRE);

    if (!solicitar_recurso(aviao, PORTAO)) return false;

    return true;
}

bool desembarque_internacional(void *arg) {
    t_node_aviao *aviao = (t_node_aviao *)arg;

    if (!solicitar_recurso(aviao, TORRE)) {
        liberar_recurso(aviao, PORTAO);
        return false;
    }

    pthread_mutex_lock(&list_mutex);
    aviao->status = DESEMBARCOU;
    pthread_mutex_unlock(&list_mutex);

    sleep(3);
    liberar_recurso(aviao, TORRE);
    sleep(1);
    liberar_recurso(aviao, PORTAO);

    return true;
}

bool decolagem_internacional(void *arg) {
    t_node_aviao *aviao = (t_node_aviao *)arg;

    if (!solicitar_recurso(aviao, PORTAO)) return false;
    if (!solicitar_recurso(aviao, PISTA)) {
        liberar_recurso(aviao, PORTAO);
        return false;
    }
    if (!solicitar_recurso(aviao, TORRE)) {
        liberar_recurso(aviao, PORTAO);
        liberar_recurso(aviao, PISTA);
        return false;
    }

    pthread_mutex_lock(&list_mutex);
    aviao->status = DECOLOU;
    pthread_mutex_unlock(&list_mutex);

    sleep(2);
    liberar_recurso(aviao, TORRE);
    liberar_recurso(aviao, PISTA);
    liberar_recurso(aviao, PORTAO);

    int tempo_total = (int)(time(NULL) - aviao->tempo_inicio_operacao);
    add_log_entry("SUCESSO", aviao->id, (aviao->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", "DECOLOU", "CONCLUÍDO", "NONE", tempo_total);

    return true;
}

void* voo_domestico(void *arg){
    t_node_aviao *aviao = (t_node_aviao *)arg;

    pthread_cleanup_push(cleanup_liberar_recursos, aviao);

    bool sucesso = true;
    if (!pouso_domestico(aviao)) sucesso = false;
    if (sucesso && !desembarque_domestico(aviao)) sucesso = false;
    if (sucesso && !decolagem_domestico(aviao)) sucesso = false;

    pthread_cleanup_pop(0);

    remove_plane_from_list(aviao->index);

    return NULL;
}

void* voo_internacional(void *arg){
    t_node_aviao *aviao = (t_node_aviao *)arg;

    pthread_cleanup_push(cleanup_liberar_recursos, aviao);

    bool sucesso = true;
    if (!pouso_internacional(aviao)) sucesso = false;
    if (sucesso && !desembarque_internacional(aviao)) sucesso = false;
    if (sucesso && !decolagem_internacional(aviao)) sucesso = false;

    pthread_cleanup_pop(0);

    remove_plane_from_list(aviao->index);

    return NULL;
}

bool dfs_ciclo_util(int u, int total_avioes, int grafo[][total_avioes], int estado[]) {
    estado[u] = 1; 

    for (int v = 0; v < total_avioes; v++) {
        if (grafo[u][v] == 1) {
            if (estado[v] == 1) {
                return true;
            }
            if (estado[v] == 0) {
                if (dfs_ciclo_util(v, total_avioes, grafo, estado)) {
                    return true;
                }
            }
        }
    }

    estado[u] = 2;
    return false;
}

bool detectar_ciclo(int total_avioes, int grafo[][total_avioes], int estado[]) {
    
    for(int i = 0; i < total_avioes; i++) {
        estado[i] = 0; 
    }

    for(int i = 0; i < total_avioes; i++) {
        if (estado[i] == 0) {
            if (dfs_ciclo_util(i, total_avioes, grafo, estado)) {
                return true; 
            }
        }
    }
    return false;
}

void deadlock_watcher(bool deadlock){
    static WINDOW *win = NULL;
    if (win == NULL) {
        int largura = getmaxx(stdscr);
        win = newwin(3, largura / 4, 0, largura / 2);  
    }

    pthread_mutex_lock(&ncurses_mutex);
    werase(win);
    box(win, 0, 0);
    if (deadlock) {
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, 1, 2, "DEADLOCK DETECTADO");
        wattroff(win, COLOR_PAIR(1));
    } else {
        wattron(win, COLOR_PAIR(2));
        mvwprintw(win, 1, 2, "Nenhum deadlock");
        wattroff(win, COLOR_PAIR(2));
    }
    wrefresh(win);
    pthread_mutex_unlock(&ncurses_mutex);
}


void* deadlock_monitor(void *arg) {
    while(1)
    {
        int total_avioes = 0;
        t_node_aviao *snapshot_avioes[100];
        t_recurso pistas_snapshot[N_PISTAS];
        t_recurso portoes_snapshot[N_PORTOES];
        t_recurso torre_snapshot[N_TORRE*2];
        
        sleep(3);

        pthread_mutex_lock(&manager_mutex);
        pthread_mutex_lock(&list_mutex);

        t_node_aviao *current = head;
        while (current != NULL && total_avioes < 100) {
            snapshot_avioes[total_avioes++] = current;
            current = current->next;
        }

        memcpy(pistas_snapshot, pistas_rec, N_PISTAS * sizeof(t_recurso));
        memcpy(portoes_snapshot, portoes_embarque_rec, N_PORTOES * sizeof(t_recurso));
        memcpy(torre_snapshot, torre_rec, N_TORRE * 2 * sizeof(t_recurso));

        pthread_mutex_unlock(&list_mutex);
        pthread_mutex_unlock(&manager_mutex);

        if (total_avioes > 0) {
            int grafo[total_avioes][total_avioes];
            memset(grafo, 0, sizeof(grafo));

            for (int i = 0; i < total_avioes; i++) {
                t_node_aviao *aviao_esperando = snapshot_avioes[i];

                if (aviao_esperando->esperando_por == NENHUM) {
                    continue;
                }
                else {
                    int recurso_tipo = aviao_esperando->esperando_por;
                    int num_recurso = 0;
                    t_recurso *recurso_rec = NULL;
                    if (recurso_tipo == PISTA) {
                        num_recurso = N_PISTAS;
                        recurso_rec = pistas_snapshot;
                    } else if (recurso_tipo == PORTAO) {
                        num_recurso = N_PORTOES;
                        recurso_rec = portoes_snapshot;
                    } else if (recurso_tipo == TORRE) {
                        num_recurso = N_TORRE*2;
                        recurso_rec = torre_snapshot;
                    }

                    for (int j = 0; j < num_recurso; j++) {
                        if (recurso_rec[j].em_use) {
                            pthread_t dono_id = recurso_rec[j].thread_id;
                            for (int k = 0; k < total_avioes; k++) {
                                if (pthread_equal(snapshot_avioes[k]->thread_id, dono_id)) {
                                    grafo[i][k] = 1;
                                }
                            }
                        }
                    }
                }
            }
            int estado_dfs[total_avioes];

            if (detectar_ciclo(total_avioes, grafo, estado_dfs)) {
                deadlock_watcher(true);
                
                t_node_aviao* vitima = NULL;
                int indice_vitima = -1;

                for (int i = 0; i < total_avioes; i++) {
                    if (estado_dfs[i] == 1) {
                        indice_vitima = i;
                        break;
                    }
                }

                if (indice_vitima != -1) {
                    vitima = snapshot_avioes[indice_vitima];
                    char tipo_str[32];
                    if (vitima->status == EM_APROXIMACAO) snprintf(tipo_str, sizeof(tipo_str), "EM_APROXIMACAO");
                    else if (vitima->status == POUSOU) snprintf(tipo_str, sizeof(tipo_str), "POUSOU");
                    else if (vitima->status == DESEMBARCOU) snprintf(tipo_str, sizeof(tipo_str), "DESEMBARCOU");
                    else if (vitima->status == DECOLOU) snprintf(tipo_str, sizeof(tipo_str), "DECOLOU");
                    else if (vitima->status == CAIU) snprintf(tipo_str, sizeof(tipo_str), "CAIU");
                    
                    int tempo_total = (int)(time(NULL) - vitima->tempo_inicio_operacao);
                    add_log_entry("ERRO", vitima->id, (vitima->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", tipo_str, "CANCELADO", "DEADLOCK", tempo_total);
                    pthread_cancel(vitima->thread_id);
                }
            } else {
                deadlock_watcher(false);
            }
        }
    }
}

void* resource_watcher(void *arg) {
    int largura = getmaxx(stdscr);
    WINDOW *win = newwin(4, largura / 2, 0, 0);

    while (1) {
        int pistas_ocupadas = 0, portoes_ocupados = 0, torres_ocupadas = 0;
        int total_avioes_ativos = 0;

        pthread_mutex_lock(&ncurses_mutex);
        werase(win);
        box(win, 0, 0);

        pthread_mutex_lock(&manager_mutex);
        pthread_mutex_lock(&list_mutex);
        
        t_node_aviao *current = head;
        while (current != NULL) {
            total_avioes_ativos++;
            current = current->next;
        }
        
        for(int i = 0; i < N_PISTAS; i++)
            if (pistas_rec[i].em_use) pistas_ocupadas++;
        for(int i = 0; i < N_PORTOES; i++)
            if (portoes_embarque_rec[i].em_use) portoes_ocupados++;
        for(int i = 0; i < N_TORRE * 2; i++)
            if (torre_rec[i].em_use) torres_ocupadas++;
            
        pthread_mutex_unlock(&list_mutex);
        pthread_mutex_unlock(&manager_mutex);

        if (pistas_ocupadas > total_avioes_ativos || 
            portoes_ocupados > total_avioes_ativos || 
            torres_ocupadas > total_avioes_ativos) {
            
            wattron(win, COLOR_PAIR(1)); 
            mvwprintw(win, 0, 2, " ERRO: Estado inconsistente! ");
            mvwprintw(win, 1, 2, "Aviões: %d | Pistas: %d/%d | Portões: %d/%d | Torres: %d/%d", 
                     total_avioes_ativos,
                     pistas_ocupadas, N_PISTAS,
                     portoes_ocupados, N_PORTOES, 
                     torres_ocupadas, N_TORRE * 2);
            wattroff(win, COLOR_PAIR(1));
        } else {
            mvwprintw(win, 0, 2, " Recursos em uso ");
            mvwprintw(win, 1, 2, "Aviões ativos: %d", total_avioes_ativos);
            mvwprintw(win, 2, 2, "Pistas: %d/%d | Portões: %d/%d | Torres: %d/%d", 
                     pistas_ocupadas, N_PISTAS,
                     portoes_ocupados, N_PORTOES,
                     torres_ocupadas, N_TORRE * 2);
        }

        wrefresh(win);
        pthread_mutex_unlock(&ncurses_mutex);
        usleep(100000);
    }

    return NULL;
}

void* plane_watcher(void *arg) {
    int altura = getmaxy(stdscr) - 3;  
    int largura = getmaxx(stdscr);
    WINDOW *win = newwin(altura, largura / 2, 4, 0);  
 

    char msg[128];

    while(1) {
        pthread_mutex_lock(&ncurses_mutex);
        t_node_aviao *avioes = head;
        werase(win);
        mvwprintw(win, 0, 2, " Aviões em voo ");

        while(avioes != NULL){
            if(avioes->index > altura - 2) break;

            char type_str[16];
            snprintf(type_str, sizeof(type_str),
                avioes->tipo == DOMESTICO ? "Doméstico" : "Internacional");

            char status_str[32];
            switch (avioes->status) {
                case EM_APROXIMACAO: snprintf(status_str, sizeof(status_str), "Aproximação"); break;
                case POUSOU: snprintf(status_str, sizeof(status_str), "Pousou"); break;
                case DESEMBARCOU: snprintf(status_str, sizeof(status_str), "Desembarcou"); break;
                case DECOLOU: snprintf(status_str, sizeof(status_str), "Decolou"); break;
                case CAIU: snprintf(status_str, sizeof(status_str), "Caiu"); break;
            }

            int tempo = (int)(time(NULL) - avioes->tempo_inicio_operacao);
            snprintf(msg, sizeof(msg), "ID: %d | Tipo: %s | Status: %s (%ds)",
                avioes->id, type_str, status_str, tempo);

            int color = 5; 
            if (avioes->status == EM_APROXIMACAO) color = 4;
            else if (avioes->status == POUSOU) color = 3;
            else if (avioes->status == DESEMBARCOU || avioes->status == DECOLOU) color = 2;
            else if (avioes->status == CAIU) color = 1;

            wattron(win, COLOR_PAIR(color));
            mvwprintw(win, avioes->index + 1, 2, "%s", msg);
            wattroff(win, COLOR_PAIR(color));

            avioes = avioes->next;
        }

        wrefresh(win);
        pthread_mutex_unlock(&ncurses_mutex);
        usleep(100000);
    }

    return NULL;
}

void* priority_queue_monitor(void *arg) {
    int altura = getmaxy(stdscr);
    static WINDOW *win = NULL;
    if (win == NULL) {
        int largura = getmaxx(stdscr);
        win = newwin(70, largura / 4, 0, (3 * largura) / 4);  
    }

    while (1) {
        pthread_mutex_lock(&ncurses_mutex);
        pthread_mutex_lock(&list_mutex);

        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " FILA DE PRIORIDADE ");

        t_node_aviao *esperando_pista[50];
        t_node_aviao *esperando_portao[50];
        t_node_aviao *esperando_torre[50];
        int count_pista = 0, count_portao = 0, count_torre = 0;

        t_node_aviao *current = head;
        while (current != NULL) {
            if (current->esperando_por == PISTA && count_pista < 50) {
                esperando_pista[count_pista++] = current;
            } else if (current->esperando_por == PORTAO && count_portao < 50) {
                esperando_portao[count_portao++] = current;
            } else if (current->esperando_por == TORRE && count_torre < 50) {
                esperando_torre[count_torre++] = current;
            }
            current = current->next;
        }

        for (int i = 0; i < count_pista - 1; i++) {
            for (int j = 0; j < count_pista - i - 1; j++) {
                if (calcular_prioridade(esperando_pista[j]) > calcular_prioridade(esperando_pista[j + 1])) {
                    t_node_aviao *temp = esperando_pista[j];
                    esperando_pista[j] = esperando_pista[j + 1];
                    esperando_pista[j + 1] = temp;
                }
            }
        }

        for (int i = 0; i < count_portao - 1; i++) {
            for (int j = 0; j < count_portao - i - 1; j++) {
                if (calcular_prioridade(esperando_portao[j]) > calcular_prioridade(esperando_portao[j + 1])) {
                    t_node_aviao *temp = esperando_portao[j];
                    esperando_portao[j] = esperando_portao[j + 1];
                    esperando_portao[j + 1] = temp;
                }
            }
        }

        for (int i = 0; i < count_torre - 1; i++) {
            for (int j = 0; j < count_torre - i - 1; j++) {
                if (calcular_prioridade(esperando_torre[j]) > calcular_prioridade(esperando_torre[j + 1])) {
                    t_node_aviao *temp = esperando_torre[j];
                    esperando_torre[j] = esperando_torre[j + 1];
                    esperando_torre[j + 1] = temp;
                }
            }
        }

        int linha = 2;
        int max_linhas = altura - 6; 

        if (count_pista > 0) {
            wattron(win, COLOR_PAIR(4)); 
            mvwprintw(win, linha++, 2, "=== PISTA ===");
            wattroff(win, COLOR_PAIR(4));
            
            for (int i = 0; i < count_pista && linha < max_linhas; i++) {
                t_node_aviao *aviao = esperando_pista[i];
                int prioridade = calcular_prioridade(aviao);
                int tempo_espera = (int)(time(NULL) - aviao->tempo_inicio_espera);
                
                int cor = 5; 
                if (prioridade <= 2) cor = 1; 
                else if (prioridade <= 4) cor = 3; 
                else if (aviao->tipo == INTERNACIONAL) cor = 6; 
                else cor = 4; 
                
                wattron(win, COLOR_PAIR(cor));
                mvwprintw(win, linha++, 4, "#%d: ID%d %s P=%d (%ds)",
                         i+1, aviao->id,
                         aviao->tipo == DOMESTICO ? "DOM" : "INT",
                         prioridade, tempo_espera);
                wattroff(win, COLOR_PAIR(cor));
            }
            linha++;
        }

        if (count_portao > 0 && linha < max_linhas) {
            wattron(win, COLOR_PAIR(4));
            mvwprintw(win, linha++, 2, "=== PORTÃO ===");
            wattroff(win, COLOR_PAIR(4));
            
            for (int i = 0; i < count_portao && linha < max_linhas; i++) {
                t_node_aviao *aviao = esperando_portao[i];
                int prioridade = calcular_prioridade(aviao);
                int tempo_espera = (int)(time(NULL) - aviao->tempo_inicio_espera);
                
                int cor = 5;
                if (prioridade <= 2) cor = 1;
                else if (prioridade <= 4) cor = 3;
                else if (aviao->tipo == INTERNACIONAL) cor = 6;
                else cor = 4;
                
                wattron(win, COLOR_PAIR(cor));
                mvwprintw(win, linha++, 4, "#%d: ID%d %s P=%d (%ds)",
                         i+1, aviao->id,
                         aviao->tipo == DOMESTICO ? "DOM" : "INT",
                         prioridade, tempo_espera);
                wattroff(win, COLOR_PAIR(cor));
            }
            linha++;
        }

        if (count_torre > 0 && linha < max_linhas) {
            wattron(win, COLOR_PAIR(4));
            mvwprintw(win, linha++, 2, "=== TORRE ===");
            wattroff(win, COLOR_PAIR(4));
            
            for (int i = 0; i < count_torre && linha < max_linhas; i++) {
                t_node_aviao *aviao = esperando_torre[i];
                int prioridade = calcular_prioridade(aviao);
                int tempo_espera = (int)(time(NULL) - aviao->tempo_inicio_espera);
                
                int cor = 5;
                if (prioridade <= 2) cor = 1;
                else if (prioridade <= 4) cor = 3;
                else if (aviao->tipo == INTERNACIONAL) cor = 6;
                else cor = 4;
                
                wattron(win, COLOR_PAIR(cor));
                mvwprintw(win, linha++, 4, "#%d: ID%d %s P=%d (%ds)",
                         i+1, aviao->id,
                         aviao->tipo == DOMESTICO ? "DOM" : "INT",
                         prioridade, tempo_espera);
                wattroff(win, COLOR_PAIR(cor));
            }
        }

        wrefresh(win);
        pthread_mutex_unlock(&list_mutex);
        pthread_mutex_unlock(&ncurses_mutex);
        
        usleep(500000); 
    }

    return NULL;
}


int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    if (argc == 5) {
        N_PISTAS = atoi(argv[1]);
        N_PORTOES = atoi(argv[2]);
        N_TORRE = atoi(argv[3]);
        TEMPO_SIMULACAO = atoi(argv[4]);

        if (N_PISTAS < 1 || N_PISTAS > 10 ||
            N_PORTOES < 1 || N_PORTOES > 15 ||
            N_TORRE < 1 || N_TORRE > 5 ||
            TEMPO_SIMULACAO < 10 || TEMPO_SIMULACAO > 600) {
            fprintf(stderr, COR_ALERTA "Erro: Argumentos inválidos fornecidos.\n" RESET);
            exit(EXIT_FAILURE);
        }

        printf(COR_SUCESSO "✓ Modo automático com argumentos ativado.\n" RESET);
        printf(COR_RECURSOS "  Pistas: %d, Portões: %d, Torre: %d, Tempo: %ds\n\n" RESET,
               N_PISTAS, N_PORTOES, N_TORRE, TEMPO_SIMULACAO);
    } else if (argc == 1) {
        configurar_simulacao();
    } else {
        fprintf(stderr, COR_ALERTA "Uso: ./trabalho [PISTAS PORTOES TORRE TEMPO]\n" RESET);
        exit(EXIT_FAILURE);
    }

    pistas_rec = malloc(N_PISTAS * sizeof(t_recurso));
    portoes_embarque_rec = malloc(N_PORTOES * sizeof(t_recurso));
    torre_rec = malloc(N_TORRE * 2 * sizeof(t_recurso));

    if (!pistas_rec || !portoes_embarque_rec || !torre_rec) {
        fprintf(stderr, COR_ALERTA "Erro ao alocar memória para recursos!\n" RESET);
        exit(EXIT_FAILURE);
    }

    initscr();
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_BLACK);
    noecho();
    curs_set(0);

    pthread_mutex_init(&ncurses_mutex, NULL);

    pthread_t thr_plane_watcher;
    pthread_t thr_resource_watcher;
    pthread_t thr_deadlock_monitor;
    pthread_t thr_priority_queue_monitor;
    pthread_create(&thr_resource_watcher, NULL, resource_watcher, NULL); 
    pthread_create(&thr_plane_watcher, NULL, plane_watcher, NULL);
    pthread_create(&thr_deadlock_monitor, NULL, deadlock_monitor, NULL);
    pthread_create(&thr_priority_queue_monitor, NULL, priority_queue_monitor, NULL);
    sleep(1);

    for (int i = 0; i < N_PISTAS; i++) {
        pistas_rec[i].em_use = false;
        pistas_rec[i].thread_id = 0;
    }
    for (int i = 0; i < N_PORTOES; i++) {
        portoes_embarque_rec[i].em_use = false;
        portoes_embarque_rec[i].thread_id = 0;
    }
    for (int i = 0; i < N_TORRE * 2; i++) {
        torre_rec[i].em_use = false;
        torre_rec[i].thread_id = 0;
    }

    t_node_thread *plane_threads = NULL;
    time_t tempo_inicio = time(NULL);
    srand(tempo_inicio);
    int tempo_simulacao_segundos = TEMPO_SIMULACAO;
    int id_aviao = 0;
    bool limite_excedido = false;

    while ((time(NULL) - tempo_inicio) < tempo_simulacao_segundos) {
        id_aviao++;
        pthread_t nova_thread_aviao;
        t_node_aviao *aviao = NULL;

        if (id_aviao > MAX_AVIOES) {
            limite_excedido = true;
            break;
        }

        if (rand() % 2 == 0) {
            aviao = add_plane_to_list(id_aviao, DOMESTICO);
            pthread_create(&nova_thread_aviao, NULL, voo_domestico, (void*)aviao);
        } else {
            aviao = add_plane_to_list(id_aviao, INTERNACIONAL);
            pthread_create(&nova_thread_aviao, NULL, voo_internacional, (void*)aviao);
        }

        pthread_mutex_lock(&list_mutex);
        add_thread_to_list(&plane_threads, nova_thread_aviao);
        aviao->thread_id = nova_thread_aviao;
        pthread_mutex_unlock(&list_mutex);

        usleep((500 + rand() % 500) * (5000 / FREQUENCIA_DE_AVIAO));
    }

    t_node_thread *current = plane_threads;
    if (!limite_excedido) {
        while (current != NULL) {
            current = current->next;
        }
    }

    current = plane_threads;
    t_node_thread *temp;
    while (current != NULL) {
        temp = current;
        pthread_join(current->thread_id, NULL);
        current = current->next;
        free(temp);
    }

    pthread_cancel(thr_plane_watcher);
    pthread_cancel(thr_resource_watcher);
    pthread_cancel(thr_deadlock_monitor);
    pthread_cancel(thr_priority_queue_monitor);

    endwin();

    print_log();

    free(pistas_rec);
    free(portoes_embarque_rec);
    free(torre_rec);

    return 0;
}