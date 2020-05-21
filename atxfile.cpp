//
// ATX file management library
//
#include "pch.h"
#include <stdio.h>
#include <memory.h>

#include "atxfile.h"

static __inline void memClear( void *buffer, size_t bufSize)
{
	memset( buffer, 0, bufSize);
}


//
// Structure level routines
//

ATX_TRACKHEADER *atxInitTrkHeader( ATX_TRACKHEADER *pTrkHdr, size_t len)
{
	memClear( pTrkHdr, sizeof( ATX_TRACKHEADER));

	pTrkHdr->type = ATX_RT_TRACK;
	pTrkHdr->next = len + sizeof( ATX_TRACKHEADER);

	pTrkHdr->startCdata = sizeof( ATX_TRACKHEADER);

	return pTrkHdr;
}

void atxCloseTrkHeader( ATX_TRACKHEADER *pTrkHdr, size_t len)
{
	pTrkHdr->next = len + sizeof( ATX_TRACKHEADER);
}


// Return pointer to first chunk for fully buffered track
ATX_SCHUNK *atxGetFirstTchunk( const void *trackBuf)
{
	const uchar *tp;

	tp = (const uchar *) trackBuf;
	tp += sizeof( ATX_TRACKHEADER);

	return (ATX_SCHUNK *) tp;
}

// Find specific extended sector information chunk

const ATX_SCHUNK *atxFindSchunk( const void *trackBuf, unsigned type, unsigned hdr)
{
	const ATX_TRACKHEADER *pTrkHdr;
	const ATX_SCHUNK *p;

	pTrkHdr = (const ATX_TRACKHEADER *) trackBuf;
	p = atxGetFirstTchunk( trackBuf);

	while( (const uchar *)p < (const uchar *)trackBuf + pTrkHdr->next)
	{
		if( p->len == 0)
			break;
		if( p->type == type && p->num == hdr)
		{
			return p;
		}

		p = (const ATX_SCHUNK *)( (const uchar *)p + p->len);
	}

	return NULL;
}

ATX_RECHEADER *atxFndFirstCommentRecord( const void *fileBuf, ulong fileSz)
{
	const ATX_FILEHEADER *pFheader;
	ATX_RECHEADER *rp;

	pFheader = (const ATX_FILEHEADER *) fileBuf;

	if( fileSz < pFheader->endData + sizeof( ATX_RECHEADER))
		return NULL;
	rp = (ATX_RECHEADER *) ((const uchar *) fileBuf + pFheader->endData);

	return rp;
}

//
// File level routines
//

// prov !!!
static void xioerr( const char *fmt, ...)
{
}
static void xerr( const char *fmt, ...)
{
}

static bool atxFileWrite( FILE *fout, const void *buf, size_t len)
{
	if( fwrite( buf, 1, len, fout) != len)
	{
		xioerr( "File write error");
		return false;
	}
	return true;
}

static bool atxFileRead( FILE *fin, void *buf, size_t len)
{
	if( fread( buf, 1, len, fin) != len)
	{
		xioerr( "File read error");
		return false;
	}
	return true;
}

bool atxWriteTrack( FILE *fout,
				   ATX_TRACKHEADER *pTrkHdr, const ATX_SECTHEADER *hdrList,
				   const uchar *dataBuf, unsigned dataLen)
{
	ATX_SCHUNK Chunk;

	memClear( &Chunk, sizeof( ATX_SCHUNK));

	// Word align
	dataLen++;
	dataLen &= ~0x01;

	pTrkHdr->next = sizeof( ATX_TRACKHEADER) +
		sizeof( ATX_SCHUNK) * 2 +
		sizeof( ATX_SECTHEADER) * pTrkHdr->nheaders +
		dataLen;

	if( !atxFileWrite( fout, pTrkHdr, sizeof( ATX_TRACKHEADER)) )
		return false;

	Chunk.len = sizeof( ATX_SECTHEADER) * pTrkHdr->nheaders + sizeof( ATX_SCHUNK);
	Chunk.type = ATX_CU_HDRLST;

	if( !atxFileWrite( fout, &Chunk, sizeof( ATX_SCHUNK)))
		return false;

	if( !atxFileWrite( fout, hdrList, sizeof( ATX_SECTHEADER) * pTrkHdr->nheaders))
		return false;

	if( !atxFileWrite( fout, dataBuf, dataLen))
		return false;

	// EOT mark
	Chunk.len = 0;
	if( !atxFileWrite( fout, &Chunk, sizeof( ATX_SCHUNK)))
		return false;

	return true;
}

bool atxCreateFile( FILE *fout, ATX_FILEHEADER *pFheader,
				   unsigned creator, unsigned creatVersion)
{
	memClear( pFheader, sizeof( ATX_FILEHEADER));

	pFheader->signature[0] = 'A';
	pFheader->signature[1] = 'T';
	pFheader->signature[2] = '8';
	pFheader->signature[3] = 'X';

	pFheader->version = pFheader->minVersion = ATX_VERSION;

	pFheader->creator = creator;
	pFheader->creatVersion = creatVersion;

	pFheader->startData = sizeof( ATX_FILEHEADER );
	pFheader->endData = 0;

	return atxFileWrite( fout, pFheader, sizeof( ATX_FILEHEADER ));
}

bool atxCloseFile( FILE *fout, ATX_FILEHEADER *pFheader)
{
	if( pFheader->endData == 0)
		pFheader->endData = ftell( fout);
	rewind( fout);
	if( !atxFileWrite( fout, pFheader, sizeof( ATX_FILEHEADER)) )
		return false;
	return fclose( fout) == 0;
}

bool atxAddCommentRecord( FILE *fout, ATX_FILEHEADER *pFheader,
	unsigned type, unsigned len, const void *buf)

{
	ATX_RECHEADER recHdr;
	ATX_RECHEADER *pRecHdr = &recHdr;

	if( pFheader->endData == 0)
		pFheader->endData = ftell( fout);

	memClear( pRecHdr, sizeof( ATX_RECHEADER));

	pRecHdr->type = type;
	pRecHdr->next = len + sizeof( ATX_RECHEADER);

	if( !atxFileWrite( fout, pRecHdr, sizeof( ATX_RECHEADER)))
		return false;

	if( !atxFileWrite( fout, buf, len))
		return false;

	return true;
}
