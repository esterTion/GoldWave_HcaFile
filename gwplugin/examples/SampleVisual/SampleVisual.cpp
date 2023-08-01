/*
	SampleVisual Copyright 2003-2008 GoldWave Inc.

	Demonstrates how to create a Visual plug-in for GoldWave.  Several
	visuals are contained in this module: a time/status visual called
	SampleTime, a waveform visual called SampleWave, a event visual
	called SampleMouse, an information visual called SampleInfo, and
	a frame rate visual call SampleFrameRate.

	The SampleTime visual would be considered the "Hello World" basic example.
	The SampleWave visual is a more complete example, showing the use of
	the waveform data, displaying a properties page, and
	reading/writing of properties.  The SampleMouse visual provides a
	simple demonstration of using mouse events.  The SampleInfo visual shows
	how to display file information such as artist, album, etc.

	The interface to the host program requires a filled VisualInterface,
	which provides:
		- the interface version (usually 'VisualVersion')
		- the number of visuals contained in this module
		- a Table array containing the list of visuals
		- a visual constructor function

	Finally, an exported VisualInterfaceDll function is needed.
	The host program uses that function to retrieve the VisualInterface.
 */

#include <windows.h>
#pragma hdrstop
#include "gwvisual.h"

using namespace Gvp;
using namespace Gbase;

static const wchar_t	Font[] = L"MS San Serif";

//---------------------------------------------------------------------------

// Enumeration of all the visuals in this module, plus total count
enum { iTime, iWave, iMouse, iInfo, iFrameRate, iTotal };

/*
	This sample visual module has four visuals: a time visual, a wave
	visual, a mouse event visual, and an information visual.

	The time visual can be displayed only in the Status
	window in GoldWave.  The wave and mouse visuals can be displayed in any
	graph window (left or right) and has a property window.  The info visual
	can be displayed anywhere.

	The names and display positions are stored within the
	Table array below.
 */
Table	SampleTable[] =
{
	{ L"Sample Time", aStatus },
	{ L"Sample Wave", aGraph | aPage },
	{ L"Sample Mouse", aGraph | aEvent },
	{ L"Sample Info", aAny },
	{ L"Sample Frame Rate", aAny },
};

/*
	Prototypes for visual constructor
 */
Visual * gdecl VisualCreate( const wchar_t *name );

/*
	Make an Interface structure that host program uses to create
	visuals in this module.
 */
Interface SampleInterface =
{
	VisualVersion,
	iTotal,
	SampleTable,
	VisualCreate,
	0
};

VisualInterfaceDll( void )
{
	return &SampleInterface;
}

//---------------------------------------------------------------------------

#ifdef _WIN32
// Windows DLL entry point
HINSTANCE gInstance;

BOOL WINAPI DllMain( HINSTANCE instance, DWORD, LPVOID )
{
	gInstance = instance;

	return 1;
}
#endif

//---------------------------------------------------------------------------
/*
	This template takes care of the Name/Ability functions automatically
	for all the visuals in this module.  It maps the Index parameter into
	the table above so that the visual's name and abilities only need to
	appear in one place.
 */

template <int Index> class SampleVisual : public Visual
{
public:
	const wchar_t * gdecl	Name( void ) { return SampleTable[Index].name; }
	unsigned int gdecl	Ability( void ) { return SampleTable[Index].abilities; }
};

//---------------------------------------------------------------------------
// Helper function to clear the visual device context with the given color 'c'

void Box( HDC hdc, int x, int y, int width, int height, DWORD c = 0 )
{
	HBRUSH	hbr = CreateSolidBrush( c ), oldbr;

	oldbr = (HBRUSH)SelectObject( hdc, hbr );

	PatBlt( hdc, x, y, width, height, PATCOPY );

	SelectObject( hdc, oldbr );
	DeleteObject( hbr );
}

//---------------------------------------------------------------------------
//						SampleTime Visual
//---------------------------------------------------------------------------

