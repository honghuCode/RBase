/*
 *	sldg.c
 *
 *	DESCRIPTION:
 *		Random dataset generator for SKYLINE operator evaluation.
 *
 *	AUTHOR:
 *		based on the work by the authors of based [Borzsonyi2001]
 *		modified by Hannes Eder <Hannes@HannesEder.net>
 *		thanks to Donald Kossmann for providing the source code
 *		of the original implementation
 *
 *	REFERENCES:
 *		[Borzsonyi2001] B�rzs�nyi, S.; Kossmann, D. & Stocker, K.: 
 *		The Skyline Operator, ICDE 2001, 421--432
 */
#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <string.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "port.h"

/*
 * some macros for warnings/errors
 */
const char *progname = NULL;

#define warning(FMT, ...) fprintf(stderr, "%s: warning: " FMT "\n", progname, ##__VA_ARGS__)
#define invalidargs(FMT, ...) do { fprintf(stderr, "%s: error: " FMT "\n", progname, ##__VA_ARGS__); usage(); exit(1); } while (0)
#define fatal(FMT, ...) do { fprintf(stderr, "%s: error: " FMT "\n", progname, ##__VA_ARGS__); exit(1); } while (0)


#ifndef round
double round(double round);
// Based on http://forums.belution.com/en/cpp/000/050/13.shtml
double round(double value)
{
    if (value < 0)
        return -(floor(-value + 0.5));
    else
        return   floor( value + 0.5);
}
#endif


/*
 * forward decl's
 */
static double sqr(double a);

static void padding_init(void);
static void padding_done(void);

static int stats_vector_count;
static double *stats_sum_x;
static double *stats_sum_x_sqr;
static double *stats_sum_x_prod;

static void stats_init(int dim);
static void stats_enter(int dim, double *x);
static void stats_output(int dim);

static double random_equal(double min, double max);
static double random_peak(double min, double max, int dim);
static double random_normal(double med, double var);

static void output_vector(int dim, double *x, int type, double MAX_VALUE);
static int is_vector_ok(int dim, double *x, double MAX_VALUE);

static void generate_indep(int count, int dim, int type, double MAX_VALUE);
static void generate_corr(int count, int dim, int type, double MAX_VALUE);
static void generate_anti(int count, int dim, int type, double MAX_VALUE);

static void usage();

static int id = 0;
static int opt_id = 0;
static int opt_pad = 0;
static int opt_plain = 0;
static FILE *fd;
static int opt_copy = 0;
static int opt_create = 0;

static char    *pad_alphabet = "abcdefghijklmnopqrstuvwxyz";
static size_t	pad_alphabet_len = 0;
static char	   *padding = NULL;

/*
 * main
 */
