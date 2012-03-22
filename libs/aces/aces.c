/*
 * Copyright (c) 2009-2012 Kimball Thurston
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "aces.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

static void builtin_error_func( const char *message );
static aces_error_func default_error_func = &builtin_error_func;

static ssize_t default_read_func( void *file_info, void *buf, size_t size );
static ssize_t default_write_func( void *file_info, const void *buf, size_t size );
static off64_t default_seek_func( void *file_info, off64_t off, int whence );

#define ACES_EXTRACT_TILE_LEVEL_MODE( x ) ( (x) & 0x0F )
#define ACES_EXTRACT_TILE_ROUND_MODE( x ) ( (x) / 16 )

#define ACES_TILE_MODE_COMBINE( level_mode, round_mode ) ( (level_mode) + (round_mode) * 16 )


typedef struct
{
	char name[32];
	ACES_PIXEL_TYPE pixel_type; /** Data representation for these pixels: uint, half, float */
	uint8_t p_linear; /**< Possible values are 0 and 1 per docs,
					   * appears deprecated and unused in openexr
					   * lib */
	uint8_t reserved[3];
	int32_t x_sampling;
	int32_t y_sampling;
} aces_attr_chlist_entry;

typedef struct
{
	int num_channels;
	aces_attr_chlist_entry *entries;
} aces_attr_chlist;

typedef struct
{
	char *name;
	char *type_name;

	ACES_ATTRIBUTE_TYPE type;
	union
	{
		uint8_t uc;
		double d;
		float f;
		int32_t i;

		aces_attr_box2i box2i;
		aces_attr_box2f box2f;
		aces_attr_chlist chlist;
		aces_attr_chromaticities chromaticities;
		aces_attr_keycode keycode;
		aces_attr_m33f m33f;
		aces_attr_m44f m44f;
		aces_attr_preview preview;
		aces_attr_rational rational;
		aces_attr_string string;
		aces_attr_string_vector stringvector;
		aces_attr_tiledesc tiledesc;
		aces_attr_timecode timecode;
		aces_attr_v2i v2i;
		aces_attr_v2f v2f;
		aces_attr_v3i v3i;
		aces_attr_v3f v3f;
		aces_attr_userdata userdata;
	};
} aces_attribute;

typedef struct aces_user_attr_s
{
	char name[32];
	aces_attribute data;
	struct aces_user_attr_s *next;
} aces_user_attribute_list;

typedef struct 
{
	ACES_STORAGE_TYPE storage_mode; /**< Part of the file version flag declaring scanlines or tiled mode */
	aces_attr_chlist channels; /**< Corresponds to required 'channels' attribute */
	uint8_t compression; /**< Corresponds to required 'compression' attribute */
	aces_attr_box2i data_window; /**< Corresponds to required 'dataWindow' attribute */
	aces_attr_box2i display_window; /**< Corresponds to required 'displayWindow' attribute */
	uint8_t line_order; /**< Corresponds to required 'lineOrder' attribute */
	float pixel_aspect_ratio; /**< Corresponds to required 'pixelAspectRatio' attribute */
	aces_attr_v2f screen_window_center; /**< Corresponds to required 'screenWindowCenter' attribute */
	float screen_window_width; /**< Corresponds to required 'screenWindowWidth' attribute */

	aces_attr_tiledesc tile_info; /**< for tiled files (@see storage_mode), corresponds to required 'tiles' attribute */

	aces_user_attribute_list *user_attributes; /**< Rest of the non-required attributes */
} ACES_PRIVATE_FILE;

typedef struct
{
	ACES_PRIVATE_FILE user_data;

	/******* PRIVATE DATA ****************/
	char given_filename[256];
	void *user_file; /* Opaque pointer used to store a reference to
					  * a user-supplied 'file' pointer during stream
					  * mode. also used internally by the default file
					  * actions */
	int fd; /* Used internally to manage file created by simple API,
			 * will be closed in aces_close if >= 0 */
	size_t cur_file_pos; /* Used to avoid calling seek to make streams work better */

	aces_file_read_func readfn; /* Function pointer used for reading data from exr file */
	aces_file_write_func writefn; /* Function pointer used for writing data to exr file */
	aces_file_seek_func seekfn; /* Function pointer used for seeing in exr file */
	aces_error_func errorfn; /* Function used to display error messages provided as strings */
} __ACES_INTERNAL_FILE_DATA;

#define ACES_FILE_TO_INTERNAL( f ) ((__ACES_INTERNAL_FILE_DATA *)(f))
#define ACES_FILE_TO_PRIVATE( f ) &(((__ACES_INTERNAL_FILE_DATA *)(f))->user_data)

/**************************************/


static __ACES_INTERNAL_FILE_DATA *
alloc_file_data_struct( void )
{
	void *ret = malloc( sizeof( __ACES_INTERNAL_FILE_DATA ) );
	if ( ret == NULL )
		return NULL;

	memset( ret, 0, sizeof( __ACES_INTERNAL_FILE_DATA ) );

	return (__ACES_INTERNAL_FILE_DATA *)ret;
}

static void
free_file_data_struct( ACES_FILE *f )
{
	if ( f != NULL )
		free( f );
}


/**************************************/


static void
print_error( __ACES_INTERNAL_FILE_DATA *f, const char *fmt, ... )
{
	if ( fmt == NULL || fmt[0] == '\0' )
		return;

	int n, size = 0;
	char msgbuf[2048];
	char *tmpbuf = NULL;
	va_list ap;

	tmpbuf = msgbuf;
	size = 2048;

	aces_error_func errfn = default_error_func;
	if ( f != NULL && f->errorfn != NULL )
		errfn = f->errorfn;

	while ( tmpbuf != NULL )
	{
		va_start( ap, fmt );
		n = vsnprintf( tmpbuf, size, fmt, ap );
		va_end( ap );

		if ( n >= 0 && n < size )
		{
			errfn( tmpbuf );
			break;
		}

		if ( n < 0 )
			size = size * 2; // old glibc didn't report needed size correctly;
		else
			size = n + 1;

		if ( tmpbuf != msgbuf )
			tmpbuf = realloc( tmpbuf, size );
		else
			tmpbuf = malloc( size );

		if ( tmpbuf == NULL )
			errfn( "Unable to alloc buffer to display error message" );
	}

	if ( tmpbuf != msgbuf )
		free( tmpbuf );
}


/**************************************/


static int
read_string( __ACES_INTERNAL_FILE_DATA *f, char attrname[32], const char *type )
{
	int pos = 0;
	char b;
	int rc;

	while ( pos < 32 )
	{
		rc = f->readfn( f->user_file, &b, 1 );
		f->cur_file_pos++;
		if ( rc != 1 )
		{
			print_error( f, "'%s': Unable to find end of %s name '%s' before EOF", f->given_filename, type, attrname );
			return -1;
		}
		attrname[pos] = b;
		if ( b == '\0' )
			break;
		++pos;
	}
	if ( pos == 32 )
	{
		attrname[31] = '\0';
		print_error( f, "'%s': %s name starting with '%s' too long", f->given_filename, type, attrname );
		return -1;
	}

	return pos;
}

