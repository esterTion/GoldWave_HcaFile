/*
	GoldWave (C) 2003-2009 GoldWave Inc.
	Purpose:
		GoldWave Effect plug-in base class and host program interface.
 */

// Why doesn't C have an #includeonce directive to quickly skip headers?
#ifndef _GEFFECT_H_
#define _GEFFECT_H_

#include "gwbase.h"
#include "gwaudiotype.h"

#pragma pack(push, 4)

namespace Gfx
{

/*---------------------------------------------------------------------
	MaxSamples is the maximum number of samples a Transform/Effect can
	read using the "Read( audio *out, int max )" member.  It is an error
	to specify a larger value for 'max' as it will cause memory
	faults.  Only values of MaxSamples or less are fine.
 */
const int MaxSamples = 32768;

/*---------------------------------------------------------------------
	The Transform base class.  This object does the audio
	processing.  It can be used for internal processing where a user
	interface is not required.  GoldWave often combines several
	Transforms to create a single effect.
 */
class Transform
{
protected:
	Transform	*source;
	unsigned    channel;
	int			channels, rate;

	virtual 	~Transform( void ) {}

public:
	Transform( void )
	{
		source = 0;
		channels = 2;
		rate = 44100;
		channel = acfAll;
	}
	virtual void gdecl Destroy( void ) { delete this; }
	virtual bool gdecl Source( Transform *Source )
	{
		source = Source;
		if ( source )
		{
			channel = source->Channel();
			channels = source->Channels();
			rate = source->Rate();
		}
		return true;
	}
	virtual int gdecl		Channels( void ) { return channels; }
	virtual unsigned gdecl 	Channel( void ) { return channel; }
	virtual int	gdecl	 	Rate( void ) { return rate; }

	virtual void gdecl		Reset( void ) { source->Reset(); }
	virtual double gdecl	Time( void ) { return source->Time(); }
	virtual double gdecl	Start( void ) { return source->Start(); }
	virtual double gdecl	Finish( void ) { return source->Finish(); }
	virtual int gdecl	 	Read( audio *buffer, int samples ) = 0;

	/*
		Seeking is allowed only if identical output is produced regardless
		if a seek is used or not.  Flanger, Echo, Filters do not qualify since
		output may vary depending on the seek time (due to delay buffers).
		In other words, seeking is allowed if a sample does not depend on
		surrounding samples (volume change, invert, offset, exchange).
	 */
	virtual bool gdecl		Seek( double time ) { return false; }

	/*
		Allows this transform to tell upstream transforms to update
		themselves (in case Start/Finish/Rate/Channels/Channel has
		changed).
	 */
	virtual int gdecl	 	Update( Transform *initiator, bool update = false )
	{
		if ( initiator != this )
			return source->Update( initiator, update );
		else
			return 0;
	}
};

/*---------------------------------------------------------------------
	Class Progress is used for an effect that has to scan the audio,
	such as maximize, volume match, offset, etc. to obtain information
	before the effect itself can be processed.  The host program creates
	a derived class to give visual feedback to the user and passes
	that to the effect.  The effect calls Set() to set the title and
	range for the progress, then Show() to show the progress
	window, then Update() while processing, then Hide() to hide the window.
	If Update() returns false, the processing must be cancelled.
 */

class Progress
{
protected:
	virtual ~Progress() {}
public:
	virtual void gdecl Destroy( void ) { delete this; }
	virtual void gdecl Set( const wchar_t *title, double start, double finish ) = 0;
	virtual void gdecl Show( void ) = 0;
	virtual bool gdecl Update( double position ) = 0;
	virtual void gdecl Hide( void ) = 0;
};

/*---------------------------------------------------------------------
	The Effect base class.  Adds user interface elements to the
	Transform class.
 */
enum Ability
{
	aPage = Gbase::baPage,					// Has a user interface page
	aScanRequested = AbilityFlag(1),		// May scan the entire source (Offset)
	aScanRequired = AbilityFlag(2),			// Must scan the entire source first (Maximize, Match)
	aSeekableRequired = AbilityFlag(3),		// Source must be seekable (Reverse, Interpolate)
	aClipboardRequested = AbilityFlag(4),	// Clipboard may be used if available (Mechanize, Noise Reduction)
	aClipboardRequired = AbilityFlag(5),	// Clipboard is required for processing (Mix)
	aFixedLength = AbilityFlag(6), 			// A static length source is required (Volume Shape, Pan, Doppler)
	aChangesLength = AbilityFlag(7),		// Output length will be different than source (Time warp, Remove silence, Doppler, Pitch, Echo)
	aStereoRequired = AbilityFlag(8),		// Source must be stereo (Pan, Channel Mix)
	aDiscontinuous = AbilityFlag(9),		// Parts of source are discarded (Remove silence)
	aModifiesAllChannels = AbilityFlag(10),	// Cannot avoid changing both channels even when only one should be changed (Reverse)
//	aRealTime = AbilityFlag(11),			// Effect can run in real-time
//	aTail = AbilityFlag(12),				// Produces tail audio (Echo, Reverb)
};

enum Input
{
	iClipboard//, iTail
};

/*
	Main Effect class with settings, scanning, and auxilary input
	interfaces added.  Ideally, Transform would be a
	virtual base class of Effect, but that destroys all
	compatibility between Borland and Microsoft virtual
	table/data class layout.
 */
class Effect : public Gbase::PluginObject, /*virtual*/ public Transform
{
protected:
	// Delete cannot be used by host program (may be in DLL memory)
	virtual ~Effect( void ) {}

public:
	// Host program must use Destroy instead of delete
	virtual void gdecl 			Destroy( void ) { delete this; }

	// Called by host program to tell effect to initiate scan (aScan required)
	virtual bool gdecl 	 Scan( Progress &progress ) { return false; }
	// Called by host program to set the clipboard or other input
	virtual bool gdecl	 Input( Transform *transform, int input ) { return false; }
};

/*---------------------------------------------------------------------
	Host program interface structures/functions
 */
#define EffectVersion 2.0F

#ifdef _WIN32
# ifdef _MSC_VER
	/*
		Microsoft does not insert an underscore.  Need to add one.
		Do not compile with __stdcall convention.  Otherwise it
		will ignore the extern "C" convention and mangle the name.
	 */
#  define EffectInterfaceDll extern "C" __declspec(dllexport) Gfx::Interface * _GetEffectInterface
#else
	// Borland does insert one
#  define EffectInterfaceDll extern "C" __declspec(dllexport) Gfx::Interface *GetEffectInterface
# endif
# define EffectInterfaceApp "_GetEffectInterface"
#else
# define EffectInterfaceDll extern "C" Gfx::Interface *GetEffectInterface
# define EffectInterfaceApp "GetEffectInterface"
#endif

/*
	Structure that defines the name and place of effects.  An array
	of these would be used to define a set of effects.
 */
struct Table
{
	const wchar_t	*name;		// Effect name
	unsigned		abilities;
	int				image;		// Icon resource ID
};

// Function prototype for effects constructor used in Interface structure
typedef Effect *(gdecl *CreateFn)( const wchar_t *name );

// This is the interface structure passed to the host program
struct Interface
{
	float 			version;	// Effect version (usually 'EffectVersion')
	int	  			count; 		// Number of effects in this module

	Table			*list; 		// Pointer to table of effects in this module
	CreateFn		create;		// Function that creates an effect
	Gbase::ConfigFn	config;		// Function to configure module (may be null)
};

extern "C"
{
	// This is the function that creates and/or returns the Interface
	typedef const Interface *(*InterfaceFn)( void );
}

} /* Gfx namespace */

#pragma pack(pop)

#endif
