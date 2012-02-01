// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#ifdef _WIN32
#include "../common/des.h"
#endif
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "grfio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#ifndef _WIN32
/// DES (Data Encryption Standard) algorithm, modified version.
/// @see http://www.eathena.ws/board/index.php?autocom=bugtracker&showbug=5099.
/// @see http://en.wikipedia.org/wiki/Data_Encryption_Standard
/// @see http://en.wikipedia.org/wiki/DES_supplementary_material


/// Bitmask for accessing individual bits of a byte.
static const uint8_t mask[8] = {
	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};


/// Initial permutation (IP).
static void IP(BIT64* src)
{
	BIT64 tmp = {{0}};

	static const uint8_t ip_table[64] = {
		58, 50, 42, 34, 26, 18, 10,  2,
		60, 52, 44, 36, 28, 20, 12,  4,
		62, 54, 46, 38, 30, 22, 14,  6,
		64, 56, 48, 40, 32, 24, 16,  8,
		57, 49, 41, 33, 25, 17,  9,  1,
		59, 51, 43, 35, 27, 19, 11,  3,
		61, 53, 45, 37, 29, 21, 13,  5,
		63, 55, 47, 39, 31, 23, 15,  7,
	};

	size_t i;
	for( i = 0; i < ARRAYLENGTH(ip_table); ++i )
	{
		uint8_t j = ip_table[i] - 1;
		if( src->b[(j >> 3) & 7] &  mask[j & 7] )
			tmp .b[(i >> 3) & 7] |= mask[i & 7];
	}

	*src = tmp;
}


/// Final permutation (IP^-1).
static void FP(BIT64* src)
{
	BIT64 tmp = {{0}};

	static const uint8_t fp_table[64] = {
		40,  8, 48, 16, 56, 24, 64, 32,
		39,  7, 47, 15, 55, 23, 63, 31,
		38,  6, 46, 14, 54, 22, 62, 30,
		37,  5, 45, 13, 53, 21, 61, 29,
		36,  4, 44, 12, 52, 20, 60, 28,
		35,  3, 43, 11, 51, 19, 59, 27,
		34,  2, 42, 10, 50, 18, 58, 26,
		33,  1, 41,  9, 49, 17, 57, 25,
	};

	size_t i;
	for( i = 0; i < ARRAYLENGTH(fp_table); ++i )
	{
		uint8_t j = fp_table[i] - 1;
		if( src->b[(j >> 3) & 7] &  mask[j & 7] )
			tmp .b[(i >> 3) & 7] |= mask[i & 7];
	}

	*src = tmp;
}


/// Expansion (E).
/// Expands upper four 8-bits (32b) into eight 6-bits (48b).
static void E(BIT64* src)
{
	BIT64 tmp = {{0}};

if( false )
{// original
	static const uint8_t expand_table[48] = {
		32,  1,  2,  3,  4,  5,
		 4,  5,  6,  7,  8,  9,
		 8,  9, 10, 11, 12, 13,
		12, 13, 14, 15, 16, 17,
		16, 17, 18, 19, 20, 21,
		20, 21, 22, 23, 24, 25,
		24, 25, 26, 27, 28, 29,
		28, 29, 30, 31, 32,  1,
	};

	size_t i;
	for( i = 0; i < ARRAYLENGTH(expand_table); ++i )
	{
		uint8_t j = expand_table[i] - 1;
		if( src->b[j / 8 + 4] &  mask[j % 8] )
			tmp .b[i / 6 + 0] |= mask[i % 6];
	}
}
else
{// optimized
	tmp.b[0] = ((src->b[7]<<5) | (src->b[4]>>3)) & 0x3f;	// ..0 vutsr
	tmp.b[1] = ((src->b[4]<<1) | (src->b[5]>>7)) & 0x3f;	// ..srqpo n
	tmp.b[2] = ((src->b[4]<<5) | (src->b[5]>>3)) & 0x3f;	// ..o nmlkj
	tmp.b[3] = ((src->b[5]<<1) | (src->b[6]>>7)) & 0x3f;	// ..kjihg f
	tmp.b[4] = ((src->b[5]<<5) | (src->b[6]>>3)) & 0x3f;	// ..g fedcb
	tmp.b[5] = ((src->b[6]<<1) | (src->b[7]>>7)) & 0x3f;	// ..cba98 7
	tmp.b[6] = ((src->b[6]<<5) | (src->b[7]>>3)) & 0x3f;	// ..8 76543
	tmp.b[7] = ((src->b[7]<<1) | (src->b[4]>>7)) & 0x3f;	// ..43210 v
}

	*src = tmp;
}


