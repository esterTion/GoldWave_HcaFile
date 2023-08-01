/*
	SampleFile Copyright (c) 2003-2008 GoldWave Inc.

	Demonstrates how to create an Audio File plug-in for GoldWave.

	The interface to the host program requires a filled Interface structure,
	which provides:
		- the interface version (usually 'AudioVersion')
		- the number of File Format plug-ins contained in this module
		- a Table array containing the list of plug-ins
		- a File Format plug-in constructor function
	Finally, an exported AudioInterfaceDll() function is needed.
	The host program uses that function to retrieve the Interface structure.
 */

#include <windows.h>
#pragma hdrstop
#include "gwaudio.h"
#include <stdio.h>			// sprintf, file
#include <time.h>

using namespace Gap;
using namespace Gerr;

//---------------------------------------------------------------------------

const wchar_t DwdExtension[] = L"dwd";

/*
	Table listing all files types supported in this module (just one).
	Each element provides the type name, extension(s), and ability flags.
	The type name must not contain : / \ . leading characters or -
	see AudioCreate() for reason.
 */
Table	SampleTable = { L"DiamondWare", aRead | aWrite, DwdExtension };

/*
	Prototype for plug-in constructor
 */
AudioFile * gdecl AudioCreate( const wchar_t *name );

/*
	Make an Interface structure that host program uses to create
	plug-ins in the module.
 */
Interface SampleInterface =
{
	AudioVersion,
	1,
	&SampleTable,
	AudioCreate,
	0				// No configuration function
};

// This function is called by the application to get the table
AudioInterfaceDll( void )
{
	return &SampleInterface;
}

//---------------------------------------------------------------------------

#ifdef _WIN32
// Windows DLL entry point
BOOL WINAPI DllMain( HINSTANCE, DWORD, LPVOID )
{
	return 1;
}
#endif

//---------------------------------------------------------------------------
/*
	Format specific structure for reading DWD header from the file
 */

typedef unsigned char	byte;
typedef unsigned short	word;
typedef unsigned int	dword;

#pragma pack(push, 1)
struct DiamondHeader
{
	char	key[23];		// "DiamondWare Digitized\n\0"
	byte	eof,			// 0x1A end of file marker
			verMajor,		// Major version = 1
			verMinor;		// Minor version = 0 for 8-bit mono, 1 otherwise
	dword	id;				// Unique ID (checksum XOR timestamp)
	byte	reserved1,
			compression;	// Compression (0 = none)
	word	rate;
	byte	channels, bits;
	word	max;			// Absolute value of largest sample
	dword	dataSize,
			dataSamples,
			dataOffset;
	byte	reserved2[6];	// Reserved + padding
};
#pragma pack(pop)

#ifndef _MSC_VER // Only Borland supports sizeof in #if
# if sizeof(DiamondHeader) != 56
# error Structure packing/alignment must be 1
# endif
#endif

static char keyText[] = "DiamondWare Digitized\n";
const int	MaxRate = 64000;

//---------------------------------------------------------------------------
/*
	A derived Format class must be created to hold format specific
	information, such as bitrate, channels, bits, sampling rate, etc.
 */

class DiamondWareFormat : public Format
{
	wchar_t		description[100];

public:
	int			channels, bits, rate;
	unsigned	flags;

	DiamondWareFormat( void )
	{
		channels = 2;
		bits = 16;
		rate = 0;
		flags = fAnyRate;
	}

	// Functions to set general format parameters
	Error gdecl SetChannels( int c )
	{
		if ( channels == 1 || channels == 2 )
		{
			channels = c;
			return eNone;
		}
		else
			return eUnsupported;
	}
	Error gdecl SetRate( int r )
	{
		if ( r >= 1000 && r <= MaxRate )
		{
			rate = r;
			return eNone;
		}
		else
			return eUnsupported;
	}
	Error gdecl SetBitrate( int bitrate ) { return eUnsupported; }

	// Functions to get format parameters
	unsigned gdecl Flags( void ) const { return flags; }
	int gdecl Channels( void ) const { return channels; }
	int gdecl Rate( void ) const { return rate; }
	int gdecl Bitrate( void ) const { return channels * bits * rate; }

	DiamondWareFormat &operator=( const DiamondWareFormat &format )
	{
		rate  = format.rate;
		channels = format.channels;
		bits = format.bits;
		flags = format.flags;
		*description = 0;
		return *this;
	}

