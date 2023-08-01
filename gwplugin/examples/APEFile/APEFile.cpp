/*
	APEFile Copyright (c) 2005-2009 GoldWave Inc.

	APE File plug-in for GoldWave.  The Monkey's Audio SDK is required
	to compile and run this plug-in (header files and the MACDll.dll file).
		http://www.monkeysaudio.com

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
#include <mmreg.h>
#include <stdio.h>			// sprintf, file
#pragma hdrstop
#include "gwaudio.h"
#define GWINCLUDESTRINGS
#include "gwmetadata.h"
#include "MACdll.h"			// From Monkey's Audio SDK
#include "APETag.h"			// From Monkey's Audio SDK
#include "ID3Genres.h"		// From Monkey's Audio SDK

using namespace Gap;
using namespace Gerr;

//---------------------------------------------------------------------------

const wchar_t	Extension[] = L"ape";
HMODULE 		MacDll = 0;

/*
	Table listing all files types supported in this module (just one).
	Each element provides the type name, extension(s), and ability flags.
	The type name must not contain : / \ . leading characters or -
	see AudioCreate() for reason.
 */
Table TableData =
{
	L"Monkey's Audio",
	aRead | aWrite | aMetaText | aMetaCue,
	Extension
};

// Prototype for plug-in constructor
AudioFile * gdecl AudioCreate( const wchar_t *name );

// Make host program Interface structure
Interface InterfaceData =
{
	AudioVersion,
	1,
	&TableData,
	AudioCreate,
	0				// No configuration function
};

// This function is called by the application to get the table
AudioInterfaceDll( void )
{
	if ( MacDll )
		return &InterfaceData;
	return 0;
}

//---------------------------------------------------------------------------
// MACDll interface functions

typedef int (__stdcall * proc_TagFileSimple)(const str_ansi *,
	const char *, const char *, const char *, const char *,
	const char *, const char *, const char *, bool = true, bool = false );
typedef int (__stdcall * proc_GetID3Tag)(const str_ansi *, ID3_TAG * pID3Tag );

struct MacEncoderStruct
{
	proc_APECompress_Create                 Create;
	proc_APECompress_Destroy                Destroy;
	proc_APECompress_StartW                 Start;
	proc_APECompress_AddData                AddData;
	proc_APECompress_Finish                 Finish;
	proc_TagFileSimple						TagFileSimple;
	proc_GetInterfaceCompatibility			Compatibility;
} MacEncoder;

struct MacDecoderStruct
{
	proc_APEDecompress_CreateW				Create;
	proc_APEDecompress_Destroy				Destroy;
	proc_APEDecompress_GetData				GetData;
	proc_APEDecompress_Seek					Seek;
	proc_APEDecompress_GetInfo				GetInfo;
	proc_GetID3Tag							GetTag;
} MacDecoder;

/*
	MACDll function tables
 */
struct DllLookupEntry
{
	const char	*name;
	void 		*function;
};

static DllLookupEntry DllTable[] =
{
	{ "c_APECompress_Create",		&MacEncoder.Create },
	{ "c_APECompress_Destroy",		&MacEncoder.Destroy },
	{ "c_APECompress_StartW",		&MacEncoder.Start },
	{ "c_APECompress_AddData",		&MacEncoder.AddData },
	{ "c_APECompress_Finish",		&MacEncoder.Finish },
	{ "TagFileSimple",				&MacEncoder.TagFileSimple },
	{ "GetInterfaceCompatibility",	&MacEncoder.Compatibility },

	{ "c_APEDecompress_CreateW",	&MacDecoder.Create },
	{ "c_APEDecompress_Destroy",	&MacDecoder.Destroy },
	{ "c_APEDecompress_GetData",	&MacDecoder.GetData },
	{ "c_APEDecompress_Seek",		&MacDecoder.Seek },
	{ "c_APEDecompress_GetInfo",	&MacDecoder.GetInfo },
	{ "GetID3Tag",					&MacDecoder.GetTag },
};