static const char *the_predefined_attr_typenames[] =
{
	"box2i",
	"box2f",
	"chlist",
	"chromaticities",
	"compression",
	"double",
	"envmap",
	"float",
	"int",
	"keycode",
	"lineOrder",
	"m33f",
	"m44f",
	"preview",
	"rational",
	"string",
	"stringvector",
	"tiledesc",
	"timecode",
	"v2i",
	"v2f",
	"v3i",
	"v3f",
	NULL
};

static ACES_ATTRIBUTE_TYPE
attr_name_to_type( const char *attrname )
{
	int idx;

	if ( attrname == NULL || attrname[0] == '\0' )
		return ACES_ATTR_UNKNOWN;

	for ( idx = 0; the_predefined_attr_typenames[idx] != NULL; ++idx )
	{
		if ( ! strcmp( the_predefined_attr_typenames[idx], attrname ) )
			return (ACES_ATTRIBUTE_TYPE)( idx + 1 );
	}
	
	return ACES_ATTR_USER;
}

static int
attr_check_size_and_read( __ACES_INTERNAL_FILE_DATA *f, int32_t given, size_t nativesize,
						  void *data, const char *type_name )
{
	ssize_t nr;

	if ( (size_t)(given) != nativesize )
	{
		print_error( f, "'%s': Native size (%u) for type '%s' does NOT match size in file (%d)", f->given_filename, nativesize, type_name, given );
		return -1;
	}

	nr = f->readfn( f->user_file, data, nativesize );
	f->cur_file_pos += nr;
	if ( ((size_t)nr) != nativesize )
	{
		print_error( f, "'%s': Unable to read attribute data for type '%s' before EOF", f->given_filename, type_name );
		return -1;
	}

	return 1;
}

static int
attr_read_string( __ACES_INTERNAL_FILE_DATA *f, int32_t size, aces_attr_string *dest )
{
	/*
	** 
	** GACK: It would appear the on-disk representation document is wrong
	** and there is NOT a length integer before the beginning of the string.
	** Makes sense, it's redundant, but leave this code in case we discover
	** some place where it matters.
	**
	*/ 
	ssize_t nr;
	int32_t ssize;
	char *outstr;
#if 0
	if ( size <= 4 )
	{
		print_error( f, "'%s': String attribute type must have at least 4 bytes for (int) size at beginning of attribute data", f->given_filename );
		return -1;
	}

	nr = f->readfn( f->user_file, &ssize, sizeof(int32_t) );
	f->cur_file_pos += nr;
	if ( nr != sizeof(int32_t) )
	{
		print_error( f, "'%s': Unable to read string attribute string length before EOF", f->given_filename );
		return -1;
	}

	if ( ssize != (size - sizeof(int32_t)) )
	{
		print_error( f, "'%s': String attribute size (%d) does not match string length (%d) + length tag", f->given_filename, size, ssize );
		return -1;
	}
#else
	ssize = size;
#endif
	dest->length = ssize;
	outstr = malloc( ssize * sizeof(char) );
	if ( outstr == NULL )
	{
		print_error( f, "'%s': Unable to allocate memory for string attribute of length %d", f->given_filename, ssize );
		return -1;
	}

	nr = f->readfn( f->user_file, outstr, ssize );
	dest->str = outstr;
	f->cur_file_pos += nr;
	if ( nr != ssize )
	{
		print_error( f, "'%s': Unable to read string attribute of length %d before EOF", f->given_filename, ssize );
		free( outstr );
		dest->str = NULL;
		return -1;
	}

	return 1;
}

static int
attr_read_userdata( __ACES_INTERNAL_FILE_DATA *f, int32_t size, aces_attr_userdata *dest )
{
	ssize_t nr;

	dest->size = size;
	char *dstdata = malloc( size * sizeof(uint8_t) );
	dest->data = dstdata;
	if ( dest->data == NULL )
	{
		print_error( f, "'%s': Unable to allocate memory for user data attribute of length %d", f->given_filename, size );
		return -1;
	}

	nr = f->readfn( f->user_file, dstdata, size );
	f->cur_file_pos += nr;
	if ( nr != size )
	{
		print_error( f, "'%s': Unable to read user data attribute of length %d before EOF", f->given_filename, size );
		free( (void *)dest->data );
		dest->data = NULL;
		return -1;
	}

	return 1;
}

static int
attr_read_preview( __ACES_INTERNAL_FILE_DATA *f, int32_t size, aces_attr_preview *dest )
{
	ssize_t nr;
	uint32_t dims[2];
	size_t nToR;

	if ( size <= 4 )
	{
		print_error( f, "'%s': Preview attribute type must have at least 8 bytes for width x height at beginning of attribute data", f->given_filename );
		return -1;
	}
	
	nr = f->readfn( f->user_file, &dims, 2 * sizeof(uint32_t) );
	f->cur_file_pos += nr;
	if ( nr != ( 2 * sizeof(uint32_t) ) )
	{
		print_error( f, "'%s': Unable to read preview attribute width & height before EOF", f->given_filename );
		return -1;
	}

	nToR = 4 * dims[0] * dims[1] * sizeof(uint8_t);

	dest->width = dims[0];
	dest->height = dims[1];
	uint8_t *rgbaptr = malloc( nToR );
	dest->rgba = rgbaptr;
	if ( dest->rgba == NULL )
	{
		print_error( f, "'%s': Unable to allocate memory for preview pixel data of size %u x %u", f->given_filename, dims[0], dims[1] );
		return -1;
	}

	nr = f->readfn( f->user_file, rgbaptr, nToR );
	f->cur_file_pos += nr;
	if ( (size_t)(nr) != nToR )
	{
		print_error( f, "'%s': Unable to read preview pixel data of data size %u before EOF", f->given_filename, nToR );
		free( rgbaptr );
		dest->rgba = NULL;
		return -1;
	}
	return 1;
}

struct tmp_ch_list
{
	aces_attr_chlist_entry c;
	struct tmp_ch_list *next;
};

