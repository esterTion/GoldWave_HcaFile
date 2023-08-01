/*
	GoldWave (C) 2003-2009 GoldWave Inc.

	Purpose:
		Class, structures, enumerations, and constants for the
		realtime visual plug-in interface.
 */

#ifndef _GVISUAL_H_
#define _GVISUAL_H_

#include "gwbase.h"

#pragma pack(push, 4)

/*
	Declare namespace and classes for file information and cue points
	so that the visual plug-in interface is not dependent on the
	file format plug-in interface and gaudio.h does not need to
	be included (unless the visual uses those classes).
 */
namespace Gap
{
	class Metadata;
};

namespace Gvp
{
#ifdef GVPVOIDHANDLES	// Avoids including convoluted windows header files
typedef void 		*DeviceContext;
typedef void 		*RenderContext;
#elif defined(_WIN32)
typedef HDC 		DeviceContext;
typedef HGLRC		RenderContext;
#else
#error Unimplemented
#endif

/*
	Number of samples and frequencies passed to a visual each Draw() call
 */
const int	Samples = 4096,
			FrequencyBits = 12,			// gives 2048 FFT magnitudes
			Frequencies = (Samples >> 1),
			MinFrequency = -100;		// min dB for frequencies

#ifdef PIXEL_16
/*---------------------------------------------------------------------
	In an ideal world, we can use the following pixel format.  However
	this may have to be changed depending on the way the compiler
	organizes the bits.
 */
struct Pixel
{
	unsigned short	b:5, g:6, r:5;
	enum Mask { mRed = 0xF800, mGreen = 0x07E0, mBlue = 0x001F };
	enum Full { fRed = 0x1F, fGreen = 0x3F, fBlue = 0x1F };
};

/*
	Function to calculate the address of the start of scanline 'row',
	given the 'width'.  Takes into account the padding/alignment
	at the end of each row in a Windows bitmap.
 */
inline Pixel *ScanLine( void *start, int row, int width, int height )
{
	int offset = (((width << 1) + 2) & ~2) * (height - row - 1);
	return (Pixel*)((char *)start + offset);
}
#else	// 32 bit pixels
/*---------------------------------------------------------------------
	Byte ordering of ARGB values may need to be changed
 */
struct Pixel
{
	unsigned char	b, g, r, a;
	enum Mask { mRed = 0x00FF0000, mGreen = 0x0000FF00,
				mBlue = 0x000000FF, mAlpha = 0xFF000000 };
	enum Full { fRed = 0xFF, fGreen = 0xFF, fBlue = 0xFF, fAlpha = 0xFF };
};

/*
	Function to calculate the address of the start of scanline 'row',
	given the 'width'.
 */
inline Pixel *ScanLine( void *start, int row, int width, int height )
{
	int offset = (width << 2) * (height - row - 1);
	return (Pixel*)((char *)start + offset);
}
#endif

enum DrawState
{
	dsStopped,	// Playback/recording has stopped
	dsPaused,	// Playback/recording is paused
	dsActive,	// Playback/recording is active
	dsInstant	// Instantaneous graph is required, not related to record/play
};


/*
	Information passed to the visual whenever it must be drawn.
*/
struct DrawInfo
{
	DeviceContext	imageDC;		// Visual Device Context
	RenderContext	imageRC;		// OpenGL Render Context
	Pixel			*pixel;			// Pointer to pixels
	DrawState		state;			// Play/record state
	double			time;			// Current position
	float			*waveform;		// Pointer to amplitude data (or NULL)
	float			*frequency;		// Pointer to frequency data (or NULL)
};

// These are used in the for flags with State and Set()
enum StateFlags {
	sfWidth = 0x001,				// Width valid/changed
	sfHeight = 0x002,				// Height valid/changed
	sfChannels = 0x004,				// Number of channels valid/changed
	sfSide = 0x008,					// Side valid/changed
	sfSamplingRate = 0x010,			// Sampling rate valid/changed
	sfPlaybackState = 0x020,		// Playback state (fast, stop, pause, etc.)
	sfRecordState = 0x040,			// Recording state (stop, pause, etc.)
	sfFrameRate = 0x080,			// Visual frame rate
	sfFileMetadata = 0x100,			// File information has changed

	sfAll = 0x03FF					// All valid/changed
};

/*
	Determines part of the audio should be processed by the visual.
	This is only the recommended channel to process.  Some visuals,
	like the X-Y Graph still process both channels.
 */
enum Side
{
	sLeft = 1,			// Process left channel only
	sRight = 2,			// Process right channel only
	sBoth = 3			// Process both channels (like VU meter in GoldWave)
};

enum PlaybackStates
{
	pStop, pPause, pPlay, pRewind, pFast
};

enum RecordStates
{
	rStop, rRecord, rPause
};

/*
	These flags define appropriate use of a visual (i.e. where the
	visual can be used), and other abilities/properties.
 */
enum Abilities
{
	aPage = Gbase::baPage,		// Visual has a property page
	aStatus = AbilityFlag(1),	// Visual can be displayed in Status window
	aLevel = AbilityFlag(2), 	// Visual can be displayed in Level window
	aLeft = AbilityFlag(3),	 	// Visual can be displayed in Left window
	aRight = AbilityFlag(4), 	// Visual can be displayed in Right window
	aEvent = AbilityFlag(5),	// Visual can process events
	aOpenGL = AbilityFlag(6),	// Visual uses OpenGL
	aVideo = AbilityFlag(7),	// Visual requires video frames

