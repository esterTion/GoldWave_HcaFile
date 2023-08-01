/*
	GoldWave (C) 2003-2009 GoldWave Inc.
		To ensure compatibility with GoldWave, modified copies of
		this file must not be redistributed.

	Purpose:
		Declaration of PluginObject base class for all plug-ins.
		Declaration of Page class for property pages.
		Declaration of ConfigRead and ConfigWrite classes for
		reading and writing class settings/properties.
 */

#ifndef _GBASE_H_
#define _GBASE_H_

#ifdef _WIN32
/*
	Unfortunately Borland and Microsoft cannot agree on a standard
	calling convention.  Borland uses standard __cdecl.  Microsoft
	VC++ 6 uses proprietary 'thiscall' (even when the __cdecl
	compiler setting is selected).  This means we have to clutter
	the code with the 'gdecl' macro.
 */
#define gdecl __cdecl
#else
#define gdecl
#endif

#pragma pack(push, 4)

namespace Gbase
{

/*
	Prototype for configuration function used in Interface structure.
	This function usually presents a window when settings affecting
	the entire module can be modified.  The DirectX Audio Plug-in
	wrapper uses this interface to allow the user select plug-ins
	(which cannot be done automatically).
 */
typedef bool (gdecl *ConfigFn)( void *ParentWindow );

//---------------------------------------------------------------------------
/*
	Interface to the properties window.  This must be
	a modeless dialog window.
 */
enum PageAbility
{
	paApply = 1, 	// Apply button needed.  Changes are live otherwise.
	paResize = 2,	// Dialog can be resized
	paHelp = 4,		// Help is available
};

class Page
{
protected:
	// Ensure host program cannot delete DLL allocated memory
	virtual ~Page( void ) {}

public:
	// Delete this object and any associated data within DLL memory space
	virtual void gdecl				Destroy( void ) { delete this; }

	virtual void * gdecl			Handle( void *ParentWindow ) = 0;
	virtual void gdecl				Show( void ) = 0;
	virtual void gdecl				Hide( void ) = 0;
	virtual const wchar_t * gdecl	Help( void ) { return 0; }

	// Update page (enable buttons, etc.)
	virtual void gdecl				Update( void ) {}

	// Apply settings on this page, return true if settings are good
	virtual bool gdecl	 			Apply( void ) { return true; }

	// Resize the page to the give width and height
	virtual void gdecl	 			Resize( int width, int height ) {}
	// Return width and height of dialog in pixels
	virtual int gdecl	 			Width( void ) = 0;
	virtual int gdecl				Height( void ) = 0;

	// Return the PageAbility flags
	virtual unsigned gdecl			Ability( void ) { return 0; }
};

//---------------------------------------------------------------------------
/*
	Class to pass settings into a plug-in.  The host program derives this
	class to replace the two virtual Read functions so it can store the
	settings whereever it wants to.
 */
class ConfigException {};
class ConfigRead
{
public:
	virtual bool gdecl ReadString( wchar_t *string, int len ) = 0;
	virtual bool gdecl Read( void *data, int len ) = 0;
	template <class T> bool Read( T &value )
	{
		return Read( (void *)&value, sizeof(T) );
	}
	template <class T> ConfigRead &operator>>( T &value )
	{
		if ( ! Read( value ) )
			throw ConfigException();
		return *this;
	}
};

/*
	Class to get settings from a plug-in.  The host program derives this
	class to replace the two virtual Write functions.
 */
class ConfigWrite
{
public:
	virtual bool gdecl WriteString( const wchar_t *string ) = 0;
	virtual bool gdecl Write( const void *data, int len ) = 0;
	template <class T> bool Write( const T &value )
	{
		return Write( (const void *)&value, sizeof(T) );
	}
	template <class T> ConfigWrite &operator<<( const T &value )
	{
		if ( ! Write( value ) )
			throw ConfigException();
		return *this;
	}
};

//---------------------------------------------------------------------------

enum BaseAbility
{
	baPage = 0x01,			// Has a user interface page
	baReserved = 0x20		// 0x02 to 0x10 reserved
};
#define AbilityFlag(x) (Gbase::baReserved << (x))

/*
	Base class for all plug-in objects.
 */
class PluginObject
{
protected:
	virtual ~PluginObject( void ) {}
public:
	/*
		This pointer can be used by the program to store host program
		specific information within plug-ins.  The plug-ins themselves
		should not use this object in any way.  The host program
		is responsible for the object.
	 */
	void *programdata;
	PluginObject( void ) { programdata = 0; }

	// Make sure plug-in is deleted from correct memory space
	virtual void gdecl 				Destroy( void ) { delete this; }
	// Returns the name of the plug-in
	virtual const wchar_t * gdecl	Name( void ) = 0;
	// Returns whatever ability flags are supported by the plug-in
	virtual unsigned gdecl			Ability( void ) { return 0; }

	// Return a pointer to a properties page handler
	virtual Page * gdecl			GetPage( void ) { return 0; }
	// Retrieves settings from the effect (store them with WriteConfig)
	virtual bool gdecl				Get( ConfigWrite &write ) { return false; }
	// Sets settings for the effect (read them with ReadConfig)
	virtual bool gdecl	 			Set( ConfigRead &read ) { return false; }
};

}	// namespace Gbase

#pragma pack(pop)

#endif