/// Transposition (P-BOX).
static void TP(BIT64* src)
{
	BIT64 tmp = {{0}};

	static const uint8_t tp_table[32] = {
		16,  7, 20, 21,
		29, 12, 28, 17,
		 1, 15, 23, 26,
		 5, 18, 31, 10,
		 2,  8, 24, 14,
		32, 27,  3,  9,
		19, 13, 30,  6,
		22, 11,  4, 25,
	};

	size_t i;
	for( i = 0; i < ARRAYLENGTH(tp_table); ++i )
	{
		uint8_t j = tp_table[i] - 1;
		if( src->b[(j >> 3) + 0] &  mask[j & 7] )
			tmp .b[(i >> 3) + 4] |= mask[i & 7];
	}

	*src = tmp;
}


/// Substitution boxes (S-boxes).
/// NOTE: This implementation was optimized to process two nibbles in one step (twice as fast).
static void SBOX(BIT64* src)
{
	BIT64 tmp = {{0}};

	static const uint8_t s_table[4][64] = {
		  {
			0xef, 0x03, 0x41, 0xfd, 0xd8, 0x74, 0x1e, 0x47,  0x26, 0xef, 0xfb, 0x22, 0xb3, 0xd8, 0x84, 0x1e,
			0x39, 0xac, 0xa7, 0x60, 0x62, 0xc1, 0xcd, 0xba,  0x5c, 0x96, 0x90, 0x59, 0x05, 0x3b, 0x7a, 0x85,
			0x40, 0xfd, 0x1e, 0xc8, 0xe7, 0x8a, 0x8b, 0x21,  0xda, 0x43, 0x64, 0x9f, 0x2d, 0x14, 0xb1, 0x72,
			0xf5, 0x5b, 0xc8, 0xb6, 0x9c, 0x37, 0x76, 0xec,  0x39, 0xa0, 0xa3, 0x05, 0x52, 0x6e, 0x0f, 0xd9,
		},{
			0xa7, 0xdd, 0x0d, 0x78, 0x9e, 0x0b, 0xe3, 0x95,  0x60, 0x36, 0x36, 0x4f, 0xf9, 0x60, 0x5a, 0xa3,
			0x11, 0x24, 0xd2, 0x87, 0xc8, 0x52, 0x75, 0xec,  0xbb, 0xc1, 0x4c, 0xba, 0x24, 0xfe, 0x8f, 0x19,
			0xda, 0x13, 0x66, 0xaf, 0x49, 0xd0, 0x90, 0x06,  0x8c, 0x6a, 0xfb, 0x91, 0x37, 0x8d, 0x0d, 0x78,
			0xbf, 0x49, 0x11, 0xf4, 0x23, 0xe5, 0xce, 0x3b,  0x55, 0xbc, 0xa2, 0x57, 0xe8, 0x22, 0x74, 0xce,
		},{
			0x2c, 0xea, 0xc1, 0xbf, 0x4a, 0x24, 0x1f, 0xc2,  0x79, 0x47, 0xa2, 0x7c, 0xb6, 0xd9, 0x68, 0x15,
			0x80, 0x56, 0x5d, 0x01, 0x33, 0xfd, 0xf4, 0xae,  0xde, 0x30, 0x07, 0x9b, 0xe5, 0x83, 0x9b, 0x68,
			0x49, 0xb4, 0x2e, 0x83, 0x1f, 0xc2, 0xb5, 0x7c,  0xa2, 0x19, 0xd8, 0xe5, 0x7c, 0x2f, 0x83, 0xda,
			0xf7, 0x6b, 0x90, 0xfe, 0xc4, 0x01, 0x5a, 0x97,  0x61, 0xa6, 0x3d, 0x40, 0x0b, 0x58, 0xe6, 0x3d,
		},{
			0x4d, 0xd1, 0xb2, 0x0f, 0x28, 0xbd, 0xe4, 0x78,  0xf6, 0x4a, 0x0f, 0x93, 0x8b, 0x17, 0xd1, 0xa4,
			0x3a, 0xec, 0xc9, 0x35, 0x93, 0x56, 0x7e, 0xcb,  0x55, 0x20, 0xa0, 0xfe, 0x6c, 0x89, 0x17, 0x62,
			0x17, 0x62, 0x4b, 0xb1, 0xb4, 0xde, 0xd1, 0x87,  0xc9, 0x14, 0x3c, 0x4a, 0x7e, 0xa8, 0xe2, 0x7d,
			0xa0, 0x9f, 0xf6, 0x5c, 0x6a, 0x09, 0x8d, 0xf0,  0x0f, 0xe3, 0x53, 0x25, 0x95, 0x36, 0x28, 0xcb,
		}
	};

	size_t i;
	for( i = 0; i < ARRAYLENGTH(s_table); ++i )
	{
		tmp.b[i] = (s_table[i][src->b[i*2+0]] & 0xf0)
		         | (s_table[i][src->b[i*2+1]] & 0x0f);
	}

	*src = tmp;
}


