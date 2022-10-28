#include <stdio.h>
#include "loader.h"

static void print_usage(void)
{
	printf("Usage: ./jpeg-dissect <JPEG file>\n");
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		print_usage();
		return 1;
	}

	const char* filename = argv[1];
	printf("Supplied file: %s\n", filename);

	if (load_jpeg(filename) != 0)
	{
		fprintf(stderr, "Failed to load jpeg\n");
		return 1;
	}

	return 0;
}
