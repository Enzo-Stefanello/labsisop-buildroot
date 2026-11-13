
#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#ifndef RUSAGE_THREAD
#define RUSAGE_THREAD 1
#endif
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SCHED_LOW_IDLE
#define SCHED_LOW_IDLE 7
#endif

#define MAX_THREADS 4
#define SUMMARY_LIMIT 120

/* Buffer compartilhado pelas threads. */
static char *shared_buffer;
static size_t buffer_size;
static size_t next_position;

/* Semaforos para sincronizacao inicial. */
static sem_t sem_ready;
static sem_t sem_start;
static int abort_run;

struct worker_arg {
    char letter;
    const char *policy_name;
    int policy;
    int priority;
    pid_t tid;

    size_t characters;

    int stats_valid;
    struct rusage before;
    struct rusage after;
};

static int parse_positive(
    const char *text,
    unsigned long long *value)
{
    char *end;
    unsigned long long number;

    if (*text == '\0' ||
        *text == '-' ||
        *text == '+')
        return -1;

    errno = 0;
    number = strtoull(text, &end, 10);

    if (errno != 0 ||
        *end != '\0' ||
        number == 0)
        return -1;

    *value = number;
    return 0;
}

static int parse_policy(const char *name)
{
    if (strcmp(name, "low_idle") == 0)
        return SCHED_LOW_IDLE;

    if (strcmp(name, "idle") == 0)
        return SCHED_IDLE;

    if (strcmp(name, "fifo") == 0)
        return SCHED_FIFO;

    if (strcmp(name, "rr") == 0)
        return SCHED_RR;

    return -1;
}

/* Repete sem_wait caso seja interrompido por sinal. */
static int wait_sem(sem_t *sem)
{
    int ret;

    do {
        ret = sem_wait(sem);
    } while (ret == -1 && errno == EINTR);

    return ret;
}

static double timeval_seconds(struct timeval value)
{
    return (double)value.tv_sec +
           (double)value.tv_usec / 1000000.0;
}

/* Funcao executada por cada thread. */
static void *worker(void *pointer)
{
    struct worker_arg *arg = pointer;
    size_t count = 0;
    int first_ok;

    arg->tid = (pid_t)syscall(SYS_gettid);

    /*
     * Informa a thread principal que esta pronta.
     * Aguarda a liberacao para iniciar o trabalho.
     */
    if (sem_post(&sem_ready) == -1)
        return NULL;

    if (wait_sem(&sem_start) == -1 || abort_run)
        return NULL;

    /* Inicia a medicao da thread. */
    first_ok = (
        getrusage(RUSAGE_THREAD, &arg->before) == 0
    );

    /*
     * Reserva posicoes exclusivas no buffer.
     * Sem mutex ou sched_yield durante o preenchimento.
     */
    for (;;) {
        size_t pos = __atomic_fetch_add(
            &next_position, 1, __ATOMIC_RELAXED
        );

        if (pos >= buffer_size)
            break;

        shared_buffer[pos] = arg->letter;
        count++;
    }

    arg->characters = count;

    /* Finaliza a medicao da thread. */
    arg->stats_valid = first_ok &&
        (getrusage(RUSAGE_THREAD, &arg->after) == 0);

    return NULL;
}

static void wake_workers(unsigned int count, int cancel)
{
    unsigned int i;

    abort_run = cancel;

    for (i = 0; i < count; i++)
        sem_post(&sem_start);
}

