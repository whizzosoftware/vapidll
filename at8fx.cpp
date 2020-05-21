//
// AT FX 8
//	Atari 8-bit protected image file DLL
//  drive emulator module
//

// We make array access for multiple drive support,
//	but is not very efficient when using a single drive
//	as will happen in SIO2PC programs. !!!
#include "pch.h"
#pragma warning(disable : 4996)

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "osDefs.h"
#include "osUtils.h"

#include "atr.h"
#include "fx7lib.h"

#define EXPORT_DLL
#include "at8fx.h"
#include "at8prot.h"
#include "iat8fx.h"


// In microsecs
#define RESPONSE_DELAY		250		// Fixed delay before sending complete/error
									// on all commands except STATUS

#define RESP_DELAY_RSECT	300		// Specific for read sector, includes overhead

#define CMNDACK_DELAY		250		// From low CMND to start of ACK

#define ACK_XMTTIME			521		// Not exact ! hmm, wrong, should be smaller!!!

// Not exact, depends on track (division), this is for mid case.
// Assumes, of course, motor already on and no need to seek
#define RSECT_WHDELAY		1782	// From low CMND to issue FDC read sect command

// Same as above but from end of ACK
#define POSTACK_DELAY		(RSECT_WHDELAY-ACK_XMTTIME-CMNDACK_DELAY)

// Total sector frame xmit time (130 bytes with 'C' and chksum)
// 71717 measured from FDC not busy to start of stop bit
// 300 for start of C delay, 26 for half stop bit
// change to full stop bit? 52
#define SECT128_XMITTIME	( 71717 - 300 + 26)

#define STATUS_XMITTIME		(3206 + 26)

#define D1050_STEP_TIME		(20*1000)		// 20 ms step time (must measure overhead !!!)
#define D1050_SETTLE_TIME	(20*1000)		// 20 ms settle delay

#define D810_STEP_TIME		5200
#define D810_SETTLE_TIME	10240

//
// PC UART times
// Time is exact.
//

#define PCU_BIT_TIMEF		(1000/19.2)
#define PCU_BYTE_TIMEF		(PCU_BIT_TIMEF*10)
#define PCU_FRAME130_TIMEF		(PCU_BYTE_TIMEF*130)

#define PCU_BIT_TIME		((unsigned)(PCU_BIT_TIMEF))
#define PCU_BYTE_TIME		((unsigned)(PCU_BYTE_TIMEF))
#define PCU_FRAME130_TIME	((unsigned)(PCU_FRAME130_TIMEF))


// Command status bits definitions

#define CMDSTS_CFRAME		0x01	// Invalid command frame
#define CMDSTS_DFRAME		0x02	// data frame error
#define CMDSTS_OPERR		0x04	// Command op. error
#define CMDSTS_WPROT		0x08	// Disk write protected
#define CMDSTS_ACTIVE		0x10	// Motor on
#define CMDSTS_DBLDNS		0x20	// Sector size 256
									// Bit 6 unusued, USD manual say always 1 !!!
#define CMDSTS_MEDDNS		0x80	// Enh density


#define MAXDRIVES 8

enum eDENSITIES { eSINGLE, eMEDIUM, eDOUBLE};


DRV_TIMING driveTiming;


static struct VAPSIO_CALLBACKS applCbacks = { 0};

#if 1
static const char *densityNames[ 3] = {"single", "medium", "double"};
#endif


static struct CONFIG_DATA
{
	eTIMEUNITS timeUnit;
	eSERCOMPENSATE serCompensateMode;
	bool inited;

	unsigned minDelay;
	unsigned minCmptDelay;

	unsigned ackXmitTime;		// Ack xmit time
	unsigned ackAdjust;			// Ack xmit time + delay in client units
	unsigned postAckOv;			// Overhead delay from ack to read sector
	unsigned ackDelay;

	unsigned xmitTime128;		// Total trans. time for sector frame
	unsigned xmitTime4;			// Total trans. time for status frame

	unsigned compens128;		// Compensation for sector size frame
	unsigned compens4;			// Compensation for status size frame

	eDRIVETYPE drvType;

} configData;


// Current command data
static struct CMND_DATA
{
	unsigned drive;
	unsigned cmnd;
	unsigned sectSize;

	unsigned logSector;			// 0 based
	unsigned Track;
	unsigned Sector;

	unsigned neededFrameSize;	// Data frame size for write commands

	int lastError;
} cmndData;

/*
 */

static struct at8fx_DRVDATA
{
	bool imgLoaded;
	bool bIsOn;

	bool wProt;
	bool bDirty;
	bool allocated;				// Using our own allocated buffer

	eDRIVETYPE drvType;

	FILE *fin;					// NULL if preloaded or from memory

	eIMGTYPES imgType;
	eDENSITIES density;

	unsigned prevTime;			// Time mark of at previous disk position

	A8PROTDSK dskInfo;

} drvData[ MAXDRIVES];


// Call logger callback
void myLogMsg( int level, const char *fmt, ...)
{
	va_list args;
	char buf[ 1024];

//	if( level > fdcConfigData.logLevel)
//		return;

	if( applCbacks.LogMsg != NULL)
	{
		va_start( args, fmt);
		vsprintf( buf, fmt, args);
		applCbacks.LogMsg( level, buf);
	}
}