	// Used by host program to see if two formats are similar
	bool gdecl operator==( const Format &f ) const
	{
		const DiamondWareFormat *format;

		/*
			Dynamic casting is not safe between Borland and Microsoft, so
			just use Type pointer to make sure the format object belongs
			to this module.
		 */
		if ( f.Type() != Type() )
			return false;

		format = dynamic_cast<const DiamondWareFormat *>( &f );

		if ( ! format )
			return false;
		else
			return (format->rate == rate
					 || (format->flags & fAnyRate) || (flags & fAnyRate))
					 && format->bits == bits && format->channels == channels;
	}

	Format * gdecl Duplicate( void ) const
	{
		DiamondWareFormat *format = new DiamondWareFormat;
		*format = *this;
		return format;
	}

	// Functions to get format text description
	const wchar_t * gdecl Type( void ) const { return SampleTable.name; }
	const wchar_t * gdecl Description( void )
	{
		wchar_t	ratetext[20];

		if ( rate )
			swprintf( ratetext, L", %dHz, %dkbps", rate, Bitrate() / 1000 );
		else
			ratetext[0] = 0;

		swprintf( description, L"PCM signed %d bit%ls, %ls", bits, ratetext,
					channels == 1 ? L"mono" : L"stereo" );

		return description;
	}
	const wchar_t * gdecl Extension( void ) const { return DwdExtension; }
};

//---------------------------------------------------------------------------
/*
	To create the File Format plug-in class:
		- Derive a class from the AudioFile base class.
		- Override all required or pure virtual functions.
 */

class DiamondWareFile : public AudioFile
{
	// Input data
	DiamondWareFormat	inFormat;			// Input file format
	dword				length,				// Number of samples in file
						inOffset;			// Byte offset to start of audio
	FILE				*inFile;			// Input file
	Error				ReadHeader( void );

	// Output data
	DiamondWareFormat	outFormat;			// Output file format
	FILE				*outFile;			// Output file
	dword				written,			// Number of samples written
						sum;				// Checksum
	int					max;				// Maximum absolute amplitude

	// Template function to handle writing both 8-bit and 16-bit audio data
	/*
		Parameter hack needed because MSVC does not handle DoWrite<T>( ... )
		notation.
		Also the MSVC compiler has problems if the code is not contained
		within the class.
	 */
	template<class T> Error DoWrite( FILE *file, const audio *data, int samples, const T )
	{
		const int 	LocalSamples = 1024;
		T			buffer[LocalSamples];
		const T		TMax = (1 << (sizeof(T) * 8 - 1)) - 1;

		while ( samples )
		{
			int		count = samples < LocalSamples ? samples : LocalSamples,
					out = count;
			T		*p = buffer;

			samples -= count;
			while ( count-- )
			{
				// Audio data may exceed +/- 1.0 if not normalized
				if ( *data > 1 )
					*p = TMax;
				else if ( *data < -1 )
					*p = -TMax - 1;
				else
					*p = (T)(*data * TMax);

				// Maximum
				if ( *p > max )
					max = *p;
				else if ( -*p > max )
					max = -*p;

				// Checksum
				for ( int i = 0; i < sizeof(T); i++ )
					sum += *((byte *)p + i);
				data++;
				p++;
			}
			if ( fwrite( buffer, sizeof(T), out, file ) != (size_t)out )
				return eWrite;
		}
		return eNone;
	}

public:
	DiamondWareFile( void )
	{
		inFile = outFile = 0;
		length = inOffset = 0;
		written = 0;
	}
	~DiamondWareFile( void )
	{
		Close();
		End();
	}

	// Input member functions
	Error gdecl Open( const wchar_t *name, const Format *format = 0 );
	int	gdecl 	Read( audio *data, int samples );
	Error gdecl	Seek( int64 start );
	Error gdecl	Close( void );

	// Output member functions
	Error gdecl Begin( const wchar_t *name, const Format &format );
	Error gdecl Write( const audio *data, int samples );
	Error gdecl End( void );

	int64 gdecl 		Length( void ) { return length; }
	FormatList * gdecl	Formats( void );
	Format * gdecl 		GetFormat( void );
	unsigned gdecl 		Ability( void ) { return SampleTable.abilities; }
	const wchar_t * gdecl Name( void ) { return SampleTable.name; }
};

//---------------------------------------------------------------------------
// Read member functions

// Open the given file and make sure it is a DiamondWare format file
Error gdecl DiamondWareFile::Open( const wchar_t *name, const Format * )
{
	if ( ! name )
		return eOpen;

	inFile = _wfopen( name, L"rb" );
	if ( ! inFile )
		return eOpen;

	// Use helper function so that file can be closed on error
	Error e = ReadHeader();
	if ( e != eNone )
	{
		fclose( inFile );
		inFile = 0;
	}
	return e;
}