static bool GetDllFunctions( void )
{
	int count = sizeof(DllTable) / sizeof(DllTable[0]), n;

	for ( n = 0; n < count; n++ )
	{
		FARPROC	&function = *(FARPROC *)DllTable[n].function;
		function = GetProcAddress( MacDll, DllTable[n].name );
		if ( function == 0 )
		{
/*			MessageBox( 0, "MACDll function missing.  Reinstall APEFIle plug-in.",
						DllTable[n].name, MB_OK );*/
			break;
		}
	}

	if ( n < count || MacEncoder.Compatibility( MAC_VERSION_NUMBER, true, 0 ) )
	{
		FreeLibrary( MacDll );
/*		MessageBox( 0, "Version of MACDll is not compatible.  Reinstall APEFile plug-in.",
					"MACDll Error", MB_OK );*/
		return false;
	}
	return true;
}

#ifdef _WIN32
// Windows DLL entry point
BOOL WINAPI DllMain( HINSTANCE instance, DWORD reason, LPVOID )
{
	if ( reason == DLL_PROCESS_ATTACH )
	{
		wchar_t			dllpath[MAX_PATH];
		const wchar_t	*dllname = L"MACDll.dll";

		// Load DLL from module's folder
		GetModuleFileName( instance, dllpath, MAX_PATH );
		wchar_t *slash = wcsrchr( dllpath, L'\\' );
		if ( slash )
		{
			wcscpy( slash + 1, dllname );
			MacDll = LoadLibrary( dllpath );
		}

		// This shouldn't happen unless the plug-in wasn't installed correctly
		if ( MacDll == 0 )
			MacDll = LoadLibrary( dllname );

		if ( MacDll == 0 || ! GetDllFunctions() )
			return false;
	}
	else if ( reason == DLL_PROCESS_DETACH )
	{
		if ( MacDll )
		{
			FreeLibrary( MacDll );
			MacDll = 0;
		}
	}

	return 1;
}
#endif

//---------------------------------------------------------------------------
/*
	A derived Format class must be created to hold format specific
	information, such as bitrate, channels, bits, sampling rate, etc.

	For APE, the bitrate is not known (variable).
 */

const int MaxRate = 192000, MaxSize = 8192;
const wchar_t	*Level[] =
{
	L"Low/Fast",		// Lowest compression, but fast
	L"Medium",
	L"High",
	L"Extra High",
	L"Maximum"		// Best compression
};

class AFormat : public Format
{
	wchar_t		description[100];

public:
	int			channels, bits, rate, level;
	unsigned	flags;

	AFormat( void )
	{
		channels = 2;
		bits = 16;
		rate = 0;
		flags = fAnyRate | fVBR | fSequential;
		level = 0;
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

	AFormat &operator=( const AFormat &format )
	{
		rate  = format.rate;
		channels = format.channels;
		bits = format.bits;
		flags = format.flags;
		level = format.level;
		*description = 0;
		return *this;
	}

	// Used by host program to see if two formats are similar
	bool gdecl operator==( const Format &f ) const
	{
		const AFormat *format;

		/*
			Dynamic casting is not safe between Borland and Microsoft, so
			just use Type pointer to make sure the format object belongs
			to this module.
		 */
		if ( f.Type() != Type() )
			return false;

		format = dynamic_cast<const AFormat *>( &f );

		if ( ! format )
			return false;
		else
			return (format->rate == rate
					 || (format->flags & fAnyRate) || (flags & fAnyRate))
					 && format->bits == bits && format->channels == channels
					 && format->level == level;
	}

	Format * gdecl Duplicate( void ) const
	{
		AFormat *format = new AFormat;
		*format = *this;
		return format;
	}

	// Functions to get format text description
	const wchar_t * gdecl Type( void ) const { return TableData.name; }
	const wchar_t * gdecl Description( void )
	{
		wchar_t	ratetext[20], leveltext[20];

		// Fixed rate or any rate?
		if ( rate )
			//sprintf( ratetext, ", %dHz, %dkbps", rate, Bitrate() / 1000 );
			swprintf( ratetext, L", %dHz", rate );
		else
			ratetext[0] = 0;

		if ( level )
			swprintf( leveltext, L"%ls, ", Level[level - 1] );
		else
			leveltext[0] = 0;

		swprintf( description, L"%ls%d bit%ls, %ls", leveltext, bits, ratetext,
					channels == 1 ? L"mono" : L"stereo" );

		return description;
	}
	const wchar_t * gdecl Extension( void ) const { return ::Extension; }
};

//---------------------------------------------------------------------------
/*
	To create the File Format plug-in class:
		- Derive a class from the AudioFile base class.
		- Override all required or pure virtual functions.
 */

class AFile : public AudioFile
{
	// Input data
	AFormat					inFormat;			// Input file format
	int64					length;				// Number of samples in file
	APE_DECOMPRESS_HANDLE	decoder;
	Error					ReadInfo( const wchar_t *name );
	Error					ReadCues( void );
	Error 					ReadCueChunks( const unsigned char *cue,
										   DWORD cuesize,
										   const unsigned char *adtl,
										   DWORD adtlsize );
	// Output data
	AFormat					outFormat;			// Output file format
	wchar_t					*outName;
	APE_COMPRESS_HANDLE		encoder;

