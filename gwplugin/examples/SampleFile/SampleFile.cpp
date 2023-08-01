/*
	SampleFile Copyright (c) 2003 GoldWave Inc.

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
#include <mmsystem.h>		// Wave structures and defines
#include "gwaudio.h"		// AudioFile base class
#include <memory.h>			// memcmp
#include <stdio.h>			// sprintf, file

using namespace Gap;
using namespace Gerr;

//---------------------------------------------------------------------------

const wchar_t	Extension[] = L"wav";
const int		MaxRate = 192000;

/*
	Table listing all files types supported in this module (just one).
	Each element provides the type name, extension(s), and ability flags.
	The type name must not contain : / \ . leading characters or -
	see AudioCreate() for reason.
 */
Table	SampleTable = { L"Sample Wave", aRead | aWrite, Extension };

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
	A derived Format class must be created to hold format specific
	information, such as bitrate, channels, bits, sampling rate, etc.
 */

class FileFormat : public Format
{
	wchar_t		description[100];

public:
	int			channels, bits, rate;
	unsigned	flags;

	FileFormat( void )
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
		if ( r >= 100 && r <= MaxRate )
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

	FileFormat &operator=( const FileFormat &format )
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
		const FileFormat *format;

		/*
			Dynamic casting is not safe between Borland and Microsoft, so
			just use Type pointer to make sure the format object belongs
			to this module.
		 */
		if ( f.Type() != Type() )
			return false;

		format = dynamic_cast<const FileFormat *>( &f );

		if ( ! format )
			return false;
		else
			return (format->rate == rate
					 || (format->flags & fAnyRate) || (flags & fAnyRate))
					 && format->bits == bits && format->channels == channels;
	}

	Format * gdecl Duplicate( void ) const
	{
		FileFormat *format = new FileFormat;
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
	const wchar_t * gdecl Extension( void ) const { return ::Extension; }
};

//---------------------------------------------------------------------------
/*
	Read the beginning of file to detect RIFF WAVE chunk and format.
	For simplicity, this sample only handles RIFF Wave files that have
	the 'fmt ' chunk at the beginning of the file and only
	WAVE_FORMAT_PCM 8 or 16 bit data.  All other formats return
	an error.

	Warning! This is *not* the correct way to read RIFF WAVE files.
	It is only a simple sample program which demonstrates creating
	a GoldWave file plug-in.  It does not demonstrate how to work
	with WAVE files.  You are prohibited from releasing compiled
	versions of this very limited plug-in.
 */

struct RiffBlocks
{
	DWORD			riff, length, wave, fmt, fmtlength;
	PCMWAVEFORMAT	format;
	DWORD			data, datalength;
};

Error DetectWaveFile( FILE *file, unsigned *length = 0, FileFormat *fmt = 0 )
{
	RiffBlocks blocks;

	if ( fread( &blocks, sizeof(blocks), 1, file ) != 1 )
		return eRead;

	// Is this a wave file?
	if ( memcmp( &blocks.riff, "RIFF", 4 ) != 0
			|| memcmp( &blocks.wave, "WAVE", 4 ) != 0 )
		return eType;

	// Is it a simple wave file with only fmt and data chunks?
	if ( memcmp( &blocks.fmt, "fmt ", 4 ) != 0
			|| memcmp( &blocks.data, "data", 4 ) != 0 )
		return eFormat;

	// Only PCM supported
	if ( blocks.format.wf.wFormatTag != WAVE_FORMAT_PCM )
		return eFormat;

	// Only 8 or 16 bit
	if ( blocks.format.wBitsPerSample != 8
			&& blocks.format.wBitsPerSample != 16 )
		return eFormat;

	// Only mono or stereo
	if ( blocks.format.wf.nChannels < 1 )
		return eCorrupt;
	else if ( blocks.format.wf.nChannels > 2 )
		return eFormat;

	// Check sampling rate
	if ( blocks.format.wf.nSamplesPerSec < 1 )
		return eCorrupt;
	else if ( blocks.format.wf.nSamplesPerSec > MaxRate )
		return eFormat;

	if ( length )
		*length = blocks.datalength;

	if ( fmt )
	{
		fmt->bits = blocks.format.wBitsPerSample;
		fmt->channels = blocks.format.wf.nChannels;
		fmt->rate = blocks.format.wf.nSamplesPerSec;
	}
	return eNone;
}