int main(int argc, char **argv)
{
    unsigned long long requested_size;
    unsigned long long requested_threads;

    unsigned int n, i, created = 0;
    int ret, result = EXIT_FAILURE;
    int needs_rt_release = 0;

    pthread_t threads[MAX_THREADS];
    struct worker_arg args[MAX_THREADS] = {{0}};

    size_t blocks[MAX_THREADS] = {0};
    size_t total_blocks = 0;
    double cpu_time[MAX_THREADS] = {0};
    double total_cpu = 0.0;

    char previous = '\0';

    struct timeval wall_before, wall_after;

    struct sched_param original_param;
    struct sched_param elevated_param = {0};

    int original_policy = SCHED_OTHER;
    int main_elevated = 0;

    /* Validacao dos argumentos. */
    if (argc < 4 ||
        parse_positive(
            argv[1], &requested_size) != 0 ||
        parse_positive(
            argv[2], &requested_threads) != 0 ||
        (requested_threads != 2 &&
         requested_threads != 4) ||
        requested_size >= SIZE_MAX ||
        (argc != 4 &&
         argc != 3 + (int)requested_threads)) {

        fprintf(stderr,
            "Uso: %s <buffer_size> <2|4> <politica>\n"
            "  ou: %s <buffer_size> <2|4> "
            "<politica_por_thread...>\n"
            "Politicas: low_idle, idle, fifo, rr\n",
            argv[0], argv[0]);

        return EXIT_FAILURE;
    }

    n = (unsigned int)requested_threads;
    buffer_size = (size_t)requested_size;

    /*
     * Define individualmente a politica de cada thread.
     * Uma politica unica continua funcionando como antes.
     */
    for (i = 0; i < n; i++) {
        const char *name =
            argv[argc == 4 ? 3 : 3 + i];

        int policy = parse_policy(name);

        if (policy < 0) {
            fprintf(stderr,
                "Politica invalida para a thread %u: %s\n",
                i, name);
            return EXIT_FAILURE;
        }

        args[i].letter = (char)('A' + i);
        args[i].policy_name = name;
        args[i].policy = policy;
        args[i].priority = 0;

        if (policy == SCHED_FIFO ||
            policy == SCHED_RR) {

            args[i].priority =
                sched_get_priority_min(policy);

            if (args[i].priority < 0) {
                perror("sched_get_priority_min");
                return EXIT_FAILURE;
            }

            needs_rt_release = 1;
        }
    }

    /* Aloca o buffer compartilhado. */
    shared_buffer = malloc(buffer_size + 1);

    if (!shared_buffer) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    /* Inicializa os semaforos com contador zero. */
    if (sem_init(&sem_ready, 0, 0) == -1) {
        perror("sem_init ready");
        goto free_buffer;
    }

    if (sem_init(&sem_start, 0, 0) == -1) {
        perror("sem_init start");
        goto destroy_ready;
    }

    /* Cria as threads. */
    for (i = 0; i < n; i++) {
        ret = pthread_create(
            &threads[i], NULL, worker, &args[i]
        );

        if (ret != 0) {
            fprintf(stderr,
                "pthread_create (%c): %s\n",
                args[i].letter, strerror(ret));
            goto abort_and_join;
        }

        created++;
    }

    /*
     * Espera todas as threads informarem que estao
     * prontas e bloqueadas no semaforo inicial.
     */
    for (i = 0; i < n; i++) {
        if (wait_sem(&sem_ready) == -1) {
            perror("sem_wait ready");
            goto abort_and_join;
        }
    }

    /* Configura e verifica cada politica. */
    for (i = 0; i < n; i++) {
        struct sched_param param = {0};
        struct sched_param verified_param;
        int verified_policy;

        param.sched_priority = args[i].priority;

        ret = pthread_setschedparam(
            threads[i], args[i].policy, &param
        );

        if (ret != 0) {
            fprintf(stderr,
                "pthread_setschedparam (%c): %s\n",
                args[i].letter, strerror(ret));
            goto abort_and_join;
        }

        ret = pthread_getschedparam(
            threads[i],
            &verified_policy,
            &verified_param
        );

        if (ret != 0 ||
            verified_policy != args[i].policy ||
            verified_param.sched_priority !=
                args[i].priority) {

            fprintf(stderr,
                "Politica nao confirmada: thread %c\n",
                args[i].letter);
            goto abort_and_join;
        }
    }

    printf("\n=== CONFIGURACAO ===\n");

    printf(
        "Buffer: %zu | Threads: %u | CPUs online: %ld\n",
        buffer_size, n,
        sysconf(_SC_NPROCESSORS_ONLN)
    );

    printf(
        "Sincronizacao: sem_ready + sem_start "
        "(semaforos POSIX)\n"
    );

    for (i = 0; i < n; i++) {
        printf(
            "Thread %c (TID %ld): %s, prioridade %d\n",
            args[i].letter,
            (long)args[i].tid,
            args[i].policy_name,
            args[i].priority
        );
    }

    fflush(stdout);

    /*
     * Quando usamos FIFO ou RR, a principal recebe
     * temporariamente prioridade RT 2.
     *
     * Isso permite liberar todas as threads RT de
     * prioridade 1 antes que alguma monopolize a CPU.
     */
    if (needs_rt_release) {
        ret = pthread_getschedparam(
            pthread_self(),
            &original_policy,
            &original_param
        );

        if (ret != 0) {
            fprintf(stderr,
                "pthread_getschedparam (principal): %s\n",
                strerror(ret));
            goto abort_and_join;
        }

        elevated_param.sched_priority = 2;

        ret = pthread_setschedparam(
            pthread_self(),
            SCHED_FIFO,
            &elevated_param
        );

        if (ret != 0) {
            fprintf(stderr,
                "Nao foi possivel liberar RT em conjunto: %s\n",
                strerror(ret));
            goto abort_and_join;
        }

        main_elevated = 1;
    }

    /* Libera todas as threads para disputar a CPU. */
    gettimeofday(&wall_before, NULL);
    wake_workers(created, 0);

    /* Restaura a politica da thread principal. */
    if (main_elevated) {
        ret = pthread_setschedparam(
            pthread_self(),
            original_policy,
            &original_param
        );

        if (ret != 0) {
            fprintf(stderr,
                "Falha ao restaurar politica principal: %s\n",
                strerror(ret));
        }
    }

    /* Espera todas terminarem. */
    for (i = 0; i < created; i++)
        pthread_join(threads[i], NULL);

    gettimeofday(&wall_after, NULL);

    /* Calcula o consumo de CPU por thread. */
    for (i = 0; i < n; i++) {
        if (args[i].stats_valid) {
            cpu_time[i] =
                timeval_seconds(
                    args[i].after.ru_utime) -
                timeval_seconds(
                    args[i].before.ru_utime) +
                timeval_seconds(
                    args[i].after.ru_stime) -
                timeval_seconds(
                    args[i].before.ru_stime);

            total_cpu += cpu_time[i];
        }
    }

    printf("\n=== RESULTADOS ===\n");

    printf(
        "Tempo total (parede): %.3f s\n",
        timeval_seconds(wall_after) -
        timeval_seconds(wall_before)
    );

    /* Exibe as metricas individuais. */
    for (i = 0; i < n; i++) {
        double percent =
            100.0 * (double)args[i].characters /
            (double)buffer_size;

        printf(
            "\nThread %c | %s | TID %ld\n",
            args[i].letter,
            args[i].policy_name,
            (long)args[i].tid
        );

        printf(
            "  Caracteres: %zu (%.2f%%)\n",
            args[i].characters,
            percent
        );

        if (args[i].stats_valid) {
            long voluntary =
                args[i].after.ru_nvcsw -
                args[i].before.ru_nvcsw;

            long involuntary =
                args[i].after.ru_nivcsw -
                args[i].before.ru_nivcsw;

            printf(
                "  Escalonamentos no trabalho "
                "(estimativa): %ld\n",
                1 + voluntary + involuntary
            );

            printf(
                "  Trocas voluntarias: %ld | "
                "involuntarias: %ld\n",
                voluntary,
                involuntary
            );

            printf(
                "  Tempo CPU: %.3f s "
                "(%.2f%% da soma)\n",
                cpu_time[i],
                total_cpu > 0 ?
                    100.0 * cpu_time[i] / total_cpu :
                    0.0
            );
        } else {
            printf(
                "  Estatisticas: indisponiveis "
                "(getrusage)\n"
            );
        }
    }

    /*
     * Comprime o buffer e conta blocos.
     * Limita a impressao para nao inundar o terminal.
     */
    printf("\n=== BLOCOS ===\n");

    printf(
        "Buffer resumido (primeiros %d blocos): ",
        SUMMARY_LIMIT
    );

    for (size_t pos = 0; pos < buffer_size; pos++) {
        char letter = shared_buffer[pos];

        if (letter != previous) {
            unsigned int index =
                (unsigned int)(letter - 'A');

            total_blocks++;
            blocks[index]++;

            if (total_blocks <= SUMMARY_LIMIT)
                putchar(letter);

            previous = letter;
        }
    }

    if (total_blocks > SUMMARY_LIMIT)
        printf("... (resumo truncado)");

    putchar('\n');

    printf(
        "Blocos totais: %zu "
        "(nao equivalem a escalonamentos)\n",
        total_blocks
    );

    for (i = 0; i < n; i++) {
        printf(
            "Thread %c: %zu blocos\n",
            args[i].letter,
            blocks[i]
        );
    }

    result = EXIT_SUCCESS;
    goto cleanup;

abort_and_join:
    wake_workers(created, 1);

    for (i = 0; i < created; i++)
        pthread_join(threads[i], NULL);

cleanup:
    sem_destroy(&sem_start);

destroy_ready:
    sem_destroy(&sem_ready);

free_buffer:
    free(shared_buffer);
    return result;
}
