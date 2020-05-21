//
// ATR include
//

//
// ATR header
//	16 (0x10 bytes)
//
//	0: 96	Signature
//	1: 02	Signature
//	2:		Paragraphs low byte (doesn't include header)
//	3:		Paragraphs high byte
//	4:		Sector size, low byte
//	5:		Sector size, high byte
//	6:		Extended paragraphs (???)
//

typedef unsigned char uchar;

struct ATR_FILEHEADER
{
	uchar signature[ 2];		// 0x96 0x02

	uchar paraLow;				// Paragraphs (doesn't include header)
	uchar paraHigh;

	uchar sectSizeLow;
	uchar sectSizeHigh;

	uchar extra[ 10];
};

#define ATR_SINGLESIZE	(720 * 128)
#define ATR_MEDIUMSIZE	(1040 * 128)
#define ATR_DOUBLESIZE	(3 * 128 + (720-3) * 256)		// Not standard !!!

#if 0

static const unsigned doubleSize = 3 * 128 + (720-3) * 256;
static const unsigned singleSize = 720 * 128;
static const unsigned mediumSize = 1040 * 128;
static const int DirectorySector = 0x169;

static const unsigned atrHdrSize = 16;

static enum diskDensity { single, medium, ddouble};

#endif