eDRIVETYPE getDrvType( unsigned drive)
{
	// prov ignore drive #
	return configData.drvType;
}

static __inline bool isProt( void)
{
	return drvData[ cmndData.drive].imgType >= eATX;
}


static ulong clientTime2fx7( ulong units)
{
	switch( configData.timeUnit)
	{
	case eCYCLES:
		return (units * 1000) / 1789;
	case eMICROS:
	default:
		break;
	}

	return units;
}

static ulong fx72clientTime( unsigned fx7ticks)
{
	switch( configData.timeUnit)
	{
	case eCYCLES:
		return (fx7ticks * 1789) / 1000;
	case eMICROS:
	default:
		break;
	}
	return fx7ticks;
}

static void setTimeUnits( void)
{
	switch( configData.timeUnit)
	{
	case eCYCLES:
		configData.minDelay = 142;
		break;

	default:
		configData.timeUnit = eMICROS;
	case eMICROS:
		configData.minDelay = 255;
		break;
	}

	configData.ackXmitTime = fx72clientTime( ACK_XMTTIME);
	configData.ackAdjust = fx72clientTime( ACK_XMTTIME);
	configData.postAckOv = fx72clientTime( RSECT_WHDELAY);
	configData.ackDelay = fx72clientTime( CMNDACK_DELAY);

	configData.ackAdjust += configData.ackDelay;
	configData.postAckOv -= configData.ackAdjust;

	configData.minCmptDelay = configData.ackAdjust + configData.minDelay;

	configData.xmitTime128 = fx72clientTime( SECT128_XMITTIME);
	configData.xmitTime4 = fx72clientTime( STATUS_XMITTIME);

	switch( configData.serCompensateMode)
	{
	case ePCUART:
		configData.compens128 =
			fx72clientTime( SECT128_XMITTIME - PCU_FRAME130_TIME);
		break;

	case eEMUSCANS:
		// (114 * 8 * 130) / 1.78979 = 66242.408
		configData.compens128 =
			fx72clientTime( SECT128_XMITTIME - 66242);
		break;

	default:
		configData.serCompensateMode = eNOSERCOMP;
	case eNOSERCOMP:
		configData.compens128 = 0;
		break;
	}
}

//
// Command status bits manipulation
//

static __inline void setCmdStatusBit( unsigned mask)
{
	drvData[ cmndData.drive].dskInfo.cmdStatus |= mask;
}

static __inline void clrCmdStatusBit( unsigned mask)
{
	drvData[ cmndData.drive].dskInfo.cmdStatus &= ~mask;
}

//
// Utility sector and frame routines
//

static void setPhySector( void)
{
	cmndData.Track = cmndData.logSector / drvData[ cmndData.drive].dskInfo.secsPerTrack;
	cmndData.Sector = cmndData.logSector % drvData[ cmndData.drive].dskInfo.secsPerTrack + 1;
}

// Get sector from AUX bytes on command frame
static __inline unsigned getAuxSector( const uchar frame[])
{
	return frame[ 3] << 8 | frame[ 2];
}

// Set logical sector # (1 based)
static __inline void calcLogSector( const uchar *cmndFrame)
{
	// From 1-based to zero based
	cmndData.logSector = getAuxSector( cmndFrame) - 1;
}

// Computes SIO frame chksum
static unsigned calcChksum( const uchar *buf, unsigned len)
{
	unsigned chk = 0;

	while( len-- > 0)
	{
		chk += *buf++;
		if( chk & 0x100)
		{
			chk++;
			chk &= 0xff;
		}
	}

	return chk;
}

static bool chkLogSector( void)
{
	// chk valid range
	if( unsigned (cmndData.logSector) >=
			drvData[ cmndData.drive].dskInfo.nSectors)
		return false;

	return true;
}

static bool setLogSector( const uchar *cmndFrame)
{
	calcLogSector( cmndFrame);
	return chkLogSector();
}

// Return default timing adjustment, when not made specific by caller
static void retDefAdjs( VAPSIO_SIOINFO *pCmndInfo)
{
	pCmndInfo->frameXmitTime = 0;
}

// Set complete delay for host. Use elapsed
void setComplDelay( VAPSIO_SIOINFO *pCmndInfo, bool bXmitTime)
{
	unsigned intDelay;

	// Before translating to client time units!
	drvData[ cmndData.drive].dskInfo.elapsed += RESP_DELAY_RSECT;
	vprotUpdDiskPosition( &drvData[ cmndData.drive].dskInfo, RESP_DELAY_RSECT);

	// Elapsed after
	intDelay = 
		fx72clientTime( drvData[ cmndData.drive].dskInfo.elapsed + configData.postAckOv);

	// Whole delay up to start of Complete
	pCmndInfo->compltDelay = intDelay + configData.ackAdjust;

	// Time at end of FX7 processing
	drvData[ cmndData.drive].prevTime = intDelay + 
		pCmndInfo->currTime + configData.ackAdjust;

	if( bXmitTime)
	{
		if( cmndData.sectSize == 128)
		{
			pCmndInfo->frameXmitTime = configData.xmitTime128;
			pCmndInfo->compltDelay += configData.compens128;
		}
		else
		{
			retDefAdjs( pCmndInfo);
		}
	}
}

//
//
//

