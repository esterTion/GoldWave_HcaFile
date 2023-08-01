/*
	Device Plug-in Interface (C) 2007 GoldWave Inc.

	Purpose:
		Classes for audio device interface encapsulation.
*/

#ifndef _GDEVICE_H_
#define _GDEVICE_H_

#pragma hdrstop
#include "gwbase.h"
#include "gwerror.h"
#include "gwaudiotype.h"

#pragma pack(push, 4)

namespace Gdp	// GoldWave Device Plugin namespace
{

#ifdef _WIN32
typedef __int64 int64;  // Use as Gdp::int64
#else
#error Unimplemented
#error
#endif

enum Quality { qPCM16, qPCM24, qFloat32 };
enum Status { Closed, Opened, Streaming, Paused };

class Device : public Gbase::PluginObject
{
protected:

public:
	Device( void );
	virtual void gdecl Destroy( void ) { delete this; }

	virtual Gerr::Error gdecl	Open( int channels, int rate, Quanity quality ) = 0;
	virtual int gdecl			Read( audio *data, int samples ) = 0;
	virtual int gdecl			Write( const audio *data, int samples ) = 0;
	virtual Gerr::Error gdecl	Close( void ) = 0;

	virtual Gerr::Error gdecl	Start( void ) = 0;	// Starts streaming
	virtual void gdecl 			Stop( void ) = 0;	// Stops & resets device
	virtual void gdecl 			Pause( void ) = 0;	// Pauses streaming

	virtual int64 gdecl			Position( void ) = 0;
};

/*---------------------------------------------------------------------
	Host program interface structures/functions
 */
#define DeviceVersion 1.0F

#ifdef _WIN32
# ifdef _MSC_VER
	/*
		Microsoft does not insert an underscore.  Need to add one.
		Do not compile with __stdcall convention.  Otherwise it
		will ignore the extern "C" convention and mangle the name.
	 */
#  define DeviceInterfaceDll extern "C" __declspec(dllexport) Gap::Interface * _GetDeviceInterface
#else
	// Borland does insert one
#  define DeviceInterfaceDll extern "C" __declspec(dllexport) Gap::Interface *GetDeviceInterface
# endif
#define DeviceInterfaceApp "_GetDeviceInterface"
#else
# define DeviceInterfaceDll extern "C" Gap::Interface *GetDeviceInterface
#define DeviceInterfaceApp "GetDeviceInterface"
#endif


enum Ability
{
	aPage = Gbase::baPage,		// Has a page where format can be specified
	aPlay = AbilityFlag(1),		// Can play audio
	aRecord = AbilityFlag(2),	// Can record audio
	aDuplex = AbilityFlag(3),	// Can play and record at the same time
};

/*
	Structure that defines the name and flags for the devices.
 */
struct Table
{
	const char	*name;			// Audio device name
	unsigned	abilities;		// Plug-in abilities (above)
};

// Type for audio device constructor function used in Interface structure
typedef Device *(gdecl *CreateFn)( const char *name );

// This is the interface structure passed to the host program
struct Interface
{
	float 			version;	// Version (usually 'DeviceVersion')
	int	  			count;		// Number of devices supported
	Table			*list;		// Pointer to table of devices supported
	CreateFn		create;		// Function that creates a device
	Gbase::ConfigFn	config;		// Function to configure module (usually null)
};


// Borland C++ gets confused if the braces are not used
extern "C"
{
	typedef const Interface *(*InterfaceFn)( void );
}

};	/* Namespace Gdp */

#pragma pack(pop)

#endif
