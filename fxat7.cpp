//
// AT FX 8
//	Atari 8-bit protected image file DLL
//  FX 7 read sector module
//
#include "pch.h"
#include <stdio.h>		// Need for below, prov !!!
#include <stdlib.h>
#include <memory.h>

#include "fx7lib.h"

#define EXPORT_DLL
#include "at8fx.h"
#include "at8prot.h"
#include "iat8fx.h"
#include "atxfile.h"

#define UPD_POSITION	1

#define DEBUG_HEADERS	0

#define STS_BDM		0x20
#define STS_RNF		0x10
#define STS_CRC		0x08
#define STS_DLOST		0x04
#define STS_BADHEADER	(STS_RNF | STS_CRC)

/* For internal use */
#define STS_OVERLAP	0x80
#define STS_WEAK		0x40
#define STS_MASK		0x3E


#define REVOL_TIME	208333UL		/* Revolution time in usecs */
#define SECTOR_TIME	(155UL*64)		/* sector+hdr FM read time */



static void xSimulWeak( A8PROTDSK *pDskInfo, unsigned wkStart)
{
	static uchar wkBuf[ 256];

	memcpy( wkBuf, pDskInfo->secPtr, 128);
	for( ; wkStart < 128; wkStart++)
	{
		wkBuf[ wkStart] = (uchar) rand();			// randomize !!!
	}

	pDskInfo->secPtr = wkBuf;
//	myDebug( "Weak sector");
}

//
// FX7 specific
//


//
// ATX specific
//

static void atxBuildSectList( const A8PROTDSK *pDskInfo, VPROT_SECTOR *vSectList)
{
	for( unsigned hdr = 0; hdr < pDskInfo->trackInfo.nheaders; hdr++)
	{
		vSectList[ hdr].sector = pDskInfo->trackInfo.atxHeaderp[ hdr].sector;
		vSectList[ hdr].status = pDskInfo->trackInfo.atxHeaderp[ hdr].status;
		vSectList[ hdr].len = 128;

		vSectList[ hdr].radPos = 
			pDskInfo->trackInfo.atxHeaderp[ hdr].timev * 8;

		vSectList[ hdr].dataDispl = pDskInfo->trackInfo.atxHeaderp[ hdr].data;

		// If big sector, get exact length from (optional!) extra header info
		if( (vSectList[ hdr].status & STS_DLOST) != 0)
		{
			const ATX_SCHUNK *chp;
			chp = atxFindSchunk( pDskInfo->trackInfo.databuf, ATX_CU_EXTHDR, hdr);
			if( chp == NULL)
				vSectList[ hdr].len = 256;		// No real size info on file
			else
				vSectList[ hdr].len = 0x80 << (chp->data & 0x03);
		}
	}
}

static bool atxSimulWeak( A8PROTDSK *pDskInfo, unsigned hdr)
{
	if( (pDskInfo->vProtHdrList[ hdr].status & STS_WEAK) == 0)
		return true;

	const ATX_SCHUNK *p;
	p = atxGetFirstTchunk( pDskInfo->trackInfo.databuf);

	for( ;;)
	{
		// Stop when out of track len !!!
		if( p->len == 0)
			break;
		if( p->type == ATX_CU_WK7 && p->num == hdr)
			goto foundit;

		p = (const ATX_SCHUNK *)( (const uchar *)p + p->len);
	}

//	myDebug( "File format error %d (%u)", 57, hdr);		// enum !!!
	return false;

foundit:
	unsigned displ;
	displ = p->data;

	xSimulWeak( pDskInfo, displ);
	return true;
}

//
//
//

static __inline void xUpdDskPosition( A8PROTDSK *pDskInfo, unsigned micros)
{
	pDskInfo->diskPosition = (pDskInfo->diskPosition + micros ) % REVOL_TIME;
}

static __inline void posSectors( A8PROTDSK *pDskInfo)
{
	atxBuildSectList( pDskInfo, pDskInfo->vProtHdrList);
}

static __inline bool gotoTrack( unsigned track, A8PROTDSK *pDskInfo)
{
	return atxGotoTrack( track, &pDskInfo->disk7Info, &pDskInfo->trackInfo);
}


