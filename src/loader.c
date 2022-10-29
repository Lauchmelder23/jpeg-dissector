#include "loader.h"

#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#define MAX_QUANTIZATION_TABLES 255

#define memzero(buffer, size) memset(buffer, 0, size)

static int load_segment(JPEG* jpeg, FILE* fp);

static int load_quantization_table(JPEG* jpeg, FILE* fp);
static int load_rst_segment(JPEG* jpeg, FILE* fp, uint8_t n);
static int load_app_segment(JPEG* jpeg, FILE* fp, uint8_t n);

static int load_app0_segment(JPEG* jpeg, FILE* fp);

JPEG* load_jpeg(const char* filename)
{
	FILE* fp = fopen(filename, "r+b");
	if (fp == NULL)
	{
		return NULL;
	}

	JPEG* jpeg = (JPEG*)malloc(sizeof(jpeg));
	memzero(jpeg, sizeof(JPEG));

	while (!feof(fp))
	{
		if (load_segment(jpeg, fp) != 0)
		{
			fprintf(stderr, "Segment loading failed\n");
			free_jpeg(jpeg);

			return NULL;
		}
	}

	fclose(fp);
	return jpeg;
}

void free_jpeg(JPEG* jpeg)
{
	if (jpeg == NULL)
		return;

	if (jpeg->app0)
	{
		if (jpeg->app0->thumbnail_data)
		{
			free(jpeg->app0->thumbnail_data);
			jpeg->app0->thumbnail_data = NULL;
		}

		free(jpeg->app0);
		jpeg->app0 = NULL;
	}

	if (jpeg->quantization_tables)
	{
		for (size_t i = 0; i < jpeg->num_quantization_tables; i++)
		{
			free(jpeg->quantization_tables[i].data);
			jpeg->quantization_tables[i].data = NULL;
		}

		free(jpeg->quantization_tables);
		jpeg->quantization_tables = NULL;
	}
}

int load_segment(JPEG* jpeg, FILE* fp)
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
		if (load_rst_segment(jpeg, fp, segment_marker[1] | 0x0F) != 0)
		{
			return 1;
		}
	}
	else if ((segment_marker[1] & 0xF0) == 0xE0)
	{
		if (load_app_segment(jpeg, fp, segment_marker[1] & 0x0F) != 0)
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

		case 0xDB:	// Quantization table
			return load_quantization_table(jpeg, fp);

		default:
			fprintf(stderr, "Unimplemented marker 0xFF 0x%02X\n", segment_marker[1]);
			return 1;
		}
	}

	return 0;
}

int load_quantization_table(JPEG* jpeg, FILE* fp)
{
	DEBUG_LOG("DQT encountered");

	if (jpeg->quantization_tables == NULL)
	{
		jpeg->quantization_tables = (QuantizationTable*)malloc(sizeof(QuantizationTable) * MAX_QUANTIZATION_TABLES);
		if (jpeg->quantization_tables == NULL)
		{
			fprintf(stderr, "Failed to allocate memory for quantization tables\n");
			return 1;
		}
	}

	QuantizationTable* current_table = jpeg->quantization_tables + jpeg->num_quantization_tables;
	if (fread(&current_table->length, sizeof(uint8_t), sizeof(uint16_t), fp) != sizeof(uint16_t))
	{
		fprintf(stderr, "Failed to read quantization table length\n");
		return 1;
	}

	jpeg->num_quantization_tables++;
	current_table->length = bswap_16(current_table->length) - 2;
	DEBUG_LOG("qt length = %u", current_table->length);
	
	current_table->data = (uint8_t*)malloc(sizeof(uint8_t) * current_table->length);
	if (current_table->data == NULL)
	{
		fprintf(stderr, "Failed to allocate memory for quantization table data\n");
		return 1;
	}

	size_t tmp = fread(current_table->data, sizeof(uint8_t), current_table->length, fp);
	if (tmp != current_table->length)
	{
		fprintf(stderr, "Failed to read quantization table data\n");
		return 1;
	}


	return 0;
}

int load_rst_segment(JPEG* jpeg, FILE* fp, uint8_t n)
{
	DEBUG_LOG("RST%d marker encountered", n);
	return 1;
}

int load_app_segment(JPEG* jpeg, FILE* fp, uint8_t n)
{
	DEBUG_LOG("APP%d marker encountered", n);

	switch (n)
	{
	case 0:	return load_app0_segment(jpeg, fp);

	default: 
		fprintf(stderr, "Unknown APP segment ID %d\n", n);
		return 1;
	}

	return 0;
}

int load_app0_segment(JPEG* jpeg, FILE* fp)
{
	if (jpeg->app0 != NULL)
	{
		fprintf(stderr, "Found more than one APP0 marker\n");
		return 1;
	}

	assert(fp);

	jpeg->app0 = (JFIFAPP0Segment*)malloc(sizeof(JFIFAPP0Segment));
	memzero(jpeg->app0, sizeof(JFIFAPP0Segment));

	if (jpeg->app0 == NULL)
	{
		fprintf(stderr, "Failed to allocate memory for APP0 header\n");
		return 1;
	}

	// Extract header without thumbnail data
	size_t jfif_header_size = sizeof(JFIFAPP0Segment) - sizeof(uint8_t*);
	if (fread(jpeg->app0, sizeof(uint8_t), jfif_header_size, fp) != jfif_header_size)
	{
		fprintf(stderr, "Incomplete APP0 header\n");
		return 1;
	}

	jpeg->app0->length = bswap_16(jpeg->app0->length);
	jpeg->app0->density_x = bswap_16(jpeg->app0->density_x);
	jpeg->app0->density_y = bswap_16(jpeg->app0->density_y);

	size_t thumbnail_data_size = jpeg->app0->thumbnail_x * jpeg->app0->thumbnail_y;
	if (thumbnail_data_size > 0)
	{
		jpeg->app0->thumbnail_data = (uint8_t*)malloc(thumbnail_data_size * sizeof(uint8_t));
		if (jpeg->app0->thumbnail_data == NULL)
		{
			fprintf(stderr, "Failed to allocate memory for thumbnail data\n");
			return 1;
		}

		if (fread(jpeg->app0->thumbnail_data, sizeof(uint8_t), thumbnail_data_size, fp) != thumbnail_data_size)
		{
			fprintf(stderr, "Incomplete thumbnail data\n");
			return 1;
		}
	}

	DEBUG_LOG(
		"JFIFAPP0Segment\n"
		"\tlength = %u\n"
		"\tidentifier = %s\n"
		"\tversion = %u.%02u\n"
		"\tdensity units = %u\n"
		"\tdensity x, y = %u, %u\n"
		"\tthumbnail x, y = %u, %u",
		jpeg->app0->length,
		jpeg->app0->identifier,
		jpeg->app0->version.major, jpeg->app0->version.minor,
		jpeg->app0->density_units,
		jpeg->app0->density_x, jpeg->app0->density_y,
		jpeg->app0->thumbnail_x, jpeg->app0->thumbnail_y
	);
	return 0;
}