
#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SCHED_LOW_IDLE
#define SCHED_LOW_IDLE 7
#endif

#define MAX_THREADS 26

/* Buffer e posicao compartilhados pelas threads. */
static char *shared_buffer;
static size_t buffer_size;
static size_t next_position;

/* Sincronizacao apenas para o inicio do experimento. */
static pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;

static unsigned int ready_threads;
static int start_released;
static int start_aborted;

struct worker_arg {
    unsigned int index;
    char letter;
};

static int parse_positive(const char *text, unsigned long long *value)
{
    char *end;
    unsigned long long parsed;

    if (*text == '\0' || *text == '-' || *text == '+')
        return -1;

    errno = 0;
    parsed = strtoull(text, &end, 10);

    if (errno != 0 || *end != '\0' || parsed == 0)
        return -1;

    *value = parsed;
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

static void *worker(void *argument)
{
    const struct worker_arg *arg = argument;
    int should_abort;

    /*
     * Espera todas as threads serem criadas e configuradas.
     */
    pthread_mutex_lock(&start_mutex);

    ready_threads++;
    pthread_cond_signal(&ready_cond);

    while (!start_released)
        pthread_cond_wait(&start_cond, &start_mutex);

    should_abort = start_aborted;
    pthread_mutex_unlock(&start_mutex);

    if (should_abort)
        return NULL;

    /*
     * Reserva uma posicao exclusiva de forma atomica.
     * Nao utiliza mutex nem sched_yield no processamento.
     */
    for (;;) {
        size_t pos = __atomic_fetch_add(
            &next_position, 1, __ATOMIC_RELAXED
        );

        if (pos >= buffer_size)
            break;

        shared_buffer[pos] = arg->letter;
    }

    return NULL;
}

static void release_workers(int abort_run)
{
    pthread_mutex_lock(&start_mutex);

    start_aborted = abort_run;
    start_released = 1;

    pthread_cond_broadcast(&start_cond);
    pthread_mutex_unlock(&start_mutex);
}

int main(int argc, char **argv)
{
    unsigned long long requested_size, requested_threads;
    unsigned int num_threads, i, created = 0;

    pthread_t threads[MAX_THREADS];
    struct worker_arg args[MAX_THREADS];

    size_t blocks[MAX_THREADS] = {0};
    char previous = '\0';

    struct sched_param param = {0};
    struct sched_param current_param;

    int policy, ret, current_policy;
    int result = EXIT_FAILURE;

    /* Validacao dos parametros. */
    if (argc != 4 ||
        parse_positive(argv[1], &requested_size) != 0 ||
        parse_positive(argv[2], &requested_threads) != 0 ||
        requested_threads > MAX_THREADS ||
        requested_size > SIZE_MAX - 1 - MAX_THREADS ||
        (policy = parse_policy(argv[3])) < 0) {

        fprintf(stderr,
            "Uso: %s <buffer_size> <num_threads> "
            "<low_idle|idle|fifo|rr>\n"
            "  buffer_size > 0; num_threads entre 1 e %d\n",
            argv[0], MAX_THREADS);

        return EXIT_FAILURE;
    }

    buffer_size = (size_t)requested_size;
    num_threads = (unsigned int)requested_threads;

    /*
     * FIFO e RR usam a menor prioridade de tempo real.
     * IDLE e LOW_IDLE usam prioridade estatica zero.
     */
    if (policy == SCHED_FIFO || policy == SCHED_RR) {
        int min = sched_get_priority_min(policy);
        int max = sched_get_priority_max(policy);

        if (min < 0 || max < 0 || min > max) {
            perror("sched_get_priority_min/max");
            return EXIT_FAILURE;
        }

        param.sched_priority = min;
    } else {
        param.sched_priority = 0;
    }

    /* Alocacao do buffer global. */
    shared_buffer = malloc(buffer_size + 1);

    if (!shared_buffer) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    /* Criacao das threads. */
    for (i = 0; i < num_threads; i++) {
        args[i].index = i;
        args[i].letter = (char)('A' + i);

        ret = pthread_create(
            &threads[i], NULL, worker, &args[i]
        );

        if (ret != 0) {
            fprintf(stderr,
                "pthread_create thread %u: %s\n",
                i, strerror(ret));
            goto abort_and_join;
        }

        created++;
    }

    /* Espera todas chegarem a barreira inicial. */
    pthread_mutex_lock(&start_mutex);

    while (ready_threads != num_threads)
        pthread_cond_wait(&ready_cond, &start_mutex);

    pthread_mutex_unlock(&start_mutex);

    /*
     * Configura e verifica a politica de cada thread.
     * Ainda nenhuma delas iniciou o preenchimento.
     */
    for (i = 0; i < num_threads; i++) {
        ret = pthread_setschedparam(
            threads[i], policy, &param
        );

        if (ret != 0) {
            fprintf(stderr,
                "pthread_setschedparam thread %u: %s\n",
                i, strerror(ret));
            goto abort_and_join;
        }

        ret = pthread_getschedparam(
            threads[i], &current_policy, &current_param
        );

        if (ret != 0 ||
            current_policy != policy ||
            current_param.sched_priority != param.sched_priority) {

            fprintf(stderr,
                "Politica/prioridade nao confirmada "
                "na thread %u\n", i);
            goto abort_and_join;
        }
    }

    printf(
        "Politica: %s (%d), prioridade: %d, "
        "threads: %u, buffer: %zu\n",
        argv[3], policy, param.sched_priority,
        num_threads, buffer_size
    );

    fflush(stdout);

    /* Libera simultaneamente as threads prontas. */
    release_workers(0);

    for (i = 0; i < created; i++)
        pthread_join(threads[i], NULL);

    shared_buffer[buffer_size] = '\0';

    /* Buffer original. */
    printf("\nBuffer completo:\n%s\n", shared_buffer);

    /*
     * Pos-processamento: comprime sequencias consecutivas
     * e contabiliza os blocos observados de cada thread.
     */
    printf("\nBuffer resumido:\n");

    for (size_t pos = 0; pos < buffer_size; pos++) {
        char letter = shared_buffer[pos];

        if (letter != previous) {
            putchar(letter);

            blocks[(unsigned int)(letter - 'A')]++;
            previous = letter;
        }
    }

    putchar('\n');

    printf(
        "\nBlocos por thread "
        "(nao equivalem a sched_switch):\n"
    );

    for (i = 0; i < num_threads; i++) {
        printf(
            "Thread %u (%c): %zu\n",
            i, args[i].letter, blocks[i]
        );
    }

    result = EXIT_SUCCESS;
    goto cleanup;

abort_and_join:
    /* Libera as threads sem executar o experimento. */
    release_workers(1);

    for (i = 0; i < created; i++)
        pthread_join(threads[i], NULL);

cleanup:
    free(shared_buffer);
    return result;
}
