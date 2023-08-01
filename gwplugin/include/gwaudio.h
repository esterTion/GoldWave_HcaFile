/*
	Audio Plug-in Interface (C) 2003-2009 GoldWave Inc.

	Purpose:
		Classes for audio file handling, format information,
		storing cue points, and audio file information (tags).
*/

#ifndef _GAUDIO_H_
#define _GAUDIO_H_

#pragma hdrstop
#include "gwbase.h"
#include "gwerror.h"
#include "gwaudiotype.h"

#pragma pack(push, 4)

namespace Gvp
{
	struct Pixel;
}

namespace Gap	// GoldWave Audio Plugin namespace
{

#ifdef _WIN32
typedef __int64 int64;	// Use as Gap::int64
#else
#error Unimplemented
#endif

class Format
{
protected:
	virtual ~Format( void ) {}
public:
	void *programdata;
	enum
	{
		fDefault = 0x001,	// This is the default format for the file type
		fVBR =  0x002,		// Format uses a variable bitrate
		fAnyRate =	0x004,	// Format can use any sampling rate
		fSequential = 0x008,// Audio data must be read sequentially (no seek)
		fBeyond4GB = 0x010,	// More than 4GB of audio can be stored in file
		fUnsized = 0x020	// There is no way to know the exact audio length
	};

	Format( void ) { programdata = 0; }
	virtual void gdecl			Destroy( void ) { delete this; }

	// Functions to set general format parameters
	virtual Gerr::Error gdecl	SetChannels( int channels ) = 0;
	virtual Gerr::Error gdecl	SetRate( int rate ) = 0;
	virtual Gerr::Error gdecl	SetBitrate( int bitrate ) = 0;

	// Functions to get format parameters
	virtual unsigned gdecl		Flags( void ) const = 0;
	virtual int gdecl			Channels( void ) const = 0;
	virtual int gdecl			Rate( void ) const = 0;
	virtual int gdecl			Bitrate( void ) const = 0;

	virtual bool gdecl			operator==( const Format &format ) const = 0;
	virtual Format * gdecl		Duplicate( void ) const = 0;
	// Functions to get format text description
	virtual const wchar_t * gdecl	Type( void ) const = 0;
	virtual const wchar_t * gdecl	Description( void ) = 0;
	virtual const wchar_t * gdecl	Extension( void ) const = 0;
};

class List
{
protected:
	virtual ~List( void ) {}
	int		count;

	// Do not allow plug-in to destroy this object
	virtual void gdecl Destroy( void ) { delete this; }

public:
	List( void ) { count = 0; }
	int gdecl Count( void ) const { return count; }
};

class FormatList : public List
{
public:
	virtual void gdecl Destroy( void ) { delete this; }
	virtual Format * gdecl operator[]( int n ) = 0;
};

// Host program creates this class to manage cue points
/*class CueList : public List
{
protected:
	// Do not allow plug-in to destroy this object
	virtual void gdecl			Destroy( void ) { delete this; }
public:
	virtual const Cue * gdecl	operator[]( int n ) const = 0;
	virtual bool gdecl 			Add( const Cue &c ) = 0;
	virtual bool gdecl			Move( int n, double position ) = 0;
	virtual bool gdecl			Update( int n, const Cue &c ) = 0;
	virtual bool gdecl			Remove( int n ) = 0;
};
*/

// Host program creates this class to manage text information
/*
class InfoList
{
protected:
	virtual ~InfoList( void ) {}
public:
	// Never change the order of this enumeration
	enum {
		Album,			// Album name
		Author,			// Author or artist
		Copyright,		// Copyright information
		Description,	// Description of file contents
		Date,			// Date of recording
		Genre,			// A short indication of music genre
		Title,			// Title of the track/file
		URL,			// Url for more information about track/file
		Tool,			// Tool used to create the file/track
		TrackNumber,	// Track number
		ISRC,			// Standard recording number
		AlbumArtist,	// Album artist

		Total
	};

	virtual void gdecl				Destroy( void ) { delete this; }
	virtual const wchar_t * gdecl	operator[]( int n ) const = 0;
	virtual bool gdecl				Update( int n, const wchar_t *information ) = 0;
};
*/

/*
	Host program creates this class to manage metadata and passes it to
	the Setup function of the AudioFile class.  The plug-in stores
	metadata using the Set function.
 */
class Metadata
{
protected:
	virtual ~Metadata( void ) {}
public:
	// Host program or plug-ins may create Data class
	class Data
	{
	protected:
		// Object can be delete only by originating module
		virtual ~Data( void ) {}
	public:
		virtual void gdecl Destroy( void ) { delete this; }
		virtual Data * gdecl Duplicate( void ) const = 0;
	};

	virtual void gdecl 		Destroy( void ) { delete this; }

	// Names beginning with the letters "GW" are reserved
	virtual Data * gdecl	New( const wchar_t *name, int size = 0 ) = 0;
	virtual bool gdecl		Set( const wchar_t *name, Data *data, bool copy = false ) = 0;
	virtual Data * gdecl	Get( const wchar_t *name, bool create = false ) = 0;

