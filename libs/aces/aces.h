/*
// Copyright (c) 2009-2012 Kimball Thurston
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

// OS/X doesn't define off64_t, even with LARGEFILE64_SOURCE
#ifdef __APPLE__
typedef int64_t off64_t;
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* Force things to be tightly packed */
#pragma pack(push, 1)

typedef struct
{
	int32_t x_min;
	int32_t y_min;
	int32_t x_max;
	int32_t y_max;
} aces_attr_box2i;

typedef struct
{
	float x_min;
	float y_min;
	float x_max;
	float y_max;
} aces_attr_box2f;

typedef struct
{
	float red_x;
	float red_y;
	float green_x;
	float green_y;
	float blue_x;
	float blue_y;
	float white_x;
	float white_y;
} aces_attr_chromaticities;

typedef struct
{
	int32_t film_mfc_code;
	int32_t film_type;
	int32_t prefix;
	int32_t count;
	int32_t perf_offset;
	int32_t perfs_per_frame;
	int32_t perfs_per_count;
} aces_attr_keycode;

typedef struct
{
	float m[9];
} aces_attr_m33f;

typedef struct
{
	float m[16];
} aces_attr_m44f;

typedef struct
{
	int32_t num;
	uint32_t denom;
} aces_attr_rational;

typedef enum
{
	ACES_TILE_ONE_LEVEL = 0,
	ACES_TILE_MIPMAP_LEVELS = 1,
	ACES_TILE_RIPMAP_LEVELS = 2
} ACES_TILE_LEVEL_TYPE;

typedef enum
{
	ACES_TILE_ROUND_DOWN = 0,
	ACES_TILE_ROUND_UP = 1
} ACES_TILE_ROUND_MODE;

typedef struct
{
	uint32_t x_size;
	uint32_t y_size;
	ACES_TILE_LEVEL_TYPE level_type;
	ACES_TILE_ROUND_MODE round_mode;
} aces_attr_tiledesc;

typedef struct
{
	uint32_t time_and_flags;
	uint32_t user_data;
} aces_attr_timecode;

typedef struct
{
	int32_t x;
	int32_t y;
} aces_attr_v2i;

typedef struct
{
	float x;
	float y;
} aces_attr_v2f;

typedef struct
{
	int32_t x;
	int32_t y;
	int32_t z;
} aces_attr_v3i;

typedef struct
{
	float x;
	float y;
	float z;
} aces_attr_v3f;

#pragma pack(pop)
/* Done with structures we'll read directly... */

typedef struct
{
	uint32_t width;
	uint32_t height;
	const uint8_t const *rgba;
} aces_attr_preview;

typedef enum
{
	ACES_PIXEL_UINT = 0,
	ACES_PIXEL_HALF = 1,
	ACES_PIXEL_FLOAT = 2
} ACES_PIXEL_TYPE;

typedef struct
{
	int32_t length;
	const char const *str;
} aces_attr_string;

typedef struct
{
	int32_t n_strings;
	const aces_attr_string const *strings;
} aces_attr_string_vector;

typedef struct
{
	size_t size;
	const void const *data;
} aces_attr_userdata;

typedef enum
{
	ACES_COMPRESSION_NONE = 0,
	ACES_COMPRESSION_RLE = 1,
	ACES_COMPRESSION_ZIPS = 2,
	ACES_COMPRESSION_ZIP = 3,
	ACES_COMPRESSION_PIZ = 4,
	ACES_COMPRESSION_PXR24 = 5,
	ACES_COMPRESSION_B44 = 6,
	ACES_COMPRESSION_B44A = 7
} ACES_COMPRESSION_TYPE;

typedef enum
{
	ACES_ENVMAP_LATLONG = 0,
	ACES_ENVMAP_CUBE = 1
} ACES_ENVMAP_TYPE;

typedef enum
{
	ACES_LINEORDER_INCREASING_Y = 0,
	ACES_LINEORDER_DECREASING_Y = 1,
	ACES_LINEORDER_RANDOM_Y = 2
} ACES_LINEORDER_TYPE;

