#include "loader.h"

#include <stdlib.h>
#include <memory.h>
#include <assert.h>
#include <math.h>

#define MAX_QUANTIZATION_TABLES 255
#define MAX_HUFFMAN_TABLES 255

#define memzero(buffer, size) memset(buffer, 0, size)

static int load_segment(JPEG* jpeg, FILE* fp);

static int load_quantization_table(JPEG* jpeg, FILE* fp);
static int load_huffman_table(JPEG* jpeg, FILE* fp);
static int load_start_of_frame(JPEG* jpeg, FILE* fp, uint8_t type);
static int load_start_of_scan(JPEG* jpeg, FILE* fp);

static int load_scan_data(JPEG* jpeg, FILE* fp, struct ScanComponent* scan_component);

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

	JPEG* jpeg = (JPEG*)malloc(sizeof(JPEG));
	memzero(jpeg, sizeof(JPEG));

	while (!feof(fp))
	{
		if (load_segment(jpeg, fp) != 0)
		{
			ERROR_LOG("Segment loading failed");
			free_jpeg(jpeg);

			fclose(fp);
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
			if (jpeg->quantization_tables[i].data)
			{
				free(jpeg->quantization_tables[i].data);
				jpeg->quantization_tables[i].data = NULL;
			}
		}

		free(jpeg->quantization_tables);
		jpeg->quantization_tables = NULL;
	}

	if (jpeg->huffman_tables)
	{
		for (size_t i = 0; i < jpeg->num_huffman_tables; i++)
		{
			for (size_t j = 0; j < 16; j++)
			{
				if (jpeg->huffman_tables[i].codes[j])
				{
					free(jpeg->huffman_tables[i].codes[j]);
					jpeg->huffman_tables[i].codes[j] = NULL;
				}
			}
		}

		free(jpeg->huffman_tables);
		jpeg->huffman_tables = NULL;
	}

	if (jpeg->frame_header)
	{
		if (jpeg->frame_header->components)
		{
			free(jpeg->frame_header->components);
			jpeg->frame_header->components = NULL;
		}

		free(jpeg->frame_header);
		jpeg->frame_header = NULL;
	}

	if (jpeg->scan_header)
	{
		if (jpeg->scan_header->components)
		{
			free(jpeg->scan_header->components);
			jpeg->scan_header->components = NULL;
		}

		free(jpeg->scan_header);
		jpeg->scan_header = NULL;
	}

	if (jpeg->scans)
	{
		for (size_t i = 0; i < jpeg->num_scans; i++)
		{
			free(jpeg->scans[i].data);
			jpeg->scans[i].data = NULL;
			jpeg->scans[i].scan_component = NULL;
			jpeg->scans[i].frame_component = NULL;
		}

		free(jpeg->scans);
		jpeg->scans = NULL;
	}

	free(jpeg);
}

int load_segment(JPEG* jpeg, FILE* fp)
{
	uint8_t segment_marker[2];
	size_t segment_marker_size = sizeof(segment_marker);

	if (fread(segment_marker, sizeof(uint8_t), segment_marker_size, fp) != segment_marker_size)
	{
		ERROR_LOG("Marker terminated unexpectedly");
		return 1;
	}

	if (segment_marker[0] != 0xFF)
	{
		ERROR_LOG("Ill-formatted marker");
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

		case 0xDA:
			return load_start_of_scan(jpeg, fp);

		case 0xDB:	// Quantization table
			return load_quantization_table(jpeg, fp);

		default:
			ERROR_LOG("Unimplemented marker 0xFF 0x%02X", segment_marker[1]);
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
		jpeg->quantization_tables = (struct QuantizationTable*)malloc(sizeof(struct QuantizationTable) * MAX_QUANTIZATION_TABLES);
		if (jpeg->quantization_tables == NULL)
		{
			ERROR_LOG("Failed to allocate memory for quantization tables");
			return 1;
		}
	}


	uint16_t total_length;
	if (fread(&total_length, sizeof(uint8_t), sizeof(uint16_t), fp) != sizeof(uint16_t))
	{
		ERROR_LOG("Failed to read length of quantization tables");
		return 1;
	}

	total_length = bswap_16(total_length);
	size_t read_length = 2;

	while (read_length < total_length)
	{
		struct QuantizationTable* current_table = jpeg->quantization_tables + jpeg->num_quantization_tables;
		jpeg->num_quantization_tables++;

		uint8_t meta_info;
		if (fread(&meta_info, sizeof(uint8_t), sizeof(uint8_t), fp) != sizeof(uint8_t))
		{
			ERROR_LOG("Failed to read quantization table #%zu meta info", jpeg->num_quantization_tables);
			return 1;
		}

		read_length += 1;

		current_table->precision = ((meta_info >> 4) == 0) ? sizeof(uint8_t) : sizeof(uint16_t);
		current_table->destination = meta_info & 0x0F;

		size_t table_length = current_table->precision * 64;

		DEBUG_LOG(
			"Quantization table #%zu\n"
			"\tprecision = %d bit\n"
			"\tdestination = %d", 
			
			jpeg->num_quantization_tables,
			current_table->precision,
			current_table->destination
		);

		current_table->data = (uint8_t*)malloc(table_length);
		if (current_table->data == NULL)
		{
			ERROR_LOG("Failed to allocate memory for quantization table #%zu data", jpeg->num_quantization_tables);
			return 1;
		}

		if (fread(current_table->data, sizeof(uint8_t), table_length, fp) != table_length)
		{
			ERROR_LOG("Failed to read quantization table #%zu data", jpeg->num_quantization_tables);
			return 1;
		}

		read_length += table_length;
	}

	return 0;
}

