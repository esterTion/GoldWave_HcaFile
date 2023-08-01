/*
	FLACFile Copyright (c) 2003-2009 GoldWave Inc.

	Demonstrates how to create an FLAC (http://flac.sourceforge.net)
	file plug-in for GoldWave.

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
#define GWINCLUDESTRINGS
#include "gwmetadata.h"
#include "all.h"


using namespace Gap;
using namespace Gerr;

//---------------------------------------------------------------------------

const wchar_t	Extension[] = L"flac";
/*
	Table listing all files types supported in this module (just one).
	Each element provides the type name, extension(s), and ability flags.
	The type name must not contain : / \ . leading characters or -
	see AudioCreate() for reason.
 */
Table TableData =
{
	L"FLAC: Lossless Codec",
	aRead | aWrite | aMetaText | aMetaCue | aMetaPicture,
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

#ifdef _WIN32

// Windows DLL entry point
BOOL WINAPI DllMain( HINSTANCE module, DWORD, LPVOID )
{
#ifdef GWFIXSEARCHPATH
	extern SetModulePath( HINSTANCE );
	SetModulePath( module );
#endif
	return 1;
}

#endif

// This function is called by the application to get the table
AudioInterfaceDll( void )
{
	return &InterfaceData;
}

//---------------------------------------------------------------------------
/*
	Structure for METADATA_BLOCK_APPLICATION for cue points.
	The block has the ID 'Cues' followed by a 32 bit value containing
	the number of cue points, followed by each cue point.  Cue points
	contain an offset, a name, and a description.

	Cues Block:
	-----------
	<32>	ID = 'Cues'
	<32>	Number of cue points in the block.
	Cue Point+


	Cue Point:
	---------------
	<32>	Length of this cue point data in bytes (8 + 4 + n1 + 4 + n2)
	<64>	Cue point position given as a sample offset from the beginning
			of file.
	<32>	Length of name in bytes (n1). Can be zero.
	<n1*8>	Name string, zero terminated.  Not present if n1 is zero.
	<32>	Length of description in bytes (n2).  Can be zero.
	<n2*8>	Description string, zero terminated.  Not present if n2 is zero.

	Example:
	--------
	'Cues' 2
		26     0  10  "Beginning\0"* 0
		54	1234  12  "Sample 1234\0" 26 "Cue point for sample 1234\0"

	* "\0" is shown only to emphasize zero (null) termination of strings.
*/
const char FLAC__Cues[] = "Cues";

//---------------------------------------------------------------------------
/*
	A derived Format class must be created to hold format specific
	information, such as bitrate, channels, bits, sampling rate, etc.

	For FLAC, the bitrate is not known.  The plug-in supports 4
	different compression levels.
 */

const int 		MaxRate = FLAC__MAX_SAMPLE_RATE,
				MaxSize = FLAC__MAX_BLOCK_SIZE;
const wchar_t	*Level[] =
{
	L"Low/Fast",		// Lowest compression, but fast
	L"Medium",
	L"High",
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
		flags = fAnyRate | fVBR | fSequential | fBeyond4GB;
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
	Error gdecl SetRate( int r );
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

		swprintf( description, L"%ls%d bit%s, %ls", leveltext, bits, ratetext,
					channels == 1 ? L"mono" : L"stereo" );

		return description;
	}
	const wchar_t * gdecl Extension( void ) const { return ::Extension; }
};

// Make sure rate is in the streamable subset
static bool StreamableRate( int rate )
{
	const int StreamableRates[] =
	{
		8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
	};
	const int rates = sizeof(StreamableRates) / sizeof(StreamableRates[0]);

	for ( int n = 0; n < rates; n++ )
	{
		if ( rate == StreamableRates[n] )
			return true;
	}
	return false;
}

Error gdecl AFormat::SetRate( int r )
{
#ifdef STREAMABLE_SUBSET
	if ( StreamableRate( r ) )
	{
		rate = r;
		return eNone;
	}
	return eUnsupported;
#else
	if ( rate < 0 || rate > 192000 )
		return eUnsupported;

	rate = r;
	return eNone;
#endif
}

//---------------------------------------------------------------------------
/*
	To create the File Format plug-in class:
		- Derive a class from the AudioFile base class.
		- Override all required or pure virtual functions.
 */

class AFile : public AudioFile
{
	// Input data
	AFormat				inFormat;			// Input file format
	FILE				*inFile;
	int64				length;				// Number of samples in file
	FLAC__StreamDecoder	*decoder;
	Error				inStatus;
	audio				buffer[MaxSize * 2];
	int					top, stored;

	// Output data
	AFormat					outFormat;			// Output file format
	FILE					*outFile;
	FLAC__StreamEncoder		*encoder;
	FLAC__StreamMetadata	**flacMetadata;		// Text, Cues, and Pictures
	int						flacMetadataN;		// Number of metadata objects

	FLAC__StreamMetadata	**AddFlacMetadata( void );

	Gmd::CueList			*cue;
	Gmd::Text				*textConvert;

	Error					WriteInfo( void );
	Error 					WriteCues( void );
	Error 					WritePictures( void );
	Error 					WritePadding( void );