int
main(int argc, char **argv)
{
	int c;
	int dim = 0;
	int type = 0;
	double MAX_VALUE;
	int count = 0;
	int dist = 0;
	int stats = 0;
	char table_name[64];
	char *user_table_name = NULL;

	/*
	 * setup progname
	 */
	if (argc>0)
		progname = basename(argv[0]);
	if (progname == NULL || progname[0] == '\0')
		progname = "randdataset";

	/*
	 * process command line arguments 
	 */
	while ((c = getopt(argc, argv, "icad:t:M:n:Is:p:PSCRT:h?")) != -1)
	{
		switch (c)
		{
		case 'i':
		case 'c':
		case 'a':
			if (dist != 0 && dist != c)
				invalidargs("distribution already selected, hint: use only one of -i | -c | -a");
			dist = c;
			break;

		case 'd':
			dim = atoi(optarg);
			break;

		case 't':
			type = atoi(optarg);
			break;

		case 'M':
			MAX_VALUE = atof(optarg);
			break;

		case 'n':
			count = atoi(optarg);
			break;

		case 'I':
			opt_id = 1;
			break;

		case 'P':
			opt_plain = 1;
			break;

		case 'C':
			opt_copy = 1;
			break;

		case 'R':
			opt_create = 1;
			opt_copy = 1;
			break;

		case 'T':
			user_table_name = optarg;
			break;

		case 'p':
			opt_pad = atoi(optarg);
			break;

		case 'S':
			stats = 1;
			break;

		case 's':
			srand(atoi(optarg));
			break;

		case 'h':
		case '?':
			usage();
			return 0;

		default:
			usage();
			return 1;
		}
	}

	/*
	 * check for additional arguments
	 */
	while (optind < argc)
	{
		warning("ignoring additional argument `%s'", argv[optind]);
		optind++;
	}

	char filename[64];
	sprintf(filename, "%cD%dT%dM%ldC%d", dist, dim, type, (long)MAX_VALUE, count);
	fd = fopen(filename, "w");

	/*
	 * perform sanity checks on arguments
	 */
	if (dist == 0)
		invalidargs("no distribution selected");

	if (dim < 1)
		invalidargs("dimension less than 1");

	if (dim < 2 && dist != 'i')
	{
		dist = 'i';
		warning("for 1 dimensional data, independent distribution is used");
	}

	if ((type != 1) && (type != 2))
		invalidargs("type must either 1 for integer or 2 for float");

	if (MAX_VALUE <= 0)
		invalidargs("max value of dimensions must be >=0");

	if (count < 0)
		invalidargs("the number of vectors must be non negative");

	if (opt_pad < 0)
		invalidargs("padding must be non negative");

	if (fd == NULL)
		invalidargs("output file not valid");

	if (opt_pad > 10240)
		invalidargs("padding is restricted to 10KB");

	/*
	 * here we go
	 */
	padding_init();
	stats_init(dim);

	if (opt_create)
	{
		int		i;

		if (user_table_name)
		{
			strncpy(table_name, user_table_name, sizeof(table_name));
		}
		else
		{
			if (opt_pad)
				snprintf(table_name, sizeof(table_name), "%c%dd%dp%d", dist, dim, count, opt_pad);
			else
				snprintf(table_name, sizeof(table_name), "%c%dd%d", dist, dim, count);
		}

		fprintf(stdout, "DROP TABLE IF EXISTS \"%s\";\n", table_name);
		fprintf(stdout, "CREATE TABLE \"%s\" (", table_name);

		if (opt_id)
			fprintf(stdout, "id int, ");

		for (i = 1; i <= dim; ++i)
			fprintf(stdout, "%sd%d float", (i>1 ? ", ": ""), i);

		if (opt_pad)
			fprintf(stdout, ", pad%d varchar(%d)", opt_pad, opt_pad);

		fprintf(stdout, ");\n");
	}

	if (opt_copy)
	{
		int		i;
		fprintf(stdout, "COPY \"%s\" (", table_name);

		if (opt_id)
			fprintf(stdout, "id, ");

		for (i = 1; i <= dim; ++i)
			fprintf(stdout, "%sd%d", (i>1 ? ", ": ""), i);

		if (opt_pad)
			fprintf(stdout, ", pad%d", opt_pad);

		fprintf(stdout, ") FROM STDIN DELIMITERS ',' CSV QUOTE '''';\n");
	}

	if (opt_plain)
	{
//		int d;
//		fprintf(fd, "%d\n", dim);
//		for (d = dim-1; d >= 0; d--)
//			fprintf(fd, "%ld%s", (long)MAX_VALUE, d ? " " : "");
//		fprintf(fd, "\n%d\n", count);
	}

	switch (dist)
	{
	case 'i':
		generate_indep(count, dim, type, MAX_VALUE);
		break;
	case 'c':
		generate_corr(count, dim, type, MAX_VALUE);
		break;
	case 'a':
		generate_anti(count, dim, type, MAX_VALUE);
		break;
	default:
		fatal("distribution `%c' is not supported", dist);
		break;
	}

	if (opt_copy)
	{
		fprintf(stdout, "\\.\n");
	}

	if (stats)
		stats_output(dim);

	padding_done();
	
	fclose(fd);

	return 0;
}

/*
 * sqr
 */
static double
sqr(double a)
{
	return a*a;
}

/*
 * padding_init
 */
static void
padding_init(void)
{
	if (opt_pad)
	{
		size_t	pad_len;
		size_t	i;
		char   *p;

		assert(pad_alphabet);
		pad_alphabet_len = strlen(pad_alphabet);
		assert(pad_alphabet_len > 0);

		pad_len = ((opt_pad+pad_alphabet_len) / pad_alphabet_len + 1) * pad_alphabet_len;

		padding = (char *) malloc(pad_len + 1);
		assert(padding);

		for (p = padding, i = 0; i<pad_len; ++i)
		{
			*p++ = pad_alphabet[i % pad_alphabet_len];
		}
		*p++ = '\0';
	}
}

/*
 * padding_done
 */
static void
padding_done(void)
{
	if (padding)
	{
		free(padding);
		padding = NULL;
	}
}

/*
 * stats_init
 *
 *	inits the statistics, what else ;) 
 */
