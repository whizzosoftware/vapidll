//
// FX A7
//	Atari 8-bit protected image file (FX7) DLL
//  Protected (generic) include
//


enum eIMGTYPES { eUNKNOWN, eATR, eATX, eFX7};

//
// Protected disk base (all formats) dynamic descriptor
//

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;

struct VPROT_SECTOR
{
	uchar sector;
	uchar status;
	ushort len;
	unsigned radPos;
	unsigned dataDispl;
};


// Move many members of this struct, not restricted to prot images !!!
struct A8PROTDSK
{
	uchar *buffer;
	uchar *secPtr;

	unsigned nSectors;
	unsigned secsPerTrack;

	unsigned curTrack;
	unsigned diskPosition;

	unsigned elapsed;

	bool complOk;		// Command completed ok

	uchar drvStatus;
	uchar cmdStatus;

	uchar fdcStatus;

	eIMGTYPES imgType;

	// Static track descriptor
	TRACKINFO trackInfo;
	DISK7INFO disk7Info;

	VPROT_SECTOR vProtHdrList[ MAXHEADERS];
};

bool vprotInitDisk( unsigned imgFlags, A8PROTDSK *pDskInfo);
bool vprotReadSector( unsigned sector, unsigned track, A8PROTDSK *pDskInfo, bool bIgnoreSoftErrs);
bool vprotWriteSector( unsigned sector, unsigned track, A8PROTDSK *pDskInfo);
void vprotUpdDiskPosition( A8PROTDSK *pDskInfo, unsigned micros);