static __inline bool simulWeak( A8PROTDSK *pDskInfo, unsigned secpos)
{
	return atxSimulWeak( pDskInfo, secpos);
}

//
//
//

// Adjust header position by skew align
static __inline void skewPosition( int dstTrack, int prevTrack, A8PROTDSK *pDskInfo)
{
#ifdef INHOUSE
	static bool bDeSkew = false;

	// De-skew for diagnostic purposes
	if( bDeSkew)
	{
		pDskInfo->diskPosition = ( REVOL_TIME) * (uchar) rand();
		pDskInfo->diskPosition /= 256;

	}
#endif
}

/* Simulate seek delay */
static unsigned simulSeek( unsigned dstTrack, A8PROTDSK *pDskInfo)
{
	int steps;
	ulong seekTime;

	/*
		Seek delays

		20 ms per step	(10 ms for each half step)
			extra step if seeking inwards
		20 ms settle time
	 */

	 if( pDskInfo->curTrack >= ANTRACKS)
	 	return 0;

	 steps = pDskInfo->curTrack - dstTrack;
	 seekTime = abs( steps) * driveTiming.stepTime + driveTiming.settleTime;
	 if( dstTrack > pDskInfo->curTrack)
	 	seekTime += driveTiming.stpbackTime;

#if UPD_POSITION
	xUpdDskPosition( pDskInfo, seekTime);
#endif

	return seekTime;
}

// Compute sector reading time according to hdr len
static unsigned getSectorTime( const A8PROTDSK *pDskInfo, unsigned hdr)
{
	// Bad for DD !!
	if( (pDskInfo->vProtHdrList[ hdr].status & STS_DLOST) != 0)
	{
		// not exact, depends on len, and it not's twice coz header and gap is the same !!!
		return SECTOR_TIME * 2;
	}

	return SECTOR_TIME;
}

// Get next header at current disk position
static unsigned locNxtHeader( const A8PROTDSK *pDskInfo)
{
	unsigned sec;

	/* Find current header */
	for( sec = 0; sec < pDskInfo->trackInfo.nheaders; sec++)
	{
		if( pDskInfo->vProtHdrList[ sec].radPos > pDskInfo->diskPosition)
			break;
	}

	if( sec >= pDskInfo->trackInfo.nheaders)
		sec = 0;

	return sec;
}

static unsigned xFndSector( const A8PROTDSK *pDskInfo,
						  unsigned hdr, unsigned sector, bool bNoRnf)
{
	for( unsigned n = 0; n < pDskInfo->trackInfo.nheaders; n++)
	{
		if( pDskInfo->vProtHdrList[ hdr].sector == sector)
		{
			if( !bNoRnf ||
				( pDskInfo->vProtHdrList[ hdr].status & STS_RNF) == 0)
			return hdr;
		}

		if( ++hdr >= pDskInfo->trackInfo.nheaders)
			hdr = 0;
	}

	return -1;
}

// Locate closest specific sector number
static __inline unsigned fndSector( const A8PROTDSK *pDskInfo, unsigned hdr, unsigned sector)
{
	return xFndSector( pDskInfo, hdr, sector, false);
}

// Locate closest specific sector number that is not RNF
static __inline unsigned fndDataSector( const A8PROTDSK *pDskInfo,
									   unsigned hdr, unsigned sector)
{
	return xFndSector( pDskInfo, hdr, sector, true);
}

static __inline bool haveDataSector( const A8PROTDSK *pDskInfo, unsigned sector)
{
	// Inefficient !!! We should map the sectors in the track when loaded
	return xFndSector( pDskInfo, 0, sector, true) != -1;
}