static ulong atrCalcPos( void)
{
	ulong displ;

	cmndData.sectSize = 128;
	displ = cmndData.logSector * 128;

	if( drvData[ cmndData.drive].density == eDOUBLE && cmndData.logSector >= 3)
	{
		cmndData.sectSize = 256;
		displ = cmndData.logSector * 256 - 3 * 128;
	}

	return displ + sizeof( ATR_FILEHEADER);

}

static bool atrGetSector( uchar *dstBuf)
{
	FILE *fin;
	ulong displ;

	displ = atrCalcPos();
	fin = drvData[ cmndData.drive].fin;

	if( fin != NULL)
	{
		if( fseek( fin, displ, SEEK_SET))
		{
			cmndData.lastError = vapsioErrOsErr;
			return false;
		}

		if( fread( dstBuf, cmndData.sectSize, 1, fin) != 1)
		{
			cmndData.lastError = vapsioErrOsErr;
			return false;
		}
	}
	else
	{
		memcpy( dstBuf, drvData[ cmndData.drive].dskInfo.buffer + displ,
			cmndData.sectSize);
	}

	return true;
}

static bool atrPutSector( const uchar *srcBuf)
{
	FILE *fin;
	ulong displ;

	displ = atrCalcPos();
	fin = drvData[ cmndData.drive].fin;

	if( fin != NULL)
	{
		if( fseek( fin, displ, SEEK_SET))
		{
			cmndData.lastError = vapsioErrOsErr;
			return false;
		}

		if( fwrite( srcBuf, cmndData.sectSize, 1, fin) != 1)
		{
			cmndData.lastError = vapsioErrOsErr;
			return false;
		}
	}
	else
	{
		memcpy( drvData[ cmndData.drive].dskInfo.buffer + displ, srcBuf,
			cmndData.sectSize);
	}

	return true;
}


// Other commands

__inline void nakFrame( VAPSIO_SIOINFO *pCmndInfo)
{
	pCmndInfo->inPtr[ 0] = 'N';
	pCmndInfo->inFrameSize = 1;
}

static __inline void ackDataFrame( VAPSIO_SIOINFO *pCmndInfo)
{
	pCmndInfo->inPtr[ 0] = 'A';
	pCmndInfo->inFrameSize = 2;	// Ack plus C/E
}

__inline void ackXmitFrame( VAPSIO_SIOINFO *pCmndInfo)
{
	pCmndInfo->inPtr[ 0] = 'A';
}

__inline void chksumXmitFrame( VAPSIO_SIOINFO *pCmndInfo,
							  unsigned frameSize)
{
	pCmndInfo->inPtr[ frameSize + 2] = calcChksum( pCmndInfo->inPtr + 2, frameSize);
	pCmndInfo->inFrameSize = frameSize + 3;		// Ack plus C/E
}


// Read sector command

static bool protRdSector( VAPSIO_SIOINFO *pCmndInfo)
{
	unsigned intDelay;	// was delay from ack to complete

	cmndData.sectSize = 128;

	// Elapsed before

	drvData[ cmndData.drive].dskInfo.elapsed =
		clientTime2fx7( pCmndInfo->currTime - drvData[ cmndData.drive].prevTime +
		+ configData.ackAdjust + configData.postAckOv);

	bool bIgnoreSoftErrs = false;

	if( !vprotReadSector( cmndData.Sector, cmndData.Track, &drvData[ cmndData.drive].dskInfo, bIgnoreSoftErrs))
	{
		// Set error !!!
		return false;
	}

	// Check caller buffer size !!!

	memcpy( pCmndInfo->inPtr + 2, drvData[ cmndData.drive].dskInfo.secPtr,
		cmndData.sectSize);
	setComplByte( pCmndInfo, drvData[ cmndData.drive].dskInfo.complOk ? 'C' : 'E');

	if( drvData[ cmndData.drive].dskInfo.complOk)
		clrCmdStatusBit( CMDSTS_OPERR);
	else
		setCmdStatusBit( CMDSTS_OPERR);

#if 1
	// Before translating to client time units!
	drvData[ cmndData.drive].dskInfo.elapsed += RESP_DELAY_RSECT;
	vprotUpdDiskPosition( &drvData[ cmndData.drive].dskInfo, RESP_DELAY_RSECT);
#endif

	// Elapsed after
	intDelay = 
		fx72clientTime( drvData[ cmndData.drive].dskInfo.elapsed + configData.postAckOv);

	// Whole delay up to start of Complete
	pCmndInfo->compltDelay = intDelay + configData.ackAdjust;

	// Time at end of FX7 processing
	drvData[ cmndData.drive].prevTime = intDelay + 
		pCmndInfo->currTime + configData.ackAdjust;

	if( drvData[ cmndData.drive].dskInfo.fdcStatus)
	{
		myLogMsg( LOG_DEBUG, "D%u: FDC status: %02X", 
			cmndData.drive + 1, drvData[ cmndData.drive].dskInfo.fdcStatus);
	}

	if( cmndData.sectSize == 128)
	{
		pCmndInfo->frameXmitTime = configData.xmitTime128;
		pCmndInfo->compltDelay += configData.compens128;
	}
	else
	{
		retDefAdjs( pCmndInfo);
	}


//	myDebug( "Delay: %u", drvData[ cmndData.drive].dskInfo.elapsed);
	return true;
}

