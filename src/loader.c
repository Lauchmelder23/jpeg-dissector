#include "loader.h"

#include <stdio.h>
#include <stdint.h>

#include "util.h"

static int load_segment(FILE* fp);

static int load_rst_segment(FILE* fp, uint8_t n);
static int load_app_segment(FILE* fp, uint8_t n);

static int load_app0_segment(FILE* fp);

int load_jpeg(const char* filename)
{
	FILE* fp = fopen(filename, "r");
	if (fp == NULL)
	{
		return 1;
	}

	while (!feof(fp))
	{
		if (load_segment(fp) != 0)
		{
			fprintf(stderr, "Segment loading failed\n");
			return 1;
		}
	}

	fclose(fp);
	return 0;
}

int load_segment(FILE* fp)
{
	uint8_t segment_marker[2];
	size_t segment_marker_size = sizeof(segment_marker);

	if (fread(segment_marker, sizeof(uint8_t), segment_marker_size, fp) != segment_marker_size)
	{
		fprintf(stderr, "Marker terminated unexpectedly\n");
		return 1;
	}

	if (segment_marker[0] != 0xFF)
	{
		fprintf(stderr, "Ill-formatted marker\n");
		return 1;
	}

	// Handle special APPn/RSTn markers
	if (segment_marker[1] >= 0xD0 && segment_marker[1] <= 0xD7)
	{
		if (load_rst_segment(fp, segment_marker[1] | 0x0F) != 0)
		{
			return 1;
		}
	}
	else if ((segment_marker[1] & 0xF0) == 0xE0)
	{
		if (load_app_segment(fp, segment_marker[1] & 0x0F) != 0)
		{
			return 1;
		}
	}
	else
	{
		switch (segment_marker[1])
		{
		case 0xD8:	// Start of image
			DEBUG_LOG("SOI marker encountered");
			break;

		default:
			fprintf(stderr, "Unimplemented marker 0xFF 0x%02X\n", segment_marker[1]);
			return 1;
		}
	}

	return 0;
}

int load_rst_segment(FILE* fp, uint8_t n)
{
	DEBUG_LOG("RST%d marker encountered", n);
	return 1;
}

int load_app_segment(FILE* fp, uint8_t n)
{
	DEBUG_LOG("APP%d marker encountered", n);

	switch (n)
	{
	case 0:	return load_app0_segment();

	default: 
		fprintf(stderr, "Unknown APP segment ID %d\n", n);
		return 1;
	}

	return 0;
}

int load_app0_segment(FILE* fp)
{

}