// Get sector by header. No timing
bool vprotGetHdrSector( unsigned hdr, A8PROTDSK *pDskInfo)
{
	unsigned status;

	if( hdr >= pDskInfo->trackInfo.nheaders)
		return false;

	status = pDskInfo->vProtHdrList[ hdr].status & STS_MASK;
	if( status & STS_DLOST)					// If big sector then DRQ will be set
		status |= 0x02;						// DRQ

	pDskInfo->fdcStatus = status;
	if( (status & STS_RNF) != 0)
	{
		pDskInfo->secPtr = pDskInfo->trackInfo.databuf;
		return false;
	}

	pDskInfo->secPtr =
			pDskInfo->trackInfo.databuf + pDskInfo->vProtHdrList[ hdr].dataDispl;

	// Must be done after setting sectPtr!
	simulWeak( pDskInfo, hdr);
	return true;
}

#if 1
// New code
bool vprotGetSector( int sector, A8PROTDSK *pDskInfo, unsigned *hdrPos, bool bDoRetry)
{
	unsigned retries;
	unsigned hdr;
	unsigned status;
	bool foundit;

	foundit = false;

	// Compute current position
	xUpdDskPosition( pDskInfo, pDskInfo->elapsed);
	pDskInfo->elapsed = 0;

	// All dups are SNF?
	if( !haveDataSector( pDskInfo, sector))
	{
		hdr = fndSector( pDskInfo, locNxtHeader( pDskInfo), sector);
		if( hdr == -1)
		{
			status = STS_RNF;
		}
		else
		{
			// Might be header with CRC
			status = pDskInfo->vProtHdrList[ hdr].status & STS_MASK;
		}

		pDskInfo->elapsed = ( REVOL_TIME) * 2;
		pDskInfo->secPtr = pDskInfo->trackInfo.databuf;

		pDskInfo->fdcStatus = status;
		return false;
	}

	for( retries = 2; retries > 0; retries--)
	{
		unsigned elapsed, sectTime;

		hdr = fndDataSector( pDskInfo, locNxtHeader( pDskInfo), sector);
		status = pDskInfo->vProtHdrList[ hdr].status & STS_MASK;


		// Compute elapsed time until this header

		if( pDskInfo->vProtHdrList[ hdr].radPos > pDskInfo->diskPosition)
			elapsed = pDskInfo->vProtHdrList[ hdr].radPos - pDskInfo->diskPosition;
		else
			elapsed = REVOL_TIME -
				(pDskInfo->diskPosition - pDskInfo->vProtHdrList[ hdr].radPos);

		// Plus sector reading time
		sectTime = getSectorTime( pDskInfo, hdr);
		elapsed += sectTime;

		// Set disk position at end of sector
		pDskInfo->diskPosition =
				(pDskInfo->vProtHdrList[ hdr].radPos + sectTime) % REVOL_TIME;

		pDskInfo->elapsed += elapsed;

		if( status == 0 || !bDoRetry)
			break;

		// It's two half steps (back and forth) + 20 ms. delay
		// 1050 ROM does it only at last retry, but there is only one retry,
		// so it is actually done always. Chk 810 !!!

		if( retries == 2)
		{
			elapsed = driveTiming.stepTime + driveTiming.settleTime;
			xUpdDskPosition( pDskInfo, elapsed);
			pDskInfo->elapsed += elapsed;
		}
	}

	// If big sector then DRQ will be set
	if( status & STS_DLOST)
		status |= 0x02;		// DRQ

	pDskInfo->fdcStatus = status;
	pDskInfo->secPtr =
			pDskInfo->trackInfo.databuf + pDskInfo->vProtHdrList[ hdr].dataDispl;

	// Must be done after setting sectPtr!
	simulWeak( pDskInfo, hdr);

	return status == 0;
}
#endif

static bool fx7PutSector( int sector, A8PROTDSK *pDskInfo)
{
	unsigned sec;
	bool foundit;

	foundit = false;
	for( sec = 0; sec < pDskInfo->trackInfo.nheaders; sec++)
	{
		if( pDskInfo->vProtHdrList[ sec].sector == sector)
		{
			foundit = true;
			break;
		}
	}

	if( !foundit || pDskInfo->vProtHdrList[ sec].status)
	{
		pDskInfo->fdcStatus = STS_RNF;	// might be other !!!
		return false;
	}

	pDskInfo->fdcStatus = 0;

	memcpy( pDskInfo->trackInfo.databuf +
		pDskInfo->vProtHdrList[ sec].dataDispl, pDskInfo->secPtr, 128);
	return true;
}