	// Graph only (most common)
	aGraph = aLeft | aRight,
	// Any place is fine
	aAny = aStatus | aLevel | aGraph
};

/*
	Information passed to the Visual's Set function to indicate a
	change in the visual, such as size, sampling rate, number of
	channels, or side (left, right, or both).
 */
struct State
{
	/*
		If you are wondering what this totally useless virtual distructor
		is doing here, it is required to maintain compatibility between
		Borland and Microsoft virtual table/data layout.
		At this time, I'd like to take a moment to damn the entire
		C/C++ compiler industry to hell for making this
		so needlessly difficult to sort out.  Lack of object
		compatibility between compilers on the same platform
		is inexcusable.  Why not have a new keyword 'stdclass'
		that would ensure object compatibility (possibly
		sacrificing efficiency for 100% compatibility) and
		let programmers choose?  The current "do whatever the hell
		you want" (implementation specific) standard is not helpful.
	 */
	virtual ~State() {}

	int		width, height,
			channels,
			side,					// What should be processed, see Side
			samplingRate,			// Playback/Recording rate
			playbackState,			// See PlaybackStates above
			recordState,			// See RecordStates above
			frameRate;				// Frame rate for visual

	Gap::Metadata *metadata;		// File information
};

/*
	Information passed to the Visual's Event function, such as mouse
	events.
 */
struct EventInfo
{
	enum Type { MouseFlag = 0x100,
				MouseEnter, MouseLeave, MouseMove,
				MouseUp, MouseDown };

	int	type;
	union
	{
		struct { int x, y; unsigned buttons; } mouse;

		char reserved[128];
	};
};

/*
	Visual base class
 */
class Visual : public Gbase::PluginObject, public State
{
protected:
	bool	refresh;

	// Do not let host program delete it (may not be in host program's memory)
	virtual ~Visual() {}

public:
	Visual( void )
	{
		width = height = 0;
		channels = 2; side = sBoth;
		samplingRate = 44100;
		playbackState = pStop;
		recordState = rStop;
		frameRate = 60;
		metadata = 0;
		refresh = true;
	}
	// Make sure visual is deleted from correct memory space
	virtual void gdecl Destroy( void ) { delete this; }

	/*
		Update bitmap to reflect new audio data.  'waveform' contains
		'Samples' sample amplitude pairs.  You cannot assume that
		'waveform' will be contiguous.  'waveform' will be null if audio has
		stopped playing from one call to the next.  This allows visuals to
		gradually fade to 0 when no audio is present.
		'frequency' contains a total of 'Frequencies' frequency magnitudes.
		This will also be null when no audio is present.
		'time' is the current playback time.  Note
		that time decreases when rewinding.  'time' may also be the
		countdown recording timer.

		Return true if bitmap was changed.  Return false otherwise.
	 */
	virtual bool gdecl Draw( DrawInfo & )
	{
		return false;
	}

	// Set size, sampling rate, channels, etc.
	virtual void gdecl SetState( const State &set, unsigned int flags )
	{
		if ( flags & sfChannels )
			channels = set.channels;
		if ( flags & sfSamplingRate )
			samplingRate = set.samplingRate;
		if ( flags & sfWidth )
			width = set.width;
		if ( flags & sfHeight )
			height = set.height;
		if ( flags & sfSide )
			side = set.side;
		if ( flags & sfPlaybackState )
			playbackState = set.playbackState;
		if ( flags & sfRecordState )
			recordState = set.recordState;
		if ( flags & sfFrameRate )
			frameRate = set.frameRate;
		if ( flags & sfFileMetadata )
			metadata = set.metadata;
		refresh = true;
	}

	// Process events, like mouse buttons, move, etc.
	virtual int gdecl Event( EventInfo & ) { return 0; }
};

/*---------------------------------------------------------------------
	Visual Plug-in interface to host program
 */

/*
	Version number.  If features are added in a backward compatible
	way, then the minor version number can be changed.  Otherwise
	the major version number must be changed.
 */
#define VisualVersion 3.0F

#ifdef _WIN32
# ifdef _MSC_VER
	/*
		Microsoft does not insert an underscore.  Need to add one.
		Do not compile with __stdcall convention.  Otherwise it
		will ignore the extern "C" convention and mangle the name.
	 */
#  define VisualInterfaceDll extern "C" __declspec(dllexport) Gvp::Interface *_GetVisualInterface
#else
	// Borland does insert one
#  define VisualInterfaceDll extern "C" __declspec(dllexport) Gvp::Interface *GetVisualInterface
# endif
# define VisualInterfaceApp "_GetVisualInterface"
#else
# define VisualInterfaceDll extern "C" Gvp::Interface *GetEffectInterface
# define VisualInterfaceApp "GetVisualInterface"
#endif

/*
	Structure that defines the name and abilities of a visual.  An array
	of these would be used to define a set of visuals.
 */
struct Table
{
	const wchar_t	*name;		// Visual name
	unsigned		abilities;
};

/*
	Type for visual constructor function used in Interface structure.
 */
typedef Visual *(gdecl *CreateFn)( const wchar_t *name );

/*
	This is the interface structure passed to GoldWave.
 */
struct Interface
{
	float			version;	// Visual version (usually 'VisualVersion')
	int	 			count;		// Number of visuals in this module

	Table			*list;		// Pointer to table of visuals in this module
	CreateFn		create;		// Function that creates a visual
	Gbase::ConfigFn	config;		// Function to configure module (usually null)
};

extern "C"
{
	typedef Interface *(*InterfaceFn)( void );
}

} /* Gvp namespace */

#pragma pack(pop)

#endif
