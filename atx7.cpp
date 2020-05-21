//
// ATX
// FX7 style interface
//
#include "pch.h"
#include <stdio.h>
#include <memory.h>

#include "fx7lib.h"

#include "atxfile.h"

// prov !!!
static void xioerr( const char *fmt, ...)
{
}
static void xerr( const char *fmt, ...)
{
}

static bool atxGetTrackInfo( const ATX_TRACKHEADER *tfHeaderp, TRACKINFO *trkInfop)
{
	trkInfop->track		= tfHeaderp->track;
	trkInfop->nheaders	= tfHeaderp->nheaders;
	trkInfop->flags		= (ushort) tfHeaderp->flags;

	trkInfop->ndatasectors = 0;

	/* Now check values are valid and consistent */

	if(	trkInfop->track >= ANTRACKS ||
		trkInfop->nheaders > MAXHEADERS
		)
	{
		xerr( "Image file format error %d\n", 1);	/* Must number these !!! */
		return false;
	}

	return true;
}

static bool atxFetchTrack( const void *trackBuf, TRACKINFO *trkInfop)
{
	uchar *tp;

	tp = (uchar *) trackBuf;

	trkInfop->databuf = tp;

	if( !atxGetTrackInfo( (const ATX_TRACKHEADER *) tp, trkInfop))
		return false;
	tp += sizeof( ATX_TRACKHEADER);

	// Skip header list chunk header
	tp += sizeof( ATX_SCHUNK);
	trkInfop->atxHeaderp = (ATX_SECTHEADER *) tp;
	return true;
}


bool atxGotoTrack( unsigned track, DISK7INFO *diskInfop, TRACKINFO *trkInfop)
{
	if( track >= ANTRACKS)
		return false;

	if( diskInfop->fin == NULL)
	{
		if( diskInfop->imgBuf == NULL)
			return false;

		if( !atxFetchTrack( diskInfop->imgBuf +
				diskInfop->trackSeekList[ track], trkInfop))
			return false;

		if( (unsigned) trkInfop->track != track)
		{
			xerr( "Image track mismatch\n");
			return false;
		}

		return true;
	}

	// Unbuffered not supported !!!
	return false;
}

static bool atxChkFheader( const ATX_FILEHEADER *pFheader)
{
	if(	pFheader->signature[0] != 'A' ||
		pFheader->signature[1] != 'T' ||
		pFheader->signature[2] != '8' ||
		pFheader->signature[3] != 'X' ||
		pFheader->version != ATX_VERSION ||
		pFheader->minVersion != ATX_VERSION
		)
	{
		xerr( "Incompatible file type or version\n");
		return false;
	}

	return true;
}

// Init track mem. displacement array
static bool buildTrackList( DISK7INFO *diskInfop, ulong *pdispl, ulong imgSize)
{
	const ATX_TRACKHEADER *tfHeaderp;
	ulong displ;
	unsigned track;

	displ = *pdispl;
	const uchar *pbuf = diskInfop->imgBuf;

	for( track = 0; track < ANTRACKS; track++)
	{
		diskInfop->trackSeekList[ track] = displ;
		tfHeaderp = (const ATX_TRACKHEADER *)( (const uchar *) pbuf + displ);
		displ += tfHeaderp->next;

		if( displ > imgSize)
			return false;
	}
	*pdispl = displ;

	return true;
}


// Fetch whole image from memory
bool atxFetchImage( DISK7INFO *diskInfop, const void *buf, ulong imgSize)
{
	const ATX_FILEHEADER *fheaderp;
	ulong displ;

	fheaderp = (ATX_FILEHEADER *) buf;

	diskInfop->fin = NULL;
	if( !atxChkFheader( fheaderp))
		return false;

	diskInfop->imgBuf = (uchar *) buf;
	diskInfop->imgFlags = 0;

	if( imgSize < sizeof( ATX_FILEHEADER) + sizeof( ATX_TRACKHEADER))
		return false;

	displ = sizeof( ATX_FILEHEADER);
	if( !buildTrackList( diskInfop, &displ, imgSize))
		return false;

	return true;
}