/*
	To create the Time visual:
		- Create a derived class from the Visual base class
			(SampleVisual template is used here instead).
		- Make new constructors and destructors to manage the font.
		- Override the Name() function to pass back the unique name for
			this visual.  Done already by the SampleVisual template.
		- Override the Ability() function.  Done already by the
			SampleVisual template.
		- Override the Draw() function to draw the visual.  The Draw()
		  function is called 60 times per second by the host program.
		  The DrawInfo structure contains the current playback
		  or recording position in the 'time' member.
 */
class SampleTime : public SampleVisual<iTime>
{
	double	oldtime;
	HFONT	font;

	~SampleTime( void ) { if ( font ) DeleteObject( font ); }

public:
	SampleTime( void ) { font = 0; }
	bool gdecl Draw( DrawInfo &info );
};

#include <stdio.h>
#include <string.h>

bool gdecl SampleTime::Draw( DrawInfo &info )
{
	// If visual size is too small, do nothing
	if ( width <= 0 || height <= 0 )
		return false;

	/*
		'refresh' is set to true if any visual settings have changed,
		such as the width or height.  Usually the entire visual has to
		be redrawn.  To minimize processing overhead, do not draw the
		visual if the time has not changed.
	 */
	if ( refresh || oldtime != info.time )
	{
		HDC		hdc = info.imageDC;
		HFONT	oldf;
		SIZE	size;
		wchar_t	time[30];
		int		len;

		Box( hdc, 0, 0, width, height, 0xC00000 );

		// No point drawing text if window is too small
		if ( height > 3 )
		{
			if ( ! font )
			{
				int ppi = GetDeviceCaps( hdc, LOGPIXELSY ), point;

				point = -23 * 72 / ppi;

				font = CreateFont( point, 0, 0, 0, FW_BOLD,
							0, 0, 0, 0, 0, 0, 0, 0, Font );
			}

			int		minutes = (int)info.time / 60,
					seconds = (int)info.time % 60,
					fraction = (int)((info.time - (int)info.time) * 1000);

			oldf = (HFONT)SelectObject( hdc, font );
			SetTextColor( hdc, 0x00FFFF );
			SetBkMode( hdc, TRANSPARENT );
			len = swprintf( time, L"%03d:%02d.%03d", minutes, seconds, fraction );
			GetTextExtentPoint32( hdc, time, len, &size );
			TextOut( hdc, (width - size.cx) / 2,
					 (height - size.cy) / 2, time, len );
			SelectObject( hdc, oldf );
		}

		/*
			IMPORTANT!
			Care must be taken to ensure all resources are freed.  Otherwise
			it will set the calling application in a bad state (and may cause
			a user to lose hours of work).
		 */

		oldtime = info.time;

		// Image has been refreshed
		refresh = false;

		// Image was updated
		return true;
	}

	// Image was not updated
	return false;
}

//---------------------------------------------------------------------------
//						SampleWave Visual
//---------------------------------------------------------------------------

/*
	To create the Wave visual:
		- Create a derived class from the Visual base class
			(SampleVisual template is used here instead).
		- Override the Name() function to pass back the unique name for
		  this visual.  Done in SampleVisual template.
		- Override the Ability() function.  Done in SampleVisual template.
		- Make a constructor to initialize current colours.
		- Override the Draw() function to draw the visual.  Use the
		  data in the 'waveform' member array to draw the wave.
		  The data is interlaced floating point left and right
		  amplitudes: L, R, L, R, ...
		  There are a total count of 'VisualSamples' left and
		  right pairs.
		- Override Page function to create property page.
		- Override Set and Get functions to store/retrieve properties.
 */

#include "SampleVisual.rh"

// Property data
const char 	*ColourList[] = { "Black", "Red", "Green", "Blue", "Yellow" };
const DWORD ColourValue[] = { 0x0, 0xFF, 0xFF00, 0xFF0000, 0x00FFFF };
const int	ColourCount = sizeof(ColourList) / sizeof(*ColourList);