/// DES round function.
/// XORs src[0..3] with TP(SBOX(E(src[4..7]))).
static void RoundFunction(BIT64* src)
{
	BIT64 tmp = *src;
	E(&tmp);
	SBOX(&tmp);
	TP(&tmp);

	src->b[0] ^= tmp.b[4];
	src->b[1] ^= tmp.b[5];
	src->b[2] ^= tmp.b[6];
	src->b[3] ^= tmp.b[7];
}


void des_decrypt_block(BIT64* block)
{
	IP(block);
	RoundFunction(block);
	FP(block);
}


void des_decrypt(unsigned char* data, size_t size)
{
	BIT64* p = (BIT64*)data;
	size_t i;

	for( i = 0; i*8 < size; i += 8 )
		des_decrypt_block(p);
}
#endif

//----------------------------
//	file entry table struct
//----------------------------
typedef struct _FILELIST {
	int		srclen;				// compressed size
	int		srclen_aligned;
	int		declen;				// original size
	int		srcpos;				// position of entry in grf
	int		next;				// index of next filelist entry with same hash (-1: end of entry chain)
	char	type;
	char	fn[128-4*5];		// file name
	char*	fnd;				// if the file was cloned, contains name of original file
	char	gentry;				// read grf file select
} FILELIST;

#define FILELIST_TYPE_FILE           0x01 // entry is a file
#define FILELIST_TYPE_ENCRYPT_HEADER 0x04 // encryption mode 1 (header DES only)
#define FILELIST_TYPE_ENCRYPT_MIXED  0x02 // encryption mode 0 (header DES + periodic DES/shuffle)

//gentry ... > 0  : data read from a grf file (gentry_table[gentry-1])
//gentry ... 0    : data read from a local file (data directory)
//gentry ... < 0  : entry "-(gentry)" is marked for a local file check
//                  - if local file exists, gentry will be set to 0 (thus using the local file)
//                  - if local file doesn't exist, sign is inverted (thus using the original file inside a grf)
//                  (NOTE: this case is only used once (during startup) and only if GRFIO_LOCAL is enabled)
//                  (NOTE: probably meant to be used to override grf contents by files in the data directory)
//#define GRFIO_LOCAL


// stores info about every loaded file
FILELIST* filelist		= NULL;
int filelist_entrys		= 0;
int filelist_maxentry	= 0;

// stores grf file names
char** gentry_table		= NULL;
int gentry_entrys		= 0;
int gentry_maxentry		= 0;

// the path to the data directory
char data_dir[1024] = "";


// little endian char array to uint conversion
static unsigned int getlong(unsigned char* p)
{
	return (p[0] << 0 | p[1] << 8 | p[2] << 16 | p[3] << 24);
}


static void NibbleSwap(unsigned char* src, int len)
{
	while( len > 0 )
	{
		*src = (*src >> 4) | (*src << 4);
		++src;
		--len;
	}
}


/// Substitutes some specific values for others, leaves rest intact. Obfuscation.
/// NOTE: Operation is symmetric (calling it twice gives back the original input).
static uint8_t grf_substitution(uint8_t in)
{
	uint8_t out;
	
	switch( in )
	{
	case 0x00: out = 0x2B; break;
	case 0x2B: out = 0x00; break;
	case 0x6C: out = 0x80; break;
	case 0x01: out = 0x68; break;
	case 0x68: out = 0x01; break;
	case 0x48: out = 0x77; break;
	case 0x60: out = 0xFF; break;
	case 0x77: out = 0x48; break;
	case 0xB9: out = 0xC0; break;
	case 0xC0: out = 0xB9; break;
	case 0xFE: out = 0xEB; break;
	case 0xEB: out = 0xFE; break;
	case 0x80: out = 0x6C; break;
	case 0xFF: out = 0x60; break;
	default:   out = in;   break;
	}

	return out;
}


