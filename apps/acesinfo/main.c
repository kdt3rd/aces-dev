/*
 * Copyright (c) 2009 Kimball Thurston
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

#include <aces.h>

#include <stdio.h>
#include <string.h>

static void
usage( const char *argv0 )
{
	fprintf( stderr, "Usage: %s [-v] <filename>\n\n", argv0 );
}

int
main( int argc, const char *argv[] )
{
	ACES_FILE *e = NULL;
	const char *filename = NULL;
	int verbose = 0;

	if ( argc == 2 )
	{
		if ( ! strcmp( argv[1], "-h" ) ||
			 ! strcmp( argv[1], "-?" ) ||
			 ! strcmp( argv[1], "--help" ) )
		{
			usage( argv[0] );
			return 0;
		}

		if ( argv[1][0] == '-' )
		{
			usage( argv[0] );
			return 1;
		}
		filename = argv[1];
	}
	else if ( argc == 3 )
	{
		filename = argv[2];
		if ( ! strcmp( argv[1], "-v" ) )
			verbose = 1;
		else
		{
			usage( argv[0] );
			return 1;
		}
	}
	else
	{
		usage( argv[0] );
		return 1;
	}

	e = aces_start_read( filename );

	if ( e )
	{
		aces_print_header( e, verbose );

		aces_close( e );
		e = NULL;
	}

	return 0;
}
