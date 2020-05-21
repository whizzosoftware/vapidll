//
// VAPI
//	Atari 8-bit protected image file (FX7) DLL
//  Internal main include
//

#include <stdio.h>

#ifdef _DEBUG
#define INHOUSE
#endif

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;

#define FALSE 0
#define TRUE 1

#define EXPORT_DLL


#define LOG_NORMAL		1
#define LOG_DEBUG		10
#define LOG_ALL			100

struct DRV_TIMING
{
	// In microsecs
	unsigned stepTime;
	unsigned stpbackTime;
	unsigned settleTime;
};

extern DRV_TIMING driveTiming;


#ifdef INHOUSE
void myDebug( const char *msg, ...);
#else
#define myDebug
#endif

void myLogMsg( int level, const char *fmt, ...);
void setLastError( int err);

bool simulInit( void);


eDRIVETYPE getDrvType( unsigned drive);
bool setDrvType( eDRIVETYPE drvType, unsigned drive);



// Drive modules interface

static __inline void setComplByte( VAPSIO_SIOINFO *pCmndInfo, uchar complByte)
{
	pCmndInfo->inPtr[ 1] = complByte;
}
static __inline void complFrame( VAPSIO_SIOINFO *pCmndInfo)
{
	pCmndInfo->inPtr[ 1] = 'C';
}
static __inline void errFrame( VAPSIO_SIOINFO *pCmndInfo)
{
	pCmndInfo->inPtr[ 1] = 'E';
}

// Ack & complete command w/o xfer
static __inline void okVoidCmnd( VAPSIO_SIOINFO *pCmndInfo)
{
	pCmndInfo->inPtr[ 0] = 'A';
	pCmndInfo->inPtr[ 1] = 'C';
	pCmndInfo->inFrameSize = 2;	// Ack plus C/E
}


void postPending( VAPSIO_SIOINFO *pCmndInfo,
								 unsigned neededFrameSize);
void nakFrame( VAPSIO_SIOINFO *pCmndInfo);
void ackXmitFrame( VAPSIO_SIOINFO *pCmndInfo);
void chksumXmitFrame( VAPSIO_SIOINFO *pCmndInfo,
							  unsigned frameSize);
void setComplDelay( VAPSIO_SIOINFO *pCmndInfo, bool bXmitTime);


extern bool vProtSeekTrack( A8PROTDSK *pDskInfo, unsigned track);
extern bool vProtGetHeaders( A8PROTDSK *pDskInfo, unsigned track,
					 const VPROT_SECTOR **pVSectList, unsigned *nHeaders);
extern bool vprotGetSector( int sector, A8PROTDSK *pDskInfo, unsigned *hdrPos, bool bDoRetry);
extern bool vprotGetHdrSector( unsigned hdr, A8PROTDSK *pDskInfo);