static void grf_shuffle_enc(BIT64* src)
{
	BIT64 out;

	out.b[0] = src->b[3];
	out.b[1] = src->b[4];
	out.b[2] = src->b[5];
	out.b[3] = src->b[0];
	out.b[4] = src->b[1];
	out.b[5] = src->b[6];
	out.b[6] = src->b[2];
	out.b[7] = grf_substitution(src->b[7]);

	*src = out;
}


static void grf_shuffle_dec(BIT64* src)
{
	BIT64 out;

	out.b[0] = src->b[3];
	out.b[1] = src->b[4];
	out.b[2] = src->b[6];
	out.b[3] = src->b[0];
	out.b[4] = src->b[1];
	out.b[5] = src->b[2];
	out.b[6] = src->b[5];
	out.b[7] = grf_substitution(src->b[7]);

	*src = out;
}


static void grf_decode_header(unsigned char* buf, size_t len)
{
	BIT64* p = (BIT64*)buf;
	size_t nblocks = len / sizeof(BIT64);
	size_t i;

	// first 20 blocks are all des-encrypted
	for( i = 0; i < 20 && i < nblocks; ++i )
		des_decrypt_block(&p[i]);

	// the rest is plaintext, done.
}


static void grf_decode_full(unsigned char* buf, size_t len, int cycle)
{
	BIT64* p = (BIT64*)buf;
	size_t nblocks = len / sizeof(BIT64);
	int dcycle, scycle;
	size_t i, j;

	// first 20 blocks are all des-encrypted
	for( i = 0; i < 20 && i < nblocks; ++i )
		des_decrypt_block(&p[i]);

	// after that only one of every 'dcycle' blocks is des-encrypted
	dcycle = cycle;

	// and one of every 'scycle' plaintext blocks is shuffled (starting from the 0th but skipping the 0th)
	scycle = 7;

	// so decrypt/de-shuffle periodically
	j = -1; // 0, adjusted to fit the ++j step
	for( i = 20; i < nblocks; ++i )
	{
		if( i % dcycle == 0 )
		{// decrypt block
			des_decrypt_block(&p[i]);
			continue;
		}

		++j;
		if( j % scycle == 0 && j != 0 )
		{// de-shuffle block
			grf_shuffle_dec(&p[i]);
			continue;
		}

		// plaintext, do nothing.
	}
}


/// Decodes grf data.
/// @param buf data to decode (in-place)
/// @param len length of the data
/// @param entry_type flags associated with the data
/// @param entry_len true (unaligned) length of the data
static void grf_decode(unsigned char* buf, size_t len, char entry_type, int entry_len)
{
	if( entry_type & FILELIST_TYPE_ENCRYPT_MIXED )
	{// fully encrypted
		int digits;
		int cycle;
		int i;

		// compute number of digits of the entry length
		digits = 1;
		for( i = 10; i <= entry_len; i *= 10 )
			++digits;

		// choose size of gap between two encrypted blocks
		// digits:  0  1  2  3  4  5  6  7  8  9 ...
		//  cycle:  1  1  1  4  5 14 15 22 23 24 ...
		cycle = ( digits < 3 ) ? 1
		      : ( digits < 5 ) ? digits + 1
		      : ( digits < 7 ) ? digits + 9
		      :                  digits + 15;

		grf_decode_full(buf, len, cycle);
	}
	else
	if( entry_type & FILELIST_TYPE_ENCRYPT_HEADER )
	{// header encrypted
		grf_decode_header(buf, len);
	}
	else
	{// plaintext
		;
	}
}


/******************************************************
 ***                Zlib Subroutines                ***
 ******************************************************/

/// zlib crc32
unsigned long grfio_crc32(const unsigned char* buf, unsigned int len)
{
	return crc32(crc32(0L, Z_NULL, 0), buf, len);
}


/// zlib uncompress
int decode_zip(void* dest, unsigned long* destLen, const void* source, unsigned long sourceLen)
{
	return uncompress((Bytef*)dest, destLen, (const Bytef*)source, sourceLen);
}


/// zlib compress
int encode_zip(void* dest, unsigned long* destLen, const void* source, unsigned long sourceLen)
{
	return compress((Bytef*)dest, destLen, (const Bytef*)source, sourceLen);
}


/***********************************************************
 ***                File List Subroutines                ***
 ***********************************************************/
// file list hash table
int filelist_hash[256];