static bool atrRdSector( VAPSIO_SIOINFO *pCmndInfo)
{
	// Check caller buffer size !!!
	if( !atrGetSector( pCmndInfo->inPtr + 2))
		return false;

	drvData[ cmndData.drive].dskInfo.fdcStatus = 0x0;
	complFrame( pCmndInfo);
	clrCmdStatusBit( CMDSTS_OPERR);

	return true;
}

static bool sioRdSector( VAPSIO_SIOINFO *pCmndInfo)
{
	if( !setLogSector( pCmndInfo->outPtr))
	{
		nakFrame( pCmndInfo);
		setCmdStatusBit( CMDSTS_CFRAME);
		myLogMsg( LOG_DEBUG, "D%u: Invalid read sector: %u.",
			cmndData.drive + 1, cmndData.logSector + 1);
		return true;
	}

	myLogMsg( LOG_NORMAL,
		"D%u: Read sector: %u.", cmndData.drive + 1, cmndData.logSector + 1);

	// Clear command and data frame error bits
	clrCmdStatusBit( CMDSTS_CFRAME | CMDSTS_DFRAME);

	// chk disk loaded
	if( !drvData[ cmndData.drive].imgLoaded)
	{
		// Wrong !!!
		// Return Error and send data
		nakFrame( pCmndInfo);
		return true;
	}

	setPhySector();

	if( isProt())
		protRdSector( pCmndInfo);
	else
	{
		if( !atrRdSector( pCmndInfo))
			return false;
	}

	// Compute chksum and store !
	chksumXmitFrame( pCmndInfo, cmndData.sectSize);
	ackXmitFrame( pCmndInfo);

	return true;
}

//
// Write sector
//

// Set expected data frame size to be received
__inline void postPending( VAPSIO_SIOINFO *pCmndInfo,
								 unsigned neededFrameSize)
{
	cmndData.neededFrameSize = pCmndInfo->neededFrameSize = neededFrameSize;
	pCmndInfo->inPtr[ 0] = 'A';
	pCmndInfo->inFrameSize = 1;
}

static void sioWrtSector( VAPSIO_SIOINFO *pCmndInfo)
{
	if( !setLogSector( pCmndInfo->outPtr))
	{
		nakFrame( pCmndInfo);
		setCmdStatusBit( CMDSTS_CFRAME);
		myLogMsg( LOG_DEBUG, "D%u: Invalid write sector: %u.",
			cmndData.drive + 1, cmndData.logSector + 1);
		return;
	}

	myLogMsg( LOG_NORMAL,
		"D%u: Write sector: %u.", cmndData.drive + 1, cmndData.logSector + 1);

	// chk disk loaded
	if( !drvData[ cmndData.drive].imgLoaded)
	{
		// Wrong !!!
		// Receive sector and return error
		nakFrame( pCmndInfo);
		return;
	}

	// Compute sector size
	if( isProt())
		cmndData.sectSize = 128;
	else
		atrCalcPos();

	postPending( pCmndInfo, cmndData.sectSize + 1);	// chksum
}

//
// Process pending write
//

static void cancelAllPending( void)
{
	cmndData.neededFrameSize = 0;
}

static void cancelDrvPending( unsigned drive)
{
	if( drive == cmndData.drive)
		cmndData.neededFrameSize = 0;
}

// Process pending write sector
static bool sioPendWrite( VAPSIO_SIOINFO *pCmndInfo)
{
	bool rc;

	// Check write protect
	if( drvData[ cmndData.drive].wProt || !drvData[ cmndData.drive].imgLoaded ||
		!chkLogSector()) // Just in case !
	{
		return true;
	}

	setPhySector();

	if( isProt())
	{
		drvData[ cmndData.drive].dskInfo.secPtr = pCmndInfo->outPtr;
		rc = vprotWriteSector( cmndData.Sector, cmndData.Track,
			&drvData[ cmndData.drive].dskInfo);
	}

	else
	{
		rc = atrPutSector( pCmndInfo->outPtr);
	}

	if( rc)
	{
		complFrame( pCmndInfo);
		drvData[ cmndData.drive].dskInfo.fdcStatus = 0;
		clrCmdStatusBit( CMDSTS_OPERR);

		drvData[ cmndData.drive].bDirty = true;
	}

	return rc;
}

static bool sioProcPending( VAPSIO_SIOINFO *pCmndInfo)
{
	// Should not happen, but just in case
	if( (unsigned) cmndData.drive >= MAXDRIVES)
	{
		cancelAllPending();
		return true;
	}

	// Should not happen, but just in case
	if( !drvData[ cmndData.drive].bIsOn)
	{
		cancelDrvPending( cmndData.drive);
		return true;
	}

	// Should check time and send NAK if too late !!!

	if( pCmndInfo->outFrameSize < cmndData.neededFrameSize)
	{
		// Should accumulate bytes !!!
		// cmndData.neededFrameSize -= pCmndInfo->outFrameSize;
//		myDebug( "Data frame too short (%u < %u)",
//				pCmndInfo->outFrameSize, cmndData.neededFrameSize);
		cmndData.neededFrameSize = 0;
		return true;
	}

	unsigned sectlen = cmndData.neededFrameSize - 1;

	if( calcChksum( pCmndInfo->outPtr, sectlen) != pCmndInfo->outPtr[ sectlen])
	{
		myLogMsg( LOG_DEBUG, "Data frame with bad chksum");
		cmndData.neededFrameSize = 0;
		nakFrame( pCmndInfo);
		setCmdStatusBit( CMDSTS_DFRAME);
		return true;
	}

	ackDataFrame( pCmndInfo);
	clrCmdStatusBit( CMDSTS_CFRAME | CMDSTS_DFRAME);

	cmndData.neededFrameSize = 0;


	errFrame( pCmndInfo);		// Assume error
	drvData[ cmndData.drive].dskInfo.fdcStatus = 0x40;
	setCmdStatusBit( CMDSTS_OPERR);

	if( cmndData.cmnd == 'W' || cmndData.cmnd == 'P')
		return sioPendWrite( pCmndInfo);

	return true;
}

