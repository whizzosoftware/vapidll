//
// OS depend
//

#include <stdio.h>

void *myMalloc( unsigned long n);
void *myRealloc( void *ptr, unsigned long n);
void myFree( void *ptr);

long getFileLen( FILE *f);
bool loadFile( const char *fname, void *buffer, unsigned bufSize, long *flen);
bool loadAllocFile( const char *fname, void **ppbuf, long *flen);

#ifdef linux

#include <unistd.h>

__inline bool isFileRw( const char *fname)
{
	return access( fname, (R_OK|W_OK)) == 0;
}

#else

#include <io.h>

__inline bool isFileRw( const char *fname)
{
	return _access( fname, 6) == 0;
}

#endif