// initializes the table that holds the first elements of all hash chains
static void hashinit(void)
{
	int i;
	for (i = 0; i < 256; i++)
		filelist_hash[i] = -1;
}

// hashes a filename string into a number from {0..255}
static int filehash(const char* fname)
{
	unsigned int hash = 0;
	while(*fname) {
		hash = (hash<<1) + (hash>>7)*9 + TOLOWER(*fname);
		fname++;
	}
	return hash & 255;
}

// finds a FILELIST entry with the specified file name
static FILELIST* filelist_find(const char* fname)
{
	int hash, index;

	if (!filelist)
		return NULL;

	hash = filelist_hash[filehash(fname)];
	for (index = hash; index != -1; index = filelist[index].next)
		if(!strcmpi(filelist[index].fn, fname))
			break;

	return (index >= 0) ? &filelist[index] : NULL;
}

// returns the original file name
char* grfio_find_file(const char* fname)
{
	FILELIST *filelist = filelist_find(fname);
	if (!filelist) return NULL;
	return (!filelist->fnd ? filelist->fn : filelist->fnd);
}

// adds a FILELIST entry into the list of loaded files
static FILELIST* filelist_add(FILELIST* entry)
{
	int hash;

	#define	FILELIST_ADDS	1024	// number increment of file lists `

	if (filelist_entrys >= filelist_maxentry) {
		filelist = (FILELIST *)aRealloc(filelist, (filelist_maxentry + FILELIST_ADDS) * sizeof(FILELIST));
		memset(filelist + filelist_maxentry, '\0', FILELIST_ADDS * sizeof(FILELIST));
		filelist_maxentry += FILELIST_ADDS;
	}

	memcpy (&filelist[filelist_entrys], entry, sizeof(FILELIST));

	hash = filehash(entry->fn);
	filelist[filelist_entrys].next = filelist_hash[hash];
	filelist_hash[hash] = filelist_entrys;

	filelist_entrys++;

	return &filelist[filelist_entrys - 1];
}

// adds a new FILELIST entry or overwrites an existing one
static FILELIST* filelist_modify(FILELIST* entry)
{
	FILELIST* fentry = filelist_find(entry->fn);
	if (fentry != NULL) {
		int tmp = fentry->next;
		memcpy(fentry, entry, sizeof(FILELIST));
		fentry->next = tmp;
	} else {
		fentry = filelist_add(entry);
	}
	return fentry;
}

// shrinks the file list array if too long
static void filelist_compact(void)
{
	if (filelist == NULL)
		return;
	
	if (filelist_entrys < filelist_maxentry) {
		filelist = (FILELIST *)aRealloc(filelist, filelist_entrys * sizeof(FILELIST));
		filelist_maxentry = filelist_entrys;
	}
}


/***********************************************************
 ***                  Grfio Subroutines                  ***
 ***********************************************************/


/// Combines are resource path with the data folder location to create local resource path.
static void grfio_localpath_create(char* buffer, size_t size, const char* filename)
{
	unsigned int i;
	size_t len;

	len = strlen(data_dir);

	if( data_dir[0] == '\0' || data_dir[len-1] == '/' || data_dir[len-1] == '\\' )
	{
		safesnprintf(buffer, size, "%s%s", data_dir, filename);
	}
	else
	{
		safesnprintf(buffer, size, "%s/%s", data_dir, filename);
	}

	// normalize path
	for( i = 0; buffer[i] != '\0'; ++i )
		if( buffer[i] == '\\' )
			buffer[i] = '/';
}


