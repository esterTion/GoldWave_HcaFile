/*
	SampleEffect Copyright (c) 2003-2008 GoldWave Inc.

	Demonstrates how to create an Effect plug-in for GoldWave.

	The interface to the host program requires a filled Interface structure,
	which provides:
		- the interface version (usually 'EffectVersion')
		- the number of effects contained in this module
		- a Table array containing the list of effects
		- an effect constructor function
	Finally, an exported EffectInterfaceDll() function is needed.
	The host program uses that function to retrieve the Interface.
 */

#include <windows.h>
#pragma hdrstop
#include "gweffect.h"
#include "SampleEffect.rh"
#include "stdio.h"			// sprintf

using namespace Gfx;
using namespace Gbase;

//---------------------------------------------------------------------------

// Enumeration of all effects in the module, plus total count
enum { iInvert, iVolume, iTotal };

/*
	Table listing all effects in this module.  Each element provides
	the effect name, ability flags, and icon resource ID.
 */
Table	SampleTable[] =
{
	{ L"Sample Invert", 0, IDI_SAMPLEINVERT },
	{ L"Sample Volume", aPage, IDI_SAMPLEVOLUME }
};

/*
	Prototype for effect constructor
 */
Effect * gdecl EffectCreate( const wchar_t *name );

/*
	Make an Interface structure that host program uses to find and create
	effects in this module.
 */
Interface SampleInterface =
{
	EffectVersion,
	iTotal,
	SampleTable,
	EffectCreate,
	0				// No configuration function
};

/*
	 This function is called by the application to get the table.  The
	 table could be created dynamically.
*/
EffectInterfaceDll( void )
{
	return &SampleInterface;
}

//---------------------------------------------------------------------------

#ifdef _WIN32
// Windows DLL entry point
HINSTANCE	gInstance;

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

template <int Index> class SampleEffect : public Effect
{
public:
	const wchar_t * gdecl	Name( void ) { return SampleTable[Index].name; }
	unsigned int gdecl	Ability( void ) { return SampleTable[Index].abilities; }
};

//---------------------------------------------------------------------------
/*
	To create the Invert effect is very simple:
		- Derive a class from the SampleEffect base class template.
		- The Name and Ability members are automatically implemented
			by the SampleEffect base class.
		- Override the Read() function to perform the inversion effect.
 */

class SampleInvert : public SampleEffect<iInvert>
{
public:
	int gdecl Read( audio *data, int samples );
};

int gdecl SampleInvert::Read( audio *dest, int samples )
{
	// Read audio from source first
	samples = source->Read( dest, samples );

	// See what channels may be modified
	bool	doLeft = channel & acfLeft,
			doRight = (channel & acfRight) && channels > 1;

	int	count = samples;

	// Process all samples
	while ( count-- )
	{
		if ( doLeft )
			dest[0] = -dest[0];
		if ( doRight )
			dest[1] = -dest[1];
		dest += channels;
	}

	/*
		Return the number of samples processed.  An effect must return the
		full number of requested samples, unless the source does not
		provide that many.
	 */
	return samples;
}

//---------------------------------------------------------------------------
/*
	To create the Volume effect is a little more complicated because a
	Page interface is required.  The first three steps are the same
	as the SampleInvert effect.
		- Derive a class from the SampleEffect base class template.
		- Override the Name() function to pass back the unique name for
			this effect.  Done by SampleEffect template.
		- Override the Ability() function to return the "Page" ability.
			Done by SampleEffect template.
		- Override the Read() function to perform the effect.
		- Override the Set() and Get() functions to store/retrieve settings.
		- Derive a class from the EffectPage base class to manage the window.
		- Override the Page() function to return the derived EffectPage.
 */
class VolumePage;
class SampleVolume : public SampleEffect<iVolume>
{
public:
	float		volume;
	VolumePage	*page;

	SampleVolume( void ) { volume = 1; page = 0; }
	bool gdecl Source( Transform *s )
	{
		return Effect::Source( s );
	}
	bool gdecl		Get( ConfigWrite &write );
	bool gdecl		Set( ConfigRead &read );
	int gdecl 		Read( audio *data, int samples );
	Page * gdecl	GetPage( void );
};

/*
	The Read() member function does all the effect processing.
	Note! A Read() call may occur at any time after the Source() call.
	It is often called from a separate thread.
	For example, a Read() may occur while in the middle of Set()
	or EffectPage::Apply() processing.
	For more complex effects, it is extremely important to use
	critical sections in the Read(), Set() and any member
	functions that change data used within Read().
	The Read() function should never try to change anything on the
	dialog window (such as calling page->UpdateText()).
	The thread does not process window messages and	deadlock will occur.
 */
int gdecl SampleVolume::Read( audio *dest, int samples )
{
	// Read audio from source first
	samples = source->Read( dest, samples );

	// EnterCriticalSection

	// See what channels may be modified
	bool	doLeft = channel & acfLeft,
			doRight = (channel & acfRight) && channels > 1;

	int	count = samples;

	// Process all samples read
	while ( count-- )
	{
		if ( doLeft )
			dest[0] *= volume;
		if ( doRight )
			dest[1] *= volume;
		dest += channels;
	}

	// LeaveCriticalSection

	return samples;
}