static int
attr_read_chlist( __ACES_INTERNAL_FILE_DATA *f, int32_t size, aces_attr_chlist *dest )
{
	struct tmp_ch_list *channel_list = NULL;
	struct tmp_ch_list *cur_chan, *prev_chan, *new_chan;
	char chstr[32];
	int i, ret = 1;

	dest->num_channels = 0;
	dest->entries = NULL;
	while ( read_string( f, chstr, "channel" ) )
	{
		new_chan = malloc( sizeof(struct tmp_ch_list) );
		if ( new_chan == NULL )
		{
			print_error( f, "'%s': Unable to allocate temporary memory while reading channel list", f->given_filename );
			ret = -1;
			dest->num_channels = 0;
			break;
		}
		memset( new_chan, 0, sizeof(struct tmp_ch_list) );
		strcpy( (char *)new_chan->c.name, chstr );
		if ( attr_check_size_and_read( f, sizeof(int32_t), sizeof(int32_t),
									   &(new_chan->c.pixel_type), "channel pixel type" ) < 0 )
		{
			ret = -1;
			dest->num_channels = 0;
			free( new_chan );
			break;
		}
		if ( attr_check_size_and_read( f, sizeof(uint8_t), sizeof(uint8_t),
									   &(new_chan->c.p_linear), "channel linear flag" ) < 0 )
		{
			ret = -1;
			dest->num_channels = 0;
			free( new_chan );
			break;
		}
		if ( attr_check_size_and_read( f, 3 * sizeof(uint8_t), 3 * sizeof(uint8_t),
									   &(new_chan->c.reserved), "channel padding" ) < 0 )
		{
			ret = -1;
			dest->num_channels = 0;
			free( new_chan );
			break;
		}
		if ( attr_check_size_and_read( f, sizeof(int32_t), sizeof(int32_t),
									   &(new_chan->c.x_sampling), "channel x sampling" ) < 0 )
		{
			ret = -1;
			dest->num_channels = 0;
			free( new_chan );
			break;
		}
		if ( attr_check_size_and_read( f, sizeof(int32_t), sizeof(int32_t),
									   &(new_chan->c.y_sampling), "channel y sampling" ) < 0 )
		{
			ret = -1;
			dest->num_channels = 0;
			free( new_chan );
			break;
		}

		/* Ok, have our new channel, need to insert (sorted) into the list */
		if ( channel_list == NULL )
			channel_list = new_chan;
		else
		{
			cur_chan = channel_list;
			prev_chan = NULL;
			while ( cur_chan != NULL )
			{
				if ( strcmp( new_chan->c.name, cur_chan->c.name ) < 0 )
					break;
				prev_chan = cur_chan;
				cur_chan = cur_chan->next;
			}
			new_chan->next = cur_chan;
			if ( prev_chan == NULL )
				channel_list = new_chan;
			else
				prev_chan->next = new_chan;
		}

		dest->num_channels++;
	}

	if ( dest->num_channels > 0 )
	{
		dest->entries = malloc( dest->num_channels * sizeof(aces_attr_chlist_entry) );
		if ( dest->entries == NULL )
		{
			print_error( f, "'%s': Unable to allocate memory for channel list structure of %d channels", f->given_filename, dest->num_channels );
			ret = -1;
		}
		else
		{
			cur_chan = channel_list;
			for ( i = 0; i < dest->num_channels; ++i )
			{
				dest->entries[i] = cur_chan->c;
				cur_chan = cur_chan->next;
			}
		}
	}
		
	while ( channel_list != NULL )
	{
		cur_chan = channel_list->next;
		free( channel_list );
		channel_list = cur_chan;
	}

	return ret;
}

static int
read_attribute( __ACES_INTERNAL_FILE_DATA *f, char attrname[32], aces_attribute *attr )
{
	int pos;
	int32_t asize;
	int rc;
	int ret = -1;
	char type_name[32];

	memset( attr, 0, sizeof(aces_attribute) );

	pos = read_string( f, attrname, "attribute" );
	if ( pos <= 0 )
		return pos;

	pos = read_string( f, type_name, "type" );
	if ( pos < 0 )
		return -1;

	rc = f->readfn( f->user_file, &asize, sizeof(int32_t) );
	f->cur_file_pos += rc;
	if ( rc != sizeof(int32_t) )
	{
		print_error( f, "'%s': Unable to read size of attribute '%s' before EOF", f->given_filename, attrname );
		return -1;
	}

	attr->type = attr_name_to_type( type_name );

	switch ( attr->type )
	{
		case ACES_ATTR_BOX2I:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_box2i),
											&(attr->box2i), type_name );
			break;
		case ACES_ATTR_BOX2F:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_box2f),
											&(attr->box2f), type_name );
			break;
		case ACES_ATTR_CHROMATICITIES:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_chromaticities),
											&(attr->chromaticities), type_name );
			break;
		case ACES_ATTR_COMPRESSION:
			ret = attr_check_size_and_read( f, asize, sizeof(uint8_t),
											&(attr->uc), type_name );
			break;
		case ACES_ATTR_DOUBLE:
			ret = attr_check_size_and_read( f, asize, sizeof(double),
											&(attr->d), type_name );
			break;
		case ACES_ATTR_ENVMAP:
			ret = attr_check_size_and_read( f, asize, sizeof(uint8_t),
											&(attr->uc), type_name );
			break;
		case ACES_ATTR_FLOAT:
			ret = attr_check_size_and_read( f, asize, sizeof(float),
											&(attr->f), type_name );
			break;
		case ACES_ATTR_INT:
			ret = attr_check_size_and_read( f, asize, sizeof(int32_t),
											&(attr->i), type_name );
			break;
		case ACES_ATTR_KEYCODE:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_keycode),
											&(attr->keycode), type_name );
			break;
		case ACES_ATTR_LINEORDER:
			ret = attr_check_size_and_read( f, asize, sizeof(uint8_t),
											&(attr->uc), type_name );
			break;
		case ACES_ATTR_M33F:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_m33f),
											&(attr->m33f), type_name );
			break;
		case ACES_ATTR_M44F:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_m44f),
											&(attr->m44f), type_name );
			break;
		case ACES_ATTR_RATIONAL:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_rational),
											&(attr->rational), type_name );
			break;
		case ACES_ATTR_TILEDESC:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_tiledesc),
											&(attr->tiledesc), type_name );
			break;
		case ACES_ATTR_TIMECODE:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_timecode),
											&(attr->timecode), type_name );
			break;
		case ACES_ATTR_V2I:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_v2i),
											&(attr->v2i), type_name );
			break;
		case ACES_ATTR_V2F:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_v2f),
											&(attr->v2f), type_name );
			break;
		case ACES_ATTR_V3I:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_v3i),
											&(attr->v3i), type_name );
			break;
		case ACES_ATTR_V3F:
			ret = attr_check_size_and_read( f, asize, sizeof(aces_attr_v3f),
											&(attr->v3f), type_name );
			break;
		case ACES_ATTR_STRING:
			ret = attr_read_string( f, asize, &(attr->string) );
			break;
		case ACES_ATTR_PREVIEW:
			ret = attr_read_preview( f, asize, &(attr->preview) );
			break;
		case ACES_ATTR_CHLIST:
			ret = attr_read_chlist( f, asize, &(attr->chlist) );
			break;
		case ACES_ATTR_USER:
			attr->type_name = strdup( type_name );
			ret = attr_read_userdata( f, asize, &(attr->userdata) );
			break;
		case ACES_ATTR_UNKNOWN:
		default:
			print_error( f, "'%s': Parsing attribute '%s', unknown type '%s', unable to parse file", f->given_filename, attrname, type_name );
			ret = -1;
			break;
	}

	return ret;
}

// if these were all the same data type, we could have a table
// but they aren't, so rather than do a table with a function pointer
// to dispatch it's handler, there's not that many, so it's
// easier to just have the code exploded below...
#define REQ_CHANNELS_STR "channels"
#define REQ_COMP_STR "compression"
#define REQ_DATA_STR "dataWindow"
#define REQ_DISP_STR "displayWindow"
#define REQ_LO_STR "lineOrder"
#define REQ_PAR_STR "pixelAspectRatio"
#define REQ_SCR_WC_STR "screenWindowCenter"
#define REQ_SCR_WW_STR "screenWindowWidth"
#define REQ_TILES_STR "tiles"

#define REQ_CHANNELS_MASK 0x0001
#define REQ_COMP_MASK 0x0002
#define REQ_DATA_MASK 0x0004
#define REQ_DISP_MASK 0x0008
#define REQ_LO_MASK 0x0010
#define REQ_PAR_MASK 0x0020
#define REQ_SCR_WC_MASK 0x0040
#define REQ_SCR_WW_MASK 0x0080
#define REQ_TILES_MASK 0x0100