static bool vprotGotoTrack( A8PROTDSK *pDskInfo, unsigned track, unsigned *seekTime)
{
	if( pDskInfo->curTrack != track)
	{
		if( !gotoTrack( track, pDskInfo))
			return false;

		/* seek time */
		*seekTime += simulSeek( track, pDskInfo);

		posSectors( pDskInfo);
		skewPosition( track, pDskInfo->curTrack, pDskInfo);
		pDskInfo->curTrack = track;
	}

	return true;
}

// Returns true if ok or FDC error is CRC and/or BDM
static bool isChipSoftErr( unsigned status)
{
	// mask out CRC & BDM bits
	return (status & ~(STS_BDM | STS_CRC)) == 0;
}

bool vprotReadSector( unsigned sector, unsigned track, A8PROTDSK *pDskInfo, bool bIgnoreSoftErrs)
{
	unsigned xtraElapsed;

	unsigned secPos;

	/* Check if elapsed not too big for overwrap !!! */
	xtraElapsed = 0;
	if( !vprotGotoTrack( pDskInfo, track, &xtraElapsed))
		return false;

	pDskInfo->complOk = vprotGetSector( sector, pDskInfo, &secPos, !bIgnoreSoftErrs);

	pDskInfo->elapsed += xtraElapsed;
	return true;
}

bool vprotWriteSector( unsigned sector, unsigned track, A8PROTDSK *pDskInfo)
{
	unsigned xtraElapsed;

	xtraElapsed = 0;
	if( !vprotGotoTrack( pDskInfo, track, &xtraElapsed))
		return false;

	pDskInfo->complOk = fx7PutSector( sector, pDskInfo);
//	pDskInfo->elapsed += xtraElapsed;

	return true;
}

// Mode disk position forward in n microseconds.
void vprotUpdDiskPosition( A8PROTDSK *pDskInfo, unsigned micros)
{
	xUpdDskPosition( pDskInfo, micros);
}

#ifdef INHOUSE

bool vProtSeekTrack( A8PROTDSK *pDskInfo, unsigned track)
{
	unsigned xtraElapsed;

	if( pDskInfo->imgType < eATX)
		return false;

	xtraElapsed = 0;
	if( !vprotGotoTrack( pDskInfo, track, &xtraElapsed))
		return false;
	pDskInfo->elapsed += xtraElapsed;
	return true;
}

// Get header list, for debugging and diagnostics
bool vProtGetHeaders( A8PROTDSK *pDskInfo, unsigned track,
					 const VPROT_SECTOR **pVSectList, unsigned *nHeaders)
{
	unsigned xtraElapsed;

	if( pDskInfo->imgType < eATX)
		return false;

	xtraElapsed = 0;
	if( !vprotGotoTrack( pDskInfo, track, &xtraElapsed))
		return false;

	pDskInfo->elapsed += xtraElapsed;
	// xUpdDskPosition( pDskInfo, pDskInfo->elapsed);

	*pVSectList = pDskInfo->vProtHdrList;
	*nHeaders = pDskInfo->trackInfo.nheaders;
	return true;
}
#endif


struct A8PROT_STATE
{
	unsigned curTrack;
	unsigned diskPosition;
};

bool vprotLoadState( A8PROT_STATE *statep, A8PROTDSK *pDskInfo)
{
	pDskInfo->curTrack = statep->curTrack;
	pDskInfo->diskPosition = statep->diskPosition;

	// Must check values are valid
	 if( pDskInfo->curTrack >= ANTRACKS)
		pDskInfo->curTrack  = 0;
	pDskInfo->diskPosition %= REVOL_TIME;

	return true;
}

// Save state
bool vprotSaveState( A8PROT_STATE *statep, const A8PROTDSK *pDskInfo)
{
	// track and header position, that's all
	statep->curTrack = pDskInfo->curTrack;
	statep->diskPosition = pDskInfo->diskPosition;
	return true;
}

bool vprotInitDisk( unsigned imgFlags, A8PROTDSK *pDskInfo)
{
	pDskInfo->diskPosition = 0;

	return true;
}