//---------------------------------------------------------------------------
// Conversion function used for DoWrite() template below

inline void ConvertAudio( audio data, BYTE &dest )
{
	if ( data > 1 )
		dest = 255;
	else if ( data < -1 )
		dest = 0;
	else // Rounded
		dest = (BYTE)((255 * (data + 1) + 1) / 2);
}

inline void ConvertAudio( audio data, short &dest )
{
	if ( data > 1 )
		dest = 32767;
	else if ( data < -1 )
		dest = -32768;
	else
		dest = (short)(data * 32767);
}

//---------------------------------------------------------------------------
/*
	To create the File Format plug-in class:
		- Derive a class from the AudioFile base class.
		- Override all required or pure virtual functions.
 */

class File : public AudioFile
{
	// Input data
	FileFormat	inFormat;			// Input file format
	DWORD	  	length,				// Number of samples in file
				inOffset;			// Byte offset to start of audio
	FILE	  	*inFile;			// Input file

	// Output data
	FileFormat	outFormat;			// Output file format
	FILE	  	*outFile;			// Output file
	DWORD	  	written;			// Number of samples written

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

		while ( samples )
		{
			int		count = samples < LocalSamples ? samples : LocalSamples,
					out = count;
			T		*p = buffer;

			samples -= count;
			while ( count-- )
			{
				ConvertAudio( *data, *p );
				data++;
				p++;
			}
			if ( fwrite( buffer, sizeof(T), out, file ) != (size_t)out )
				return eWrite;
		}
		return eNone;
	}

public:
	File( void )
	{
		inFile = outFile = 0;
		length = inOffset = 0;
		written = 0;
	}
	~File( void )
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

// Open the given file and make sure it is a Wave format file
Error gdecl File::Open( const wchar_t *name, const Format * )
{
	if ( ! name )
		return eOpen;

	inFile = _wfopen( name, L"rb" );
	if ( ! inFile )
		return eOpen;

	unsigned	size, len;
	Error		error;

	// Make sure size of file is large enough for minimal RIFF WAVE data
	fseek( inFile, 0, SEEK_END );
	size = ftell( inFile );
	fseek( inFile, 0, SEEK_SET );
	if ( size <= sizeof(RiffBlocks) )
	{
		Close();
		return eEmpty;
	}

	// Set offset where audio data will be
	inOffset = sizeof(RiffBlocks);

	// Check that this is a RIFF Wave file and get format information
	error = DetectWaveFile( inFile, &len, &inFormat );

	// Make sure audio data size and chunk size are correct
	if ( error == eNone && size - sizeof(RiffBlocks) < len )
	{
		if ( Ask( L"Internal size is incorrect.\n\nContinue anyway?",
				Asker::YesNo, Asker::Yes ) == Asker::No )
		{
			Close();
			return eAbort;
		}
		// Fix size
		len = size - sizeof(RiffBlocks);
	}

	if ( error != eNone )
		Close();
	else
		length = len / inFormat.channels / (inFormat.bits / 8);

	return error;
}

