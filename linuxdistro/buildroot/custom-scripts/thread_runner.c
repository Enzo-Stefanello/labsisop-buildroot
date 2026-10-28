#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>

#ifndef SCHED_LOW_IDLE
#define SCHED_LOW_IDLE 7
#endif

static const char *policy_name(int policy)
{
	switch (policy) {
	case SCHED_OTHER:
		return "SCHED_NORMAL";

	case SCHED_IDLE:
		return "SCHED_IDLE";

	case SCHED_LOW_IDLE:
		return "SCHED_LOW_IDLE";

	default:
		return "UNKNOWN";
	}
}

static void *worker(void *arg)
{
	int policy = *(int *)arg;
	struct sched_param param = {
		.sched_priority = 0
	};
	cpu_set_t cpuset;
	unsigned long long counter = 0;

	/*
	 * Executa a thread na CPU 0.
	 */
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);

	if (pthread_setaffinity_np(pthread_self(),
				   sizeof(cpuset),
				   &cpuset) != 0) {
		perror("pthread_setaffinity_np");
	}

	/*
	 * Define a politica de escalonamento escolhida.
	 */
	if (sched_setscheduler(0, policy, &param) == -1) {
		perror("sched_setscheduler");
		return NULL;
	}

	printf("Thread iniciada com %s (policy=%d)\n",
	       policy_name(policy),
	       sched_getscheduler(0));

	/*
	 * Mantem a thread utilizando CPU para permitir
	 * observar o comportamento do escalonador.
	 */
	for (;;) {
		counter++;

		if ((counter % 100000000ULL) == 0) {
			printf("%s: %llu\n",
			       policy_name(policy),
			       counter);
		}
	}

	return NULL;
}

int main(int argc, char **argv)
{
	pthread_t thread;
	int policy;

	if (argc != 2) {
		printf("Uso: %s normal|idle|low_idle\n", argv[0]);
		return 1;
	}

	if (!strcmp(argv[1], "normal")) {
		policy = SCHED_OTHER;
	} else if (!strcmp(argv[1], "idle")) {
		policy = SCHED_IDLE;
	} else if (!strcmp(argv[1], "low_idle")) {
		policy = SCHED_LOW_IDLE;
	} else {
		printf("Politica invalida: %s\n", argv[1]);
		return 1;
	}

	if (pthread_create(&thread, NULL, worker, &policy) != 0) {
		perror("pthread_create");
		return 1;
	}

	pthread_join(thread, NULL);

	return 0;
}