//
// SIO Command processing
//

// Add pal parameter to structure !!! ???
BOOL vapsioSio( struct VAPSIO_SIOINFO *pCmndInfo)
{
	uchar device;
	BOOL rc;

	pCmndInfo->ackDelay = configData.ackDelay;		// Actually slightly variable !!!
	pCmndInfo->compltDelay = configData.minCmptDelay;

	pCmndInfo->xmitBitRate = 19200;
	pCmndInfo->ackXmitTime = configData.ackXmitTime;

	pCmndInfo->frameXmitTime = 0;
	pCmndInfo->neededFrameSize = 0;
	pCmndInfo->inFrameSize = 0;			// Assume timeout

	if( cmndData.neededFrameSize)
		return sioProcPending( pCmndInfo);

	// Ignore non command frames
	if( !pCmndInfo->bCommand)
	{
//		myDebug( "Spurious data frame");
		return TRUE;
	}

	// Check frame size not too short
	// Extra bytes are ignored by the drive!
	if( pCmndInfo->outFrameSize < 5)
	{
//		myDebug( "Command frame too short (%u bytes)", pCmndInfo->outFrameSize);
		return TRUE;
	}

	device = pCmndInfo->outPtr[ 0];
	// check cmnd frame chksum
	if( calcChksum( pCmndInfo->outPtr, 4) != pCmndInfo->outPtr[ 4])
	{
		myLogMsg( LOG_DEBUG, "Command frame with bad chksum");
		return TRUE;
	}

	// Check valid device & drive #
	if( (device & 0xf0) != 0x30)
	{
		myLogMsg( LOG_DEBUG, "Invalid device: %02X", device);
		return TRUE;
	}

	cmndData.drive = (device & 0x0f) - 1;
	if( (unsigned) cmndData.drive >= MAXDRIVES)
	{
		myLogMsg( LOG_DEBUG, "Invalid drive: %02X", device);
		return TRUE;
	}

	// check drive is turned on
	if( !drvData[ cmndData.drive].bIsOn)
		return TRUE;

	cmndData.cmnd = pCmndInfo->outPtr[ 1];
	rc = TRUE;

	switch( cmndData.cmnd)
	{
	case 'R':
		if( !sioRdSector( pCmndInfo))
			rc = FALSE;
		break;

	case 'S':

		myLogMsg( LOG_DEBUG, "D%u: Read status", cmndData.drive + 1);

		// store status frame
		pCmndInfo->inPtr[ 2+0] = drvData[ cmndData.drive].dskInfo.cmdStatus;
		pCmndInfo->inPtr[ 2+1] = 
			~(drvData[ cmndData.drive].dskInfo.fdcStatus |
			drvData[ cmndData.drive].dskInfo.drvStatus);
		pCmndInfo->inPtr[ 2+2] = 224;
		pCmndInfo->inPtr[ 2+3] = 0;

		// Compute and store chksum, ack and C
		chksumXmitFrame( pCmndInfo, 4);
		ackXmitFrame( pCmndInfo);
		complFrame( pCmndInfo);

		// 1050 performs double reset of FDC, send frame, read FDC status
		// bit 5 of FDC is set in some drives!
		drvData[ cmndData.drive].dskInfo.fdcStatus =
			drvData[ cmndData.drive].wProt ? 0x60 : 0x20;

		// Clear all error bit
		clrCmdStatusBit( CMDSTS_CFRAME | CMDSTS_DFRAME | CMDSTS_OPERR);


		break;

	case 'P':
	case 'W':

		sioWrtSector( pCmndInfo);
		break;

	case 0x21:
	case 0x22:

	// prov !!!
	default:
		myLogMsg( LOG_DEBUG,
			"Invalid/unimplemented command %02X", pCmndInfo->outPtr[ 1]);

		nakFrame( pCmndInfo);
		setCmdStatusBit( CMDSTS_CFRAME);

		return TRUE;
	}

	return rc;
}

//
// File management
//

static void drvReset( int drive)
{
	cancelDrvPending( drive);

	// Retain previous values if image loaded
	if( !drvData[ drive].imgLoaded)
	{
		drvData[ drive].dskInfo.drvStatus = 0x80;		// Door open
		drvData[ drive].dskInfo.cmdStatus = 0;
		drvData[ drive].dskInfo.fdcStatus = 0x0;

		drvData[ drive].dskInfo.nSectors = 720;
		drvData[ drive].dskInfo.secsPerTrack = 18;
		drvData[ drive].density = eSINGLE;
	}

	drvData[ drive].dskInfo.curTrack = ANTRACKS;
	drvData[ drive].dskInfo.diskPosition = 0;
	drvData[ drive].prevTime = 0;
}

