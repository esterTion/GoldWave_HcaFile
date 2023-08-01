/*
	Mute example Copyright (c) 2003-2008 GoldWave Inc.

	Trivial example of how to create an Effect plug-in for GoldWave.
 */

#include <windows.h>	// Needed for DllMain function only
#pragma hdrstop
#include "gweffect.h"	// Effect plug-in base class

using namespace Gfx;	// Plug-in namespaces

//---------------------------------------------------------------------------
// Host Program Interface

Effect * gdecl	Create( const wchar_t *name );
Table			MuteTable = { L"Sample Mute", 0, 0 };
Interface		MuteInterface = { EffectVersion, 1, &MuteTable, Create, 0 };

BOOL WINAPI DllMain( HINSTANCE, DWORD, LPVOID )
{
	return 1;
}

EffectInterfaceDll( void )
{
	return &MuteInterface;
}

//---------------------------------------------------------------------------
// Derived Effect

class Mute : public Effect
{
public:
	const wchar_t * gdecl	Name( void ) { return MuteTable.name; }
	unsigned int gdecl	Ability( void ) { return MuteTable.abilities; }
	int gdecl			Read( audio *data, int samples );
	bool gdecl			Seek( double time ) { return source->Seek( time ); }
};

int gdecl Mute::Read( audio *dest, int samples )
{
	// Read audio from source first
	samples = source->Read( dest, samples );

	// See what channels may be modified
	bool doLeft = channel & acfLeft,
		 doRight = (channel & acfRight) && channels > 1;
	int  count = samples;

	// Mute samples
	while ( count-- )
	{
		if ( doLeft )
			dest[0] = 0;
		if ( doRight )
			dest[1] = 0;
		dest += channels;
	}

	return samples;
}

//---------------------------------------------------------------------------
// Effect Creator Function

Effect * gdecl Create( const wchar_t *name )
{
	if ( name && wcscmp( name, MuteTable.name ) == 0 )
		return new Mute;
	return 0;
};

//---------------------------------------------------------------------------