// SampleWave visual class
class WavePage;
class SampleWave : public SampleVisual<iWave>
{
	bool		flat;

public:
	int			back, wave;
	WavePage	*page;

	SampleWave( void );
	bool gdecl			Draw( DrawInfo &info );
	bool gdecl			Get( ConfigWrite &write );
	bool gdecl			Set( ConfigRead &read );
	Page * gdecl		GetPage( void );

	void Repaint( void ) { refresh = true; }
};

SampleWave::SampleWave( void )
{
	flat = false;
	back = 0;		// Black background
	wave = 2;		// Green waveform
	page = 0;
}

/*
	Draws the waveform visual into the 'imageDC' context
 */
bool gdecl  SampleWave::Draw( DrawInfo &info )
{
	// If visual size is too small, do nothing
	if ( width <= 0 || height <= 0 )
		return false;

	HDC		hdc = info.imageDC;

	// Are there any amplitudes to graph?
	if ( ! info.waveform )
	{
		// No new amplitudes, set visual to default state
		if ( refresh || ! flat )
		{
			// Blank display
			Box( hdc, 0, 0, width, height, ColourValue[back] );

			// Draw flat line
			Box( hdc, 0, height / 2, width, 1, ColourValue[wave] );

			refresh = false;
			flat = true;
			// Image was updated
			return true;
		}
		// No update required
		return false;
	}

	/*
		If paused and no refresh required, no update required.  It is
		important to minimize processor load as much as possible.
	 */
	if ( info.state == dsPaused && ! refresh )
		return false;

	refresh = false;

	// Wave will not be flat after this point (most likely)
	flat = false;

	Box( hdc, 0, 0, width, height, ColourValue[back] );

	// Get a pointer to the most recent amplitudes near the end
	int		points = Samples < width ? Samples : width;
	float	*amp = info.waveform + (Samples - points) * 2;
	int 	mid = height / 2;
	HPEN	hpen = CreatePen( PS_SOLID, 1, ColourValue[wave] ), oldpen;

	oldpen = (HPEN)SelectObject( hdc, hpen );

	// If this visual is displayed on the right side, graph right data only
	if ( side == sRight )
		amp++;

	MoveToEx( hdc, 0, mid + (int)(*amp * mid), 0 );
	amp += 2;
	for ( int n = 1; n < points; n++, amp += 2 )
		LineTo( hdc, n, mid + (int)(*amp * mid) );

	/*
		IMPORTANT!
		Care must be taken to ensure all resources are freed.  Otherwise
		it will set the calling application in a bad state (and cause
		a user to lose hours of work).
	 */
	SelectObject( hdc, oldpen );
	DeleteObject( hpen );

	// Image was updated
	return true;
}

//---------------------------------------------------------------------------

/*
	WavePage manages an interface for the effect.  It must:
		- Create a child dialog (one that can be contained in another dialog)
		- Return the size of the window to the application
		- Handle events like Show, Hide, Apply, Resize (optional)
 */
class WavePage : public Page
{
public:
	HWND		hwnd;
	SampleWave	&wave;