static void resetAllDrives( void)
{
	for( int drive = 0; drive < MAXDRIVES; drive++)
	{
		drvReset( drive);
	}
}

static void xFreeImg( int drive)
{
	if( drvData[ drive].allocated)
	{
		myFree( drvData[ drive].dskInfo.buffer);
		drvData[ drive].allocated = false;
	}
}

static void xEject( int drive)
{
	if( drvData[ drive].imgLoaded)
	{
		// Chk auto save !!!
		if( drvData[ drive].fin != NULL)
		{
			fclose( drvData[ drive].fin);
			drvData[ drive].fin = NULL;
		}

		xFreeImg( drive);
	}
	else
	{
		// Issue error msg if not NULL or allocated !!!
		drvData[ drive].fin = NULL;
	}

	drvData[ drive].bDirty = false;
	drvData[ drive].imgLoaded = false;
	drvData[ drive].dskInfo.drvStatus = 0x80;		// Door open
}

static const char *getFnameExt( const char *fname)
{
	const char *ext;
	const char *slash;

	if( (ext = strrchr( fname, '.')) == NULL)
		return NULL;
	slash = strrchr( fname, '\\');
	if( slash != NULL && slash > ext)
		return NULL;

	return ext + 1;
}

static eIMGTYPES getFileType( const char *fname)
{
	const char *ext;

	ext = getFnameExt( fname);

	if( ext == NULL)
		return eUNKNOWN;

	if( !_stricmp( ext, "atr"))
		return  eATR;

	if( !_stricmp( ext, "atx"))
		return eATX;

	return eUNKNOWN;
}

static bool imgBufLoad( int drive, VAPSIO_IMGLOADINFO *pImgInfo)
{
	if( pImgInfo->buffer == NULL)
	{
		// This call set lastError
		if( !loadAllocFile( pImgInfo->fileName, &pImgInfo->buffer, &pImgInfo->fileLen))
			return false;
		drvData[ drive].allocated = true;
	}
	else
	{
		if( !loadFile( pImgInfo->fileName, pImgInfo->buffer,
			pImgInfo->bufSize, &pImgInfo->fileLen))
		{
			cmndData.lastError = vapsioErrOsErr;
			return false;
		}
	}

	return true;
}

static __inline FILE *myFopen( const char *fname, bool rw)
{
	return fopen( fname, rw ? "r+b" : "rb");
}

static void protInsert( int drive, bool wProt)
{
	drvData[ drive].dskInfo.nSectors = 720;
	drvData[ drive].dskInfo.secsPerTrack = 18;

	// Single dens.
	drvData[ drive].dskInfo.cmdStatus &= 0x1f;

	drvData[ drive].dskInfo.curTrack = ANTRACKS;

	cmndData.lastError = vapsioErrNoerr;

	vprotInitDisk( drvData[ drive].dskInfo.disk7Info.imgFlags, &drvData[ drive].dskInfo);

	// Add disk density !!!
	myLogMsg( LOG_NORMAL, "D%u: Protected disk loaded (%s)",
		cmndData.drive + 1, wProt ? "ro" : "rw");
}

static bool atxLoad( int drive, bool wProt, VAPSIO_IMGLOADINFO *pImgInfo)
{
	// Only write mode supported
	if( !wProt)
		pImgInfo->mode |= VAPSIO_LDPRELOAD | VAPSIO_LDEJLOST;

	if( (pImgInfo->mode & 0xff) == VAPSIO_LDFNAME)
	{
		if( !imgBufLoad( drive, pImgInfo))
			return false;
	}

	if( !atxFetchImage( &drvData[ drive].dskInfo.disk7Info,
			pImgInfo->buffer, pImgInfo->fileLen))
	{
		// Could be bad version !!!
		cmndData.lastError = vapsioErrFileFmt;
		xFreeImg( drive);
		return false;
	}

	drvData[ drive].dskInfo.buffer = (uchar *) pImgInfo->buffer;
	protInsert( drive, wProt);
	drvData[ drive].fin = NULL;

	return true;
}

static bool atrInsert( int drive, const ATR_FILEHEADER *atrHeaderp, ulong imgLen)
{
	unsigned sectSize, imgSize;

	if( imgLen < sizeof( ATR_FILEHEADER))
		return false;

	imgSize = (*(const ushort *) &atrHeaderp->paraLow) * 16;
	sectSize = *(const ushort *) &atrHeaderp->sectSizeLow;

	if( imgLen != imgSize + sizeof( ATR_FILEHEADER))
		return false;

	// Mask off density bits
	drvData[ drive].dskInfo.cmdStatus &= 0x1f;

	switch( imgSize)
	{
	case ATR_SINGLESIZE:
		if( sectSize != 128)
			return false;

		drvData[ drive].dskInfo.nSectors = 720;
		drvData[ drive].dskInfo.secsPerTrack = 18;

		drvData[ drive].density = eSINGLE;
		break;

	case ATR_MEDIUMSIZE:
		if( sectSize != 128)
			return false;


		drvData[ drive].dskInfo.nSectors = 1040;
		drvData[ drive].dskInfo.secsPerTrack = 26;

		drvData[ drive].density = eMEDIUM;
		drvData[ drive].dskInfo.cmdStatus |= CMDSTS_MEDDNS;
		break;

	case ATR_DOUBLESIZE:
		if( sectSize != 256)
			return false;

		drvData[ drive].dskInfo.nSectors = 720;
		drvData[ drive].dskInfo.secsPerTrack = 18;

		drvData[ drive].density = eDOUBLE;
		drvData[ drive].dskInfo.cmdStatus |= CMDSTS_DBLDNS;
		break;
		// Add for other double dens. standars !!!

	default:
			return false;
	}

	myLogMsg( LOG_NORMAL, "D%u: %s density disk loaded", drive + 1,
		densityNames[ drvData[ drive].density] );

	return true;
}