static uint16_t
add_attribute( ACES_PRIVATE_FILE *f, char attrname[32], aces_attribute *attr )
{
	aces_user_attribute_list *uattr, *pEnd;

	if ( ! strcmp( attrname, REQ_CHANNELS_STR ) )
	{
		f->channels = attr->chlist;
		return REQ_CHANNELS_MASK;
	}

	if ( ! strcmp( attrname, REQ_COMP_STR ) )
	{
		f->compression = attr->uc;
		return REQ_COMP_MASK;
	}

	if ( ! strcmp( attrname, REQ_DATA_STR ) )
	{
		f->data_window = attr->box2i;
		return REQ_DATA_MASK;
	}

	if ( ! strcmp( attrname, REQ_DISP_STR ) )
	{
		f->display_window = attr->box2i;
		return REQ_DISP_MASK;
	}

	if ( ! strcmp( attrname, REQ_LO_STR ) )
	{
		f->line_order = attr->uc;
		return REQ_LO_MASK;
	}

	if ( ! strcmp( attrname, REQ_PAR_STR ) )
	{
		f->pixel_aspect_ratio = attr->f;
		return REQ_PAR_MASK;
	}

	if ( ! strcmp( attrname, REQ_SCR_WC_STR ) )
	{
		f->screen_window_center = attr->v2f;
		return REQ_SCR_WC_MASK;
	}

	if ( ! strcmp( attrname, REQ_SCR_WW_STR ) )
	{
		f->screen_window_width = attr->f;
		return REQ_SCR_WW_MASK;
	}

	if ( ! strcmp( attrname, REQ_TILES_STR ) )
	{
		if ( f->storage_mode == ACES_STORAGE_TILED )
		{
			f->tile_info = attr->tiledesc;
			return REQ_TILES_MASK;
		}
	}

	uattr = malloc( sizeof(aces_user_attribute_list) );
	memset( uattr, 0, sizeof(aces_user_attribute_list) );
	strcpy( uattr->name, attrname );
	uattr->data = *attr;
	uattr->next = NULL;

	pEnd = f->user_attributes;

	if ( pEnd == NULL )
	{
		f->user_attributes = uattr;
		return 0;
	}
	
	while ( pEnd->next != NULL )
		pEnd = pEnd->next;

	pEnd->next = uattr;

	return 0;
}

static int
check_attr_mask( ACES_PRIVATE_FILE *fp, uint16_t v, uint16_t mask, const char *name )
{
	if ( ( v & mask ) == 0 )
	{
		__ACES_INTERNAL_FILE_DATA *f = ACES_FILE_TO_INTERNAL( fp );
		print_error( f, "'%s': Missing required attribute '%s'", f->given_filename, name );
		return 1;
	}
	return 0;
}

static int
report_missing_attributes( ACES_PRIVATE_FILE *f, uint16_t accumAttrsRead )
{
	int numMissing = 0;

	numMissing += check_attr_mask( f, accumAttrsRead, REQ_CHANNELS_MASK, REQ_CHANNELS_STR );
	numMissing += check_attr_mask( f, accumAttrsRead, REQ_COMP_MASK, REQ_COMP_STR );
	numMissing += check_attr_mask( f, accumAttrsRead, REQ_DATA_MASK, REQ_DATA_STR );
	numMissing += check_attr_mask( f, accumAttrsRead, REQ_DISP_MASK, REQ_DISP_STR );
	numMissing += check_attr_mask( f, accumAttrsRead, REQ_LO_MASK, REQ_LO_STR );
	numMissing += check_attr_mask( f, accumAttrsRead, REQ_PAR_MASK, REQ_PAR_STR );
	numMissing += check_attr_mask( f, accumAttrsRead, REQ_SCR_WC_MASK, REQ_SCR_WC_STR );
	numMissing += check_attr_mask( f, accumAttrsRead, REQ_SCR_WW_MASK, REQ_SCR_WW_STR );

	if ( f->storage_mode == ACES_STORAGE_TILED )
		numMissing += check_attr_mask( f, accumAttrsRead, REQ_TILES_MASK, REQ_TILES_STR );

	return numMissing;
}

static int
read_header( ACES_PRIVATE_FILE *f )
{
	int32_t magic_version[2];
	char attrname[32];
	aces_attribute curattr;
	int rda;
	uint16_t reqMask = 0;
	__ACES_INTERNAL_FILE_DATA *intfp = ACES_FILE_TO_INTERNAL( f );

	if ( intfp->readfn( intfp->user_file, magic_version, sizeof(uint32_t) * 2 ) != ( sizeof(uint32_t) * 2 ) )
	{
		print_error( intfp, "'%s': Unable to read file magic and version", intfp->given_filename );
		return -1;
	}
	intfp->cur_file_pos += sizeof(uint32_t) * 2;

	if ( magic_version[0] != 20000630 )
	{
		print_error( intfp, "'%s': File is not an OpenEXR format file, magic is 0x%08X (%d)", intfp->given_filename, magic_version[0], magic_version[0] );
		return -1;
	}

	if ( magic_version[1] != 0x2 && magic_version[1] != 0x202 )
	{
		print_error( intfp, "'%s': File is an unsupported version of the OpenEXR format: 0x%08X", intfp->given_filename, magic_version[1] );
		return -1;
	}

	if ( magic_version[1] == 0x202 )
		f->storage_mode = ACES_STORAGE_TILED;
	else
		f->storage_mode = ACES_STORAGE_SCANLINE;

	do
	{
		rda = read_attribute( intfp, attrname, &curattr );

		if ( rda == 1 )
			reqMask |= add_attribute( f, attrname, &curattr );
	} while ( rda == 1 );

	/* EXIT POINT */
	if ( rda < 0 )
		return rda;

	if ( 0 != report_missing_attributes( f, reqMask ) )
		return -1;

	return 0;
}


/**************************************/


ACES_FILE *
aces_start_read( const char *filename )
{
	int fd = -1;
	__ACES_INTERNAL_FILE_DATA *ret = NULL;
	ACES_FILE *rval;

	if ( filename == NULL || filename[0] == '\0' )
	{
		(*default_error_func)( "Invalid empty filename passed" );
		return NULL;
	}

#if !defined(__LITTLE_ENDIAN__) && __BYTE_ORDER == __BIG_ENDIAN
#  error "Sorry, big endian format machine architectures are not yet supported"
	(*default_error_func)( "Sorry, big endian format machine architectures are not yet supported" );
	return NULL;
#endif

	ret = alloc_file_data_struct();
	if ( ret == NULL )
	{
		(*default_error_func)( "Unable to allocate file structure memory" );
		return NULL;
	}

	rval = (ACES_FILE *)ret;

	fd = open( filename, O_RDONLY );

	if ( fd == -1 )
	{
		char errbuf[1024];

		if ( strerror_r( errno, errbuf, 1024 ) != 0 )
		{
			if ( errno == EINVAL )
				strcpy( errbuf, "Unknown error" );
			else if ( errno == ERANGE )
				strcpy( errbuf, "Error too large for display" );
		}

		print_error( ret, "'%s': %s", filename, errbuf );
		aces_close( rval );
		return NULL;
	}

	ret->readfn = default_read_func;
	ret->seekfn = default_seek_func;
	ret->errorfn = default_error_func;

	ret->fd = fd;
	ret->user_file = (void *)ret;
	strncpy( ret->given_filename, filename, 255 );

	if ( read_header( &ret->user_data ) < 0 )
	{
		aces_close( rval );
		rval = NULL;
	}

	return rval;
}