static void
stats_init(int dim)
{
	int		d;

	/*
	 * We don't explicitly free this buffers, since they are freed when
	 * the program terminates anyway.
	 */
	stats_sum_x = (double *) malloc(dim * sizeof(double));
	stats_sum_x_sqr = (double *) malloc(dim * sizeof(double));
	stats_sum_x_prod = (double *) malloc(dim * dim * sizeof(double));

	stats_vector_count = 0;

	for (d = 0; d < dim; d++)
	{
		int		dd;

		stats_sum_x[d] = 0.0;
		stats_sum_x_sqr[d] = 0.0;
		for (dd = 0; dd < dim; dd++)
			stats_sum_x_prod[d * dim + dd] = 0.0;
	}
}


/*
 * stats_enter
 *
 *	adds the vector x[0..dim-1] into the stats
 */
static void
stats_enter(int dim, double *x)
{
	int		d;
	stats_vector_count++;
	for (d = 0; d < dim; d++)
	{
		int		dd;

		stats_sum_x[d] += x[d];
		stats_sum_x_sqr[d] += x[d] * x[d];
		for (dd = 0; dd < dim; dd++)
			stats_sum_x_prod[d * dim + dd] += x[d] * x[dd];
	}
}

/*
 * stats_output
 *
 *	write the statistics to STDERR
 */
static void
stats_output(int dim)
{
	int		d;

	/*
	 *	mean, var, sd
	 */
	for (d = 0; d < dim; d++)
	{
		double E = stats_sum_x[d] / stats_vector_count;
		double V = stats_sum_x_sqr[d] / stats_vector_count - E * E;
		double s = sqrt(V);
		fprintf(stderr, "-- E[X%d]=%5.2f Var[X%d]=%5.2f s[X%d]=%5.2f\n", d + 1, E, d + 1, V,
			d + 1, s);
	}

	/*
	 * correlation factor matrix
	 */
	fprintf(stderr, "--\n-- correlation factor matrix:\n");
	for (d = 0; d < dim; d++) {
		int		dd;

		fprintf(stderr, "--");
		for (dd = 0; dd < dim; dd++) {
			double cov =
				(stats_sum_x_prod[d * dim + dd] / stats_vector_count) -
				(stats_sum_x[d] / stats_vector_count) * (stats_sum_x[dd] /
				stats_vector_count);
			double cor =
				cov / sqrt(stats_sum_x_sqr[d] / stats_vector_count -
				sqr(stats_sum_x[d] / stats_vector_count)) /
				sqrt(stats_sum_x_sqr[dd] / stats_vector_count -
				sqr(stats_sum_x[dd] / stats_vector_count));
			fprintf(stderr, " %5.2f", cor);
		}
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "--\n-- %d vector(s) generated\n", stats_vector_count);
}

/*
 * random_equal
 *
 *	returns a random value x \in [min,max]	
 */
static double
random_equal(double min, double max)
{
	double x = (double) rand() / RAND_MAX;
	return x * (max - min) + min;
}

/*
 * random_peak
 *
 *	Returns a random value x \in [min,max) as sum of dim equally 
 *	distributed random values.
 */
static double
random_peak(double min, double max, int dim)
{
	int		d;
	double	sum = 0.0;

	for (d = 0; d < dim; d++)
		sum += random_equal(0, 1);
	sum /= dim;
	return sum * (max - min) + min;
}

/*
 * random_normal
 *
 *	Returns a normally distributed random value x \in (med-var,med+var)
 *	with E[x] = med.
 *
 *	NOTE: This implementation works well if the random values returned by
 *	the underlaying random_equal are sufficiently independent.
 */
static double
random_normal(double med, double var)
{
	return random_peak(med - var, med + var, 12);
}

/*
 * output_vector
 *
 *	Writes the vector x[0..dim-1] to stdout, separated by spaces and with a
 *	trailing linefeed.
 */
static void
output_vector(int dim, double *x, int type, double MAX_VALUE)
{
	++id;
	if (opt_id)
	{
		if (opt_plain)
			fprintf(fd, "%d,", id);
		else
			fprintf(stdout, "%d,", id);
	}

	while (dim--)
        {
		if (type == 1)
		{
			if (opt_plain)
				fprintf(fd, "%d%s", (long)round((*(x++))), dim ? " " : "");
			else
				fprintf(stdout, "%d%s", (long)round((*(x++))), dim ? " " : "");
		}
		else
		{
			if (opt_plain)
				fprintf(fd, "%.*e%s", DBL_DIG, *(x++), dim ? " " : "");
			else
				fprintf(stdout, "%.*e%s", DBL_DIG, *(x++), dim ? " " : "");
		}
	}

	if (opt_pad)
	{
		if (opt_plain)
			fprintf(fd, ",'%.*s'", opt_pad, padding+(id-1) % pad_alphabet_len);
		else
			fprintf(stdout, ",'%.*s'", opt_pad, padding+(id-1) % pad_alphabet_len);
	}
	
	if (opt_plain)
		fprintf(fd, "\n");
	else
		fprintf(stdout, "\n");
}