int load_huffman_table(JPEG* jpeg, FILE* fp)
{
	DEBUG_LOG("DHT encountered");

	assert(jpeg);
	assert(fp);

	if (jpeg->huffman_tables == NULL)
	{
		jpeg->huffman_tables = (struct HuffmanTable*)malloc(sizeof(struct HuffmanTable) * MAX_HUFFMAN_TABLES);
		if (jpeg->huffman_tables == NULL)
		{
			ERROR_LOG("Failed to allocate memory for huffman tables");
			return 1;
		}
	}

	size_t total_length;
	if (fread(&total_length, sizeof(uint8_t), sizeof(uint16_t), fp) != sizeof(uint16_t))
	{
		ERROR_LOG("Failed to read length of huffman tables");
		return 1;
	}

	total_length = bswap_16(total_length);
	size_t read_length = 2;

	while (read_length < total_length)
	{
		struct HuffmanTable* current_table = jpeg->huffman_tables + jpeg->num_huffman_tables;
		jpeg->num_huffman_tables++;

		uint8_t meta_info;
		if (fread(&meta_info, sizeof(uint8_t), sizeof(uint8_t), fp) != sizeof(uint8_t))
		{
			ERROR_LOG("Failed to read huffman table #%zu meta info", jpeg->num_huffman_tables);
			return 1;
		}

		read_length += 1;

		current_table->class = ((meta_info >> 4) == 0) ? DCTable : ACTable;
		current_table->destination = meta_info & 0x0F;

		if (fread(current_table->num_codes, sizeof(uint8_t), 16, fp) != 16)
		{
			ERROR_LOG("Failed to read huffman code lengths for table #%zu", jpeg->num_huffman_tables);
			return 1;
		}

		read_length += 16;

		DEBUG_LOG(
			"Huffman table #%zu\n"
			"\tclass = %s\n"
			"\tdestination = %d\n"
			"\tcode lengths = %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d",

			jpeg->num_huffman_tables,
			(current_table->class == DCTable) ? "DC" : "AC",
			current_table->destination,
			current_table->num_codes[0], current_table->num_codes[1], current_table->num_codes[2], current_table->num_codes[3],
			current_table->num_codes[4], current_table->num_codes[5], current_table->num_codes[6], current_table->num_codes[7],
			current_table->num_codes[8], current_table->num_codes[9], current_table->num_codes[10], current_table->num_codes[11],
			current_table->num_codes[12], current_table->num_codes[13], current_table->num_codes[14], current_table->num_codes[15]
		);

		for (size_t i = 0; i < 16; i++)
		{
			if (current_table->num_codes[i] == 0)
			{
				current_table->codes[i] = NULL;
				continue;
			}

			current_table->codes[i] = (uint8_t*)malloc(sizeof(uint8_t) * current_table->num_codes[i]);
			if (current_table->codes[i] == NULL)
			{
				ERROR_LOG("Failed to allocate memory for huffman table #%zu list of codes of length %zu", jpeg->num_huffman_tables, i);
				return 1;
			}

			if (fread(current_table->codes[i], sizeof(uint8_t), current_table->num_codes[i], fp) != current_table->num_codes[i])
			{
				ERROR_LOG("Failed to read huffman codes of length %zu for table #%zu", i, jpeg->num_huffman_tables);
				return 1;
			}

			read_length += current_table->num_codes[i];
		}
	}

	return 0;
}