static bool atrOpen( int drive, bool wProt, VAPSIO_IMGLOADINFO *pImgInfo)
{
	ATR_FILEHEADER AtrHeader;
	FILE *fin;

	// Problem if write enabled and file is read only !!!
	if( (fin = myFopen( pImgInfo->fileName, !wProt)) == NULL)
	{
		cmndData.lastError = vapsioErrOsErr;
		return false;
	}

	pImgInfo->fileLen = getFileLen( fin);

	// Read ATR header
	if( fread( &AtrHeader, sizeof( ATR_FILEHEADER), 1, fin) != 1)
	{
		cmndData.lastError = vapsioErrOsErr;
		fclose( fin);
		return false;
	}

	cmndData.lastError = vapsioErrFileFmt;
	if( !atrInsert( drive, &AtrHeader, pImgInfo->fileLen))
	{
		fclose( fin);
		return false;
	}

	drvData[ drive].dskInfo.buffer = NULL;
	drvData[ drive].fin = fin;
	return true;
}

static bool atrLoad( int drive, bool wProt, VAPSIO_IMGLOADINFO *pImgInfo)
{
	const ATR_FILEHEADER *atrHeaderp;

	if( (pImgInfo->mode & 0xff) == VAPSIO_LDFNAME)
	{
		if( pImgInfo->mode & VAPSIO_LDPRELOAD)
		{
			if( !imgBufLoad( drive, pImgInfo))
				return false;
		}

		else
		{
			return atrOpen( drive, wProt, pImgInfo);
		}
	}

	drvData[ drive].dskInfo.buffer = (uchar *) pImgInfo->buffer;
	atrHeaderp = (const ATR_FILEHEADER *) pImgInfo->buffer;

	cmndData.lastError = vapsioErrFileFmt;
	if( !atrInsert( drive, atrHeaderp, pImgInfo->fileLen))
		goto errFree;

	return true;

errFree:
	// Must free buffer if allocated
	xFreeImg( drive);
	return false;
}

//
// Public interface
//

// Load image file

BOOL vapsioImgLoad( int drive, BOOL wProt, VAPSIO_IMGLOADINFO *pImgInfo)
{
	eIMGTYPES imgType;

	if( (unsigned) drive >= MAXDRIVES)
	{
		cmndData.lastError = vapsioErrInvalidDrv;
		return FALSE;
	}

	// Eject
	xEject( drive);

	pImgInfo->mode &= ~0xff;
	pImgInfo->mode |= VAPSIO_LDFNAME;

	switch( pImgInfo->mode & 0xff)
	{
	case VAPSIO_LDFNAME:
		if( pImgInfo->fileName == NULL)
		{
			cmndData.lastError = vapsioErrInvParam;
			return FALSE;
		}

		// Change mode if won't be able to open r/w
		if( !wProt && (pImgInfo->mode & VAPSIO_LDEJLOST) == 0)
		{
			if( !isFileRw( pImgInfo->fileName))
			{
				// Should be configurable !!!
				pImgInfo->mode |= VAPSIO_LDEJLOST;
			}
		}

		// If write enable and lost changes on eject, force whole file load
		if( !wProt && (pImgInfo->mode & VAPSIO_LDEJLOST))
			pImgInfo->mode |= VAPSIO_LDPRELOAD;

		break;

	case VAPSIO_LDMEM:
		if( pImgInfo->buffer == NULL)
		{
			cmndData.lastError = vapsioErrInvParam;
			return FALSE;
		}
		pImgInfo->mode |= (VAPSIO_LDPRELOAD | VAPSIO_LDEJLOST);
		break;

	default:
		cmndData.lastError = vapsioErrInvParam;
		return FALSE;
	}

	imgType = getFileType( pImgInfo->fileName);

	switch( imgType)
	{
	case eATR:
		if( !atrLoad( drive, (wProt != 0), pImgInfo))
			return FALSE;
		break;
	case eATX:
		if( !atxLoad( drive, (wProt != 0), pImgInfo))
			return FALSE;
		break;
	default:
		cmndData.lastError = vapsioErrInvParam;
		return FALSE;
	}

	// Door closed, write protected
	drvData[ drive].dskInfo.drvStatus = 0x0;
	if( wProt)
		drvData[ drive].dskInfo.cmdStatus |= CMDSTS_WPROT;

	drvData[ drive].dskInfo.fdcStatus = 0x0;

	// Redundant !!!
	drvData[ drive].dskInfo.imgType =
	drvData[ drive].imgType = imgType;

	drvData[ drive].wProt = (wProt != 0);
	drvData[ drive].imgLoaded = true;

	cmndData.lastError = vapsioErrNoerr;
	return TRUE;
}