// Read audio data from file and convert to native 'audio' type
int gdecl File::Read( audio *dest, int samples )
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
		The unitsize of audio data is larger than the unitsize of 8/16 bit
		data, so it is safe to read it directly into the passed destination
		buffer and convert it in-place.
	 */
	samples = fread( dest, unitsize, samples, inFile );
	total = samples;

	samples *= inFormat.channels;
	if ( inFormat.bits == 8 )
	{
		BYTE *data = (BYTE *)dest + samples - 1;

		// Need to start at end to avoid overwriting data
		dest += samples - 1;
		while ( samples-- )
		{
			*dest = (audio)((int)*data - 128) / 128;
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
Error gdecl File::Seek( int64 position )
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
Error gdecl File::Close( void )
{
	if ( ! inFile )
		return eForbidden;
	fclose( inFile );
	length = 0;
	inFile = 0;

	// Reset format to default
	inFormat = FileFormat();

	return eNone;
}

//---------------------------------------------------------------------------
// Write member functions

/*
	Begin writing a file.  For Wave files, we just create the
	file and leave room for the RIFF WAVE blocks.
 */
Error gdecl File::Begin( const wchar_t *name, const Format &f )
{
	if ( outFile )
		return eForbidden;

	const FileFormat *format;

	// Make sure the format object is appropriate and belongs to this plug-in
	if ( f.Type() != SampleTable.name )
		return eFormat;
	format = dynamic_cast<const FileFormat *>( &f );
	if ( ! format || format->rate == 0 )
		return eFormat;

	outFormat = *format;

	written = 0;

	// Create file
	outFile = _wfopen( name, L"wb" );
	if ( ! outFile )
		return eCreate;

	// Leave room for RIFF WAVE blocks and write it later in End()
	if ( fseek( outFile, sizeof(RiffBlocks), SEEK_SET ) )
	{
		fclose( outFile );
		return eSeek;
	}

	return eNone;
}

// Write audio data to file in the correct format - either 8 or 16 bit
Error gdecl File::Write( const audio *data, int samples )
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
		return DoWrite( outFile, data, samples, (BYTE)0 );
	else
		return DoWrite( outFile, data, samples, (short)0 );
}

// Close output file by writing RIFF blocks
Error gdecl File::End( void )
{
	RiffBlocks	blocks;

	if ( ! outFile )
		return eForbidden;

	// Set all riff information
	DWORD	bytes = written * (outFormat.bits / 8) * outFormat.channels;

	blocks.riff = *(DWORD*)"RIFF";
	blocks.wave = *(DWORD*)"WAVE";
	blocks.fmt = *(DWORD*)"fmt ";
	blocks.data = *(DWORD*)"data";
	blocks.datalength = bytes;
	blocks.fmtlength = sizeof(PCMWAVEFORMAT);

	WAVEFORMAT	&wf = blocks.format.wf;

	blocks.format.wBitsPerSample = (WORD)outFormat.bits;
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = (WORD)outFormat.channels;
	wf.nSamplesPerSec = outFormat.rate;
	wf.nBlockAlign = (WORD)(outFormat.channels * outFormat.bits / 8);
	wf.nAvgBytesPerSec = wf.nBlockAlign * outFormat.rate;

	// Blocks must contain even number of bytes, so pad data block if necessary
	if ( bytes & 1 )
	{
		bytes++;
		fputc( 0, outFile );
	}

	// Set total RIFF size (must not include RIFF and length DWORDs)
	blocks.length = sizeof(RiffBlocks) - 8 + bytes;

	Error error = eNone;
	// Seek to beginning
	if ( fseek( outFile, 0, SEEK_SET ) )
		error = eSeek;

	// Write blocks
	if ( error == eNone && fwrite( &blocks, sizeof(blocks), 1, outFile ) != 1 )
		error = eWrite;

	// Close file
	fclose( outFile );
	outFile = 0;

	// File must be closed at this point, regardless of how things went

	return error;
}

//---------------------------------------------------------------------
// Wave FormatList functions

/*
	Only 4 combinations of attributes are supported:
		PCM signed 8 bit, mono
		PCM signed 8 bit, stereo
		PCM signed 16 bit, mono		(default)
		PCM signed 16 bit, stereo	(default)
	To create this list, derive a class from FormatList and
	initialize an array of 4 FileFormat objects with
	the above attributes.

	Note that a pointer to a static structure MUST NOT be
	returned since the application may modify the Format
	objects to set rates, channels, or bitrates.
 */
class FileList : public FormatList
{
	FileFormat	format[4];
public:
	FileList( void )
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

// Create a FileList and pass it back to host program
FormatList * gdecl File::Formats( void )
{
	// Host program will Destroy() this object
	return new FileList;
}

Format * gdecl File::GetFormat( void )
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
	// Should do a wcsstr( name, L"://" ) for URL detection
	if ( name[1] == L':' || name[0] == L'\\'
			|| name[0] == L'/' || name[0] == L'.' )
	{
		FILE			*file = _wfopen( name, L"rb" );
		Error			error;

		if ( ! file )
			return 0;

		error = DetectWaveFile( file );
		fclose( file );

		// Does the file contain Wave audio?
		if ( error == eNone )
			return new File;
	}
	else if ( wcscmp( name, SampleTable.name ) == 0 )
		return new File;

	return 0;
};

//---------------------------------------------------------------------------