int load_start_of_frame(JPEG* jpeg, FILE* fp, uint8_t type)
{
	DEBUG_LOG("SOF%u encountered", type);

	assert(jpeg);
	assert(fp);

	if (jpeg->frame_header != NULL)
	{
		ERROR_LOG("Found multiple frames");
		return 1;
	}

	jpeg->frame_header = (struct FrameHeader*)malloc(sizeof(struct FrameHeader));
	if (jpeg->frame_header == NULL)
	{
		ERROR_LOG("Failed to allocate memory for frame header");
		return 1;
	}

	memzero(jpeg->frame_header, sizeof(struct FrameHeader));

	if (fread(jpeg->frame_header, sizeof(uint8_t), FRAME_HEADER_SIZE, fp) != FRAME_HEADER_SIZE)
	{
		ERROR_LOG("Failed to read data from frame header");
		return 1;
	}

	jpeg->frame_header->length = bswap_16(jpeg->frame_header->length);
	jpeg->frame_header->num_lines = bswap_16(jpeg->frame_header->num_lines);
	jpeg->frame_header->num_samples = bswap_16(jpeg->frame_header->num_samples);

	jpeg->frame_header->encoding = type;

	jpeg->frame_header->components = (struct FrameComponent*)malloc(sizeof(struct FrameComponent) * jpeg->frame_header->num_components);
	if (jpeg->frame_header->components == NULL)
	{
		ERROR_LOG("Failed to allocate memory for frame components");
		return 1;
	}

	for (size_t c = 0; c < jpeg->frame_header->num_components; c++)
	{
		struct FrameComponent* current_component = jpeg->frame_header->components + c;
		if (fread(current_component, sizeof(uint8_t), sizeof(struct FrameComponent), fp) != sizeof(struct FrameComponent))
		{
			ERROR_LOG("Failed to read component #%zu", c);
			return 1;
		}

		jpeg->frame_header->max_sampling_factor.v = fmaxl(jpeg->frame_header->max_sampling_factor.v, current_component->sampling_factor.v);
		jpeg->frame_header->max_sampling_factor.h = fmaxl(jpeg->frame_header->max_sampling_factor.h, current_component->sampling_factor.h);
	}

	DEBUG_LOG(
		"Frame header\n"
		"\tlength = %d\n"
		"\tprecision = %d\n"
		"\tlines, samples = %d, %d\n"
		"\tcomponents = %d\n"
		"\tencoding = %s %s, %s\n"
		"\tmax sampling factor v/h = %d/%d",

		jpeg->frame_header->length,
		jpeg->frame_header->precision,
		jpeg->frame_header->num_lines, jpeg->frame_header->num_samples,
		jpeg->frame_header->num_components,
		(jpeg->frame_header->encoding & ENCODING_DCT_MASK) == NonDifferential ? "Non-differential" : "Differential",

		(jpeg->frame_header->encoding & ENCODING_PROCESS_MASK) == Baseline ? "baseline DCT" :
		(jpeg->frame_header->encoding & ENCODING_PROCESS_MASK) == Extended ? "extended sequential DCT" :
		(jpeg->frame_header->encoding & ENCODING_PROCESS_MASK) == Progressive ? "progressive DCT" : "Lossless (sequential)",

		(jpeg->frame_header->encoding & ENCODING_CODING_MASK) == Huffman ? "huffman coding" : "arithmetic coding",
	
		jpeg->frame_header->max_sampling_factor.v, jpeg->frame_header->max_sampling_factor.h
	);

	for (size_t i = 0; i < jpeg->frame_header->num_components; i++)
	{
		DEBUG_LOG(
			"Frame header component #%zu\n"
			"\tid = %d\n"
			"\tsampling factor v/h = %d/%d\n"
			"\ttable = %d",

			i,
			jpeg->frame_header->components[i].identifier,
			jpeg->frame_header->components[i].sampling_factor.v, jpeg->frame_header->components[i].sampling_factor.h,
			jpeg->frame_header->components[i].quantization_table
		);
	}

	return 0;
}

