//
// VAPI.DLL
//
//	OS utils
//
#include "pch.h"
#pragma warning(disable : 4996)

#ifdef linux

#include <stdlib.h>

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#endif

#include <stdio.h>

#include "osUtils.h"

#include "fx7lib.h"
#include "at8fx.h"
#include "at8prot.h"
#include "iat8fx.h"


//
// OS depends
//

#ifdef linux

#include <stdlib.h>

// inline them !!!

void *myMalloc( ulong n)				{ return malloc( n); }
void *myRealloc( void *ptr, ulong n)	{ return realloc( ptr, n);	}
void myFree( void *ptr)					{ return free( ptr);	}

#else

void *myMalloc( ulong n)
{
	return HeapAlloc( GetProcessHeap(), 0, n);
}

void *myRealloc( void *ptr, ulong n)
{
	return HeapReAlloc( GetProcessHeap(), 0, ptr, n);
}

void myFree( void *ptr)
{
	HeapFree( GetProcessHeap(), 0, ptr);
}

#endif

//
// OS utils
//

long getFileLen( FILE *f)
{
	long l;

	fseek( f, 0, SEEK_END);
	l = ftell( f);
	rewind( f);		// Hmm, should return to previous pos !!!

	return l;
}

bool loadFile( const char *fname, void *buffer, unsigned bufSize, long *flen)
{
	FILE *fin;

	if( (fin = fopen( fname, "rb")) == NULL)
		return false;

	*flen = fread( buffer, 1, bufSize, fin);

	// prov !!! needs extra byte on buffer like this
	if( (unsigned) *flen >= bufSize)
	{
		fclose( fin);
		return false;
	}

	fclose( fin);

	return true;
}

bool loadAllocFile( const char *fname, void **ppbuf, long *flen)
{
	FILE *fin;
	void *buffer;
	long n;

	if( (fin = fopen( fname, "rb")) == NULL)
	{
		setLastError( vapsioErrOsErr);
		return false;
	}

	n = getFileLen( fin);

	if( (buffer = myMalloc( n)) == NULL)
	{
		setLastError( vapsioErrNoMem);
		fclose( fin);
		return false;
	}

	if( fread( buffer, n, 1, fin) != 1)
	{
		setLastError( vapsioErrOsErr);
		fclose( fin);
		myFree( buffer);
		return false;
	}

	*flen = n;
	*ppbuf = buffer;

	fclose( fin);
	return true;
}