	WavePage( SampleWave &v ) : wave( v )
	{
		hwnd = 0;
	}
	void gdecl Destroy( void )
	{
		wave.page = 0;
		DestroyWindow( hwnd );
		Page::Destroy();
	}
	void * gdecl Handle( void *parent )
	{
		if ( ! hwnd )
		{
			LPCTSTR	res = MAKEINTRESOURCE( IDD_WAVEPROPS );
			hwnd = CreateDialog( gInstance, res, (HWND)parent, 0 );
			for ( int n = 0; n < ColourCount; n++ )
			{
				SendDlgItemMessage( hwnd, IDC_WAVECOLOUR,
									CB_ADDSTRING, 0, (LPARAM)ColourList[n] );
				SendDlgItemMessage( hwnd, IDC_BACKCOLOUR,
									CB_ADDSTRING, 0, (LPARAM)ColourList[n] );
			}
			Init();
		}
		return hwnd;
	}
	void Init( void )
	{
		SendDlgItemMessage( hwnd, IDC_WAVECOLOUR,
							CB_SETCURSEL, wave.wave, 0 );
		SendDlgItemMessage( hwnd, IDC_BACKCOLOUR,
							CB_SETCURSEL, wave.back, 0 );
	}
	bool gdecl Apply( void )
	{
		// EnterCriticalSection
		wave.wave = SendDlgItemMessage( hwnd, IDC_WAVECOLOUR,
											CB_GETCURSEL, 0, 0 );
		wave.back = SendDlgItemMessage( hwnd, IDC_BACKCOLOUR,
											CB_GETCURSEL, 0, 0 );
		// LeaveCriticalSection
		wave.Repaint();

		return true;
	}
	void gdecl Show( void ) { ::ShowWindow( hwnd, SW_SHOW ); }
	void gdecl Hide( void ) { ::ShowWindow( hwnd, SW_HIDE ); }
	int	gdecl  Width( void )
	{
		RECT	r;
		::GetWindowRect( hwnd, &r );
		return r.right - r.left;
	}
	int gdecl  Height( void )
	{
		RECT	r;
		::GetWindowRect( hwnd, &r );
		return r.bottom - r.top;
	}

	/*
		For simplicity, the user has to press Apply for the new settings
		to take effect.  Real-time changes are possible (though
		it gets complicated using raw WIN32 API).  Use VCL, MFC,
		or something else instead.
	 */
	unsigned gdecl Ability( void ) { return paApply; }
};

/*
	Store volume into the provided ConfigWrite.
 */
const int WavePropertiesVersion = 0x100;

bool gdecl SampleWave::Get( ConfigWrite &write )
{
	try
	{
		// Save a version number in case changes are made in the future
		write << WavePropertiesVersion;

		write << wave;
		write << back;
	}
	catch( ... )
	{
		// Do not let ConfigException get to application
		return false;
	}
	return true;
}

/*
	Read the volume from the provided ConfigRead.  Also note that if
	a page is currently being displayed, the volume in the edit control
	must be updated as well.  All exceptions must be trapped at the DLL
	level.  Borland and Microsoft exception handling may be incompatible.
 */
bool gdecl SampleWave::Set( ConfigRead &read )
{
	try
	{
		int	version;

		read >> version;

		// Make sure version number is correct
		if ( version == WavePropertiesVersion )
		{
			// EnterCriticalSection
			read >> wave;
			read >> back;
			// LeaveCriticalSection

			Repaint();

			if ( page )
				page->Init();
		}
	}
	catch( ... )
	{
		// Do not let ConfigException get to application (compiler specific)
		return false;
	}

	return true;
}

/*
	Create the page and return it to the application.
 */
Page * gdecl SampleWave::GetPage( void )
{
	// Return existing page or new one?
	if ( ! page )
		page = new WavePage( *this );
	return page;
}

//---------------------------------------------------------------------------
//						SampleMouse Visual
//---------------------------------------------------------------------------

/*
	To create the Mouse visual:
		- Create a derived class from the Visual base class
			(SampleVisual template is used here instead).
		- Override the Name() function to pass back the unique name for
		  this visual.  Done in SampleVisual template.
		- Override the Ability() function.  Done in SampleVisual template.
		- Make a constructor to initialize the mouse state.
		- Override the Event() function to store the current mouse state
		- Override the Draw() function to draw the visual to reflect the
		  current mouse state.

 */
class SampleMouse : public SampleVisual<iMouse>
{
	bool	in, down, update;
	int		x, y;

public:
	SampleMouse( void ) { in = down = false; update = true; }
	bool gdecl Draw( DrawInfo &info );
	int gdecl Event( EventInfo &event );
};

