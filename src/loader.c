#include "loader.h"

#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#define MAX_QUANTIZATION_TABLES 255

#define memzero(buffer, size) memset(buffer, 0, size)

static int load_segment(JPEG* jpeg, FILE* fp);

static int load_quantization_table(JPEG* jpeg, FILE* fp);
static int load_huffman_table(JPEG* jpeg, FILE* fp);
static int load_start_of_frame(JPEG* jpeg, FILE* fp, uint8_t type);

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
		case 0xC0: case 0xC1: case 0xC2: case 0xC3:
		case 0xC5: case 0xC6: case 0xC7:
		case 0xC9: case 0xCA: case 0xCB: 
		case 0xCD: case 0xCE: case 0xCF:
			return load_start_of_frame(jpeg, fp, segment_marker[1] & 0x0F);

		case 0xC4:
			return load_huffman_table(jpeg, fp);

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

	assert(jpeg);
	assert(fp);

	if (jpeg->quantization_tables == NULL)
	{
		jpeg->quantization_tables = (QuantizationTable*)malloc(sizeof(QuantizationTable) * MAX_QUANTIZATION_TABLES);
		if (jpeg->quantization_tables == NULL)
		{
			fprintf(stderr, "Failed to allocate memory for quantization tables\n");
			return 1;
		}
	}

	uint16_t total_length;
	if (fread(&total_length, sizeof(uint8_t), sizeof(uint16_t), fp) != sizeof(uint16_t))
	{
		fprintf(stderr, "Failed to read length of quantization tables\n");
		return 1;
	}

	total_length = bswap_16(total_length);
	size_t read_length = 2;

	while (read_length < total_length)
	{
		QuantizationTable* current_table = jpeg->quantization_tables + jpeg->num_quantization_tables;

		uint8_t meta_info;
		if (fread(&meta_info, sizeof(uint8_t), sizeof(uint8_t), fp) != sizeof(uint8_t))
		{
			fprintf(stderr, "Failed to read quantization table #%d meta info\n", jpeg->num_quantization_tables);
			return 1;
		}

		current_table->precision = ((meta_info & 0xF) == 0) ? 8 : 16;
		current_table->destination = (meta_info >> 4);

		size_t table_length = current_table->precision * 64;

		DEBUG_LOG(
			"Quantization table #%d\n"
			"\tprecision = %d bit\n"
			"\tdestination = %d\n", 
			
			jpeg->num_quantization_tables,
			current_table->precision,
			current_table->destination
		);

		current_table->data = (uint8_t*)malloc(table_length);
		if (current_table->data == NULL)
		{
			fprintf(stderr, "Failed to allocate memory for quantization table #%d data\n", jpeg->num_quantization_tables);
			return 1;
		}

		if (fread(current_table->data, sizeof(uint8_t), table_length, fp) != table_length)
		{
			fprintf(stderr, "Failed to read quantization table #%d data\n", jpeg->num_quantization_tables);
			return 1;
		}

		jpeg->num_quantization_tables++;
		read_length += table_length;
	}

	return 0;
}

int load_huffman_table(JPEG* jpeg, FILE* fp)
{
	DEBUG_LOG("DHT encountered");

	assert(jpeg);
	assert(fp);

	return 1;

	return 0;
}

int load_start_of_frame(JPEG* jpeg, FILE* fp, uint8_t type)
{
	DEBUG_LOG("SOF%u encountered", type);

	assert(jpeg);
	assert(fp);

	if (jpeg->frame_header != NULL)
	{
		fprintf(stderr, "Found multiple frames\n");
		return 1;
	}

	jpeg->frame_header = (FrameHeader*)malloc(sizeof(FrameHeader));
	if (jpeg->frame_header == NULL)
	{
		fprintf(stderr, "Failed to allocate memory for frame header\n");
		return 1;
	}

	if (fread(jpeg->frame_header, sizeof(uint8_t), FRAME_HEADER_SIZE, fp) != FRAME_HEADER_SIZE)
	{
		fprintf(stderr, "Failed to read data from frame header\n");
		return 1;
	}

	jpeg->frame_header->length = bswap_16(jpeg->frame_header->length);
	jpeg->frame_header->num_lines = bswap_16(jpeg->frame_header->num_lines);
	jpeg->frame_header->num_samples = bswap_16(jpeg->frame_header->num_samples);

	jpeg->frame_header->encoding = type;

	jpeg->frame_header->components = (FrameComponent*)malloc(sizeof(FrameComponent) * jpeg->frame_header->num_components);
	if (jpeg->frame_header->components == NULL)
	{
		fprintf(stderr, "Failed to allocate memory for frame components\n");
		return 1;
	}

	for (size_t c = 0; c < jpeg->frame_header->num_components; c++)
	{
		FrameComponent* current_component = jpeg->frame_header->components + c;
		if (fread(current_component, sizeof(uint8_t), sizeof(FrameComponent), fp) != sizeof(FrameComponent))
		{
			fprintf(stderr, "Failed to read component #%u\n", c);
			return 1;
		}
	}

	DEBUG_LOG(
		"Frame header\n"
		"\tlength = %d\n"
		"\tprecision = %d\n"
		"\tlines, samples = %d, %d\n"
		"\tcomponents = %d\n"
		"\tencoding = %s %s, %s\n",

		jpeg->frame_header->length,
		jpeg->frame_header->precision,
		jpeg->frame_header->num_lines, jpeg->frame_header->num_samples,
		jpeg->frame_header->num_components,
		(jpeg->frame_header->encoding & ENCODING_DCT_MASK) == NonDifferential ? "Non-differential" : "Differential",

		(jpeg->frame_header->encoding & ENCODING_PROCESS_MASK) == Baseline ? "baseline DCT" :
		(jpeg->frame_header->encoding & ENCODING_PROCESS_MASK) == Extended ? "extended sequential DCT" :
		(jpeg->frame_header->encoding & ENCODING_PROCESS_MASK) == Progressive ? "progressive DCT" : "Lossless (sequential)",

		(jpeg->frame_header->encoding & ENCODING_CODING_MASK) == Huffman ? "huffman coding" : "arithmetic coding"
	);

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

	assert(jpeg);
	assert(fp);

	jpeg->app0 = (JFIFAPP0Segment*)malloc(sizeof(JFIFAPP0Segment));
	memzero(jpeg->app0, sizeof(JFIFAPP0Segment));

	if (jpeg->app0 == NULL)
	{
		fprintf(stderr, "Failed to allocate memory for APP0 header\n");
		return 1;
	}

	// Extract header without thumbnail data
	if (fread(jpeg->app0, sizeof(uint8_t), JFIF_APP0_SIZE, fp) != JFIF_APP0_SIZE)
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