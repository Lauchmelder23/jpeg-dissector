#ifndef _LODAER_H
#define _LOADER_H

#include <stdint.h>
#include "util.h"

#define ENCODING_PROCESS_MASK 3
#define ENCODING_DCT_MASK 4
#define ENCODING_CODING_MASK 8

typedef enum EncodingProcess
{
	Baseline = 0,
	Extended = 1,
	Progressive = 2,
	Lossless = 3,
} EncodingProcess;

typedef enum Coding
{
	Huffman = 0,
	Arithmetic = (1 << 3)
} Coding;

typedef enum DCTType
{
	NonDifferential = 0,
	Differential = (1 << 2)
} DCTType;

typedef enum TableClass
{
	DCTable,
	ACTable
} TableClass;

PACK(
typedef struct QuantizationTable
{
	uint8_t precision;
	uint8_t destination;
	uint8_t* data;
} QuantizationTable;
)

PACK(
typedef struct HuffmanTable
{
	TableClass class;
	uint8_t destination;
	uint8_t num_codes[16];
	uint8_t* codes[16];
} HuffmanTable;
)

#define QUANTIZATION_TABLE_SIZE sizeof(QuantizationTable) - sizeof(uint8_t*)

PACK(
typedef struct FrameComponent
{
	uint8_t identifier;
	struct
	{
		uint8_t v : 4;
		uint8_t h : 4;
	} sampling_factor;
	uint8_t quantization_table;
} FrameComponent;
)

PACK(
typedef struct FrameHeader
{
	uint16_t length;
	uint8_t precision;
	uint16_t num_lines;
	uint16_t num_samples;
	uint8_t num_components;

	uint8_t encoding;
	FrameComponent* components;
} FrameHeader;
)

#define FRAME_HEADER_SIZE sizeof(FrameHeader) - (sizeof(FrameComponent*) + sizeof(uint8_t))

PACK (
typedef struct JFIFAPP0Segment
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
} JFIFAPP0Segment;
)

#define JFIF_APP0_SIZE sizeof(JFIFAPP0Segment) - sizeof(uint8_t*)

typedef struct JPEG
{
	JFIFAPP0Segment* app0;

	size_t num_quantization_tables;
	QuantizationTable* quantization_tables;

	size_t num_huffman_tables;
	HuffmanTable* huffman_tables;

	FrameHeader* frame_header;
} JPEG;

JPEG* load_jpeg(const char* filename);
void free_jpeg(JPEG* jpeg);

#endif // _LOADER_H