	// FLAC callback functions
	static FLAC__StreamDecoderReadStatus ReadCallback(
					const FLAC__StreamDecoder *decoder,
					FLAC__byte buffer[], size_t *bytes, void *client_data );
	static FLAC__StreamDecoderSeekStatus SeekCallback(
					const FLAC__StreamDecoder *decoder,
					FLAC__uint64 absolute_byte_offset, void *client_data );
	static FLAC__StreamDecoderTellStatus TellCallback(
					const FLAC__StreamDecoder *decoder,
					FLAC__uint64 *absolute_byte_offset, void *client_data );
	static FLAC__StreamDecoderLengthStatus LengthCallback(
					const FLAC__StreamDecoder *decoder,
					FLAC__uint64 *stream_length, void *client_data );
	static FLAC__bool EofCallback(
					const FLAC__StreamDecoder *decoder, void *client_data );
	static FLAC__StreamDecoderWriteStatus WriteCallback(
					const FLAC__StreamDecoder *decoder,
					const FLAC__Frame *frame,
					const FLAC__int32 * const buffer[], void *client_data );
	static void MetadataCallback( const FLAC__StreamDecoder *decoder,
					const FLAC__StreamMetadata *metadata, void *client_data );
	static void ErrorCallback( const FLAC__StreamDecoder *decoder,
					FLAC__StreamDecoderErrorStatus, void *client_data );

