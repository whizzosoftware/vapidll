//
// VAPI.DLL
//	external interface definitions
//

#ifndef VAPI_H
#define VAPI_H


#ifdef linux

#ifdef EXPORT_DLL
#define DllExport __attribute__ ((visibility ("default")))
#else
#define DllExport
#endif

#else	// linux

#ifdef EXPORT_DLL
#define DllExport   __declspec( dllexport )
#endif

#ifndef DllExport
#define DllExport __declspec( dllimport )
#endif

#endif	// linux

#ifndef WINVER
typedef void *HWND;
typedef int BOOL;
#endif


#define VAPSIO_MAJOR_VERS	0
#define VAPSIO_MINOR_VERS	3

#define VAPSIO_VERSION		( (VAPSIO_MAJOR_VERS<<8) | VAPSIO_MINOR_VERS)


// Application flags


// Image load/save modes and flags

#define VAPSIO_LDQUERYSIZE	0x00
#define VAPSIO_LDFNAME		0x01
#define VAPSIO_LDMEM		0x02

#define VAPSIO_LDPRELOAD	0x100			// Load whole image file in memory
#define VAPSIO_LDEJLOST		0x200			// Lost all changes on eject

// Error values

#define vapsioErrNoerr		0
#define vapsioErrNotInited	-1
#define vapsioErrGeneric	-2
#define vapsioErrOsErr		-3
#define vapsioErrNoMem		-4
#define vapsioErrFileFmt	-5
#define vapsioErrUnimpl		-6
#define vapsioErrInvalidDrv	-7
#define vapsioErrNoDisk		-8
#define vapsioErrInvParam	-9

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum eTIMEUNITS { eMICROS, eCYCLES, ePCTIMER};
enum eDRIVETYPE { eDRV_1050DD, eDRV_810, eDRV_THECHIP};
enum eSERCOMPENSATE { eNOSERCOMP, ePCUART, eEMUSCANS}; 


struct VAPSIO_FUNCS
{
	BOOL ( *Sio) ( struct VAPSIO_SIOINFO *);

	BOOL ( *Config) ( struct VAPSIO_CONFIGINFO *);
	int  ( *GetLastError) ( void);
	BOOL ( *TurnOnDrive) ( int drive, BOOL bOn);
	BOOL ( *ConfigureDrive)( int drive, unsigned, unsigned);
	BOOL ( *ResetDrives) ( void);
	BOOL ( *PowerOn) ( BOOL bOn);

	BOOL ( *ImgLoad) ( int drive, BOOL wProt, struct VAPSIO_IMGLOADINFO *);
	BOOL ( *Eject) ( int drive);
	int ( *GetFileExtensions) ( char *buf, int bufSize, BOOL bAll);

	BOOL ( *SaveState) ( struct VAPSIO_STATEINFO *);
	BOOL ( *LoadState) ( struct VAPSIO_STATEINFO *);

	BOOL ( *DlgConfig) ( HWND hWnd, unsigned flags, void *);
	BOOL ( *DlgDebugger) ( HWND hWnd);

	BOOL ( *extra) ( unsigned code, unsigned, void *);
};

struct VAPSIO_CALLBACKS
{
	void ( *MotorChg) ( unsigned);

	void ( *LogMsg) ( unsigned level, const char *msg);
	void ( *WarnMsg) ( const char *msg);

	void ( *BreakHit) ( int n);
};


struct VAPSIO_INITINFO
{
	unsigned dwSize;

	unsigned applVersion;
	unsigned applFlags;
	int timeUnits;
	int serCompensateMode;

	// pointer to array of callbacks
	const struct VAPSIO_CALLBACKS *cBacks;


	// Output parameters

	unsigned dllVersion;

	// array of dll public func pointers
	const struct VAPSIO_FUNCS *funcs;
};


struct VAPSIO_IMGLOADINFO
{
	unsigned mode;

	const char *fileName;
	void *buffer;
	long bufSize;
	long fileLen;
};

struct VAPSIO_SIOINFO
{
	// Input parameters
	BOOL bCommand;						// Command line signaled
	unsigned char *outPtr;				// Computer to drive frame buffer pointer
	unsigned char *inPtr;				// Drive to computer frame pointer
	unsigned bufLen;					// Max size of input buffer
	unsigned outFrameSize;				// Computer to drive frame size

	unsigned long currTime;				// Current time (in host defined units)
										// for command frame: at CMND signal negated
										// Otherwise: at end of transmission of last bit

	unsigned pokeyDivisor;
	unsigned inReserved;


	// Result parameters

	unsigned long ackDelay;				// From currTime to start of ACK
	unsigned long compltDelay;			// From currtime to start of C/E

	unsigned long frameXmitTime;		// Frame transmission time incl. C/E and CHKSUM
	unsigned long ackXmitTime;
	unsigned xmitBitRate;

	unsigned neededFrameSize;			// Drive is expecting so many bytes
	unsigned inFrameSize;				// Drive to computer frame size
	unsigned outReserved;
};


// typedef prototype for casting GetProcAddress/dlsym pointer

typedef BOOL VAPSIO_INITPROC( struct VAPSIO_INITINFO *);
typedef VAPSIO_INITPROC *LPVAPSIO_INITPROC;

DllExport BOOL vapsioInit( struct VAPSIO_INITINFO *);

DllExport BOOL vapsioSio( struct VAPSIO_SIOINFO *);

DllExport int vapsioGetLastError( void);
DllExport BOOL vapsioTurnOnDrive( int drive, BOOL bOn);
DllExport BOOL vapsioConfigureDrive( int drive, unsigned, unsigned);
DllExport BOOL vapsioResetDrives( void);
DllExport BOOL vapsioPowerOn( BOOL bOn);


DllExport BOOL vapsioImgLoad( int drive, BOOL wProt, struct VAPSIO_IMGLOADINFO *);
DllExport BOOL vapsioEject( int drive);

DllExport int vapsioGetFileExtensions( char *buf, int bufSize, BOOL bAll);

DllExport BOOL vapsioDlgConfig( HWND hWnd, unsigned flags, void *);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif	/* VAPI_H */