/// Reads a file into a newly allocated buffer (from grf or data directory).
void* grfio_reads(const char* fname, int* size)
{
	unsigned char* buf2 = NULL;

	FILELIST* entry = filelist_find(fname);
	if( entry == NULL || entry->gentry <= 0 )
	{// LocalFileCheck
		char lfname[256];
		int declen;
		FILE* in;

		grfio_localpath_create(lfname, sizeof(lfname), ( entry && entry->fnd ) ? entry->fnd : fname);

		in = fopen(lfname, "rb");
		if( in != NULL )
		{
			fseek(in,0,SEEK_END);
			declen = ftell(in);
			fseek(in,0,SEEK_SET);
			buf2 = (unsigned char *)aMallocA(declen+1);  // +1 for resnametable zero-termination
			fread(buf2, 1, declen, in);
			fclose(in);

			if( size )
				*size = declen;
		}
		else
		{
			if (entry != NULL && entry->gentry < 0) {
				entry->gentry = -entry->gentry;	// local file checked
			} else {
				ShowError("grfio_reads: %s not found (local file: %s)\n", fname, lfname);
				return NULL;
			}
		}
	}

	if( entry != NULL && entry->gentry > 0 )
	{// Archive[GRF] File Read
		char* grfname = gentry_table[entry->gentry - 1];
		FILE* in = fopen(grfname, "rb");
		if( in != NULL )
		{
			unsigned char *buf = (unsigned char *)aMallocA(entry->srclen_aligned);
			fseek(in, entry->srcpos, 0);
			fread(buf, 1, entry->srclen_aligned, in);
			fclose(in);

			buf2 = (unsigned char *)aMallocA(entry->declen+1);  // +1 for resnametable zero-termination
			if( entry->type & FILELIST_TYPE_FILE )
			{// file
				uLongf len;
				grf_decode(buf, entry->srclen_aligned, entry->type, entry->srclen);
				len = entry->declen;
				decode_zip(buf2, &len, buf, entry->srclen);
				if (len != (uLong)entry->declen) {
					ShowError("decode_zip size mismatch err: %d != %d\n", (int)len, entry->declen);
					aFree(buf);
					aFree(buf2);
					return NULL;
				}
			}
			else
			{// directory?
				memcpy(buf2, buf, entry->declen);
			}

			if( size )
				*size = entry->declen;

			aFree(buf);
		}
		else
		{
			ShowError("grfio_reads: %s not found (GRF file: %s)\n", fname, grfname);
			return NULL;
		}
	}

	return buf2;
}


/// Decodes encrypted filename from a version 01xx grf index.
static char* decode_filename(unsigned char* buf, int len)
{
	int lop;
	for(lop=0;lop<len;lop+=8) {
		NibbleSwap(&buf[lop],8);
		des_decrypt(&buf[lop],8);
	}
	return (char*)buf;
}


/// Compares file extension against known large file types.
/// @return true if the file should undergo full mode 0 decryption, and true otherwise.
static bool isFullEncrypt(const char* fname)
{
	static const char extensions[4][5] = { ".gnd", ".gat", ".act", ".str" };
	size_t i;

	const char* ext = strrchr(fname, '.');
	if( ext != NULL )
		for( i = 0; i < ARRAYLENGTH(extensions); ++i )
			if( strcmpi(ext, extensions[i]) == 0 )
				return false;

	return true;
}


