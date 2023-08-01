/*
	Audio Plug-in Interface (C) 2009 GoldWave Inc.

	Purpose:
		Classes for handling metadata, such as images and other non-audio data
		that may be stored in a file.
*/

#pragma once
#pragma hdrstop

#ifndef _GMETADATA_H_
#define _GMETADATA_H_

/*
	Define GWINCLUDESTRINGS if you want the strings included in your code.
	That should be done only in one module to avoid duplicete strings.
 */
#ifdef GWINCLUDESTRINGS
  #define GWDEFINENAMESTRING(x) L##x
  #define GWDEFINENAME(x) const wchar_t *x = GWDEFINENAMESTRING(#x);
  #define GWDEFINESTRING(x,y) const wchar_t *x = L##y;
#else
  #define GWDEFINENAME(x) extern const wchar_t *x;
  #define GWDEFINESTRING(x,y) extern const wchar_t *x;
#endif

#include "gwaudio.h"

#pragma pack(push, 4)

namespace Gmd	// GoldWave Metadata namespace
{

class MetadataBase : public Gap::Metadata::Data {};

/*
	Name = "GWRaw": Raw metadata for storing custom or proprietary data.
	This name should be used only in New().  Do not use in Set().
	Use a unique name based on the module's name, for example "APEFileCustom".
 */
GWDEFINENAME( GWRaw );
class Raw : public MetadataBase
{
public:
	virtual int gdecl Size( void ) const = 0;
	virtual void * gdecl Data( void ) = 0;
};

/*
	Text encoding.  Unicode (UTF-16) is used by default.
	Otherwise the encoding must be specified for functions that accept
	single byte strings.  Old Windows/DOS/ASCII/C/C++ are ANSI.  Multibyte
	is UTF-8.
 */
enum { Ansi, Utf8 };

/*
	Name = "GWText": Unicode text metadata.
	This root name should be used only in Metadata::New().
	Do not use root name in Metadata::Set().
	Use the appropriate text name, such as "GWText_Author", "GWText_Album",
	in Metadata::Set() and Metadata::Get().
 */
GWDEFINENAME( GWText );
class Text : public MetadataBase
{
public:
	virtual gdecl operator const wchar_t *( void ) const = 0;
	virtual Text & gdecl operator=( const wchar_t *string ) = 0;
	virtual bool gdecl Set( char *string, int encoding ) = 0;
	virtual const char * gdecl Get( int encoding ) = 0;
};

GWDEFINENAME( GWText_Album );		// Album name
GWDEFINENAME( GWText_Author );		// Author or artist
GWDEFINENAME( GWText_Copyright );	// Copyright information
GWDEFINENAME( GWText_Description );	// Description of file contents
GWDEFINENAME( GWText_Date );  		// Date of recording
GWDEFINENAME( GWText_Genre );		// A short indication of music genre
GWDEFINENAME( GWText_Title );		// Title of the track/file
GWDEFINENAME( GWText_URL );	 		// URL for more information about track/file
GWDEFINENAME( GWText_Tool );	  	// Tool used to create the file/track
GWDEFINENAME( GWText_TrackNumber );	// Track number
GWDEFINENAME( GWText_ISRC );	  	// Standard recording number
GWDEFINENAME( GWText_AlbumArtist );	// Album artist
GWDEFINENAME( GWText_Composer );

// See http://www.goldwave.com/developer.php for any additions

struct Cue
{
	double			position;
	const wchar_t	*name, *description;
	Cue( void )
	{
		position = 0;
		name = 0;
		description = 0;
	}
};

/*
	Name = "GWCueList": Cue point metadata.  Manages cue points.
	If Metadata::Get( GWCueList ) returns null, plug-in can assume that
	cue points are not required by host program.
 */
GWDEFINENAME( GWCueList );
class CueList : public Gap::Metadata::Data, public Gap::List
{
protected:
	// Do not allow plug-in to destroy this object
	virtual void gdecl			Destroy( void ) { delete this; }
public:
	virtual const Cue * gdecl	operator[]( int n ) const = 0;
	virtual bool gdecl 			Add( const Cue &c ) = 0;
	virtual bool gdecl 			Add( const char *name, const char *description,
									 double position, int encoding ) = 0;
	virtual bool gdecl			Move( int n, double position ) = 0;
	virtual bool gdecl			Update( int n, const Cue &c ) = 0;
	virtual bool gdecl			Remove( int n ) = 0;
};


// See APIC frame in ID3 specification (www.id3.org)
struct Picture
{
	enum Type
	{
		tOther, tIcon32x32, tIconOther, tCoverFront, tCoverBack, tLeaflet,
		tMedia, tLeadArtist, tArtist, tConductor, tBand, tComposer, tLyricist,
		tLocation, tDuringRecording, tDuringPerformance, tVideo, tReserved,
		tIllustration, tLogoBand, tLogoPublisher
	};
	const wchar_t	*format,		// MIME type (jpeg, png)
					*description;	// Description of the picture
	int				type,			// APIC type (one of enum above)
					size;			// Number of bytes of binary picture data
	void			*picture;		// Binary picture data
	Picture( void )
	{
		format = description = 0;
		type = tOther;
		size = 0;
		picture = 0;
	}
};

GWDEFINENAME( GWPictureList );
/*
	Name = "GWPictureList": A list of Pictures (cover art, etc.).
	Host program creates this class to manage embedded pictures.
	Plug-ins use Metadata::Get( GWPictureList, true ) to retrieve/create this.
 */
class PictureList : public Gap::Metadata::Data, public Gap::List
{
protected:
	// Do not allow plug-in to destroy this object
	virtual void gdecl				Destroy( void ) { delete this; }
public:
	virtual const Picture * gdecl	operator[]( int n ) const = 0;
	virtual bool gdecl 				Add( const Picture &c ) = 0;
	virtual bool gdecl				Remove( int n ) = 0;
};

/*
	Name = "GWPadding": Tells plug-in how much padding is required (if present).
	Plug-ins use Metadata::Get( GWPadding, false ) to retrieve this.
*/
GWDEFINENAME( GWPadding );
class Padding : public MetadataBase
{
public:
	virtual int gdecl Get( void ) = 0;			// Returns padding length
	virtual void gdecl Set( int length ) = 0;	// Sets padding length
};

}
#pragma pack(pop)

#endif