// Helper function to read header
Error DiamondWareFile::ReadHeader( void )
{
	DiamondHeader	header;
	dword			size;

	fseek( inFile, 0, SEEK_END );
	size = ftell( inFile );
	fseek( inFile, 0, SEEK_SET );
	if ( size < sizeof(header) )
		return eEmpty;

	if ( fread( &header, sizeof(header), 1, inFile ) != 1 )
		return eRead;

	// Is it a DiamondWare file?
	if ( strcmp( keyText, header.key ) != 0 )
		return eType;

	// Make sure we can handle the format of the file
	if ( (header.bits != 8 && header.bits != 16) || header.channels > 2
		 || header.rate > MaxRate || header.rate < 1000
		 || header.verMajor > 1 || header.verMinor > 2 || header.compression )
		return eFormat;

	// Check the header size,the file size, and the channels
	if ( header.dataSize + header.dataOffset > size || header.channels < 1 )
		return eCorrupt;

	// Check the offset
	if ( header.dataOffset < sizeof(header) - sizeof(header.reserved2) )
		return eCorrupt;

	if ( ! header.dataSize )
		return eEmpty;

	if ( header.dataSamples * (header.bits / 8) * header.channels
			!= header.dataSize )
	{
		if ( Ask( L"Internal size is incorrect.\n\nContinue anyway?",
				Asker::YesNo, Asker::Yes ) == Asker::No )
			return eAbort;
		length = header.dataSize / header.channels / (header.bits / 8);
	}
	else
		length = header.dataSamples;

	if ( ! length )
		return eEmpty;

	inFormat.bits = header.bits;
	inFormat.channels = header.channels;
	inFormat.rate = header.rate;
	//inFormat.flags &= ~Format::fAnyRate;
	inOffset = header.dataOffset;

	return eNone;
}

// Read audio data from file and convert to native 'audio' type
int gdecl DiamondWareFile::Read( audio *dest, int samples )
{
	if ( ! inFile )
		return -eForbidden;

	int unitsize = (inFormat.bits / 8) * inFormat.channels,
		position = ftell( inFile ) - inOffset, total;

	position /= unitsize;

	// Make sure we do not read past end of file (or any trailing garbage)
	if ( (unsigned)position + samples > length )
	{
		samples = length - position;
		if ( samples <= 0 )
			return 0;
	}

	/*
		The unitsize of audio data is larger than the unitsize of DWD
		data, so it is safe to read it directly into the passed destination
		buffer and convert it in-place.
	 */
	samples = fread( dest, unitsize, samples, inFile );
	total = samples;

	samples *= inFormat.channels;
	if ( inFormat.bits == 8 )
	{
		char *data = (char *)dest + samples - 1;

		// Need to start at end to avoid overwriting data
		dest += samples - 1;
		while ( samples-- )
		{
			*dest = (audio)*data / 128;
			dest--;
			data--;
		}
	}
	else
	{
		short *data = (short *)dest + samples - 1;

		// Need to start at end to avoid overwriting data
		dest += samples - 1;
		while ( samples-- )
		{
			*dest = (audio)*data / 32768;
			dest--;
			data--;
		}
	}
	return total;
}

// Seek to sample position within the input stream
Error gdecl DiamondWareFile::Seek( int64 position )
{
	// Seek only applies to input.  If an input file is not open, return error
	if ( ! inFile )
		return eForbidden;

	// Convert samples to bytes
	position *= (inFormat.bits / 8) * inFormat.channels;

	// Note: limited to 2GB on 32-bit systems
	if ( fseek( inFile, (int)(position + inOffset), SEEK_SET ) != 0 )
		return eSeek;

	return eNone;
}

// Close input file
Error gdecl DiamondWareFile::Close( void )
{
	if ( ! inFile )
		return eForbidden;
	fclose( inFile );
	length = 0;
	inFile = 0;

	// Reset format to default
	inFormat = DiamondWareFormat();
	
	return eNone;
}

//---------------------------------------------------------------------------
// Write member functions

/*
	Begin writing a file.  For DiamondWare files, we just create the
	file and leave room for the header.
 */
Error gdecl DiamondWareFile::Begin( const wchar_t *name, const Format &f )
{
	if ( outFile )
		return eForbidden;

	const DiamondWareFormat *format;

	// Make sure the format object is appropriate and belongs to this plug-in
	if ( f.Type() != SampleTable.name )
		return eFormat;
	format = dynamic_cast<const DiamondWareFormat *>( &f );
	if ( ! format || format->rate == 0 )
		return eFormat;

	outFormat = *format;

	written = 0;
	max = 0;
	sum = 0;

	// Create file
	outFile = _wfopen( name, L"wb" );
	if ( ! outFile )
		return eCreate;

	// Leave room for header and write it later in End()
	if ( fseek( outFile, sizeof(DiamondHeader), SEEK_SET ) )
	{
		fclose( outFile );
		return eSeek;
	}

	return eNone;
}

