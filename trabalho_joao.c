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


pthread_mutex_t ncurses_mutex;
pthread_mutex_t list_mutex;
pthread_mutex_t alocacao_mutex;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t manager_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t recurso_liberado_cond = PTHREAD_COND_INITIALIZER;

#define N_PISTAS 3 //3
#define N_PORTOES 5 //5
#define N_TORRE 1 //1
#define TEMPO_SIMULACAO 120
#define TEMPO_ALERTA 10
#define TEMPO_CRITICO 20
#define FREQUENCIA_DE_AVIAO 100 // nao pode ser 0
#define MAX_AVIOES 2 

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

t_recurso pistas_rec[N_PISTAS];
t_recurso portoes_embarque_rec[N_PORTOES];
t_recurso torre_rec[N_TORRE*2];

typedef struct node_log {
    char tag[32];
    char tipo[32];
    char final[32];
    char motivo[32];
    char status_final[32];
    int id;
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

// [Example] Log structure
// [ERRO] [ID: 1] [Tipo: DOMESTICO] [Status: CAIU] [Final: CANCELADO] [MOTIVO: TIMEOUT]
// [SUCESSO] [ID: 2] [Tipo: INTERNACIONAL] [Status: DECOLOU] [Final: CONCLUÍDO] [MOTIVO: NONE]
// [ERRO] [ID: 3] [Tipo: DOMESTICO] [Status: POUSOU] [Final: CANCELADO] [MOTIVO: DEADLOCK]
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
    bool em_alerta_critico;

} t_node_aviao;