/**************************************/


ACES_FILE *
aces_start_read_stream( void *file_info,
						const char *source_name,
						aces_file_read_func readfn,
						aces_file_seek_func seekfn,
						aces_error_func errfn )
{
	__ACES_INTERNAL_FILE_DATA *ret = NULL;
	ACES_FILE *rval;

#if !defined(__LITTLE_ENDIAN__) && __BYTE_ORDER == __BIG_ENDIAN
#  error "Sorry, big endian format machine architectures are not yet supported"
	errfn( "Sorry, big endian format machine architectures are not yet supported" );
	return NULL;
#endif

	if ( errfn == NULL )
		errfn = default_error_func;

	if ( readfn == NULL )
	{
		errfn( "Missing required read function pointer argument" );
		return NULL;
	}

	if ( seekfn == NULL )
	{
		errfn( "Missing required seek function pointer argument" );
		return NULL;
	}

	ret = alloc_file_data_struct();
	if ( ret == NULL )
	{
		errfn( "Unable to allocate file structure memory" );
		return NULL;
	}

	rval = (ACES_FILE *)ret;

	ret->user_file = file_info;
	ret->readfn = readfn;
	ret->seekfn = seekfn;
	ret->errorfn = errfn;
	if ( source_name == NULL || source_name[0] == '\0' )
		strcpy( ret->given_filename, "<stream>" );
	else
		strncpy( ret->given_filename, source_name, 255 );

	if ( read_header( &ret->user_data ) < 0 )
	{
		aces_close( rval );
		rval = NULL;
	}

	return rval;
}


/**************************************/


ACES_FILE *
aces_start_write( int width, int height, int channels, int compression, const char *filename )
{
	if ( filename == NULL || filename[0] == '\0' )
	{
		(*default_error_func)( "Invalid empty filename passed" );
		return NULL;
	}

	if ( width == 0 || height == 0 || channels == 0 )
	{
		(*default_error_func)( "Cowardly refusing to write out a zero sized image" );
		return NULL;
	}

	return NULL;
}


/**************************************/


ACES_FILE *
aces_start_write_stream( int width, int height, int channels,
						 int compression, void *file_info,
						 const char *dest_name,
						 aces_file_write_func writefn,
						 aces_file_seek_func seekfn,
						 aces_error_func errfn )
{
	return NULL;
}


/**************************************/


size_t
aces_get_data_size( int datatype )
{
	switch ( datatype )
	{
		case ACES_PIXEL_UINT: return 4;
		case ACES_PIXEL_HALF: return 2;
		case ACES_PIXEL_FLOAT: return 4;
		default:
			break;
	}

	(*default_error_func)( "Unknown data type passed to aces_get_data_size" );
	return 0;
}


/**************************************/


size_t
aces_get_image_bytes( ACES_FILE *f )
{
	int w, h, c, d;
	if ( f == NULL )
	{
		(*default_error_func)( "NULL file pointer" );
		return 0;
	}

	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );
	w = pf->display_window.x_max - pf->display_window.x_min + 1;
	h = pf->display_window.y_max - pf->display_window.y_min + 1;
	c = pf->channels.num_channels;
	(*default_error_func)( "Loop over channels to check pixel type nyi" );
	d = aces_get_data_size( pf->channels.entries[0].pixel_type );

	return w * h * c * d;
}


/**************************************/


size_t
aces_get_plane_bytes( ACES_FILE *f, const char *p )
{
	int w, h, c, cb;

	if ( f == NULL )
	{
		(*default_error_func)( "NULL file pointer" );
		return 0;
	}

	if ( p == NULL || p[0] == '\0' )
	{
		ACES_FILE_TO_INTERNAL( f )->errorfn( "Empty plane name passed to aces_get_plane_bytes" );
		return 0;
	}

	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );
	w = pf->display_window.x_max - pf->display_window.x_min + 1;
	h = pf->display_window.y_max - pf->display_window.y_min + 1;
	cb = 0;
	for ( c = 0; c < pf->channels.num_channels; ++c )
	{
		if ( ! strcmp( pf->channels.entries[c].name, p ) )
		{
			cb = aces_get_data_size( pf->channels.entries[c].pixel_type );
			break;
		}
	}

	if ( cb == 0 )
	{
		__ACES_INTERNAL_FILE_DATA *fp = ACES_FILE_TO_INTERNAL( f );
		print_error( fp, "'%s': Unable to find plane '%s'", fp->given_filename, p );
		return 0;
	}

	return w * h * cb;
}


/**************************************/


static void
release_attribute_memory( aces_attribute *attr )
{
	switch ( attr->type )
	{
		case ACES_ATTR_CHLIST:
			if ( attr->chlist.entries )
				free( attr->chlist.entries );
			break;
		case ACES_ATTR_PREVIEW:
			if ( attr->preview.rgba )
				free( (void *)attr->preview.rgba );
			break;
		case ACES_ATTR_STRING:
			if ( attr->string.str )
				free( (void *)attr->string.str );
			break;
		case ACES_ATTR_USER:
			if ( attr->type_name )
				free( (void *)attr->type_name );

			if ( attr->userdata.data )
				free( (void *)attr->userdata.data );
			break;
		default:
			break;
	}
}

void
aces_close( ACES_FILE *f )
{
	aces_user_attribute_list *cur_ua, *next_ua;
	__ACES_INTERNAL_FILE_DATA *intf = ACES_FILE_TO_INTERNAL( f );

	if ( f == NULL )
		return;

	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );

	if ( pf->channels.entries )
		free( pf->channels.entries );

	cur_ua = pf->user_attributes;
	while ( cur_ua != NULL )
	{
		release_attribute_memory( &(cur_ua->data) );
		next_ua = cur_ua->next;
		free( cur_ua );
		cur_ua = next_ua;
	}
	pf->user_attributes = NULL;

	if ( intf->fd >= 0 )
		close( intf->fd );
	// else the user is responsible for closing the stream

	free_file_data_struct( f );
}


/**************************************/


