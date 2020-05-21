/*
 * ATX file structure
 */

// Not portable ??? !!!

#pragma once

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;


#define ATX_VERSION		0x01

// Creator

#define ATX_CR_FX7		0x01	// Conversion
#define ATX_CR_FX8		0x02	// Conversion
#define ATX_CR_ATR		0x03	// Conversion
#define ATX_CR_WH2PC	0x10


// Record types

#define ATX_RT_TRACK	0x00

#define ATX_RT_HOSTDATA	0x100		// Private

// Track flags

#define ATX_TF_NOSKEW	0x100		// Couldn't measure skew align

// In track chunk types

#define ATX_CU_DATA		0x00		// Data
#define ATX_CU_HDRLST	0x01		// Header list
#define ATX_CU_WK7		0x10		// FX7 style weak
#define ATX_CU_EXTHDR	0x11		// Extended sector header


/* Pack !!! avoid compiler dependency */
struct ATX_FILEHEADER
{
	uchar signature[ 4];
	ushort version;
	ushort minVersion;

	ushort creator;
	ushort creatVersion;
	unsigned long flags;
	ushort imgType;
	ushort reserved0;

	unsigned long imageId;
	unsigned short imageVersion;
	unsigned short reserved1;

	unsigned long startData;
	unsigned long endData;

	unsigned char reserved2[ 12];
/*	ushort rpm;	*/					/* Put RPM here !!! */
};


struct ATX_RECHEADER
{
	ulong next;
	ushort type;
	ushort pad0;
};

struct ATX_TRACKHEADER
{
	ulong next;
	ushort type;
	ushort pad0;

	uchar track;
	uchar pad1;
	ushort nheaders;
	ushort rate;
	ushort pad3;

	ulong flags;
	ulong startCdata;

	uchar reserved[ 8];
};


// Time is start of header in 8 microsecs. (at nominal speed) units

struct ATX_SECTHEADER
{
	uchar sector;
	uchar status;
	ushort timev;
	ulong data;
	// ulong other;
};

// Extra sector information chunk

struct ATX_SCHUNK
{
	unsigned len;
	uchar type;
	uchar num;
	ushort data;
};


//
// Prov here
//

ATX_TRACKHEADER *atxInitTrkHeader( ATX_TRACKHEADER *pTrkHdr, size_t len);

bool atxCreateFile( FILE *fout, ATX_FILEHEADER *pFheader,
				   unsigned creator, unsigned creatVersion);
bool atxCloseFile( FILE *fout, ATX_FILEHEADER *pFheader);

bool atxWriteTrack( FILE *fout,
				   ATX_TRACKHEADER *pTrkHdr, const ATX_SECTHEADER *hdrList,
				   const uchar *dataBuf, unsigned dataLen);

bool atxAddCommentRecord( FILE *fout, ATX_FILEHEADER *pFheader,
	unsigned type, unsigned len, const void *buf);
ATX_RECHEADER *atxFndFirstCommentRecord( const void *fileBuf, ulong fileSz);

ATX_SCHUNK *atxGetFirstTchunk( const void *trackBuf);
const ATX_SCHUNK *atxFindSchunk( const void *trackBuf, unsigned type, unsigned hdr);