/// Loads all entries in the specified grf file into the filelist.
/// @param gentry index of the grf file name in the gentry_table
static int grfio_entryread(const char* grfname, int gentry)
{
	long grf_size,list_size;
	unsigned char grf_header[0x2e];
	int entry,entrys,ofs,grf_version;
	unsigned char *grf_filelist;

	FILE* fp = fopen(grfname, "rb");
	if( fp == NULL )
	{
		ShowWarning("GRF data file not found: '%s'\n",grfname);
		return 1;	// 1:not found error
	}
	else
		ShowInfo("GRF data file found: '%s'\n",grfname);

	fseek(fp,0,SEEK_END);
	grf_size = ftell(fp);
	fseek(fp,0,SEEK_SET);

	fread(grf_header,1,0x2e,fp);
	if( strcmp((const char*)grf_header,"Master of Magic") != 0 ||
		fseek(fp,getlong(grf_header+0x1e),SEEK_CUR) != 0 )
	{
		fclose(fp);
		ShowError("GRF %s read error\n", grfname);
		return 2;	// 2:file format error
	}

	grf_version = getlong(grf_header+0x2a) >> 8;

	if( grf_version == 0x01 )
	{// ****** Grf version 01xx ******
		list_size = grf_size - ftell(fp);
		grf_filelist = (unsigned char *) aMallocA(list_size);
		fread(grf_filelist,1,list_size,fp);
		fclose(fp);

		entrys = getlong(grf_header+0x26) - getlong(grf_header+0x22) - 7;

		// Get an entry
		for( entry = 0, ofs = 0; entry < entrys; ++entry )
		{
			FILELIST aentry;

			int ofs2 = ofs+getlong(grf_filelist+ofs)+4;
			unsigned char type = grf_filelist[ofs2+12];
			if( type & FILELIST_TYPE_FILE )
			{
				char* fname = decode_filename(grf_filelist+ofs+6, grf_filelist[ofs]-6);
				int srclen = getlong(grf_filelist+ofs2+0) - getlong(grf_filelist+ofs2+8) - 715;

				if( strlen(fname) > sizeof(aentry.fn) - 1 )
				{
					ShowFatalError("GRF file name %s is too long\n", fname);
					aFree(grf_filelist);
					exit(EXIT_FAILURE);
				}

				type |= ( isFullEncrypt(fname) ) ? FILELIST_TYPE_ENCRYPT_MIXED : FILELIST_TYPE_ENCRYPT_HEADER;

				aentry.srclen         = srclen;
				aentry.srclen_aligned = getlong(grf_filelist+ofs2+4)-37579;
				aentry.declen         = getlong(grf_filelist+ofs2+8);
				aentry.srcpos         = getlong(grf_filelist+ofs2+13)+0x2e;
				aentry.type           = type;
				safestrncpy(aentry.fn, fname, sizeof(aentry.fn));
				aentry.fnd			  = NULL;
#ifdef	GRFIO_LOCAL
				aentry.gentry         = -(gentry+1);	// As Flag for making it a negative number carrying out the first time LocalFileCheck
#else
				aentry.gentry         = gentry+1;		// With no first time LocalFileCheck
#endif
				filelist_modify(&aentry);
			}

			ofs = ofs2 + 17;
		}

		aFree(grf_filelist);
	}
	else
	if( grf_version == 0x02 )
	{// ****** Grf version 02xx ******
		unsigned char eheader[8];
		unsigned char *rBuf;
		uLongf rSize, eSize;

		fread(eheader,1,8,fp);
		rSize = getlong(eheader);	// Read Size
		eSize = getlong(eheader+4);	// Extend Size

		if( (long)rSize > grf_size-ftell(fp) )
		{
			fclose(fp);
			ShowError("Illegal data format: GRF compress entry size\n");
			return 4;
		}

		rBuf = (unsigned char *)aMallocA(rSize);	// Get a Read Size
		grf_filelist = (unsigned char *)aMallocA(eSize);	// Get a Extend Size
		fread(rBuf,1,rSize,fp);
		fclose(fp);
		decode_zip(grf_filelist, &eSize, rBuf, rSize);	// Decode function
		list_size = eSize;
		aFree(rBuf);

		entrys = getlong(grf_header+0x26) - 7;

		// Get an entry
		for( entry = 0, ofs = 0; entry < entrys; ++entry )
		{
			FILELIST aentry;

			char* fname = (char*)(grf_filelist+ofs);
			int ofs2 = ofs + (int)strlen(fname)+1;
			int type = grf_filelist[ofs2+12];

			if( strlen(fname) > sizeof(aentry.fn)-1 )
			{
				ShowFatalError("GRF file name %s is too long\n", fname);
				aFree(grf_filelist);
				exit(EXIT_FAILURE);
			}

			if( type & FILELIST_TYPE_FILE )
			{// file
				aentry.srclen         = getlong(grf_filelist+ofs2+0);
				aentry.srclen_aligned = getlong(grf_filelist+ofs2+4);
				aentry.declen         = getlong(grf_filelist+ofs2+8);
				aentry.srcpos         = getlong(grf_filelist+ofs2+13)+0x2e;
				aentry.type           = type;
				safestrncpy(aentry.fn, fname, sizeof(aentry.fn));
				aentry.fnd			  = NULL;
#ifdef	GRFIO_LOCAL
				aentry.gentry         = -(gentry+1);	// As Flag for making it a negative number carrying out the first time LocalFileCheck
#else
				aentry.gentry         = gentry+1;		// With no first time LocalFileCheck
#endif
				filelist_modify(&aentry);
			}

			ofs = ofs2 + 17;
		}

		aFree(grf_filelist);
	}
	else
	{// ****** Grf Other version ******
		fclose(fp);
		ShowError("GRF version %04x not supported\n",getlong(grf_header+0x2a));
		return 4;
	}

	filelist_compact();	// Unnecessary area release of filelist

	return 0;	// 0:no error
}