bool gdecl SampleMouse::Draw( DrawInfo &info )
{
	// If visual size is too small, do nothing
	if ( width <= 0 || height <= 0 )
		return false;

	/*
		'refresh' is set to true if any visual settings have changed.
		'update' is set to true if a mouse event occurred.
		To minimize processing overhead, do not draw the
		visual if nothing has changed.
	 */
	if ( refresh || update )
	{
		HDC		hdc = info.imageDC;

		/*
			Draw a different colour background depending on mouse
			location/state:
			- Black when mouse is outside visual
			- Green when mouse is in visual
			- Red when mouse is in visual and button is down
		 */
		if ( ! in )
			Box( hdc, 0, 0, width, height, 0 );
		else if ( ! down )
			Box( hdc, 0, 0, width, height, 0x008000 );
		else
			Box( hdc, 0, 0, width, height, 0x0000FF );

		// Draw mouse pointer crosshair (only if inside)
		if ( in )
		{
			Box( hdc, 0, y, width, 1, 0xFFFFFF );
			Box( hdc, x, 0, 1, height, 0xFFFFFF );
		}

		// Image has been refreshed
		refresh = false;
		update = false;	// Critical section may be needed

		// Image was updated
		return true;
	}

	// Image was not updated
	return false;
}

int gdecl SampleMouse::Event( EventInfo &event )
{
	switch( event.type )
	{
		case EventInfo::MouseEnter:
		case EventInfo::MouseMove:
			in = true;
			x = event.mouse.x;
			y = event.mouse.y;
			break;
		case EventInfo::MouseDown:
			down = true;
			break;
		case EventInfo::MouseUp:
			down = false;
			break;
		case EventInfo::MouseLeave:
			in = false;
			break;
	}
	update = true;
	return 0;
}

//---------------------------------------------------------------------------
//						SampleInfo Visual
//---------------------------------------------------------------------------

/*
	To create the Info visual:
		- Include the gwaudio.h file for the InfoList class
		- Create a derived class from the Visual base class
			(SampleVisual template is used here instead).
		- Override the Name() function to pass back the unique name for
		  this visual.  Done in SampleVisual template.
		- Override the Ability() function.  Done in SampleVisual template.
		- Make a constructor to initialize the plug-in state.
		- Override the Draw() function to draw the visual.

 */
#include "gwaudio.h"
#define GWINCLUDESTRINGS
#include "gwmetadata.h"

static const wchar_t *Sequence[] = {
	Gmd::GWText_Title,
	Gmd::GWText_Author,
	Gmd::GWText_Album,
	Gmd::GWText_Copyright
};

class SampleInfo : public SampleVisual<iMouse>
{
	int		item;
	DWORD	time;
	HFONT	font;

	~SampleInfo( void ) { if ( font ) DeleteObject( font ); }

public:
	SampleInfo( void ) { item = 0; time = 0; font = 0; }
	bool gdecl Draw( DrawInfo &info );
};

bool gdecl SampleInfo::Draw( DrawInfo &draw )
{
	// If visual size is too small, do nothing
	if ( width <= 0 || height <= 0 )
		return false;

	/*
		'refresh' is set to true if any visual settings have changed.
		To minimize processing overhead, do not draw the
		visual if nothing has changed.
	 */
	bool update = refresh;

	// Update item every 2 seconds
	if ( metadata && GetTickCount() - time > 2000 )
	{
		time = GetTickCount();
		update = true;
		++item;
		if ( item >= (int)(sizeof(Sequence) / sizeof(*Sequence)) )
			item = 0;
	}

	if ( update )
	{
		HDC				hdc = draw.imageDC;
		const wchar_t	*text;
		HFONT			oldf;
		SIZE			size;
		int				len;

		text = metadata ? metadata->GetText( Sequence[item] ) : 0;
		Box( hdc, 0, 0, width, height, 0 );

		// Image has been refreshed
		refresh = false;

		// Make sure there is some info to draw
		if ( ! text )
			return update;

		if ( ! font )
			font = CreateFont( -11, 0, 0, 0, FW_NORMAL,
								0, 0, 0, 0, 0, 0, 0, 0, Font );

		oldf = (HFONT)SelectObject( hdc, font );
		SetTextColor( hdc, 0x00FF00 );
		SetBkMode( hdc, TRANSPARENT );
		len = wcslen( text );
		GetTextExtentPoint32( hdc, text, len, &size );
		TextOut( hdc, (width - size.cx) / 2,
				 (height - size.cy) / 2, text, len );
		SelectObject( hdc, oldf );
	}

	return update;
}