typedef enum
{
	ACES_ATTR_UNKNOWN = 0, /**< type indicating an error or uninitialized attribute */
	ACES_ATTR_BOX2I, /**< integer region definition. @see aces_box2i */
	ACES_ATTR_BOX2F, /**< float region definition. @see aces_box2f */
	ACES_ATTR_CHLIST, /**< Definition of channels in file @see aces_chlist_entry */
	ACES_ATTR_CHROMATICITIES, /**< Values to specify color space of colors in file @see aces_chromaticities */
	ACES_ATTR_COMPRESSION, /**< uint8_t declaring compression present */
	ACES_ATTR_DOUBLE, /**< double precision floating point number */
	ACES_ATTR_ENVMAP, /**< uint8_t declaring environment map type */
	ACES_ATTR_FLOAT, /**< a normal (4 byte) precision floating point number */
	ACES_ATTR_INT, /**< a 32-bit signed integer value */
	ACES_ATTR_KEYCODE, /**< structure recording keycode @see aces_keycode */
	ACES_ATTR_LINEORDER, /**< uint8_t declaring scanline ordering */
	ACES_ATTR_M33F, /**< 9 32-bit floats representing a 3x3 matrix */
	ACES_ATTR_M44F, /**< 16 32-bit floats representing a 4x4 matrix */
	ACES_ATTR_PREVIEW, /**< 2 unsigned ints followed by 4 x w x h uint8_t image */
	ACES_ATTR_RATIONAL, /**< int followed by unsigned int */
	ACES_ATTR_STRING, /**< int (length) followed by char string data */
	ACES_ATTR_STRING_VECTOR, /**< 0 or more text strings (int + string). number is based on attribute size */
	ACES_ATTR_TILEDESC, /**< 2 unsigned ints xSize, ySize followed by mode */
	ACES_ATTR_TIMECODE, /**< 2 unsigned ints time and flags, user data */
	ACES_ATTR_V2I, /**< pair of 32-bit integers */
	ACES_ATTR_V2F, /**< pair of 32-bit floats */
	ACES_ATTR_V3I, /**< set of 3 32-bit integers */
	ACES_ATTR_V3F, /**< set of 3 32-bit floats */
	ACES_ATTR_USER, /**< user provided type */
} ACES_ATTRIBUTE_TYPE;

typedef enum
{
	ACES_STORAGE_SCANLINE = 0,
	ACES_STORAGE_TILED = 1
} ACES_STORAGE_TYPE;

typedef ssize_t (*aces_file_read_func)( void *, void *, size_t );
typedef ssize_t (*aces_file_write_func)( void *, const void *, size_t );
typedef off64_t (*aces_file_seek_func)( void *, off64_t, int );

typedef void (*aces_error_func)( const char *message );

typedef struct aces_file ACES_FILE;

/* //////////////////////////////////////// */
/** @group Read functions */

ACES_FILE *aces_start_read( const char *filename );
ACES_FILE *aces_start_read_stream( void *user_data,
								   const char *source_name,
								   aces_file_read_func readfn,
								   aces_file_seek_func seekfn,
								   aces_error_func errfn );

int aces_get_num_channels( ACES_FILE *f );
const char *aces_get_nth_channel_name( ACES_FILE *f, int n );
ACES_PIXEL_TYPE aces_get_nth_channel_type( ACES_FILE *f, int n );
void aces_get_nth_channel_sampling( ACES_FILE *f, int n, int32_t *xSamp, int32_t *ySamp );

ACES_COMPRESSION_TYPE aces_get_compression_type( ACES_FILE *f );

aces_attr_box2i aces_get_data_window( ACES_FILE *f );
aces_attr_box2i aces_get_display_window( ACES_FILE *f );

ACES_LINEORDER_TYPE aces_get_lineorder( ACES_FILE *f );

float aces_get_pixel_aspect_ratio( ACES_FILE *f );

aces_attr_v2f aces_get_screen_window_center( ACES_FILE *f );
float aces_get_screen_window_width( ACES_FILE *f );

int aces_get_num_attributes( ACES_FILE *f );
int aces_find_attribute_index( ACES_FILE *f, const char *name );