static int
read_check_argument( __ACES_INTERNAL_FILE_DATA *f, void *dst )
{
	if ( f == NULL )
	{
		(*default_error_func)( "NULL file reference passed to aces read function" );
		return -1;
	}

	if ( f->readfn == NULL && f->writefn != NULL )
	{
		print_error( f, "Attempt to retrieve image from an exr image structure opened for write" );
		return -1;
	}

	if ( dst == NULL )
	{
		print_error( f, "Attempt to retrieve image into an un-allocated destination buffer" );
		return -1;
	}

	switch ( f->user_data.compression )
	{
		case ACES_COMPRESSION_NONE:
		case ACES_COMPRESSION_RLE:
		case ACES_COMPRESSION_ZIPS:
		case ACES_COMPRESSION_ZIP:
		case ACES_COMPRESSION_PIZ:
			break;
		case ACES_COMPRESSION_PXR24:
			print_error( f, "PXR24 compression not available, please use the full OpenEXR library to read this image" );
			return -1;
		case ACES_COMPRESSION_B44:
			print_error( f, "B44 compression not available, please use the full OpenEXR library to read this image" );
			return -1;
		case ACES_COMPRESSION_B44A:
			print_error( f, "B44A compression not available, please use the full OpenEXR library to read this image" );
			return -1;
		default:
			print_error( f, "Unknown compression %u not supported", f->user_data.compression );
			return -1;
	}

	if ( f->user_data.storage_mode == ACES_STORAGE_TILED &&
		 f->user_data.tile_info.level_type != ACES_TILE_ONE_LEVEL )
	{
		print_error( f, "aces only supports single image tiled images, please use full OpenEXR library to read this image" );
		return -1;
	}

	if ( f->user_data.storage_mode != ACES_STORAGE_TILED && f->user_data.storage_mode != ACES_STORAGE_SCANLINE )
	{
		print_error( f, "'%s': Unknown storage mode while reading: '%u'", f->given_filename, f->user_data.storage_mode );
		return -1;
	}

	if ( f->user_data.line_order == ACES_LINEORDER_RANDOM_Y )
	{
		print_error( f, "aces doesn't support random y line ordering, please use full OpenEXR library to read this image" );
		return -1;
	}

	if ( f->user_data.line_order != ACES_LINEORDER_INCREASING_Y && f->user_data.line_order != ACES_LINEORDER_DECREASING_Y )
	{
		print_error( f, "'%s': Unknown line order while reading: '%u'", f->given_filename, f->user_data.line_order );
		return -1;
	}

	return 0;
}


/**************************************/


static int
fill_packed_data( ACES_FILE *f )
{
	int lines_per_buffer;
	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );
	switch ( pf->compression )
	{
		case ACES_COMPRESSION_NONE:
			lines_per_buffer = 1;
			break;
		case ACES_COMPRESSION_RLE:
			lines_per_buffer = 1;
			break;
		case ACES_COMPRESSION_ZIPS:
			lines_per_buffer = 1;
			break;
		case ACES_COMPRESSION_ZIP:
			lines_per_buffer = 16;
			break;
		case ACES_COMPRESSION_PIZ:
			lines_per_buffer = 32;
			break;
		case ACES_COMPRESSION_PXR24:
			lines_per_buffer = -16;
			print_error( ACES_FILE_TO_INTERNAL( f ), "PXR24 compression support not yet implemented, consider using full OpenEXR library" );
			break;
		case ACES_COMPRESSION_B44:
			lines_per_buffer = -32;
			print_error( ACES_FILE_TO_INTERNAL( f ), "B44 compression support not yet implemented, consider using full OpenEXR library" );
			break;
		case ACES_COMPRESSION_B44A:
			lines_per_buffer = -32;
			print_error( ACES_FILE_TO_INTERNAL( f ), "B44A compression support not yet implemented, consider using full OpenEXR library" );
			break;
		default:
			print_error( ACES_FILE_TO_INTERNAL( f ), "Unknown compression %u not supported", pf->compression );
			return -1;
	}

	// Unsupported compression check
	if ( lines_per_buffer < 0 )
		return -1;

	return 0;
}


/**************************************/


static uint32_t *
read_offset_table( __ACES_INTERNAL_FILE_DATA *f, int num_off )
{
	uint32_t *ret;
	size_t nb;
	ssize_t nr;

	nb = sizeof(uint32_t) * num_off;

	ret = malloc( nb );
	if ( ret == NULL )
	{
		print_error( f, "'%s': Unable to alloc memory creating offset table (%d) entries", f->given_filename, num_off );
		return NULL;
	}

	nr = f->readfn( f->user_file, ret, nb );
	f->cur_file_pos += nr;
	if ( (size_t)(nr) != nb )
	{
		print_error( f, "'%s': Unable to read offset table from file", f->given_filename );
		free( ret );
		return NULL;
	}

	return ret;
}

static void
compute_scanline_block_info( ACES_FILE *f, int *lines_per, int *n_blocks )
{
	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );
	switch ( pf->compression )
	{
		case ACES_COMPRESSION_NONE:
		case ACES_COMPRESSION_RLE:
		case ACES_COMPRESSION_ZIPS:
			*lines_per = 1;
			break;
		case ACES_COMPRESSION_ZIP:
		case ACES_COMPRESSION_PXR24:
			*lines_per = 16;
			break;
		case ACES_COMPRESSION_PIZ:
		case ACES_COMPRESSION_B44:
		case ACES_COMPRESSION_B44A:
			*lines_per = 32;
			break;
		default:
		{
			__ACES_INTERNAL_FILE_DATA *fp = ACES_FILE_TO_INTERNAL( f );
			print_error( fp, "'%s': Unknown compression type: %d", fp->given_filename, pf->compression );
			*lines_per = -1;
			break;
		}
	}

	int n_lines = pf->data_window.y_max - pf->data_window.y_min;
	if ( *lines_per > 0 )
		*n_blocks = ( n_lines + *lines_per - 1 ) / *lines_per;
	else
		*n_blocks = 0;
}

static void
compute_tile_block_info( ACES_FILE *f, int *tile_size_x, int *tile_size_y, int *n_tiles )
{
	*tile_size_x = 0;
	*tile_size_y = 0;
	*n_tiles = 0;
}

typedef void (*uncompressfn)( void *dst, const void *src, size_t packed_size );


/**************************************/


static int
read_scanlines_interleaved( ACES_FILE *f, void *dst, int order )
{
	uint32_t *offsetTable = NULL;
	int lpb, nb;

	compute_scanline_block_info( f, &lpb, &nb );

	offsetTable = read_offset_table( ACES_FILE_TO_INTERNAL( f ), nb );
	if ( offsetTable == NULL )
		return -1;

	free( offsetTable );
	return 0;
}

static int
read_tiles_interleaved( ACES_FILE *f, void *dst, int order )
{
	print_error( ACES_FILE_TO_INTERNAL( f ), "aces tile reading not yet supported" );
/*	uint32_t *offsetTable = NULL;

	offsetTable = read_offset_table( f );

	if ( offsetTable != NULL )
		free( offsetTable );
*/
	return -1;
}


/**************************************/


int
aces_get_image( ACES_FILE *f, void *dst, int line_stride )
{
	int ret;

	if ( read_check_argument( ACES_FILE_TO_INTERNAL( f ), dst ) != 0 )
		return -1;

	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );
	if ( pf->storage_mode == ACES_STORAGE_SCANLINE )
	{
		ret = read_scanlines_interleaved( f, dst, 1 );
	}
	else
	{
		ret = read_tiles_interleaved( f, dst, 1 );
	}

	return ret;
}


/**************************************/


int
aces_get_image_rgba( ACES_FILE *f, void *dst, int line_stride )
{
	if ( read_check_argument( ACES_FILE_TO_INTERNAL( f ), dst ) != 0 )
		return -1;

	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );
	if ( pf->storage_mode == ACES_STORAGE_SCANLINE )
		return read_scanlines_interleaved( f, dst, -1 );

	return read_tiles_interleaved( f, dst, -1 );
}