// Write audio data to file in the correct format - either 8 or 16 bit
Error gdecl DiamondWareFile::Write( const audio *data, int samples )
{
	if ( ! outFile )
		return eForbidden;

	written += samples;
	samples *= outFormat.channels;

	/*
		Due to problems with MSVC, we cannot use DoWrite<char>( ... )
		and DoWrite<short>( ... ) notation.  We use a type parameter
		hack instead.
	 */
	if ( outFormat.bits == 8 )
		return DoWrite( outFile, data, samples, (char)0 );
	else
		return DoWrite( outFile, data, samples, (short)0 );
}

// Close output file by writing complete header
Error gdecl DiamondWareFile::End( void )
{
	DiamondHeader	header;

	if ( ! outFile )
		return eForbidden;

	// Set all header information
	memset( &header, 0, sizeof(header) );
	strcpy( header.key, keyText );
	header.eof = 0x1A;
	header.verMajor = 1;
	if ( outFormat.channels > 1 || outFormat.bits != 8 )
		header.verMinor = 1;
	header.id = time( 0 ) ^ sum;
	header.compression = 0;
	header.rate = (word)outFormat.rate;
	header.channels = (byte)outFormat.channels;
	header.bits = (byte)outFormat.bits;
	header.max = (word)max;
	header.dataSize = written * outFormat.channels * (outFormat.bits / 8);
	header.dataSamples = written;
	header.dataOffset = sizeof(header);

	// Seek to beginning
	if ( fseek( outFile, 0, SEEK_SET ) )
	{
		fclose( outFile );
		return eSeek;
	}

	// Write header
	if ( fwrite( &header, sizeof(header), 1, outFile ) != 1 )
	{
		fclose( outFile );
		return eWrite;
	}

	// Close file
	fclose( outFile );
	outFile = 0;

	return eNone;
}

//---------------------------------------------------------------------
// DiamondWare FormatList functions

/*
	Only 4 combinations of attributes are supported:
		PCM signed 8 bit, mono
		PCM signed 8 bit, stereo
		PCM signed 16 bit, mono		(default)
		PCM signed 16 bit, stereo	(default)
	To create this list, derive a class from FormatList and
	initialize an array of 4 DiamondWareFormat objects with
	the above attributes.

	Note that a pointer to a static structure MUST NOT be
	returned since the application may modify the Format
	objects to set rates, channels, or bitrates.
 */
class DiamondWareList : public FormatList
{
	DiamondWareFormat	format[4];
public:
	DiamondWareList( void )
	{
		count = sizeof(format) / sizeof(*format);

		// Setup 8-bit mono and stereo, 16-bit mono and stereo
		format[0].channels = format[2].channels = 1;
		format[0].bits = format[1].bits = 8;

		// One default format must be provided each for mono and stereo

		// Set 16-bit formats as default
		format[2].flags |= Format::fDefault;
		format[3].flags |= Format::fDefault;
	}
	Format * gdecl operator[]( int i )
	{
		if ( i >= 0 && i < count )
			return format + i;
		else
			return 0;
	}
};

// Create a DiamondWareList and pass it back to host program
FormatList * gdecl DiamondWareFile::Formats( void )
{
	// Host program will Destroy() this object
	return new DiamondWareList;
}

Format * gdecl DiamondWareFile::GetFormat( void )
{
	// Return current format
	return inFormat.Duplicate();
}

//---------------------------------------------------------------------------
/*
	File Format plug-in constructor.  Takes a string containing the filename
	or type name and creates an appropriate plug-in or returns 0 if
	it does not recognize the file format or type name.
 */
AudioFile * gdecl AudioCreate( const wchar_t *name )
{
	if ( ! name )
		return 0;

	// Is it a filename?  Host program provides full pathname
	// Should do a wcsstr( name, "://" ) for URL detection
	if ( name[1] == L':' || name[0] == L'\\'
			|| name[0] == L'/' || name[0] == L'.' )
	{
		FILE			*file = _wfopen( name, L"rb" );
		DiamondHeader	header;
		int				read;

		if ( ! file )
			return 0;

		read = fread( &header, sizeof(header), 1, file );
		fclose( file );
		// Does the file contain the DiamondWare key?
		if ( read == 1 && strcmp( keyText, header.key ) == 0 )
			return new DiamondWareFile;
	}
	else if ( wcscmp( name, SampleTable.name ) == 0 )
		return new DiamondWareFile;

	return 0;
};

//---------------------------------------------------------------------------