const char *aces_get_nth_attribute_name( ACES_FILE *f, int index );
ACES_ATTRIBUTE_TYPE aces_get_nth_attribute_type( ACES_FILE *f, int index );
const char *aces_get_nth_attribute_typename( ACES_FILE *f, int index );

const aces_attr_box2i *aces_get_nth_attribute_box2i( ACES_FILE *f, int index );
const aces_attr_box2f *aces_get_nth_attribute_box2f( ACES_FILE *f, int index );
const aces_attr_chromaticities *aces_get_nth_attribute_chromaticities( ACES_FILE *f, int index );
const aces_attr_keycode *aces_get_nth_attribute_keycode( ACES_FILE *f, int index );
const aces_attr_m33f *aces_get_nth_attribute_m33f( ACES_FILE *f, int index );
const aces_attr_m44f *aces_get_nth_attribute_m44f( ACES_FILE *f, int index );
const aces_attr_preview *aces_get_nth_attribute_preview( ACES_FILE *f, int index );
const aces_attr_rational *aces_get_nth_attribute_rational( ACES_FILE *f, int index );
const aces_attr_string *aces_get_nth_attribute_string( ACES_FILE *f, int index );
const aces_attr_string_vector *aces_get_nth_attribute_string_vector( ACES_FILE *f, int index );
const aces_attr_tiledesc *aces_get_nth_attribute_tiledesc( ACES_FILE *f, int index );
const aces_attr_timecode *aces_get_nth_attribute_timecode( ACES_FILE *f, int index );
const aces_attr_v2i *aces_get_nth_attribute_v2i( ACES_FILE *f, int index );
const aces_attr_v2f *aces_get_nth_attribute_v2f( ACES_FILE *f, int index );
const aces_attr_v3i *aces_get_nth_attribute_v3i( ACES_FILE *f, int index );
const aces_attr_v3f *aces_get_nth_attribute_v3f( ACES_FILE *f, int index );
const aces_attr_userdata *aces_get_nth_attribute_userdata( ACES_FILE *f, int index );


/** @endgroup */


/* //////////////////////////////////////// */
/** @group Write functions */

ACES_FILE *aces_start_write( int width, int height, int channels,
							 int compression,
							 const char *filename );
ACES_FILE *aces_start_write_stream( int width, int height, int channels,
									int compression,
									void *user_data,
									const char *dest_name,
									aces_file_write_func writefn,
									aces_file_seek_func seekfn,
									aces_error_func errfn );

void aces_clear_channels( ACES_FILE *f );
void aces_add_channel( ACES_FILE *f, const char *name, ACES_PIXEL_TYPE pixelType,
					   int xSampling, int ySampling );

void aces_set_compression( ACES_FILE *f, ACES_COMPRESSION_TYPE compType );

void aces_set_data_window( ACES_FILE *f, aces_attr_box2i *dataWindow );
void aces_set_display_window( ACES_FILE *f, aces_attr_box2i *dataWindow );

void aces_set_lineorder( ACES_FILE *f, ACES_LINEORDER_TYPE lo );
void aces_set_pixel_aspect_ratio( ACES_FILE *f, float par );

void aces_set_screen_window( ACES_FILE *f, float xCenter, float yCenter, float width );

int aces_set_attribute_box2i( ACES_FILE *f, const char *name,
							  int32_t xMin, int32_t yMin, int32_t xMax, int32_t yMax );
int aces_set_attribute_box2f( ACES_FILE *f, const char *name,
							  float xMin, float yMin, float xMax, float yMax );

int aces_set_attribute_chromaticities( ACES_FILE *f, const char *name, const aces_attr_chromaticities *attr );

int aces_set_attribute_keycode( ACES_FILE *f, const char *name, const aces_attr_keycode *attr );