/**************************************/


static int
read_scanlines_plane( __ACES_INTERNAL_FILE_DATA *f, const char *p, void *dst, int line_stride )
{
	print_error( f, "nyi" );
	return -1;
}

static int
read_tiles_plane( __ACES_INTERNAL_FILE_DATA *f, const char *p, void *dst, int line_stride )
{
	print_error( f, "aces tile reading not yet supported" );
	return -1;
}

int
aces_get_plane( ACES_FILE *f, const char *p, void *dst, int line_stride )
{
	if ( read_check_argument( ACES_FILE_TO_INTERNAL( f ), dst ) != 0 )
		return -1;

	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );
	if ( pf->storage_mode == ACES_STORAGE_SCANLINE )
		return read_scanlines_plane( ACES_FILE_TO_INTERNAL( f ), p, dst, line_stride );

	return read_tiles_plane( ACES_FILE_TO_INTERNAL( f ), p, dst, line_stride );
}


/**************************************/


int
aces_set_image( ACES_FILE *f, const void *src, int32_t datatype, int line_stride )
{
	print_error( ACES_FILE_TO_INTERNAL( f ), "function not yet implemented" );
	return -1;
}


/**************************************/


int
aces_set_image_rgba( ACES_FILE *f, const void *src, int32_t datatype, int line_stride )
{
	print_error( ACES_FILE_TO_INTERNAL( f ), "function not yet implemented" );
	return -1;
}


/**************************************/


int
aces_set_image_plane( ACES_FILE *f, const char *p, void *dst, int32_t datatype, int line_stride )
{
	print_error( ACES_FILE_TO_INTERNAL( f ), "function not yet implemented" );
	return -1;
}


/**************************************/


static void
print_attr( aces_attribute *a )
{
	switch ( a->type )
	{
		case ACES_ATTR_BOX2I:
			printf( "box2i [ %d, %d - %d %d ]", a->box2i.x_min, a->box2i.y_min, a->box2i.x_max, a->box2i.y_max );
			break;
		case ACES_ATTR_BOX2F:
			printf( "box2f [ %g, %g - %g %g ]", a->box2f.x_min, a->box2f.y_min, a->box2f.x_max, a->box2f.y_max );
			break;
		case ACES_ATTR_CHLIST:
			printf( "channel list" );
			break;
		case ACES_ATTR_CHROMATICITIES:
			printf( "chromaticities r[%g, %g] g[%g, %g] b[%g, %g] w[%g, %g]",
					a->chromaticities.red_x, a->chromaticities.red_y,
					a->chromaticities.green_x, a->chromaticities.green_y,
					a->chromaticities.blue_x, a->chromaticities.blue_y,
					a->chromaticities.white_x, a->chromaticities.white_y );
			break;
		case ACES_ATTR_COMPRESSION:
			printf( "compression 0x%02X", a->uc );
			break;
		case ACES_ATTR_DOUBLE:
			printf( "double %g", a->d );
			break;
		case ACES_ATTR_ENVMAP:
			printf( "envmap %s", a->uc == 0 ? "latlong" : "cube" );
			break;
		case ACES_ATTR_FLOAT:
			printf( "float %g", a->f );
			break;
		case ACES_ATTR_INT:
			printf( "int %d", a->i );
			break;
		case ACES_ATTR_KEYCODE:
			printf( "keycode mfgc %d film %d prefix %d count %d perf_off %d ppf %d ppc %d",
					a->keycode.film_mfc_code, a->keycode.film_type, a->keycode.prefix,
					a->keycode.count, a->keycode.perf_offset, a->keycode.perfs_per_frame,
					a->keycode.perfs_per_count );
			break;
		case ACES_ATTR_LINEORDER:
			printf( "lineorder %d (%s)", a->uc,
					a->uc == ACES_LINEORDER_INCREASING_Y ? "increasing" :
					( a->uc == ACES_LINEORDER_DECREASING_Y ? "decreasing" :
					  ( a->uc == ACES_LINEORDER_RANDOM_Y ? "random" : "unknown" ) ) );
			break;
		case ACES_ATTR_M33F:
			printf( "m33f [ [%g %g %g] [%g %g %g] [%g %g %g] ]",
					a->m33f.m[0], a->m33f.m[1], a->m33f.m[2],
					a->m33f.m[3], a->m33f.m[4], a->m33f.m[5],
					a->m33f.m[6], a->m33f.m[7], a->m33f.m[8] );
			break;
		case ACES_ATTR_M44F:
			printf( "m44f [ [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] ]",
					a->m44f.m[0], a->m44f.m[1], a->m44f.m[2], a->m44f.m[3],
					a->m44f.m[4], a->m44f.m[5], a->m44f.m[6], a->m44f.m[7],
					a->m44f.m[8], a->m44f.m[9], a->m44f.m[10], a->m44f.m[11],
					a->m44f.m[12], a->m44f.m[13], a->m44f.m[14], a->m44f.m[15] );
			break;
		case ACES_ATTR_PREVIEW:
			printf( "preview %u x %u", a->preview.width, a->preview.height );
			break;
		case ACES_ATTR_RATIONAL:
			printf( "rational %d / %u", a->rational.num, a->rational.denom );
			if ( a->rational.denom != 0 )
				printf( " (%g)", (double)( a->rational.num ) / (double)( a->rational.denom ) );
			break;
		case ACES_ATTR_STRING:
			printf( "'%s'", a->string.str ? a->string.str : "<NULL>" );
			break;
		case ACES_ATTR_TILEDESC:
		{
			static const char *lvlModes[] = { "single image", "mipmap", "ripmap" };
			uint8_t lvlMode = a->tiledesc.level_type;
			uint8_t rndMode = a->tiledesc.round_mode;
			printf( "tile %u x %u level %u (%s) round %u (%s)",
					a->tiledesc.x_size, a->tiledesc.y_size,
					lvlMode, lvlMode < 3 ? lvlModes[lvlMode] : "<UNKNOWN>",
					rndMode, rndMode == 0 ? "down" : "up" );
			break;
		}
		case ACES_ATTR_TIMECODE:
			printf( "timecode %u %u", a->timecode.time_and_flags, a->timecode.user_data );
			break;
		case ACES_ATTR_V2I:
			printf( "v2i [ %d, %d ]", a->v2i.x, a->v2i.y );
			break;
		case ACES_ATTR_V2F:
			printf( "v2f [ %g, %g ]", a->v2f.x, a->v2f.y );
			break;
		case ACES_ATTR_V3I:
			printf( "v3i [ %d, %d, %d ]", a->v3i.x, a->v3i.y, a->v3i.z );
			break;
		case ACES_ATTR_V3F:
			printf( "v3f [ %g, %g, %g ]", a->v3f.x, a->v3f.y, a->v3f.z );
			break;
		case ACES_ATTR_USER:
			printf( "'%s' (size %lu)", a->type_name, a->userdata.size );
			break;
		case ACES_ATTR_UNKNOWN:
		default:
			printf( "<ERROR Unknown type '%s'>", a->type_name );
			break;
	}
}

