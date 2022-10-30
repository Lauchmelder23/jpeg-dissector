#ifndef _LOADER_H
#define _LOADER_H

#include <stdint.h>
#include "util.h"

#define ENCODING_PROCESS_MASK 3
#define ENCODING_DCT_MASK 4
#define ENCODING_CODING_MASK 8

enum EncodingProcess
{
	Baseline = 0,
	Extended = 1,
	Progressive = 2,
	Lossless = 3,
};

enum Coding
{
	Huffman = 0,
	Arithmetic = (1 << 3)
};

enum DCTType
{
	NonDifferential = 0,
	Differential = (1 << 2)
};

enum TableClass
{
	DCTable,
	ACTable
};

PACK(struct QuantizationTable
{
	uint8_t precision;
	uint8_t destination;
	uint8_t* data;
});

PACK(struct HuffmanTable
{
	enum TableClass class;
	uint8_t destination;
	uint8_t num_codes[16];
	uint8_t* codes[16];
});

#define QUANTIZATION_TABLE_SIZE sizeof(struct QuantizationTable) - sizeof(uint8_t*)

PACK(struct FrameComponent
{
	uint8_t identifier;
	struct
	{
		uint8_t v : 4;
		uint8_t h : 4;
	} sampling_factor;
	uint8_t quantization_table;
});

PACK(struct FrameHeader
{
	uint16_t length;
	uint8_t precision;
	uint16_t num_lines;
	uint16_t num_samples;
	uint8_t num_components;

	struct
	{
		uint8_t v : 4;
		uint8_t h : 4;
	} max_sampling_factor;

	uint8_t encoding;
	struct FrameComponent* components;
});

#define FRAME_HEADER_SIZE sizeof(struct FrameHeader) - (sizeof(struct FrameComponent*) + sizeof(uint8_t) + sizeof(uint8_t))

PACK(struct ScanComponent
{
	uint8_t identifier;
	struct
	{
		uint8_t dc : 4;
		uint8_t ac : 4;
	} table_destination;
});

PACK(struct ScanHeader
{
	uint16_t length;
	uint8_t num_components;
	struct ScanComponent* components;

	uint8_t spectral_select_start;
	uint8_t spectral_select_end;

	struct
	{
		uint8_t high : 4;
		uint8_t low : 4;
	} approx_bit_pos;
});

struct Scan
{
	size_t length;
	struct ScanComponent* scan_component;
	struct FrameComponent* frame_component;

	uint16_t width;
	uint16_t height;
	uint8_t* data;
};

#define SCAN_HEADER_PRE_SIZE sizeof(uint16_t) + sizeof(uint8_t)
#define SCAN_HEADER_POST_SIZE sizeof(uint8_t) * 3

PACK(struct JFIFAPP0Segment
{
	uint16_t length;
	const char identifier[5];

	struct {
		uint8_t major;
		uint8_t minor;
	} version;

	uint8_t density_units;
	uint16_t density_x;
	uint16_t density_y;

	uint8_t thumbnail_x;
	uint8_t thumbnail_y;

	uint8_t* thumbnail_data;
});

#define JFIF_APP0_SIZE sizeof(struct JFIFAPP0Segment) - sizeof(uint8_t*)

typedef struct JPEG
{
	struct JFIFAPP0Segment* app0;

	size_t num_quantization_tables;
	struct QuantizationTable* quantization_tables;

	size_t num_huffman_tables;
	struct HuffmanTable* huffman_tables;

	struct FrameHeader* frame_header;
	struct ScanHeader* scan_header;

	size_t num_scans;
	struct Scan* scans;
} JPEG;

JPEG* load_jpeg(const char* filename);
void free_jpeg(JPEG* jpeg);

#endif // _LOADER_H