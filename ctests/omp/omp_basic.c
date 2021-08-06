#include <omp.h>

int main(int argc, char **argv)
{
	int size, rank;
#pragma omp parallel
	{
		size = omp_get_num_threads();
		rank = omp_get_thread_num();
	}
	fprintf(stdout, "Hello, I'm %u of %u\n", rank, size);
	return 0;
}