int vapsioGetFileExtensions( char *buf, int bufSize, BOOL bAll)
{
	if( bufSize < 24)
		return 0;

	if( bAll)
		strcpy( buf, "*.atr;*.atx");
	else
		strcpy( buf, "*.atx");

	return bAll ? 2 : 1;
}

BOOL vapsioEject( int drive)
{
	// Set error !!!
	if( (unsigned) drive >= MAXDRIVES)
		return FALSE;

	xEject( drive);
	return TRUE;
}

BOOL vapsioTurnOnDrive( int drive, BOOL bOn)
{
	// Set error !!!
	if( (unsigned) drive >= MAXDRIVES)
		return FALSE;

	if( bOn && !drvData[ drive].bIsOn)
		drvReset( drive);

	drvData[ drive].bIsOn = ( bOn != FALSE);
	return TRUE;
}

BOOL vapsioResetDrives( void)
{
	cancelAllPending();
	resetAllDrives();
	return TRUE;
}

// Turn on/off computer
BOOL vapsioPowerOn( BOOL bOn)
{
	// Implement !!!
	// Ignore commands while computer off !!!
	// Reset if in the middle of command frame
	return TRUE;
}

// For internal use
void setLastError( int err)
{
	cmndData.lastError = err;
}

int vapsioGetLastError( void)
{
	return cmndData.lastError;
}


// Unimplemented stubs

BOOL vapsioConfig( struct VAPSIO_CONFIGINFO *)
	{ return FALSE; }
BOOL vapsioConfigureDrive( int drive, unsigned, unsigned)
	{ return FALSE; }
BOOL vapsioSaveState( struct VAPSIO_STATEINFO *)
	{ return FALSE; }
BOOL vapsioLoadState( struct VAPSIO_STATEINFO *)
	{ return FALSE; }
BOOL vapsioDlgDebugger( HWND hWnd)
	{ return FALSE; }
BOOL vapSioextra( unsigned code, unsigned, void *)
	{ return FALSE; }


//
// Initialization
//

static struct VAPSIO_FUNCS vapsioFuncs =
{
	&vapsioSio,

	&vapsioConfig,
	&vapsioGetLastError,
	&vapsioTurnOnDrive,
	&vapsioConfigureDrive,
	&vapsioResetDrives,
	&vapsioPowerOn,

	&vapsioImgLoad,
	&vapsioEject,
	&vapsioGetFileExtensions,

	&vapsioSaveState,
	&vapsioLoadState,

	&vapsioDlgConfig,
	&vapsioDlgDebugger,

	&vapSioextra,

};


static void drvTimeInit( void)
{
	switch( configData.drvType)
	{
	case eDRV_1050DD:
		driveTiming.stpbackTime =
		driveTiming.stepTime =		D1050_STEP_TIME;
		driveTiming.settleTime =	D1050_SETTLE_TIME;
		break;
	case eDRV_810:
	default:
		driveTiming.stepTime =		D810_STEP_TIME;
		driveTiming.stpbackTime =	0;
		driveTiming.settleTime =	D810_SETTLE_TIME;
	}
}

bool setDrvType( eDRIVETYPE drvType, unsigned drive)
{
	switch( drvType)
	{
	case eDRV_1050DD:
	case eDRV_810:
		break;
	default:
		return false;
	}

	// prov ignore drive # !!!
	configData.drvType = drvType;
	drvTimeInit();
	return true;
}

BOOL vapsioInit( struct VAPSIO_INITINFO *pInitInfo)
{
	if( pInitInfo->dwSize != sizeof( VAPSIO_INITINFO))
		return FALSE;	// prov !!!
	if( pInitInfo->applVersion != VAPSIO_VERSION)
		return FALSE;	// prov !!!

	configData.inited = true;

	// Check range !!!
	configData.serCompensateMode = (eSERCOMPENSATE) pInitInfo->serCompensateMode;
	configData.timeUnit = (eTIMEUNITS) pInitInfo->timeUnits;

	pInitInfo->dllVersion = VAPSIO_VERSION;
	setTimeUnits();

	if( pInitInfo->cBacks != NULL)
		applCbacks = *pInitInfo->cBacks;

	pInitInfo->funcs = &vapsioFuncs;

	return TRUE;
}

//
// Initialization
//

bool simulInit( void)
{
	int i;

	cmndData.neededFrameSize = 0;

	for( i = 0; i < MAXDRIVES; i++)
	{
		drvData[ i].imgLoaded = false;
		drvData[ i].bIsOn = false;
		drvData[ i].bDirty = false;
		drvData[ i].allocated = false;

		drvData[ i].drvType = eDRV_1050DD;
		drvData[ i].fin = NULL;

		drvReset( i);
	}

	drvData[ 0].bIsOn = true;		// Only D1: on by default

	cmndData.lastError = vapsioErrNoerr;

	configData.serCompensateMode = eNOSERCOMP;
	configData.timeUnit = eMICROS;
	configData.inited = false;
	configData.drvType = eDRV_1050DD;

	setTimeUnits();
	drvTimeInit();

	return true;
}