	static FLAC__StreamEncoderWriteStatus outWriteCallback(
					const FLAC__StreamEncoder *encoder,
					const FLAC__byte buffer[], size_t bytes, unsigned samples,
					unsigned current_frame, void *client_data );
	static FLAC__StreamEncoderSeekStatus outSeekCallback(
					const FLAC__StreamEncoder *encoder,
					FLAC__uint64 absolute_byte_offset, void *client_data );
	static FLAC__StreamEncoderTellStatus outTellCallback(
					const FLAC__StreamEncoder *encoder,
					FLAC__uint64 *absolute_byte_offset, void *client_data );

public:
	AFile( void )
	{
		length = 0;
		inStatus = eNone;
		decoder = 0;
		encoder = 0;
		flacMetadataN = 0;
		flacMetadata = 0;
		inFile = outFile = 0;
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
			/*
				Replace text conversion object to use new Metadata object
				(not really necessary, but just in case a module unloads).
			 */
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
/*
	Create all callback functions used by FLAC.
 */

FLAC__StreamDecoderReadStatus
AFile::ReadCallback( const FLAC__StreamDecoder *,
					 FLAC__byte buffer[], size_t *bytes, void *client_data )
{
	FILE *file = ((AFile *)client_data)->inFile;
	if( *bytes > 0 )
	{
		*bytes = fread( buffer, sizeof(FLAC__byte), *bytes, file );
		if( ferror( file ) )
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		else if( *bytes == 0 )
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		else
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
   }
	return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
}

FLAC__StreamDecoderSeekStatus
AFile::SeekCallback( const FLAC__StreamDecoder *,
					 FLAC__uint64 absolute_byte_offset, void *client_data )
{
	FILE *file = ((AFile *)client_data)->inFile;
	if( _fseeki64( file, absolute_byte_offset, SEEK_SET ) < 0 )
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

FLAC__StreamDecoderTellStatus
AFile::TellCallback( const FLAC__StreamDecoder *,
					 FLAC__uint64 *absolute_byte_offset, void *client_data )
{
   FILE 	*file = ((AFile *)client_data)->inFile;
   __int64	pos;
	if( (pos = _ftelli64( file )) < 0 )
		return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;

	*absolute_byte_offset = (FLAC__uint64)pos;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

FLAC__StreamDecoderLengthStatus
AFile::LengthCallback(
					const FLAC__StreamDecoder *,
					FLAC__uint64 *stream_length, void *client_data )
{
	FILE 	*file = ((AFile *)client_data)->inFile;
	__int64	pos = _ftelli64( file ), len;
	_fseeki64( file, 0, SEEK_END );
	len = _ftelli64( file );
	if ( len < 0 )
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;

	_fseeki64( file, pos, SEEK_SET );
	*stream_length = (FLAC__uint64)len;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

FLAC__bool
AFile::EofCallback( const FLAC__StreamDecoder *, void *client_data )
{
	FILE 	*file = ((AFile *)client_data)->inFile;
	return feof( file ) ? true : false;
}

FLAC__StreamDecoderWriteStatus
AFile::WriteCallback( const FLAC__StreamDecoder */*decoder*/,
					  const FLAC__Frame *frame,
					  const FLAC__int32 * const buffer[],
					  void *client_data )
{
	AFile	*afile = (AFile *)client_data;

	afile->stored = frame->header.blocksize;
	afile->top = 0;

	audio	scale = 1 / (audio)((1 << (afile->inFormat.bits - 1)) - 1);
	int		channels = afile->inFormat.channels;

	// Convert 8, 16, or 24 bits to audio +/- 1.0 range
	for ( int c = 0; c < channels; c++ )
	{
		audio	*dest = afile->buffer + c;

		for ( int n = 0; n < afile->stored; n++, dest += channels )
			*dest = (audio)(buffer[c][n] * scale);
	}

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

FLAC__StreamEncoderWriteStatus
AFile::outWriteCallback( const FLAC__StreamEncoder *,
						 const FLAC__byte buffer[], size_t bytes,
						 unsigned /*samples*/, unsigned /*current_frame*/,
						 void *client_data )
{
	FILE *file = ((AFile *)client_data)->outFile;

	if ( fwrite( buffer, 1, bytes, file ) != bytes )
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

FLAC__StreamEncoderSeekStatus
AFile::outSeekCallback( const FLAC__StreamEncoder *,
						FLAC__uint64 absolute_byte_offset, void *client_data )
{
	FILE *file = ((AFile *)client_data)->outFile;
	if( _fseeki64( file, absolute_byte_offset, SEEK_SET ) < 0 )
		return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
	return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

FLAC__StreamEncoderTellStatus
AFile::outTellCallback( const FLAC__StreamEncoder *,
						FLAC__uint64 *absolute_byte_offset, void *client_data )
{
	FILE 	*file = ((AFile *)client_data)->outFile;
	__int64	pos;
	if( (pos = _ftelli64( file )) < 0 )
		return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
	*absolute_byte_offset = (FLAC__uint64)pos;
	return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static const char *VorbisInfo[] =
{
	"ALBUM",
	"ARTIST",
	"COPYRIGHT",
	"DESCRIPTION",
	"DATE",
	"GENRE",
	"TITLE",
	"URL",
	"SOFTWARE",
	"TRACKNUMBER",
	"ISRC",
	"ALBUMARTIST",
	"COMPOSER",
};

static const wchar_t *MetaName[] =
{
	Gmd::GWText_Album,
	Gmd::GWText_Author,
	Gmd::GWText_Copyright,
	Gmd::GWText_Description,
	Gmd::GWText_Date,
	Gmd::GWText_Genre,
	Gmd::GWText_Title,
	Gmd::GWText_URL,
	Gmd::GWText_Tool,
	Gmd::GWText_TrackNumber,
	Gmd::GWText_ISRC,
	Gmd::GWText_AlbumArtist,
	Gmd::GWText_Composer
};

static_assert( sizeof(VorbisInfo) == sizeof(MetaName),
				"Information item missing" );

const int TextTotal = sizeof(VorbisInfo) / sizeof(*VorbisInfo);

/*
	Use the metadata callback to obtain format information, file text
	information (vorbis comments), and cue point information.
 */
void
AFile::MetadataCallback( const FLAC__StreamDecoder */*decoder*/,
						 const FLAC__StreamMetadata *metadata,
						 void *client_data )
{
	AFile	*afile = (AFile *)client_data;

	if ( metadata->type == FLAC__METADATA_TYPE_STREAMINFO )
	{
		// Get format details of file
		AFormat	&aformat = afile->inFormat;

		// Seems quadword padding is required for libFLAC
		aformat.bits = metadata->data.stream_info.bits_per_sample;
		aformat.rate = metadata->data.stream_info.sample_rate;
		aformat.channels = metadata->data.stream_info.channels;
		afile->length = metadata->data.stream_info.total_samples;
		if ( ! afile->length )
		{
			aformat.flags |= Format::fUnsized;
			afile->length = 1;
		}
	}
	else if ( metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT )
	{
		// Make sure info object was assigned
		if ( ! afile->metadata )
			return;

		// Read all comments/info supported by this plug-in
		int count = metadata->data.vorbis_comment.num_comments;
		FLAC__StreamMetadata_VorbisComment_Entry *comment;

		comment = metadata->data.vorbis_comment.comments;
		for ( int n = 0; n < count; n++ )
		{
			char	*ptr;

			try
			{
				ptr = new char [comment[n].length + 1];
			}
			catch( ... )
			{
				afile->inStatus = eMemory;
				return;
			}

			// Must have null termination
			memcpy( ptr, comment[n].entry, comment[n].length );
			ptr[comment[n].length] = 0;

			char *equal = strchr( ptr, '=' );

			// Must have an equal sign
			if ( equal )
			{
				unsigned taglen = equal - ptr;
				// Search for matching header= identifier string
				for ( int n = 0; n < TextTotal; n++ )
				{
					if ( strlen( VorbisInfo[n] ) == taglen
						 && strncmpi( VorbisInfo[n], ptr, taglen ) == 0 )
					{
						/*
							Strip header= field, convert to unicode and
							add to metadata.
						 */
						Gmd::Text *text = (Gmd::Text *)afile->metadata->Get(
											 MetaName[n], true );
						if ( text )
							text->Set( equal + 1, Gmd::Utf8 );
						break;
					}
				}
			}
			delete [] ptr;
		}

	}
	else if ( metadata->type == FLAC__METADATA_TYPE_APPLICATION )
	{
		// Make sure this is a "Cues" application object
		if ( memcmp( metadata->data.application.id,
					 FLAC__Cues, sizeof(FLAC__int32) ) != 0 )
			return;

		// Make sure cue object was assigned and there is valid metadata
		if ( ! afile->cue || metadata->length < sizeof(FLAC__int32) * 2 )
			return;

		Gmd::CueList	&list = *afile->cue;
		FLAC__byte		*data = metadata->data.application.data,
						*current, *end;
		int				count = *(FLAC__int32*)data;

		current = data + sizeof(FLAC__int32);	// Skip count
		end = data + metadata->length - sizeof(FLAC__int32);
		for ( int n = 0; n < count && current < end; n++ )
		{
			int			bytes;
			double		position;
			const char	*name, *description;

			// Check length
			bytes = *(FLAC__int32*)current;
			if ( current + bytes > end )
				break; // Block is corrupt, abort
			current += sizeof(FLAC__int32);

			// Get sample offset (convert to time in Open() when rate is known)
			if ( current + sizeof(FLAC__int64) > end )
				break;
			position = *(FLAC__int64*)current;
			current += sizeof(FLAC__int64);

			// Get name length and string
			if ( current + sizeof(FLAC__int32) > end )
				break;	// Corrupt
			bytes = *(FLAC__int32*)current;
			current += sizeof(FLAC__int32);
			if ( bytes )
			{
				// Check bounds and make sure string is zero terminated
				if ( current + bytes > end || current[bytes - 1] != 0 )
					break;	// Corrupt
				name = (const char *)current;
				current += bytes;
			}
			else
				name = "";	// GoldWave requires non-null name

			// Get description length and string
			if ( current + sizeof(FLAC__int32) > end )
				break;	// Corrupt
			bytes = *(FLAC__int32*)current;
			current += sizeof(FLAC__int32);
			if ( bytes )
			{
				// Check bounds and make sure string is zero terminated
				if ( current + bytes > end || current[bytes - 1] != 0 )
					break;	// Corrupt
				description = (const char *)current;
				current += bytes;
			}
			else
				description = 0;	// A null description is fine

			list.Add( name, description, position, Gmd::Utf8 );
		}
	}
	else if ( metadata->type == FLAC__METADATA_TYPE_PICTURE )
	{
		// Make sure info object was assigned
		if ( ! afile->metadata )
			return;

		Gmd::PictureList *list = (Gmd::PictureList *)afile->metadata->Get(
									Gmd::GWPictureList, true );
		if ( ! list )
			return;

		Gmd::Picture	picture;
		Gmd::Text		*format, *description;

		// Create conversion objects to hold unicode versions of the strings
		format = (Gmd::Text *)afile->metadata->New( Gmd::GWText );
		if ( ! format )
			return;

		description = (Gmd::Text *)afile->metadata->New( Gmd::GWText );
		if ( ! description )
		{
			format->Destroy();
			return;
		}
		format->Set( metadata->data.picture.mime_type, Gmd::Ansi );
		description->Set( (char *)metadata->data.picture.description, Gmd::Utf8 );

		// Assign all values to the picture structure
		picture.format = *format;
		picture.description = *description;
		picture.type = metadata->data.picture.type;
		picture.size = metadata->data.picture.data_length;
		picture.picture = metadata->data.picture.data;

		// Add it to the picture list
		list->Add( picture );

		// Release text conversion objects
		format->Destroy();
		description->Destroy();
	}
}

void
AFile::ErrorCallback( const FLAC__StreamDecoder */*decoder*/,
					  FLAC__StreamDecoderErrorStatus /*status*/,
					  void *client_data )
{
	AFile	*afile = (AFile *)client_data;

	afile->inStatus = eCorrupt;
}

//---------------------------------------------------------------------------
// Read member functions

// Open the given file and make sure it is a FLAC format file
Error gdecl AFile::Open( const wchar_t *name, const Format * )
{
	if ( ! name )
		return eOpen;

	if ( decoder )
		return eForbidden;

	inStatus = eNone;
	decoder = FLAC__stream_decoder_new();
	if ( ! decoder )
		return eOpen;

	if ( ! FLAC__stream_decoder_set_metadata_respond( decoder,
					FLAC__METADATA_TYPE_STREAMINFO ) )
	{
		Close();
		return eOpen;
	}

	// Open file (extremely important to remember to close it later)
	inFile = _wfopen( name, L"rb" );
	if ( ! inFile )
	{
		Close();
		return eOpen;
	}
	
	// Support info.  Continue even if this does not succeed
	if ( metadata )
	{
		FLAC__stream_decoder_set_metadata_respond( decoder,
			FLAC__METADATA_TYPE_VORBIS_COMMENT );
		FLAC__stream_decoder_set_metadata_respond( decoder,
			FLAC__METADATA_TYPE_PICTURE );
	}

	// Support cue points
	if ( cue )
		FLAC__stream_decoder_set_metadata_respond( decoder,
			FLAC__METADATA_TYPE_APPLICATION );

	/*
		Initialize decoder.  Since unicode filenames are being used, the
		FLAC__stream_decoder_init_stream function must be used.
		FLAC__stream_decoder_init_FILE cannot be used because the file pointer
		created by CodeGear's _wfopen is not guaranteed to be compatible with
		Microsoft's.
	 */
	FLAC__StreamDecoderInitStatus state;
	state =	FLAC__stream_decoder_init_stream( decoder,
					ReadCallback, SeekCallback, TellCallback,
					LengthCallback, EofCallback,
					WriteCallback, MetadataCallback, ErrorCallback, this );

	switch ( state )
	{
		case FLAC__STREAM_DECODER_INIT_STATUS_OK:
			break;	// No change in status

		case FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR:
			inStatus = eMemory;
			break;

		default:
			inStatus = eOpen;
			break;
	}

	// Continue even if this does not succeed
	FLAC__stream_decoder_process_until_end_of_metadata( decoder );

	if ( inStatus == eNone )
	{
		if ( inFormat.bits == 0 || inFormat.rate == 0 || inFormat.channels == 0 )
			inStatus = eOpen;
		else if ( (inFormat.bits != 8 && inFormat.bits != 16 && inFormat.bits != 24)
					|| (inFormat.channels > 2 || inFormat.channels < 1)
					|| (inFormat.rate > MaxRate || inFormat.rate < 1000) )
			inStatus = eCorrupt;
	}

	if ( inStatus != eNone )
		Close();

	// Setup circular buffer for storing decoded audio during callback
	top = 0;
	stored = 0;

	// Convert cue point sample offsets to time now that rate is known
	if ( cue )
	{
		for ( int n = 0; n < cue->Count(); n++ )
		{
			double	time = (*cue)[n]->position / inFormat.rate;
			cue->Move( n, time );
		}
	}

	return inStatus;
}

// Read audio data from file and convert to native 'audio' type
int gdecl AFile::Read( audio *dest, int samples )
{
	if ( ! decoder  )
		return -eForbidden;

	if ( inStatus != eNone )
		return -inStatus;

	int total = samples;
	while ( samples )
	{
		if ( stored )
		{
			int		count = stored > samples ? samples : stored,
					bps = sizeof(audio) * inFormat.channels;

			samples -= count;
			stored -= count;
			memcpy( dest, buffer + top * inFormat.channels, count * bps );
			dest += count * inFormat.channels;
			top += count;
		}
		else
		{
			if ( ! FLAC__stream_decoder_process_single( decoder ) )
			{
				FLAC__StreamDecoderState state;
				state = FLAC__stream_decoder_get_state( decoder );
				if ( state != FLAC__STREAM_DECODER_END_OF_STREAM )
				{
					if ( state == FLAC__STREAM_DECODER_MEMORY_ALLOCATION_ERROR )
						return -eMemory;
					else
						return -eRead;
				}
			}
			if ( ! stored )	// EOF?
				break;
			else if ( inStatus == eCorrupt )
			{
				// If decoded data is available, ignore error for now.
				// Might just be glitch in the stream.
				inStatus = eNone;
			}
		}
	}

	return total - samples;
}

// Seek to sample position within the input stream
Error gdecl AFile::Seek( int64 position )
{
	// Seek only applies to input.  If an input file is not open, return error
	if ( ! decoder )
		return eForbidden;

	// Bug: This freezes/deadlocks on small mono FLAC files (bug in libFLAC?)
	if ( ! FLAC__stream_decoder_seek_absolute( decoder, position ) )
		return eSeek;

	// Clear any stored samples and read directly from file
	stored = 0;

	return eNone;
}

// Close input file
Error gdecl AFile::Close( void )
{
	if ( ! decoder )
		return eForbidden;

	length = 0;
	FLAC__stream_decoder_delete( decoder );
	decoder = 0;

	if ( inFile )
	{
		fclose( inFile );
		inFile = 0;
	}
	
	// Reset format to default
	inFormat = AFormat();

	return eNone;
}

//---------------------------------------------------------------------------
// Converts AFormat "Level" to encoder parameters

bool SetEncodingFormat( FLAC__StreamEncoder *encoder, const AFormat &format )
{
	// Assume level 5
	bool		do_exhaustive_model_search = false,
				do_escape_coding = false,
				do_mid_side_stereo = format.channels > 1,
				loose_mid_side_stereo = false;
	unsigned    blocksize = 4608,
				max_lpc_order = 8,
				qlp_coeff_precision = 0,
				min_residual_partition_order = 3,
				max_residual_partition_order = 3,
				rice_parameter_search_dist = 0;

	switch( format.level )
	{
		case 1:	// -0
			do_mid_side_stereo = false;
			blocksize = 1152;
			max_lpc_order = 0;
			min_residual_partition_order = 2;
			max_residual_partition_order = 2;
			break;

		case 2:	// -3
			do_mid_side_stereo = false;
			blocksize = 1152;
			max_lpc_order = 6;
			break;

		case 4:	// -8
			do_exhaustive_model_search = true;
			min_residual_partition_order = 0;
			max_residual_partition_order = 6;
			max_lpc_order = 12;

		/*
			If level is 0, then the format is from a file.  There does not
			seem to be any way to determine what setting the original file
			used, so just assume High.  May be able to store block
			sizes as a possible future enhancement.
		 */
		//case 0:
		//case 3:
		default:	// -5
			break;
	}

	return
		FLAC__stream_encoder_set_bits_per_sample( encoder, format.bits )
		&& FLAC__stream_encoder_set_channels( encoder, format.channels )
		&& FLAC__stream_encoder_set_sample_rate( encoder, format.rate )
		&& FLAC__stream_encoder_set_do_exhaustive_model_search( encoder,
				do_exhaustive_model_search )
		&& FLAC__stream_encoder_set_do_escape_coding( encoder,
				do_escape_coding )
		&& FLAC__stream_encoder_set_do_mid_side_stereo( encoder,
			do_mid_side_stereo )
		&& FLAC__stream_encoder_set_loose_mid_side_stereo( encoder,
			loose_mid_side_stereo )
		&& FLAC__stream_encoder_set_max_lpc_order( encoder, max_lpc_order )
		&& FLAC__stream_encoder_set_blocksize( encoder, blocksize )
		&& FLAC__stream_encoder_set_qlp_coeff_precision( encoder,
			qlp_coeff_precision )
		&& FLAC__stream_encoder_set_min_residual_partition_order( encoder,
			min_residual_partition_order )
		&& FLAC__stream_encoder_set_max_residual_partition_order( encoder,
			max_residual_partition_order )
		&& FLAC__stream_encoder_set_rice_parameter_search_dist( encoder,
			rice_parameter_search_dist );
}


//---------------------------------------------------------------------------
/*
	Write member functions
 */

FLAC__StreamMetadata **AFile::AddFlacMetadata( void )
{
	try
	{
		FLAC__StreamMetadata **newdata;
		newdata = new FLAC__StreamMetadata*[flacMetadataN + 1];
		if ( flacMetadata )
		{
			memcpy( newdata, flacMetadata, sizeof(*flacMetadata) * flacMetadataN );
			delete flacMetadata;
		}
		flacMetadata = newdata;
		++flacMetadataN;
		return &flacMetadata[flacMetadataN - 1];
	}
	catch( ... )
	{
	}
	return 0;
}

// Write all info into Vorbis comments
Error gdecl AFile::WriteInfo( void )
{
	if ( ! metadata )
		return eNone;

	int count = 0, index = 0;
	for ( int n = 0; n < TextTotal; n++ )
	{
		const wchar_t *text = metadata->GetText( MetaName[n] );
		if ( text && *text )
			++count;
	}
	if ( ! count )
		return eNone;

	FLAC__StreamMetadata **sdata = AddFlacMetadata();
	if ( ! sdata )
		return eMemory;

	FLAC__StreamMetadata *&metaInfo = *sdata;

	metaInfo = FLAC__metadata_object_new( FLAC__METADATA_TYPE_VORBIS_COMMENT );

	if ( ! metaInfo )
		return eMemory;

	FLAC__metadata_object_vorbiscomment_resize_comments( metaInfo, count );

	for ( int n = 0; n < TextTotal; n++ )
	{
		Gmd::Text	*td = (Gmd::Text *)metadata->Get( MetaName[n] );
		if ( td && *td && (*td)[0] )
		{
			const char *inf = td->Get( Gmd::Utf8 );

			int		len = strlen( inf ) + strlen( VorbisInfo[n] ) + 2;
			char	*text;

			// Make XXX=YYY vorbis comment string
			try
			{
				text = new char[len];
			}
			catch( ... )
			{
				FLAC__metadata_object_delete( metaInfo );
				metaInfo = 0;
				return eMemory;
			}
			strcpy( text, VorbisInfo[n] );
			strcat( text, "=" );
			strcat( text, inf );

			FLAC__StreamMetadata_VorbisComment_Entry	entry;

			// Set entry string and length, excluding null terminator
			entry.entry = (unsigned char *)text;
			entry.length = len - 1;

			FLAC__metadata_object_vorbiscomment_set_comment(
				metaInfo, index, entry, true );

			++index;
			delete [] text;
		}
	}

	return eNone;
}

// Return the UTF8 length of a string including zero terminator
static int bytelen( Gmd::Text &convert, const wchar_t *s )
{
	if ( s )
	{
		// Use UTF-8 conversion function in Gmd::Text
		convert = s;
		return strlen( convert.Get( Gmd::Utf8 ) ) + 1;
	}
	else
		return 0;
}

// Return the number of bytes required to store a cue point
static int cuebytes( Gmd::Text &convert, const Gmd::Cue *cue )
{
	return sizeof(FLAC__uint32)					// Size of this cue point data
			+ sizeof(FLAC__uint64)				// Offset
			+ sizeof(FLAC__uint32)				// Length of name
			+ bytelen( convert, cue->name )		// Name UTF8 string len
			+ sizeof(FLAC__uint32)				// Length of description
			+ bytelen( convert, cue->description );	// Description UTF8 len
}

// Write all cue points.  See comments near top of this module.
Error gdecl AFile::WriteCues( void )
{
	if ( ! cue || cue->Count() == 0 )
		return eNone;

	if ( ! textConvert )
		return eMemory;

	// Total up number of bytes to store entire block
	int bytes = sizeof(FLAC__uint32);	// Number of cue points
	for ( int n = 0; n < cue->Count(); n++ )
		bytes += cuebytes( *textConvert, (*cue)[n] );

	FLAC__StreamMetadata **sdata = AddFlacMetadata();
	if ( ! sdata )
		return eMemory;

	FLAC__StreamMetadata *&metaCues = *sdata;

	metaCues = FLAC__metadata_object_new( FLAC__METADATA_TYPE_APPLICATION );

	if ( ! metaCues )
		return eMemory;

	// Set 'Cues' ID
	memcpy( metaCues->data.application.id, FLAC__Cues, sizeof(FLAC__uint32) );

	FLAC__byte *data, *current;
	try
	{
		data = new unsigned char[bytes];
	}
	catch( ... )
	{
		FLAC__metadata_object_delete( metaCues );
		metaCues = 0;
		return eMemory;
	}

	current = data;

	// Set block ID and number of cue points
	*(FLAC__uint32*)current = cue->Count();
	current += sizeof(FLAC__uint32);

	// Add each cue point to the data block
	for ( int n = 0; n < cue->Count(); n++ )
	{
		const Gmd::Cue	*c = (*cue)[n];
		Gmd::Text		&convert = *textConvert;
		FLAC__uint32  	*sizeptr = (FLAC__uint32*)current;

		//*(FLAC__uint32*)current = cuebytes( c );	// done using sizeptr later

		// Skip size value
		current += sizeof(FLAC__uint32);

		// Offset in samples (convert time to samples, round up)
		*(FLAC__uint64*)current = (FLAC__uint64)(c->position
									* outFormat.rate + 0.5);
		current += sizeof(FLAC__uint64);

		// Write name length
		convert = c->name;
		int len = strlen( convert.Get( Gmd::Utf8 ) ) + 1;
		*(FLAC__uint32*)current = len;
		current += sizeof(FLAC__uint32);

		// Write name (may be blank)
		memcpy( current, convert.Get( Gmd::Utf8 ), len );
		current += len;

		// Write description length
		convert = c->description;
		len = strlen( convert.Get( Gmd::Utf8 ) ) + 1;
		*(FLAC__uint32*)current = len;
		current += sizeof(FLAC__uint32);

		// Write description
		memcpy( current, convert.Get( Gmd::Utf8 ), len );
		current += len;

		*sizeptr = current - (FLAC__byte *)sizeptr;
	}

	if ( ! FLAC__metadata_object_application_set_data( metaCues,
			data, bytes, true ) )
	{
		delete [] data;
		return eMemory;
	}

	delete [] data;

	return eNone;
}

// Write all picture into FLAC metadata
Error gdecl AFile::WritePictures( void )
{
	if ( ! metadata )
		return eNone;

	if ( ! textConvert )
		return eMemory;

	Gmd::PictureList  	*list;
	const Gmd::Picture	*picture;

	list = (Gmd::PictureList *)metadata->Get( Gmd::GWPictureList );
	if ( ! list )
		return eNone;

	Gerr::Error	error = eNone;

	for ( int n = 0; n < list->Count(); n++ )
	{
		FLAC__StreamMetadata **sdata = AddFlacMetadata();
		if ( ! sdata )
		{
			error = eMemory;
			break;
		}

		FLAC__StreamMetadata *&metaPict = *sdata;

		metaPict = FLAC__metadata_object_new( FLAC__METADATA_TYPE_PICTURE );
		if ( ! metaPict )
		{
			error = eMemory;
			break;
		}

		picture = (*list)[n];

		*textConvert = picture->format;
		FLAC__metadata_object_picture_set_mime_type( metaPict,
						(char *)textConvert->Get( Gmd::Ansi ), true );

		*textConvert = picture->description;
		FLAC__metadata_object_picture_set_description( metaPict,
						(unsigned char *)textConvert->Get( Gmd::Ansi ), true );

		metaPict->data.picture.type = (FLAC__StreamMetadata_Picture_Type)picture->type;

		FLAC__metadata_object_picture_set_data( metaPict,
						(unsigned char *)picture->picture, picture->size, true );

		if ( metaPict->data.picture.mime_type == 0
			 || metaPict->data.picture.description == 0
			 || metaPict->data.picture.data == 0 )
		{
			error = eMemory;
			break;
		}
		// No easy way to determine these without knowing every image format
		metaPict->data.picture.width = 0;
		metaPict->data.picture.height = 0;
		metaPict->data.picture.depth = 0;
		metaPict->data.picture.colors = 0;
	}

	return error;
}

// Write padding to make it more efficient to edit metadata later
Error gdecl AFile::WritePadding( void )
{
	if ( ! metadata )
		return eNone;

	Gmd::Padding  	*pad;
	pad = (Gmd::Padding *)metadata->Get( Gmd::GWPadding );
	if ( ! pad || pad->Get() <= 0 )
		return eNone;

	FLAC__StreamMetadata **sdata = AddFlacMetadata();
	if ( ! sdata )
		return eMemory;

	FLAC__StreamMetadata *&metaPad = *sdata;

	metaPad = FLAC__metadata_object_new( FLAC__METADATA_TYPE_PADDING );
	metaPad->length = pad->Get();

	return eNone;
}

// Begin writing a file
Error gdecl AFile::Begin( const wchar_t *name, const Format &f )
{
	if ( encoder )
		return eForbidden;

	const AFormat *format;

	// Make sure the format object is appropriate and belongs to this plug-in
	if ( f.Type() != TableData.name )
		return eFormat;
	format = dynamic_cast<const AFormat *>( &f );
	if ( ! format || format->rate == 0 )
		return eFormat;

	// Create a new encoder
	outFormat = *format;
	encoder = FLAC__stream_encoder_new();
	if ( ! encoder )
		return eMemory;

	Error	error = eNone;

	// Set encoding format and write information
	if ( error == eNone )
	{
		if ( ! SetEncodingFormat( encoder, outFormat ) )
			error = eFormat;
		else
		{
			// Store info and cue points in metadata blocks
			error = WriteInfo();
			if ( error == eNone )
				error = WriteCues();
			if ( error == eNone )
				error = WritePictures();
			WritePadding();
			if ( error == eNone && flacMetadataN > 0 )
			{
				if ( ! FLAC__stream_encoder_set_metadata( encoder,
						flacMetadata, flacMetadataN ) )
					error = eMemory;
			}
		}
	}

	if ( error == eNone )
	{
		// Open output file (extremely important to remember to close it later)
		outFile = _wfopen( name, L"wb" );
		if ( ! outFile )
			error = eCreate;
	}

	// Initialize FLAC encoder
	if ( error == eNone )
	{
		if ( ! StreamableRate( outFormat.Rate() ) )
			FLAC__stream_encoder_set_streamable_subset( encoder, false );

		FLAC__StreamEncoderInitStatus state;
		state = FLAC__stream_encoder_init_stream( encoder,
				outWriteCallback, outSeekCallback, outTellCallback, 0, this );
		switch ( state )
		{
			case FLAC__STREAM_ENCODER_INIT_STATUS_OK:
				break;
			case FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_NUMBER_OF_CHANNELS:
			case FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_BITS_PER_SAMPLE:
			case FLAC__STREAM_ENCODER_INIT_STATUS_INVALID_SAMPLE_RATE:
				error = eFormat;
				break;
			case FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR:
			{
				FLAC__StreamEncoderState state;

				state = FLAC__stream_encoder_get_state( encoder );
				switch ( state )
				{
					case FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR:
						error = eMemory;
						break;
					case FLAC__STREAM_ENCODER_IO_ERROR:
					case FLAC__STREAM_ENCODER_FRAMING_ERROR:
						error = eWrite;
						break;
					default:
						error = eCreate;
						break;
				}
				break;
			}
			default:
				error = eCreate;
				break;
		}
	}

	// Delete metadata objects
	while ( flacMetadataN > 0 )
	{
		--flacMetadataN;
		FLAC__metadata_object_delete( flacMetadata[flacMetadataN] );
	}
	delete [] flacMetadata;
	flacMetadata = 0;

	if ( error != eNone )
	{
		FLAC__stream_encoder_delete( encoder );
		encoder = 0;
		if ( outFile )
		{
			fclose( outFile );
			outFile = 0;
		}
	}

	return error;
}

// Write audio data to file in the correct format - either 8, 16, or 24 bits
Error gdecl AFile::Write( const audio *data, int samples )
{
	if ( ! encoder )
		return eForbidden;

	FLAC__int32	buffer[MaxSize * 2];
	int			channels = outFormat.channels,
				max = 1 << (outFormat.bits - 1);
	audio		scale = max - 1;

	while ( samples )
	{
		int			count = samples > MaxSize ? MaxSize : samples,
					values = count * channels;
		FLAC__int32	*dest = buffer;

		while ( values-- > 0 )
		{
			/*
				Limit conversion and hack retarded C rounding rules
				where 5.9999999999... becomes 5.
				Add/subtract small value to compensate for rounding problem
				so that the magnitude is the closest integer.
			 */
			if ( *data >= 1.0 )
				*dest = max - 1;
			else if ( *data < -1.0 )
				*dest = (int)-max;
			else if ( *data >= 0 )
				*dest = (int)(*data * scale + 0.5);
			else
				*dest = (int)(*data * scale - 0.5);

			++dest;
			++data;
		}

		if ( ! FLAC__stream_encoder_process_interleaved( encoder,
														   buffer, count ) )
			return eWrite;
		samples -= count;
	}

	return eNone;
}

// Close output file by writing complete header
Error gdecl AFile::End( void )
{
	if ( ! encoder )
		return eForbidden;

	FLAC__StreamEncoderState state = FLAC__stream_encoder_get_state( encoder );
	FLAC__stream_encoder_finish( encoder );

	FLAC__stream_encoder_delete( encoder );
	encoder = 0;

	if ( outFile )
	{
		fclose( outFile );
		outFile = 0;
	}

	return state == FLAC__STREAM_ENCODER_OK ? eNone : eClose;
}

//---------------------------------------------------------------------
/*
	FLAC FormatList class

	To create this list, derive a class from FormatList and
	initialize an array of AFormat objects with	attributes.

	Note that a pointer to a static structure MUST NOT be
	returned since the application may modify the Format
	objects to set rates, channels, or bitrates.
 */

// 2 channels * 4 quality settings * 3 bit depths (8, 16, 24)
const int FormatItems = 2 * 4 * 3;

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
		format[n].bits = (n & (0xFF - 7)) + 8;
		format[n].channels = (n & 1) + 1;
		format[n].level = ((n >> 1) & 3) + 1;

		// Set 16-bit, level 3 format as default for mono/stereo
		if ( format[n].bits == 16 && format[n].level == 3 )
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
	if ( name[1] == L':' || name[0] == L'\\'
			|| name[0] == L'/' || name[0] == L'.' )
	{
		/*
			libFLAC will try to scan the entire file, which wastes a lot
			of time if it is not a FLAC file.  Instead, perform a limited
			search at the beginning of the file for the "fLaC" marker
			and abort if it is not present.  Note that if a large ID3
			tag (>64k) is located at the beginning, FLAC detection will
			fail.  An ID3 tag shouldn't be there anyway.
		 */

		char    buffer[1024*64];
		FILE	*fp;
		bool	markerFound = false;

		// If _wfopen is not available, use ustring.uft8() with fopen()
		fp = _wfopen( name, L"rb" );

		if ( fp )
		{
			int		count = fread( buffer, 1, sizeof(buffer), fp );
			char	*p = buffer;

			while ( count-- > 4 && ! markerFound )
			{
				markerFound = memcmp( p, "fLaC", 4 ) == 0;
				++p;
			}

			fclose( fp );
		}

		if ( ! markerFound )
			return 0;

		// The "fLaC" marker was found, make sure it really is a FLAC file
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