int load_start_of_scan(JPEG* jpeg, FILE* fp)
{
	DEBUG_LOG("SOS encountered");

	assert(jpeg);
	assert(fp);

	if (jpeg->scan_header != NULL)
	{
		ERROR_LOG("Found multiple scans");
		return 1;
	}

	jpeg->scan_header = (struct ScanHeader*)malloc(sizeof(struct ScanHeader));
	if (jpeg->scan_header == NULL)
	{
		ERROR_LOG("Failed to allocate memory for scan header");
		return 1;
	}

	if (fread(jpeg->scan_header, sizeof(uint8_t), SCAN_HEADER_PRE_SIZE, fp) != SCAN_HEADER_PRE_SIZE)
	{
		ERROR_LOG("Failed to read length of scan header or scan header components");
		return 1;
	}

	jpeg->scan_header->length = bswap_16(jpeg->scan_header->length);

	jpeg->scan_header->components = (struct ScanComponent*)malloc(sizeof(struct ScanComponent) * jpeg->scan_header->num_components);
	if (jpeg->scan_header == NULL)
	{
		ERROR_LOG("Failed to allocate memory for scan header components");
		return 1;
	}

	for (size_t i = 0; i < jpeg->scan_header->num_components; i++)
	{
		struct ScanComponent* current_component = jpeg->scan_header->components + i;

		if (fread(current_component, sizeof(uint8_t), sizeof(struct ScanComponent), fp) != sizeof(struct ScanComponent))
		{
			ERROR_LOG("Failed to load component #%zu of scan header", i);
			return 1;
		}
	}

	if (fread(&jpeg->scan_header->spectral_select_start, sizeof(uint8_t), SCAN_HEADER_POST_SIZE, fp) != SCAN_HEADER_POST_SIZE)
	{
		ERROR_LOG("Failed to read spectral selection info");
		return 1;
	}

	DEBUG_LOG(
		"Scan header\n"
		"\tcomponents = %d\n"
		"\tspectral select s/e = %d/%d\n"
		"\tapprox bit pos h/l = %d/%d",

		jpeg->scan_header->num_components,
		jpeg->scan_header->spectral_select_start, jpeg->scan_header->spectral_select_end,
		jpeg->scan_header->approx_bit_pos.high, jpeg->scan_header->approx_bit_pos.low
	);

	jpeg->scans = (struct Scan*)malloc(sizeof(struct Scan) * jpeg->scan_header->num_components);
	if (jpeg->scans == NULL)
	{
		ERROR_LOG("Failed to allocate memory for scan data");
		return 1;
	}

	for (size_t i = 0; i < jpeg->scan_header->num_components; i++)
	{
		struct ScanComponent* scan_component = jpeg->scan_header->components + i;

		DEBUG_LOG(
			"Scan header component #%zu\n"
			"\tid = %d\n"
			"\tdestination dc/ac = %d/%d",

			i,
			scan_component->identifier,
			scan_component->table_destination.dc, scan_component->table_destination.ac
		);

		if (load_scan_data(jpeg, fp, scan_component) != 0)
		{
			return 1;
		}
	}

	return 0;
}

int load_scan_data(JPEG* jpeg, FILE* fp, struct ScanComponent* scan_component)
{
	DEBUG_LOG("Loading scan data (component #%d)", scan_component->identifier);

	assert(jpeg);
	assert(fp);

	struct Scan* scan = jpeg->scans + jpeg->num_scans;

	struct FrameComponent* frame_component = NULL;
	for (size_t i = 0; i < jpeg->frame_header->num_components; i++)
	{
		if (jpeg->frame_header->components[i].identifier == scan_component->identifier)
		{
			frame_component = jpeg->frame_header->components + i;
			break;
		}
	}

	if (frame_component == NULL)
	{
		ERROR_LOG("Couldn't find matching frame component for scan component (%d)", scan_component->identifier);
		return 1;
	}

	scan->scan_component = scan_component;
	scan->frame_component = frame_component;

	uint16_t lines = jpeg->frame_header->num_lines;
	uint16_t samples = jpeg->frame_header->num_samples;

	scan->width = ceill(samples * (float)frame_component->sampling_factor.h / (float)jpeg->frame_header->max_sampling_factor.h);
	scan->height = ceill(lines * (float)frame_component->sampling_factor.v / (float)jpeg->frame_header->max_sampling_factor.v);
	
	DEBUG_LOG("size of scan (w,h) = %d,%d", scan->width, scan->height);

	scan->length = (size_t)scan->width * scan->height;

	scan->data = (uint8_t*)malloc(sizeof(uint8_t) * scan->length);
	if (scan->data == NULL)
	{
		ERROR_LOG("Failed to allocate memory for scan data");
		return 1;
	}

	jpeg->num_scans++;

	size_t tmp = fread(scan->data, sizeof(uint8_t), scan->length, fp);
	if (tmp != scan->length)
	{
		ERROR_LOG("Failed to load scan data from file");
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
		ERROR_LOG("Unknown APP segment ID %d", n);
		return 1;
	}

	return 0;
}

int load_app0_segment(JPEG* jpeg, FILE* fp)
{
	if (jpeg->app0 != NULL)
	{
		ERROR_LOG("Found more than one APP0 marker");
		return 1;
	}

	assert(jpeg);
	assert(fp);

	jpeg->app0 = (struct JFIFAPP0Segment*)malloc(sizeof(struct JFIFAPP0Segment));
	memzero(jpeg->app0, sizeof(struct JFIFAPP0Segment));

	if (jpeg->app0 == NULL)
	{
		ERROR_LOG("Failed to allocate memory for APP0 header");
		return 1;
	}

	// Extract header without thumbnail data
	if (fread(jpeg->app0, sizeof(uint8_t), JFIF_APP0_SIZE, fp) != JFIF_APP0_SIZE)
	{
		ERROR_LOG("Incomplete APP0 header");
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
			ERROR_LOG("Failed to allocate memory for thumbnail data");
			return 1;
		}

		if (fread(jpeg->app0->thumbnail_data, sizeof(uint8_t), thumbnail_data_size, fp) != thumbnail_data_size)
		{
			ERROR_LOG("Incomplete thumbnail data");
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