t_node_aviao *head = NULL;
void add_log_entry(const char *tag, int id, const char *tipo, const char *status_final, const char *final, const char *motivo) {
    pthread_mutex_lock(&log_mutex);
    
    node_log *new_entry = malloc(sizeof(node_log));
    snprintf(new_entry->tag, sizeof(new_entry->tag), "%s", tag);
    snprintf(new_entry->tipo, sizeof(new_entry->tipo), "%s", tipo);
    snprintf(new_entry->status_final, sizeof(new_entry->status_final), "%s", status_final);
    snprintf(new_entry->final, sizeof(new_entry->final), "%s", final);
    snprintf(new_entry->motivo, sizeof(new_entry->motivo), "%s", motivo);
    new_entry->id = id;
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
    while (current != NULL) {
        fprintf(stderr,"[%s] [ID: %d] [Tipo: %s] [Status: %s] [Final: %s] [Motivo: %s]\n", 
               current->tag, current->id, current->tipo, current->status_final, current->final, current->motivo);
        free_node = current;
        current = current->next;
        free(free_node);
    }

    fprintf(stderr, "\nEstatísticas:\n");
    fprintf(stderr, "Tempo de simulação: %d segundos\n", TEMPO_SIMULACAO);
    fprintf(stderr, "Total de voos: %d\n", stats.total_voos);
    fprintf(stderr, "Aviões com sucesso: %d\n", stats.avioes_sucesso);
    fprintf(stderr, "Aviões com falha: %d\n", stats.avioes_falha);
    fprintf(stderr, "Falhas por deadlock: %d\n", stats.falha_por_deadlock);
    fprintf(stderr, "Falhas por timeout: %d\n", stats.falha_por_timeout);

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

bool solicitar_recurso(t_node_aviao *aviao, recurso_tipo tipo_desejado) {
    pthread_mutex_lock(&manager_mutex);
    
    aviao->tempo_inicio_espera = time(NULL);
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
        if (aviao->tipo == DOMESTICO && !aviao->em_alerta_critico) {
            if (criticos_esperando > 0 || internacionais_nao_criticos_esperando > 0) {
                deve_esperar_por_prioridade = true;
            }
        } else if (aviao->tipo == INTERNACIONAL && !aviao->em_alerta_critico) {
            if (criticos_esperando > 0) {
                deve_esperar_por_prioridade = true;
            }
        }

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

        if (aviao->em_alerta_critico) criticos_esperando++;
        else if (aviao->tipo == INTERNACIONAL) internacionais_nao_criticos_esperando++;
        

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int wait_result = pthread_cond_timedwait(&recurso_liberado_cond, &manager_mutex, &ts);

        if (aviao->em_alerta_critico) criticos_esperando--;
        else if (aviao->tipo == INTERNACIONAL) internacionais_nao_criticos_esperando--;

        if (wait_result == ETIMEDOUT){
            time_t tempo_espera = time(NULL) - aviao->tempo_inicio_espera;

            if(tempo_espera == 0) aviao->tempo_inicio_espera = time(NULL);

            if (tempo_espera > TEMPO_CRITICO) {
                aviao->status = CAIU;
                aviao->esperando_por = NENHUM;
                pthread_mutex_unlock(&manager_mutex);
                add_log_entry("ERRO", aviao->id, (aviao->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", "CAIU", "CANCELADO", "TIMEOUT");
                return false;
            } else if (tempo_espera > TEMPO_ALERTA && !aviao->em_alerta_critico) {
                aviao->em_alerta_critico = true;
            }
        }
    }
}

void liberar_recurso(t_node_aviao *aviao, recurso_tipo tipo_a_liberar)
{
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

void print_aviao_list()
{
    pthread_mutex_lock(&list_mutex);
    
    t_node_aviao *current = head;

    while(current != NULL)
    {
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

    add_log_entry("SUCESSO", aviao->id, (aviao->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", "DECOLOU", "CONCLUÍDO", "NONE");

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

    add_log_entry("SUCESSO", aviao->id, (aviao->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", "DECOLOU", "CONCLUÍDO", "NONE");

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
    pthread_mutex_lock(&ncurses_mutex);
    WINDOW *win = newwin(3, 30, 0, 50);
    box(win, 0, 0);
    if (deadlock) {
        mvwprintw(win, 1, 1, "Deadlock detected!");
    } else {
        mvwprintw(win, 1, 1, "No deadlock.");
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

        memcpy(pistas_snapshot, pistas_rec, sizeof(pistas_rec));
        memcpy(portoes_snapshot, portoes_embarque_rec, sizeof(portoes_embarque_rec));
        memcpy(torre_snapshot, torre_rec, sizeof(torre_rec));

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
                    add_log_entry("ERRO", vitima->id, (vitima->tipo == DOMESTICO) ? "DOMESTICO" : "INTERNACIONAL", tipo_str, "CANCELADO", "DEADLOCK");
                    pthread_cancel(vitima->thread_id);

                }
            } else {
                deadlock_watcher(false);
            }

        }
    }
}

void* resource_watcher(void *arg) {

    WINDOW *win = newwin(5, 50, 0, 0);
    box(win, 0, 0);

    while (1) {
        int valor_pistas = 0, valor_portoes = 0, valor_torre = 0;

        pthread_mutex_lock(&ncurses_mutex);
        pthread_mutex_lock(&manager_mutex);

        for(int i = 0; i < N_PISTAS; i++) {
            if (pistas_rec[i].em_use) {
                valor_pistas++;
            }
        }
        for(int i = 0; i < N_PORTOES; i++) {
            if (portoes_embarque_rec[i].em_use) {
                valor_portoes++;
            }
        }
        for(int i = 0; i < N_TORRE * 2; i++) {
            if (torre_rec[i].em_use) {
                valor_torre++;
            }
        }
        pthread_mutex_unlock(&manager_mutex);

        mvwprintw(win, 0, 15, "Recursos disponíveis");
        mvwprintw(win, 1, 2, "Pistas: %d / %d", (N_PISTAS - valor_pistas), N_PISTAS);
        mvwprintw(win, 2, 2, "Portões: %d / %d", (N_PORTOES - valor_portoes), N_PORTOES);
        mvwprintw(win, 3, 2, "Torre de Controle: %d / %d", valor_torre, (2 * N_TORRE));
        wrefresh(win);
        pthread_mutex_unlock(&ncurses_mutex);
        usleep(100000);
    }

    endwin();
    return NULL;
}

void* plane_watcher(void *arg) {

    WINDOW *win = newwin(0,50,5,0);
    char msg[128];

    while(1) {
        pthread_mutex_lock(&ncurses_mutex);
        t_node_aviao *avioes = head;
        werase(win);
        box(win,0,0);
        mvwprintw(win, 0, 15, "Aviões em Voo");

        while(avioes != NULL){

            if(avioes->index > 15) break;

            char type_str[32];
            switch (avioes->tipo)
            {
            case DOMESTICO:
                snprintf(type_str, sizeof(type_str), "Doméstico");
                break;
            case INTERNACIONAL:
                snprintf(type_str, sizeof(type_str), "Internacional");
                break;
            }
            char status_str[32];
            switch (avioes->status)
            {
                case EM_APROXIMACAO:
                    snprintf(status_str, sizeof(status_str), "Em Aproximação");
                    break;
                case POUSOU: 
                    snprintf(status_str, sizeof(status_str), "Pousou");
                    break;
                case DESEMBARCOU:
                    snprintf(status_str, sizeof(status_str), "Desembarcou");
                    break;
                case DECOLOU:
                    snprintf(status_str, sizeof(status_str), "Decolou");
                    break;
                case CAIU:
                    snprintf(status_str, sizeof(status_str), "Caiu");
                    break;
            }

            snprintf(msg, sizeof(msg), "ID: %d, Tipo: %s, Status: %s", avioes->id, type_str, status_str);
            mvwprintw(win, avioes->index + 1, 2, "%s", msg);
            avioes = avioes->next;
        }
        
        wrefresh(win);
        pthread_mutex_unlock(&ncurses_mutex);
        usleep(100000);
    }
 
    return NULL;
}



int main(int argc, char *argv[]) {

    setlocale(LC_ALL, "");

    initscr();
    noecho();
    curs_set(0);

    pthread_mutex_init(&ncurses_mutex, NULL);

    pthread_t thr_plane_watcher;
    pthread_t thr_resource_watcher;
    pthread_t thr_deadlock_monitor;
    pthread_create(&thr_resource_watcher, NULL, resource_watcher, NULL); 
    pthread_create(&thr_plane_watcher, NULL, plane_watcher, NULL);
    pthread_create(&thr_deadlock_monitor, NULL, deadlock_monitor, NULL);
    sleep(1); // Aguardar o watcher iniciar
   
    t_node_thread *plane_threads = NULL;

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

    time_t tempo_inicio = time(NULL);
    srand(tempo_inicio);
    int tempo_simulacao_segundos = TEMPO_SIMULACAO;
    int id_aviao = 0;
    bool limite_excedido = false;
    while ((time(NULL) - tempo_inicio) < tempo_simulacao_segundos)
    {
        id_aviao++;
        pthread_t nova_thread_aviao;

        t_node_aviao *aviao = NULL;

        if (id_aviao > MAX_AVIOES) {
            limite_excedido = true;
            break;
        }

        if (rand() % 2 == 0)
        {
            aviao = add_plane_to_list(id_aviao, DOMESTICO);
            pthread_create(&nova_thread_aviao, NULL, voo_domestico, (void*)aviao);
        }
        else
        {
            aviao = add_plane_to_list(id_aviao, INTERNACIONAL);
            pthread_create(&nova_thread_aviao, NULL, voo_internacional, (void*)aviao);
        }
        pthread_mutex_lock(&list_mutex);
        add_thread_to_list(&plane_threads,nova_thread_aviao);
        aviao->thread_id = nova_thread_aviao;
        pthread_mutex_unlock(&list_mutex);

        usleep((500+ rand() % 500) * (5000/FREQUENCIA_DE_AVIAO));
        
    }

    t_node_thread *current = plane_threads;
    if (!limite_excedido){
        while(current != NULL) {
            pthread_cancel(current->thread_id);
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
    
    endwin();

    print_log();


    return 0;
}