//---------------------------------------------------------------------------
//						SampleFrameRate Visual
//---------------------------------------------------------------------------

/*
	Use this visual to check the frame rate of all visuals.  If your
	visual does a lot of processing, it may affect the frame rate of
	other visuals.
 */
class SampleFrameRate : public SampleVisual<iFrameRate>
{
	DWORD	ticks, frames;
	HFONT	font;

	~SampleFrameRate( void ) { if ( font ) DeleteObject( font ); }

public:
	SampleFrameRate( void ) { font = 0; ticks = 0; frames = 0; }
	bool gdecl Draw( DrawInfo &info );
};

bool gdecl SampleFrameRate::Draw( DrawInfo &info )
{
	// If visual size is too small, do nothing
	if ( width <= 0 || height <= 0 )
		return false;

	/*
		'refresh' is set to true if any visual settings have changed,
		such as the width or height.  Usually the entire visual has to
		be redrawn.  To minimize processing overhead, draw the
		visual once a second.
	 */

	DWORD now = GetTickCount();
	++frames;
	if ( refresh || now >= ticks + 1000 || ticks > now /* handle roll over */ )
	{
		HDC		hdc = info.imageDC;
		HFONT	oldf;
		SIZE	size;
		wchar_t	time[30];
		int		len, rate;

		Box( hdc, 0, 0, width, height, 0 );

		if ( refresh )
			rate = frameRate;	// Frame count not accurate when refreshing
		else
			rate = frames;

		frames = 0;
		ticks = now;

		// No point drawing text if window is too small
		if ( height > 3 )
		{
			if ( ! font )
			{
				int ppi = GetDeviceCaps( hdc, LOGPIXELSY ), point;

				point = -23 * 72 / ppi;

				font = CreateFont( point, 0, 0, 0, FW_BOLD,
							0, 0, 0, 0, 0, 0, 0, 0, Font );
			}

			oldf = (HFONT)SelectObject( hdc, font );
			SetTextColor( hdc, 0x00FF00 );
			SetBkMode( hdc, TRANSPARENT );
			len = swprintf( time, L"%02d/%02d", rate, frameRate );
			GetTextExtentPoint32( hdc, time, len, &size );
			TextOut( hdc, (width - size.cx) / 2,
					 (height - size.cy) / 2, time, len );
			SelectObject( hdc, oldf );
		}

		// Image has been refreshed
		refresh = false;

		// Image was updated
		return true;
	}

	// Image was not updated
	return false;
}

//---------------------------------------------------------------------------

/*
	Visual constructor.  Takes a string containing the name of the
	visual and creates an instance of the corresponding visual.
 */
Visual * gdecl VisualCreate( const wchar_t *name )
{
	int n = 0;

	if ( ! name )
		return 0;

	while ( n < iTotal && wcscmp( name, SampleTable[n].name ) != 0 )
		n++;

	switch ( n )
	{
		case iTime:			return new SampleTime;
		case iWave:			return new SampleWave;
		case iMouse:		return new SampleMouse;
		case iInfo:			return new SampleInfo;
		case iFrameRate:	return new SampleFrameRate;

		default:
			return 0;
	}
};

