#ifndef _LODAER_H
#define _LOADER_H

#include <stdint.h>
#include "util.h"

PACK(
typedef struct QuantizationTable
{
	uint16_t length;
	uint8_t* data;
} QuantizationTable;
)

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

typedef struct JPEG
{
	JFIFAPP0Segment* app0;

	size_t num_quantization_tables;
	QuantizationTable* quantization_tables;
} JPEG;

JPEG* load_jpeg(const char* filename);
void free_jpeg(JPEG* jpeg);

#endif // _LOADER_H