/*
 * is_vector_ok
 *
 *	returns 1 iif all x_i \in [0,MAX_VALUE]
 */
static int
is_vector_ok(int dim, double *x, double MAX_VALUE)
{
	while (dim--)
	{
		if (*x < 0.0 || *x > MAX_VALUE)
			return 0;
		x++;
	}

	return 1;
}

/*
 * generate_indep
 *
 *	Generate count vectors x[0..dim-1] with x_i \in [0,1] independently
 *	and equally distributed and outputs them to STDOUT.
 */
static void
generate_indep(int count, int dim, int type, double MAX_VALUE)
{
	double *x = (double *) malloc(sizeof(double) * dim);

	while (count--)
	{
		int		d;

		for (d = 0; d < dim; d++)
			x[d] = random_equal(0, MAX_VALUE);

		output_vector(dim, x, type, MAX_VALUE);
		stats_enter(dim, x);
	}

	free(x);
}

/*
 * generate_corr
 *
 *	Generates count vectors x[0..dim-1] with x_i \in [0,1].
 *	The x_i are correlated, i.e. if x is high in one dimension
 *	it is likely that x is high in another.
 */
static void
generate_corr(int count, int dim, int type, double MAX_VALUE)
{
	double *x = (double *) malloc(sizeof(double) * dim);

	while (count--)
	{
		do
		{
			int		d;
			double	v = random_peak(0, MAX_VALUE, dim);
			double	l = v <= 0.5*MAX_VALUE ? v : MAX_VALUE - v;

			for (d = 0; d < dim; d++)
				x[d] = v;

			for (d = 0; d < dim; d++)
			{
				double h = random_normal(0, l);
				x[d] += h;
				x[(d + 1) % dim] -= h;
			}
		} while (!is_vector_ok(dim, x, MAX_VALUE));

		output_vector(dim, x, type, MAX_VALUE);
		stats_enter(dim, x);
	}

	free(x);
}

/*
 * generate_anti
 *
 *	Generates count vectors x[0..dim-1] with x_i in [0,1], such that
 *	if x is high in one dimension it is likely that x is low in another.
 */
static void
generate_anti(int count, int dim, int type, double MAX_VALUE)
{
	double *x = (double *) malloc(sizeof(double) * dim);

	while (count--)
	{
		do
		{
			int		d;
			double	v = random_normal(0.5*MAX_VALUE, 0.25*MAX_VALUE);
			double	l = v <= 0.5*MAX_VALUE ? v : MAX_VALUE - v;
			
			for (d = 0; d < dim; d++)
				x[d] = v;
		
			for (d = 0; d < dim; d++)
			{
				double h = random_equal(-l, l);
				x[d] += h;
				x[(d + 1) % dim] -= h;
			}
		} while (!is_vector_ok(dim, x, MAX_VALUE));

		output_vector(dim, x, type, MAX_VALUE);
		stats_enter(dim, x);
	}
}

/*
 * usage
 */
static void
usage()
{
	fprintf(stderr, 
"\
Test data generator for Skyline Operator Evaluation\n\
usage: %s (-i|-c|-a) -d DIM -t TYPE -M MAX -n COUNT [-s SEED] [-p] [-S] [-h|-?]\n\
\n\
Options:\n\
       -i       independent (dim >= 1)\n\
       -c       correlated (dim >= 2)\n\
       -a       anti-correlated (dim >= 2)\n\
\n\
       -d DIM   dimensions >=1\n\
       -t TYPE  values type (1 long, 2 double)\n\
       -M MAX   max value for each dimension\n\
       -n COUNT number of vectors\n\
       -I       unique id for every vector\n\
       -p PAD   add a padding field, PAD characters long\n\
\n\
       -P       plain text\n\
       -C       generate SQL COPY statement\n\
       -R       generate SQL CREATE TABLE statement\n\
       -T NAME  use NAME instead of default table name\n\
\n\
       -s SEED  set random generator seed to SEED\n\
\n\
       -S       output stats to stderr\n\
\n\
       -h -?    display this help message and exit\n\
\n\
Examples:\n\
       %s -i -d 5 -t 1 -M 100 -n 10000 -P\n\
\n\
", progname, progname, progname);
}