	Error					WriteInfo( void );
	unsigned char			*WriteCues( int & );
	Gmd::CueList			*cue;
	Gmd::Text				*textConvert;

public:
	AFile( void )
	{
		length = 0;
		decoder = 0;
		encoder = 0;
		outName = 0;
		cue = 0;
		textConvert = 0;
	}
	~AFile( void )
	{
		Close();
		End();
		if ( textConvert )
			textConvert->Destroy();
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
	unsigned gdecl 		Ability( void ) { return TableData.abilities; }
	const wchar_t * gdecl Name( void ) { return TableData.name; }

	void Setup( Metadata *Data, Asker *Asker )
	{
		AudioFile::Setup( Data, Asker );
		if ( Data )
		{
			cue = (Gmd::CueList *)Data->Get( Gmd::GWCueList );
			if ( textConvert )
				textConvert->Destroy();
			// Use textConvert to convert between unicode and Utf8
			textConvert = (Gmd::Text *)Data->New( Gmd::GWText );
		}
		else
			cue = 0;
	}
};

//---------------------------------------------------------------------------
// Read member functions

// Open the given file and make sure it is an APE format file
Error gdecl AFile::Open( const wchar_t *name, const Format * )
{
	if ( ! name )
		return eOpen;

	if ( decoder )
		return eForbidden;

	// Try to read information (must do this before file is opened)
	ReadInfo( name );		// Errors ignored for now

	int retval;
	decoder = MacDecoder.Create( name, &retval );
	if ( ! decoder )
		return eOpen;

	int	val = MacDecoder.GetInfo( decoder, APE_INFO_COMPRESSION_LEVEL, 0, 0 );

	inFormat.level = (val + 500) / 1000;
	inFormat.rate = MacDecoder.GetInfo( decoder, APE_INFO_SAMPLE_RATE, 0, 0 );
	inFormat.bits = MacDecoder.GetInfo( decoder, APE_INFO_BITS_PER_SAMPLE, 0, 0 );
	inFormat.channels = MacDecoder.GetInfo( decoder, APE_INFO_CHANNELS, 0, 0 );
	val = MacDecoder.GetInfo( decoder, APE_INFO_WAV_TOTAL_BYTES, 0, 0 );
	if ( inFormat.bits != 8 && inFormat.bits != 16 && inFormat.bits != 24
			|| inFormat.channels > 2 || inFormat.channels < 1
			|| inFormat.rate < 1000 || inFormat.rate > MaxRate
			|| inFormat.level < 0 || inFormat.level > 5 )
	{
		Close();
		return eCorrupt;
	}

	// Read cues from terminating data, if any (errors are ignored)
	ReadCues();

	length = val / (inFormat.bits / 8) / inFormat.channels;

	return eNone;
}

static const char *
ConvertText( Gmd::Text *textConvert, const wchar_t *text, int encoding )
{
	if ( ! textConvert )
		return 0;
	try
	{
		*textConvert = text;
		return textConvert->Get( encoding );
	}
	catch ( ... )
	{
		return 0;
    }
}
#define ANSI( x ) ConvertText( textConvert, x, Gmd::Ansi )
#define UTF8( x ) ConvertText( textConvert, x, Gmd::Utf8 )

// Null terminate and add text to metadata
static void SetInfo( char *data, int len, Gap::Metadata &metadata,
					 const wchar_t *name )
{
	const int       max = 30 + 1;
	char            text[max];

	if ( len > max )
			len = max;

	memcpy( text, data, len );
	text[len] = 0;
	if ( *text )
	{
		// ID3v1 tag contains ANSI text, so metadata.SetText() cannot be used
		Gmd::Text	*mdtext = (Gmd::Text *)metadata.Get( name, true );
		if ( mdtext )
			mdtext->Set( text, Gmd::Ansi );
	}
}

Error AFile::ReadInfo( const wchar_t *name )
{
	if ( ! metadata )
		return eNone;

	if ( ! ANSI( name ) )
		return eMemory;

	/*
		 Unfortunately APE_INFO_TAG/CAPETag cannot be used through a DLL,
		 so there is no compatible way to read the information without
		 duplicating a whole lot of code.  If MACDll is ever updated to
		 provide a compiler independent method of reading information,
		 then insert code here.
	 */

/*	CAPETag *tag;

	tag = (CAPETag *)MacDecoder.GetInfo( decoder, APE_INFO_TAG, 0, 0 );
	if ( ! tag )
		return eRead;*/

	// The MAC interfaces does not support a unicode filename for GetMP3Info
	ID3_TAG	tag;
	if ( MacDecoder.GetTag( ANSI( name ), &tag ) != ERROR_SUCCESS )
		return eRead;

	SetInfo( tag.Artist, sizeof(tag.Artist), *metadata, Gmd::GWText_Author );
	SetInfo( tag.Album, sizeof(tag.Album), *metadata, Gmd::GWText_Album );
	SetInfo( tag.Title, sizeof(tag.Title), *metadata, Gmd::GWText_Title );
	SetInfo( tag.Comment, sizeof(tag.Comment), *metadata, Gmd::GWText_Description );
	SetInfo( tag.Year, sizeof(tag.Year), *metadata, Gmd::GWText_Date );

	if ( tag.Genre < GENRE_COUNT )
		metadata->SetText( Gmd::GWText_Genre, g_ID3Genre[tag.Genre] );

	if ( tag.Track )
	{
		wchar_t track[10];
		_itow( tag.Track, track, 10 );
		metadata->SetText( Gmd::GWText_TrackNumber, track );
	}

	return eNone;
}

#define ID(a, b, c, d) (((DWORD)(d) << 24) | ((DWORD)(c) << 16) | ((DWORD)(b) << 8) | (a))
#define IDNOTE ID( 'n', 'o', 't', 'e' )
#define IDLIST ID( 'L', 'I', 'S', 'T' )
#define IDADTL ID( 'a', 'd', 't', 'l' )
#define IDCUE  ID( 'c', 'u', 'e', ' ' )
#define IDLABL ID( 'l', 'a', 'b', 'l' )
#define IDDATA ID( 'd', 'a', 't', 'a' )

// RIFF Cue Point structure
struct CuePoint
{
	DWORD  name, position, chunk, chunkStart, blockStart, offset;
};

struct Chunk
{
	DWORD id, size, type;
};

/*
	Searches LIST adtl for note and labl chunks to extract name and
	description for the cue identified by 'name'.
 */
Error FindCueStrings( DWORD name, const unsigned char *adtl, DWORD adtlsize,
							 const char *&label, const char *&note )
{
	note = label = 0;

	DWORD i = 0;
	while ( i < adtlsize - 8 && (note == 0 || label == 0) )
	{
		Chunk	&c = *(Chunk*)(adtl + i);

		if ( c.size > adtlsize - i - 8 || c.size < 4 )
			return eCorrupt;

		if ( (c.id == IDLABL || c.id == IDNOTE )
			&& c.type == name && c.size > 4 )
		{
			char *text = (char *)adtl + i + 12;
			if ( c.id == IDLABL )
				label = text;
			else
				note = text;
		}
		// Seek to next even boundary
		i += c.size + 8 + (c.size & 1);
	}

	return eNone;
}

/*
	Reads cue points from RIFF Wave structured data.  Matches note and
	label string in other chunks to cue points.
 */
Error AFile::ReadCueChunks( const unsigned char *cue, DWORD cuesize,
					 const unsigned char *adtl, DWORD adtlsize )
{
	DWORD	count = *(DWORD*)cue;

	if ( count * sizeof(CuePoint) > cuesize )
		return eCorrupt;

	cue += 4;
	for ( DWORD n = 0; n < count; n++ )
	{
		CuePoint	&point = *(CuePoint*)cue;
		const char	*label, *note;
		double		position;
		Error		error;

		error = FindCueStrings( point.name, adtl, adtlsize, label, note );
		if ( error != eNone )
			return error;

		position = point.position / (double)inFormat.rate;
		AFile::cue->Add( label, note, position, Gmd::Ansi );
		cue += sizeof(CuePoint);
	}
	return eNone;
}

Error AFile::ReadCues( void )
{
	if ( ! cue )
		return eNone;

	DWORD			size;
	unsigned char	*buffer;

	// Get any wave chunks that might be in terminating data
	size = MacDecoder.GetInfo( decoder, APE_INFO_WAV_TERMINATING_BYTES, 0, 0 );

	if ( size <= 0 )
		return eNone;

	buffer = new unsigned char[size];
	if ( ! buffer )
		return eMemory;

	if ( MacDecoder.GetInfo( decoder, APE_INFO_WAV_TERMINATING_DATA,
							 (int)buffer, size ) != ERROR_SUCCESS )
	{
		delete [] buffer;
		return eRead;
	}

	// Search for 'cue ' and 'LIST' 'adtl' chunks
	unsigned char	*cue, *adtl;
	DWORD			cuesize = 0, adtlsize = 0;

	for ( DWORD n = 0; n < size - 12; n++ )
	{
		Chunk	&c = *(Chunk*)(buffer + n);

		if ( c.size < 4 || c.size > size - n - 8 )
			continue;

		if ( c.id == IDCUE )
		{
			cue = buffer + n + 8;
			cuesize = c.size;
			n += c.size + 7;	// Skip rest of chunk
			if ( adtlsize )		// Done if atdl found too
				break;
		}
		else if ( c.id == IDLIST
				&& c.type == IDADTL )
		{
			adtl = buffer + n + 12;
			adtlsize = c.size;
			n += c.size + 7;	// Skip rest of chunk
			if ( cuesize )		// Done if cues found too
				break;
		}
	}

	Error	error = eNone;
	if ( cuesize > sizeof(CuePoint) + 4 && adtlsize > 4 )
		error = ReadCueChunks( cue, cuesize, adtl, adtlsize );

	delete [] buffer;

	return error;
}

// Read audio data from file and convert to native 'audio' type
int gdecl AFile::Read( audio *dest, int samples )
{
	if ( ! decoder )
		return -eForbidden;

	int read;
	if ( MacDecoder.GetData( decoder, (char *)dest, samples, &read ) != 0 )
		return -eRead;

	if ( read > samples )
		return -eBounds;

	samples = read;
	read *= inFormat.channels;

	// Move pointers to end of buffer and setup conversion
	int		bits = inFormat.bits,
			bps = (bits / 8),	// Bytes per sample
			zero = (bits == 8) ? 128 : 0,
			shift = (32 - bits),
			mask = (1 << bits) - 1;
	char	*data = (char *)dest + read * bps;
	audio	scale = (audio)(1 / (double)((0x7FFFFF00 >> shift) << shift));

	dest += read;

	// Move each sample to final destination before conversion, then convert it
	while ( read-- > 0 )
	{
		data -= bps;
		--dest;

		// Move & pad to 4 bytes (warning: little endian, non-portable code)
		int32	tmp = *(int32*)data & mask;

		// Convert (adjust zero, make signed 32 bit, normalize)
		*dest = ((tmp - zero) << shift) * scale;
	}

	return samples;
}

// Seek to sample position within the input stream
Error gdecl AFile::Seek( int64 position )
{
	// Seek only applies to input.  If an input file is not open, return error
	if ( ! decoder )
		return eForbidden;

	if ( MacDecoder.Seek( decoder, (int)position ) != 0 )
		return eSeek;

	return eNone;
}

// Close input file
Error gdecl AFile::Close( void )
{
	if ( ! decoder )
		return eForbidden;

	length = 0;
	MacDecoder.Destroy( decoder );
	decoder = 0;

	// Reset format to default
	inFormat = AFormat();

	return eNone;
}

//---------------------------------------------------------------------------
/*
	Write member functions
 */

// Begin writing a file
Error gdecl AFile::Begin( const wchar_t *name, const Format &f )
{
	if ( encoder )
		return eForbidden;

	if ( ! name )
		return eParameter;

	const AFormat	*format;

	// Make sure the format object is appropriate and belongs to this plug-in
	if ( f.Type() != TableData.name )
		return eFormat;
	format = dynamic_cast<const AFormat *>( &f );
	if ( ! format || format->rate == 0 )
		return eFormat;

	int				retval, level;
	WAVEFORMATEX	wfe;

	outFormat = *format;

	// Create a new encoder
	encoder = MacEncoder.Create( &retval );
	if ( ! encoder )
		return eMemory;

	wfe.cbSize = 0;
	wfe.wFormatTag = WAVE_FORMAT_PCM;
	wfe.nSamplesPerSec = outFormat.rate;
	wfe.wBitsPerSample = (WORD)outFormat.bits;
	wfe.nChannels = (WORD)outFormat.channels;
	wfe.nBlockAlign = (WORD)((outFormat.bits / 8) * outFormat.channels);
	wfe.nAvgBytesPerSec = wfe.nBlockAlign * outFormat.rate;
	if ( outFormat.level == 0 )
		level = COMPRESSION_LEVEL_HIGH;
	else
		level = outFormat.level * 1000;

	retval = MacEncoder.Start( encoder, name, &wfe, MAX_AUDIO_BYTES_UNKNOWN,
								level, 0, CREATE_WAV_HEADER_ON_DECOMPRESSION );

	try { outName = new wchar_t[wcslen( name ) + 1]; } catch ( ... ) {}

	if ( retval != 0 || ! outName )
	{
		MacEncoder.Destroy( encoder );
		encoder = 0;
		delete [] outName;
		outName = 0;
		return eCreate;
	}
	else
		wcscpy( outName, name );

	return eNone;
}

enum { Author, Album, Title, Description, Genre, Date, TrackNumber };
static const wchar_t *MetaNames[] =
{
	Gmd::GWText_Author,
	Gmd::GWText_Album,
	Gmd::GWText_Title,
	Gmd::GWText_Description,
	Gmd::GWText_Genre,
	Gmd::GWText_Date,
	Gmd::GWText_TrackNumber
};
const int TextTotal = sizeof(MetaNames) / sizeof(*MetaNames);

// Write text metadata
Error gdecl AFile::WriteInfo( void )
{
	if ( ! metadata )
		return eNone;

	if ( ! ANSI( outName ) )
		return eMemory;

	Error		error = eNone;
	Gmd::Text	*text[TextTotal];
	int 		count = 0;

	try
	{
		/*
			The ANSI and UTF8 macros cannot be used because the same buffer is
			reused each	call.  So we need to get the Gmd::Text object for every
			item written to tag because it has to be converted from unicode
			to UTF8 in its own buffer for the TagFileSimple call.
		 */
		for ( int n = 0; n < TextTotal; n++ )
		{
			text[n] = (Gmd::Text *)metadata->Get( MetaNames[n] );
			if ( text[n] && (*text[n]) && *(*text[n]) )
				++count;
		}

		if ( count )
		{
			// Must use simple tag because other tag mechanism does not work in DLL
			// TagFileSimple appears to expect UTF-8 strings and ANSI filename
			MacEncoder.TagFileSimple( ANSI( outName ),
				text[Author]->Get( Gmd::Utf8 ),
				text[Album]->Get( Gmd::Utf8 ),
				text[Title]->Get( Gmd::Utf8 ),
				text[Description]->Get( Gmd::Utf8 ),
				text[Genre]->Get( Gmd::Utf8 ),
				text[Date]->Get( Gmd::Utf8 ),
				text[TrackNumber]->Get( Gmd::Utf8 ) );
		}
	}
	catch ( ... )
	{
		error = eMemory;
	}

	// text[..] does not need to be destroyed

	return error;
}

// Writes a 'note' or 'adtl' chunk to adtl memory block
int WriteAdtl( const char *text, DWORD id, int n, unsigned char *adtl )
{
	int	size = 0;

	if ( text && *text )
	{
		Chunk		&c = *(Chunk *)adtl;

		c.id = id;
		size = 4 + strlen( text ) + 1; // name, string length + zero
		c.size = size;
		c.type = n;
		memcpy( adtl + 12, text, size - 4 );
		size += 8;
		// Even padding
		if ( size & 1 )
		{
			*(adtl + size) = 0;
			++size;
		}
	}

	return size;
}

// Process all cues into a block of memory for writing as terminating data
unsigned char *AFile::WriteCues( int &size )
{
	size = 0;
	if ( ! cue )
		return 0;

	// Calculate total size of 'cue ' and 'LIST' 'adtl' chunks
	int count = cue->Count(), adtlsize = 0, cuesize = 0;
	for ( int n = 0; n < count; n++ )
	{
		cuesize += sizeof(CuePoint);

		const Gmd::Cue	&c = *(*cue)[n];

		// Add room for 'note' or 'labl', size, name, and string len
		if ( c.description && c.description[0] )
		{
			int size = strlen( ANSI( c.description ) ) + 1 + 12;
			adtlsize += size + (size & 1);	// Maintain even alignment
		}
		if ( c.name && c.name[0] )
		{
			int size = strlen( ANSI( c.name )  ) + 1 + 12;
			adtlsize += size + (size & 1);	// Maintain even alignment
		}
	}

	if ( ! cuesize )
		return 0;

	cuesize += 12;		// Add room for 'cue ', size, count
	adtlsize += 12;		// Add room for 'LIST', size, 'adtl'

	size = cuesize + adtlsize;

	unsigned char *buffer = new unsigned char[size];

	unsigned char 	*cue = buffer, *adtl = buffer + cuesize;
	Chunk			*chunk;

	chunk = (Chunk *)cue;
	chunk->id = IDCUE;
	chunk->size = cuesize - 8;
	chunk->type = count;
	cue += 12;

	chunk = (Chunk *)adtl;
	chunk->id = IDLIST;
	chunk->size = adtlsize - 8;
	chunk->type = IDADTL;
	adtl += 12;

	for ( int n = 0; n < count; n++ )
	{
		CuePoint		&point = *(CuePoint*)cue;
		const Gmd::Cue	&c = *(*AFile::cue)[n];

		point.name = n;
		point.chunkStart = 0;
		point.chunk = IDDATA;
		point.blockStart = 0;
		point.position = (DWORD)(c.position * outFormat.rate + 0.5);
		point.offset = point.position;
		cue += sizeof(CuePoint);

		adtl += WriteAdtl( ANSI( c.name ), IDLABL, n, adtl );
		adtl += WriteAdtl( ANSI( c.description ), IDNOTE, n, adtl );
	}

	return buffer;
}

// Write audio data to file in the correct format - either 8, 16, or 24 bits
Error gdecl AFile::Write( const audio *data, int samples )
{
	if ( ! encoder )
		return eForbidden;

	int		buffer[MaxSize * 2];
	int		channels = outFormat.channels,
			bits = outFormat.bits,
			zero = (bits == 8) ? 128 : 0,
			max = 1 << (bits - 1),
			bps = bits / 8;
	audio	scale = max - 1;

	while ( samples > 0 )
	{
		int		count = samples > MaxSize ? MaxSize : samples,
				values = count * channels;
		char	*dest = (char *)buffer;

		while ( values-- > 0 )
		{
			/*
				Limit conversion and hack retarded C rounding rules
				where 5.9999999999... becomes 5.
				Add/subtract small value to compensate for rounding problem
				so that the magnitude is the closest integer.
			 */
			int	quantized;
			if ( *data >= 1.0 )
				quantized = max - 1;
			else if ( *data < -1.0 )
				quantized = (int)-max;
			else if ( *data >= 0 )
				quantized = (int)(*data * scale + 0.5);
			else
				quantized = (int)(*data * scale - 0.5);

			quantized += zero;
			// warning: little endian, non-portable code
			*(int *)dest = quantized;

			dest += bps;
			++data;
		}

		int bytes = count * channels * bps;
		if ( MacEncoder.AddData( encoder, (unsigned char *)buffer, bytes )
				!= ERROR_SUCCESS )
			return eWrite;

		samples -= count;
	}

	return eNone;
}

// Close output file
Error gdecl AFile::End( void )
{
	if ( ! encoder )
		return eForbidden;

	unsigned char	*cues;
	int				cuesize;

	// Store cues into a block of memory
	cues = WriteCues( cuesize );

	int retval = MacEncoder.Finish( encoder, cues, cuesize, cuesize );

	delete [] cues;

	MacEncoder.Destroy( encoder );
	encoder = 0;

	WriteInfo();
	delete [] outName;
	outName = 0;

	return retval == 0 ? eNone : eClose;
}

//---------------------------------------------------------------------
/*
	APE FormatList class

	To create this list, derive a class from FormatList and
	initialize an array of AFormat objects with	attributes.

	Note that a pointer to a static structure MUST NOT be
	returned since the application may modify the Format
	objects to set rates, channels, or bitrates.
 */

// 2 channels * 5 quality settings * 3 bit depths (8, 16, 24)
const int FormatItems = 2 * 5 * 3;

class AList : public FormatList
{
	AFormat	format[FormatItems];

public:
	AList( void );
	Format * gdecl operator[]( int i )
	{
		if ( i >= 0 && i < count )
			return format + i;
		else
			return 0;
	}
};

AList::AList( void )
{
	count = FormatItems;
	for ( int n = 0; n < count; n++ )
	{
		format[n].channels = (n & 1) + 1;
		format[n].level = ((n >> 1) % 5) + 1;
		format[n].bits = (n / 10) * 8 + 8;

		// Set 16-bit, level 4 format as default for mono/stereo
		if ( format[n].bits == 16 && format[n].level == 4 )
			format[n].flags |= Format::fDefault;
	}
}

// Create AList and pass it back to host program
FormatList * gdecl AFile::Formats( void )
{
	// Host program will Destroy() this object
	return new AList;
}

Format * gdecl AFile::GetFormat( void )
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
	if ( name[1] == ':' || name[0] == '\\' || name[0] == '/' || name[0] == '.' )
	{
		/*
			Check the beginning of the file for the "MAC " ID.
		 */

		char    buffer[1024*64];
		FILE	*fp;
		bool	markerFound = false;

		fp = _wfopen( name, L"rb" );

		if ( fp )
		{
			int		count = fread( buffer, 1, sizeof(buffer), fp );
			char	*p = buffer;

			while ( count-- > 4 && ! markerFound )
			{
				markerFound = memcmp( p, "MAC ", 4 ) == 0;
				++p;
			}

			fclose( fp );
		}

		if ( ! markerFound )
			return 0;

		// The "MAC " marker was found, make sure it really is a MAC file
		AFile	afile;

		if ( afile.Open( name, 0 ) == eNone )
		{
			afile.Close();
			return new AFile;
		}
	}
	else if ( wcscmp( name, TableData.name ) == 0 )
		return new AFile;

	return 0;
};

//---------------------------------------------------------------------------