static bool grfio_parse_restable_row(const char* row)
{
	char w1[256], w2[256];
	char src[256], dst[256];
	char local[256];
	FILELIST* entry;

	if( sscanf(row, "%[^#\r\n]#%[^#\r\n]#", w1, w2) != 2 )
		return false;

	if( strstr(w2, ".gat") == NULL && strstr(w2, ".rsw") == NULL )
		return false; // we only need the maps' GAT and RSW files

	sprintf(src, "data\\%s", w1);
	sprintf(dst, "data\\%s", w2);

	entry = filelist_find(dst);
	if( entry != NULL )
	{// alias for GRF resource
		FILELIST fentry;
		memcpy(&fentry, entry, sizeof(FILELIST));
		safestrncpy(fentry.fn, src, sizeof(fentry.fn));
		fentry.fnd = aStrdup(dst);
		filelist_modify(&fentry);
		return true;
	}

	grfio_localpath_create(local, sizeof(local), dst);
	if( exists(local) )
	{// alias for local resource
		FILELIST fentry;
		memset(&fentry, 0, sizeof(fentry));
		safestrncpy(fentry.fn, src, sizeof(fentry.fn));
		fentry.fnd = aStrdup(dst);
		filelist_modify(&fentry);
		return true;
	}

	return false;
}


/// Grfio Resource file check.
static void grfio_resourcecheck(void)
{
	char restable[256];
	char *ptr, *buf;
	int size;
	FILE* fp;
	int i = 0;

	// read resnametable from data directory and return if successful
	grfio_localpath_create(restable, sizeof(restable), "data\\resnametable.txt");

	fp = fopen(restable, "rb");
	if( fp != NULL )
	{
		char line[256];
		while( fgets(line, sizeof(line), fp) )
		{
			if( grfio_parse_restable_row(line) )
				++i;
		}

		fclose(fp);
		ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", i, "resnametable.txt");
		return;	// we're done here!
	}
	
	// read resnametable from loaded GRF's, only if it cannot be loaded from the data directory
	buf = (char *)grfio_reads("data\\resnametable.txt", &size);
	if( buf != NULL )
	{
		buf[size] = '\0';

		ptr = buf;
		while( ptr - buf < size )
		{
			if( grfio_parse_restable_row(ptr) )
				++i;

			ptr = strchr(ptr, '\n');
			if( ptr == NULL ) break;
			ptr++;
		}

		aFree(buf);
		ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", i, "data\\resnametable.txt");
		return;
	}
}


/// Reads a grf file and adds it to the list.
static int grfio_add(const char* fname)
{
	if( gentry_entrys >= gentry_maxentry )
	{
		#define	GENTRY_ADDS	4	// The number increment of gentry_table entries
		gentry_maxentry += GENTRY_ADDS;
		gentry_table = (char**)aRealloc(gentry_table, gentry_maxentry * sizeof(char*));
		memset(gentry_table + (gentry_maxentry - GENTRY_ADDS), 0, sizeof(char*) * GENTRY_ADDS);
	}

	gentry_table[gentry_entrys++] = aStrdup(fname);

	return grfio_entryread(fname, gentry_entrys - 1);
}


/// Finalizes grfio.
void grfio_final(void)
{
	if (filelist != NULL) {
		int i;
		for (i = 0; i < filelist_entrys; i++)
			if (filelist[i].fnd != NULL)
				aFree(filelist[i].fnd);

		aFree(filelist);
		filelist = NULL;
	}
	filelist_entrys = filelist_maxentry = 0;

	if (gentry_table != NULL) {
		int i;
		for (i = 0; i < gentry_entrys; i++)
			if (gentry_table[i] != NULL)
				aFree(gentry_table[i]);

		aFree(gentry_table);
		gentry_table = NULL;
	}
	gentry_entrys = gentry_maxentry = 0;
}


/// Initializes grfio.
void grfio_init(const char* fname)
{
	FILE* data_conf;
	int grf_num = 0;

	hashinit();	// hash table initialization

	data_conf = fopen(fname, "r");
	if( data_conf != NULL )
	{
		char line[1024];
		while( fgets(line, sizeof(line), data_conf) )
		{
			char w1[1024], w2[1024];

			if( line[0] == '/' && line[1] == '/' )
				continue; // skip comments

			if( sscanf(line, "%[^:]: %[^\r\n]", w1, w2) != 2 )
				continue; // skip unrecognized lines

			// Entry table reading
			if( strcmp(w1, "grf") == 0 ) // GRF file
			{
				if( grfio_add(w2) == 0 )
					++grf_num;
			}
			else
			if( strcmp(w1,"data_dir") == 0 ) // Data directory
			{
				safestrncpy(data_dir, w2, sizeof(data_dir));
			}
		}

		fclose(data_conf);
		ShowStatus("Done reading '"CL_WHITE"%s"CL_RESET"'.\n", fname);
	}

	if( grf_num == 0 )
		ShowInfo("No GRF loaded, using default data directory\n");

	// Unneccessary area release of filelist
	filelist_compact();

	// Resource check
	grfio_resourcecheck();
}
