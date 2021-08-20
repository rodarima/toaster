#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/random.h>
#include <pthread.h>
#include <semaphore.h>

struct worker {
	pthread_t thread;
	volatile int do_work;
	volatile int should_die;
	sem_t sem;
};

/* Returns a random double from [0, 1] draw
 * from an uniform distribution */
static double
random_double()
{
	double x;
	uint64_t n;
	ssize_t ret;

	while(1)
	{
		ret = getrandom(&n, sizeof(uint64_t), 0);

		if(ret == sizeof(uint64_t))
			break;

		if(errno != EAGAIN)
		{
			perror("getrandom");
			exit(EXIT_FAILURE);
		}
	};

	x = (double) n;
	x /= ((uint64_t) -1UL);

	return x;
}

static long
random_interval(long a, long b)
{
	long x;
	
	x = a + (long) (random_double() * (double) (b - a));

	return x;
}

static double
get_timestamp()
{
	struct timespec now;
	if(clock_gettime(CLOCK_REALTIME, &now) != 0)
	{
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}

	return (double) now.tv_sec + (double) now.tv_nsec * 1.0E-9;
}

static int
is_toasted(struct timespec *end)
{
	struct timespec now;
	if(clock_gettime(CLOCK_MONOTONIC, &now) != 0)
	{
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}

	if(end->tv_sec > now.tv_sec)
	{
		//printf("not toasted yet endsec=%ld nowsec=%ld\n",
		//		end->tv_sec, now.tv_sec);
		return 0;
	}
	if(end->tv_nsec > now.tv_nsec)
	{
		//printf("not toasted yet endnsec=%ld nownsec=%ld\n",
		//		end->tv_nsec, now.tv_nsec);
		return 0;
	}

	return 1;
}

static void
burn()
{
	volatile double out;
	double e;
	long i, n;

	n = 10000L;
	e = 0.0;

	for(i=0; i<n; i++)
		e = sqrt((double) i + e);

	/* Don't optimize this dummy loop */
	out = e;
}

void *
worker_routine(void *arg)
{
	struct worker *data;

	data = (struct worker *) arg;

	while(1)
	{
		while(data->do_work)
			burn();

		if(data->should_die)
			break;

		if(sem_wait(&data->sem) != 0)
		{
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}
	}

	return NULL;
}

struct worker *
create_workers(int n)
{
	int i;
	struct worker *workers;

	workers = calloc(n, sizeof(struct worker));

	if(workers == NULL)
	{
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	for(i=0; i<n; i++)
	{
		if(sem_init(&workers[i].sem, 0, 0) != 0)
		{
			perror("sem_init");
			exit(EXIT_FAILURE);
		}

		workers[i].do_work = 0;
		workers[i].should_die = 0;

		if(pthread_create(&workers[i].thread, NULL,
				worker_routine, &workers[i]) != 0)
		{
			perror("pthread_create");
			exit(EXIT_FAILURE);
		}
	}

	return workers;
}

void
kill_workers(struct worker *workers, int n)
{
	int i;

	/* Unset the work flag and notify the workers */
	for(i=0; i<n; i++)
	{
		workers[i].should_die = 1;
		workers[i].do_work = 0;
		sem_post(&workers[i].sem);
	}

	/* Wait for the workers to die */
	for(i=0; i<n; i++)
	{
		if(pthread_join(workers[i].thread, NULL) != 0)
		{
			perror("pthread_join");
			exit(EXIT_FAILURE);
		}
	}
}

void
burn_cpu(struct worker *workers, int n, long us_len)
{
	int i;

	for(i=0; i<n; i++)
	{
		workers[i].do_work = 1;
		sem_post(&workers[i].sem);
	}

	usleep(us_len);

	for(i=0; i<n; i++)
		workers[i].do_work = 0;
}

static void
usage(int argc, char *argv[])
{
	fprintf(stderr, "Usage: %s [-w ms] [-W ms] [-b ms] [-B ms] [-n N] [-t threads]\n",
			argv[0]);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	struct worker *workers;
	int opt, always=1;

	/* Wait between 5 to 60 seconds before next noise */
	long wait_min = 5000, wait_max = 60000;

	/* Noise duration between 200 and 5000 ms */
	long burn_min = 200, burn_max = 5000;

	long nthreads = 1;

	/* Actual random times */
	long wait, burn;
	long i, loops;

	while((opt = getopt(argc, argv, "w:W:b:B:n:t:")) != -1)
	{
		switch(opt)
		{
			case 'n':
				loops = atoi(optarg);
				always = 0;
				break;
			case 'w':
				wait_min = atoi(optarg);
				break;
			case 'W':
				wait_max = atoi(optarg);
				break;
			case 'b':
				burn_min = atoi(optarg);
				break;
			case 'B':
				burn_max = atoi(optarg);
				break;
			case 't':
				nthreads = atoi(optarg);
				break;
			default: /* ? */
				usage(argc, argv);
				break;
		}
	}

	workers = create_workers(nthreads);

	for(i=0; always || i<loops; i++)
	{
		wait = random_interval(wait_min, wait_max);
		printf("%.3f W %ld\n", get_timestamp(), wait);
		usleep(wait * 1000);

		burn = random_interval(burn_min, burn_max);
		printf("%.3f B %ld\n", get_timestamp(), burn);
		burn_cpu(workers, nthreads, burn * 1000);
	}

	kill_workers(workers, nthreads);

	return 0;
}