int aces_set_attribute_m33f( ACES_FILE *f, const char *name, const float m[9] );
int aces_set_attribute_m44f( ACES_FILE *f, const char *name, const float m[16] );
int aces_set_attribute_preview( ACES_FILE *f, const char *name, const aces_attr_preview *attr );
int aces_set_attribute_rational( ACES_FILE *f, const char *name, int32_t num, uint32_t denom );
int aces_set_attribute_string( ACES_FILE *f, const char *name, const char *val );
int aces_set_attribute_string_vector( ACES_FILE *f, const char *name, const aces_attr_string_vector *attr );
int aces_set_attribute_tiledesc( ACES_FILE *f, const char *name, uint32_t xSize, uint32_t ySize, ACES_TILE_LEVEL_TYPE levType, ACES_TILE_ROUND_MODE rndMode );
int aces_set_attribute_timecode( ACES_FILE *f, const char *name, uint32_t time, uint32_t data );
int aces_set_attribute_v2i( ACES_FILE *f, const char *name, int32_t x, int32_t y );
int aces_set_attribute_v2f( ACES_FILE *f, const char *name, float x, float y );
int aces_set_attribute_v3i( ACES_FILE *f, const char *name, int32_t x, int32_t y, int32_t z );
int aces_set_attribute_v3f( ACES_FILE *f, const char *name, float x, float y, float z );
int aces_set_attribute_userdata( ACES_FILE *f, const char *name, const char *type_name, size_t size, const void *data );

/** @endgroup */

/** Closes and frees any internally allocated memory */
void aces_close( ACES_FILE *f );

/** number of bytes consumed by the given data type */
size_t aces_get_data_size( int datatype );

/** Total size of uncompressed image assuming no extra line stride */
size_t aces_get_image_bytes( ACES_FILE *f );
/** Size in bytes of an uncompressed plane in the image */
size_t aces_get_plane_bytes( ACES_FILE *f, const char *p );

/**
 * Reads the actual pixel / image data from the file and interleaves
 * the pixels into the order as in the channel list (commonly ABGR).
 *
 * All the image data must be of the same data type, or this function
 * fails.
 *
 * User is expected to have allocated enough memory to hold the
 * unpacked data.
 */
int aces_get_image( ACES_FILE *f, void *dst, int line_stride );
/**
 * Like aces_get_image, but special case which orders the planes in
 * descending alphabetical order (commonly RGBA)
 */
int aces_get_image_rgba( ACES_FILE *f, void *dst, int line_stride );
/**
 * Retrieves the particular plane from the file, decompressing using
 * the appropriate routine along the way.
 *
 * User is expected to have allocated enough memory to hold the
 * unpacked plane data.
 */
int aces_get_plane( ACES_FILE *f, const char *p, void *dst_plane, int line_stride );

int aces_set_image( ACES_FILE *f, const void *src, int32_t datatype, int line_stride );
int aces_set_image_rgba( ACES_FILE *f, const void *src, int32_t datatype, int line_stride );
int aces_set_plane( ACES_FILE *f, const char *p, void *dst, int32_t datatype, int line_stride );

/**
 * Debugging function that prints the information contained in the
 * header to stdout
 */
void aces_print_header( ACES_FILE *f, int verbose );

/**
 * Simple utility functions which does all of the above and extracts
 * in descending order (RGBA) since that is what is commonly expected,
 * storing data into a pointer that is allocated using malloc (must be
 * cleared using free to avoid memory leak)
 */
int aces_read_image( const char *filename, int *w, int *h, int *channels, int *datatype, void **data );
/**
 * Simple utility functions which does all of the above and writes an
 * image, assuming the data is coming in descending order (RGBA) since
 * that is what is commonly expected.
 */
int aces_write_image( const char *filename, int compression, int w, int h, int channels, int datatype, const void *data );


/**************************************/
/* Advanced portion of API */

void aces_set_default_error_function( aces_error_func errfn );

/**
 * Registers the given routine as the replacement unpack routine for the given
 * compression type
 */
typedef struct
{
	int dest_x_off, dest_y_off;
	int dest_width, dest_height;

	void *comp_block;
} compressed_block;

typedef int (*unpack_plane_blocks_func)( void *dest_plane, compressed_block *blocks, int n_blocks );

void aces_register_unpack_function( int comp_type, unpack_plane_blocks_func f_ptr );

typedef int (*pack_plane_blocks_func)( const void *dest_plane, compressed_block *blocks, int n_blocks );

void aces_register_pack_function( int comp_type, pack_plane_blocks_func f_ptr );


#ifdef __cplusplus
}
#endif