//---------------------------------------------------------------------------
/*
	VolumePage manages an interface for the effect.  It must:
		- Create a child dialog (one that can be contained in another dialog)
		- Return the size of the window to the application
		- Handle events like Show, Hide, Apply, Resize (optional)
 */
#include <commctrl.h>
class VolumePage : public Page
{
public:
	HWND			hwnd;
	SampleVolume	&effect;

	VolumePage( SampleVolume &e ) : effect( e )
	{
		hwnd = 0;
	}
	void gdecl Destroy( void )
	{
		effect.page = 0;
		DestroyWindow( hwnd );
		Page::Destroy();
	}
	void * gdecl Handle( void *parent )
	{
		if ( ! hwnd )
		{
			LPCTSTR	res = MAKEINTRESOURCE( IDD_VOLUMESETTING );
			hwnd = CreateDialog( gInstance, res, (HWND)parent, 0 );
			UpdateText();

/*
			MoveWindow( hwnd, 0, 0, 200, 200, false );
			HWND h = CreateWindowEx( WS_EX_CONTROLPARENT, WC_TABCONTROL, 0,
						WS_CHILD | WS_VISIBLE | WS_TABSTOP,
						50, 50, 150, 150, hwnd, 0, gInstance, 0 );
			TCITEM tie;
			tie.mask = TCIF_TEXT | TCIF_IMAGE;
			tie.iImage = -1;
			tie.pszText = L"Page 1";
			TabCtrl_InsertItem( h, 0, &tie );
			tie.pszText = L"Page 2";
			TabCtrl_InsertItem( h, 1, &tie );

			h = CreateWindowEx( WS_EX_CONTROLPARENT, L"BUTTON", 0,
						BS_GROUPBOX | WS_CHILD | WS_VISIBLE | WS_GROUP,
						25, 30, 100, 100, h, 0, gInstance, 0 );
			CreateWindow( L"COMBOBOX", L"Drop Down", CBS_DROPDOWN
						| CBS_AUTOHSCROLL | WS_CHILD | WS_VISIBLE
						| WS_VSCROLL | WS_TABSTOP,
						10, 20, 80, 20, h, 0, gInstance, 0 );
*/
		}
		return hwnd;
	}
	bool gdecl Apply( void )
	{
		const int	chars = 20;
		wchar_t 	text[chars];

		GetDlgItemText( hwnd, IDC_VOLUME, text, chars );

		// EnterCriticalSection

		effect.volume = (float)_wtof( text );
		// Should check range and show error messages as appropriate here
		if ( effect.volume > 2 )
		{
			effect.volume = 2;
			UpdateText();
		}
		else if ( effect.volume < -2 )
		{
			effect.volume = -2;
			UpdateText();
		}

		// LeaveCriticalSection

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
		For simplicity, the user has to press Apply for the new volume
		level to take effect.  Real-time changes are possible (though
		it gets complicated using raw WIN32 API).  Use VCL, MFC,
		or something else instead.
	 */
	unsigned gdecl Ability( void ) { return paApply; }

	void gdecl UpdateText( void )
	{
		wchar_t		val[20];
		swprintf( val, L"%.2f", effect.volume );
		SetDlgItemText( hwnd, IDC_VOLUME, val );
	}
};

/*
	Store volume into the provided ConfigWrite object.
 */
const int VolumePropertiesVersion = 0x100;

bool gdecl SampleVolume::Get( ConfigWrite &write )
{
	try
	{
		// Save a version number in case plug-in changes in the future
		write << VolumePropertiesVersion;

		write << volume;
	}
	catch( ... )
	{
		// Do not let ConfigException get to application
		return false;
	}
	return true;
}

/*
	Read the volume from the provided ConfigRead object.  Also note that if
	a page is currently being displayed, the volume in the edit control
	must be updated as well.  All exceptions must be trapped at the DLL
	level.  Borland and Microsoft exception handling may be incompatible.
 */
bool gdecl SampleVolume::Set( ConfigRead &read )
{
	try
	{
		int	version;

		read >> version;

		// Make sure version number is correct
		if ( version == VolumePropertiesVersion )
		{
			// EnterCriticalSection
			read >> volume;
			// LeaveCriticalSection

			if ( page )
				page->UpdateText();
		}
	}
	catch( ... )
	{
		// Do not let ConfigException get to application
		return false;
	}

	return true;
}

/*
	Create the page and return it to the application.
 */
Page * gdecl SampleVolume::GetPage( void )
{
	// Return existing page or new one?
	if ( ! page )
		page = new VolumePage( *this );
	return page;
}

//---------------------------------------------------------------------------
/*
	Effect constructor.  Takes a string containing the name of the
	effect and creates an object of the corresponding effect.
 */
Effect * gdecl EffectCreate( const wchar_t *name )
{
	if ( ! name )
		return 0;

	if ( wcscmp( name, SampleTable[iInvert].name ) == 0 )
		return new SampleInvert;
	else if ( wcscmp( name, SampleTable[iVolume].name ) == 0 )
		return new SampleVolume;
	return 0;
};

//---------------------------------------------------------------------------

