#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/random.h>

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

void
burn_cpu(long us_len)
{
#define E9 1000000000L

	long s_len, ns_len;
	struct timespec end;
	if(clock_gettime(CLOCK_MONOTONIC, &end) != 0)
	{
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}

	//printf("now sec=%ld nsec=%ld\n", end.tv_sec, end.tv_nsec);

	ns_len = us_len * 1000;
	s_len = ns_len / E9;
	ns_len -= s_len * E9;

	end.tv_nsec += ns_len;
	end.tv_sec += s_len;

	/* Compute the end timespec by adding us_len */
	if(end.tv_nsec > E9)
	{
		end.tv_sec += 1;
		end.tv_nsec -= 1000000000L;
	}

	//printf("end sec=%ld nsec=%ld\n", end.tv_sec, end.tv_nsec);

	while(!is_toasted(&end))
	{
		burn();
	}
#undef E9
}

static void
usage(int argc, char *argv[])
{
	fprintf(stderr, "Usage: %s [-w ms] [-W ms] [-b ms] [-B ms] [-n N]\n",
			argv[0]);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	int opt, always=1;

	/* Wait between 5 to 60 seconds before next noise */
	long wait_min = 5000, wait_max = 60000;

	/* Noise duration between 200 and 5000 ms */
	long burn_min = 200, burn_max = 5000;

	/* Actual random times */
	long wait, burn;
	long i, loops;

	while((opt = getopt(argc, argv, "w:W:b:B:n:")) != -1)
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
			default: /* ? */
				usage(argc, argv);
				break;
		}
	}

	for(i=0; always || i<loops; i++)
	{
		wait = random_interval(wait_min, wait_max);
		printf("%.3f W %ld\n", get_timestamp(), wait);
		usleep(wait * 1000);

		burn = random_interval(burn_min, burn_max);
		printf("%.3f B %ld\n", get_timestamp(), burn);
		burn_cpu(burn * 1000);
	}

	return 0;
}