void
aces_print_header( ACES_FILE *f, int verbose )
{
	int c;
	__ACES_INTERNAL_FILE_DATA *fp = ACES_FILE_TO_INTERNAL( f );

	ACES_PRIVATE_FILE *pf = ACES_FILE_TO_PRIVATE( f );

	printf( "File '%s':\n", fp->given_filename );
	printf( " width: %d\n", pf->display_window.x_max - pf->display_window.x_min + 1 );
	printf( " height: %d\n", pf->display_window.y_max - pf->display_window.y_min + 1 );
	printf( " %d channels:", pf->channels.num_channels );
	for ( c = 0; c < pf->channels.num_channels; ++c )
	{
		const char *data_type;
		switch ( pf->channels.entries[c].pixel_type )
		{
			case ACES_PIXEL_UINT: data_type = "uint"; break;
			case ACES_PIXEL_HALF: data_type = "half"; break;
			case ACES_PIXEL_FLOAT: data_type = "float"; break;
			default: data_type = "<ERROR>"; break;
		}
		if ( c > 0 )
			printf( "," );

		printf( " '%s' %s", pf->channels.entries[c].name, data_type );
		if ( pf->channels.entries[c].x_sampling != 1 || pf->channels.entries[c].y_sampling != 1 )
			printf( " (samp %d,%d)", pf->channels.entries[c].x_sampling, pf->channels.entries[c].y_sampling );
	}
	printf( "\n" );

	if ( verbose )
	{
		char tmpbuf[256];
		const char *msg = NULL;
		printf( " storage: %s\n", ( pf->storage_mode == ACES_STORAGE_SCANLINE ) ? "scanline" : "tiled" );

		if ( pf->storage_mode == ACES_STORAGE_TILED )
		{
			static const char *lvlModes[] = { "single image", "mipmap", "ripmap" };
			uint8_t lvlMode = pf->tile_info.level_type;
			uint8_t rndMode = pf->tile_info.round_mode;
			printf( " tile info: %u x %u level %u (%s) round %u (%s)\n",
					pf->tile_info.x_size, pf->tile_info.y_size,
					lvlMode, lvlMode < 3 ? lvlModes[lvlMode] : "<UNKNOWN>",
					rndMode, rndMode == 0 ? "down" : "up" );
		}
		switch ( pf->compression )
		{
			case ACES_COMPRESSION_NONE: msg = "uncompressed"; break;
			case ACES_COMPRESSION_RLE: msg = "rle"; break;
			case ACES_COMPRESSION_ZIPS: msg = "zips"; break;
			case ACES_COMPRESSION_ZIP: msg = "zip"; break;
			case ACES_COMPRESSION_PIZ: msg = "piz"; break;
			case ACES_COMPRESSION_PXR24: msg = "pxr24"; break;
			case ACES_COMPRESSION_B44: msg = "b44"; break;
			case ACES_COMPRESSION_B44A: msg = "b44a"; break;
			default:
				snprintf( tmpbuf, 256, "unknown 0x%02X (%u)", pf->compression, pf->compression );
				msg = tmpbuf;
				break;
		}
		printf( " compression: %s\n", msg );

		printf( " lineorder: %d (%s)\n", pf->line_order,
					pf->line_order == ACES_LINEORDER_INCREASING_Y ? "increasing" :
					( pf->line_order == ACES_LINEORDER_DECREASING_Y ? "decreasing" :
					  ( pf->line_order == ACES_LINEORDER_RANDOM_Y ? "random" : "unknown" ) ) );
		printf( " pixel aspect ratio: %g\n", pf->pixel_aspect_ratio );

		printf( " data window: [%d, %d - %d, %d]\n", pf->data_window.x_min, pf->data_window.y_min, pf->data_window.x_max, pf->data_window.y_max );
		printf( " display window: [%d, %d - %d, %d]\n", pf->display_window.x_min, pf->display_window.y_min, pf->display_window.x_max, pf->display_window.y_max );
		printf( " screen window center: [%g, %g]\n", pf->screen_window_center.x, pf->screen_window_center.y );
		printf( " screen window width: %g\n", pf->screen_window_width );

		if ( pf->user_attributes != NULL )
		{
			aces_user_attribute_list *cur_ua = pf->user_attributes;
			printf( "\n optional/user attributes:\n" );
			while ( cur_ua != NULL )
			{
				printf( "  %s: ", cur_ua->name );
				print_attr( &( cur_ua->data ) );
				printf( "\n" );
				cur_ua = cur_ua->next;
			}
		}
	}
}


/**************************************/


int
aces_read_image( const char *filename, int *w, int *h, int *channels, int *datatype, void **data )
{
	int rval = -1;
	ACES_FILE *e = NULL;

	if ( w == NULL || h == NULL || channels == NULL || datatype == NULL || data == NULL)
	{
		(*default_error_func)( "Missing / NULL pointer argument to aces_read_image" );
		return -1;
	}

	e = aces_start_read( filename );
	if ( e )
	{
		size_t bytes = aces_get_image_bytes( e );
		ACES_PRIVATE_FILE *pe = ACES_FILE_TO_PRIVATE( e );
		*w = pe->display_window.x_max - pe->display_window.x_min + 1;
		*h = pe->display_window.y_max - pe->display_window.y_min + 1;
		*channels = pe->channels.num_channels;
		(*default_error_func)( "Loop over channels to check pixel type nyi" );
		*datatype = pe->channels.entries[0].pixel_type;
		*data = malloc( bytes );

		rval = aces_get_image_rgba( e, *data, *w * *channels * aces_get_data_size( *datatype ) );

		aces_close( e );
	}

	return rval;
}


/**************************************/


int
aces_write_image( const char *filename, int compression, int w, int h, int channels, int datatype, const void *data )
{
	int rval = -1;

	ACES_FILE *e = aces_start_write( w, h, channels, compression, filename );

	if ( e )
	{
		rval = aces_set_image_rgba( e, data, datatype, w * channels * aces_get_data_size( datatype ) );
		aces_close( e );
	}

	return rval;
}


/**************************************/


void
aces_set_default_error_function( aces_error_func errfn )
{
	if ( errfn == NULL )
		default_error_func = &builtin_error_func;
	else
		default_error_func = errfn;
}


/**************************************/


void
aces_register_unpack_function( int comp_type, unpack_plane_blocks_func f_ptr )
{
}


/**************************************/


void
aces_register_pack_function( int comp_type, pack_plane_blocks_func f_ptr )
{
}


/**************************************/


static void
builtin_error_func( const char *message )
{
	if ( message == NULL )
		return;

	fputs( "ERROR: ", stderr );
	fputs( message, stderr );
	fputc( '\n', stderr );
	fflush( stderr );
}

static ssize_t
default_read_func( void *file_info, void *buf, size_t size )
{
	int fd = ((__ACES_INTERNAL_FILE_DATA *)file_info)->fd;
	return read( fd, buf, size );
}

static ssize_t
default_write_func( void *file_info, const void *buf, size_t size )
{
	int fd = ((__ACES_INTERNAL_FILE_DATA *)file_info)->fd;
	return write( fd, buf, size );
}

static off64_t
default_seek_func( void *file_info, off64_t off, int whence )
{
	int fd = ((__ACES_INTERNAL_FILE_DATA *)file_info)->fd;
#ifdef __APPLE__
	return lseek( fd, off, whence );
#else
	return lseek64( fd, off, whence );
#endif
}