	/*
		 These functions simplify getting and setting text metadata.  The
		 text is accessed directly rather than using a Gmd::Text object.
	 */
	virtual bool gdecl				SetText( const wchar_t *name, const wchar_t *text ) = 0;
	virtual const wchar_t * gdecl	GetText( const wchar_t *name ) = 0;
};

/*
	The Asker class is used to ask the user questions if a problem
	occurs when handling a file.  Host program creates this class.
 */
class Asker
{
protected:
	virtual ~Asker( void ) {}
public:
	enum { No = 1, Yes = 2, Ok = 4, Cancel = 8, Abort = 16, YesNo = Yes|No };
	virtual int gdecl Ask( const wchar_t *message, int options, int def ) const
	{
		return def;
	}
	virtual void gdecl Destroy( void ) { delete this; }
};

/*
	Frame information structure
 */
struct Frame
{
	double	rate;			// - for variable, 0 for still image, fps otherwise
	int		width, height;	// Video size (buffer is width*height*4 bytes)
};

class AudioFile : public Gbase::PluginObject
{
protected:
	Asker		*asker;
	Metadata	*metadata;

	virtual ~AudioFile( void ) {}
	int Ask( const wchar_t *message, int options, int def )
	{
		if ( asker )
			return asker->Ask( message, options, def );
		else
			return def;
	}
public:
	AudioFile( void ) { asker = 0; metadata = 0; }
	virtual void gdecl Destroy( void ) { delete this; }

	// Input member functions
	virtual Gerr::Error gdecl	Open( const wchar_t *name, const Format *format = 0 ) = 0;
	virtual int gdecl			Read( audio *data, int samples ) = 0;
	virtual Gerr::Error gdecl	Seek( int64 start ) = 0;
	virtual Gerr::Error gdecl	Close( void ) = 0;
	virtual Gerr::Error gdecl	UpdateInfo( bool force ) { return Gerr::eUnsupported; }

	// Output member functions
	virtual Gerr::Error gdecl	Begin( const wchar_t *name, const Format &format ) = 0;
	virtual Gerr::Error gdecl	Write( const audio *data, int samples ) = 0;
	virtual Gerr::Error gdecl	End( void ) = 0;

	virtual int64 gdecl			Length( void ) = 0;
	virtual FormatList * gdecl	Formats( void ) = 0;
	virtual Format * gdecl		GetFormat( void ) = 0;

	virtual Metadata * gdecl  	GetMetadata( void ) { return metadata; }
	virtual Asker * gdecl	  	GetAsker( void ) { return asker; }

	virtual void gdecl Setup( Metadata *Data, Asker *Asker )
	{
		asker = Asker;
		metadata = Data;
	}
	// Video handling
	virtual Gerr::Error gdecl	ReadFrame( double time, Gvp::Pixel *frame, Frame *info = 0 ) { return Gerr::eUnsupported; }
	virtual Gerr::Error gdecl	WriteFrame( double time, const Gvp::Pixel *frame, const Frame *info = 0 )  { return Gerr::eUnsupported; }
};

/*---------------------------------------------------------------------
	Host program interface structures/functions
 */
#define AudioVersion 3.0F

#ifdef _WIN32
# ifdef _MSC_VER
	/*
		Microsoft does not insert an underscore.  Need to add one.
		Do not compile with __stdcall convention.  Otherwise it
		will ignore the extern "C" convention and mangle the name.
	 */
#  define AudioInterfaceDll extern "C" __declspec(dllexport) Gap::Interface * _GetAudioInterface
#else
	// Borland does insert one
#  define AudioInterfaceDll extern "C" __declspec(dllexport) Gap::Interface *GetAudioInterface
# endif
#define AudioInterfaceApp "_GetAudioInterface"
#else
# define AudioInterfaceDll extern "C" Gap::Interface *GetAudioInterface
#define AudioInterfaceApp "GetAudioInterface"
#endif


enum Ability
{
	aPage = Gbase::baPage,			// Has a page where format can be specified
	aRead = AbilityFlag(1),			// Can read this type
	aWrite = AbilityFlag(2),		// Can write this type
	aRaw = AbilityFlag(3),			// Can handle raw/headerless audio data
	aMetaText = AbilityFlag(4),		// Can read/write text
	aMetaCue = AbilityFlag(5),		// Can read/write cue points
	aMetaPicture = AbilityFlag(6),	// Can read/write pictures
	aMetaLoop = AbilityFlag(7),		// Can read/write loop points
	aReadVideo = AbilityFlag(8),	// Can read video
	aWriteVideo = AbilityFlag(9)	// Can write video
};

/*
	Structure that defines the name, extension, and flags for file handlers.
 */
struct Table
{
	const wchar_t	*name;		// File type.  It cannot begin with \, /, or X:
	unsigned		abilities;

	const wchar_t	*extensions;// Filename extensions associated with type
};

// Type for audio constructor function used in Interface structure
typedef AudioFile *(gdecl *CreateFn)( const wchar_t *name );

// This is the interface structure passed to the host program
struct Interface
{
	float 			version;	// Version (usually 'AudioVersion')
	int	  			count;		// Number of supported types in this module
	Table			*list;		// Pointer to table of types in this module
	CreateFn		create;		// Function that creates file type handler
	Gbase::ConfigFn	config;		// Function to configure module (usually null)
};


// Borland C++ gets confused if the braces are not used
extern "C"
{
	typedef const Interface *(*InterfaceFn)( void );
}

};	/* Namespace Gap */

#pragma pack(pop)

#endif
