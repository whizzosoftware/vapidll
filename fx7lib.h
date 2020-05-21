/*
 * FX 7 file management library
 *		include file
 */

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;

#define ANTRACKS	40
#define MAXHEADERS	50

/*
 * Internal structures
 */

struct ATX_SECTHEADER;

typedef struct
{
	unsigned nheaders;
	unsigned ndatasectors;			/* sectors with data (ok or with crc) */
	int track;
	uchar *databuf;

	ushort flags;
	bool unprotected;
	bool hasDups;

	ATX_SECTHEADER *atxHeaderp;

} TRACKINFO;

struct DISK7INFO
{
	FILE *fin;
	uchar *imgBuf;			// When fully preloaded

	ushort imgFlags;
	ushort pad0;

	ulong trackSeekList[ ANTRACKS];
};

bool atxFetchImage( DISK7INFO *diskInfop, const void *buf, ulong imgSize);

bool atxGotoTrack( unsigned track, DISK7INFO *diskInfop, TRACKINFO *trkInfop);
