/*
	Audio Plug-in Interface (C) 2008 GoldWave Inc.

	Purpose:
		Simple class to make Unicode, UTF8, and ANSI conversions easier.
*/

#ifndef _GUNICODE_H_
#define _GUNICODE_H_

class ustring
{
	char 	*utf8, *ansi;
	wchar_t	*wide;
	void	init( void ) { utf8 = ansi = 0; wide = 0; }

public:
	ustring( const wchar_t *wide_string )
	{
		init();
		if ( wide_string )
		{
			wide = new wchar_t[wcslen( wide_string ) + 1];
			wcscpy( wide, wide_string );
		}
	}
	ustring( const char *string, bool utf8 = true )
	{
		init();
		if ( ! string )
			return;

#ifdef __WIN32__
		int len = MultiByteToWideChar( utf8 ? CP_UTF8 : CP_ACP, 0, string, -1, 0, 0 );
		if ( len <= 0 )
			return;

		wide = new wchar_t[len + 1];
		MultiByteToWideChar( utf8 ? CP_UTF8 : CP_ACP, 0, string, -1, wide, len + 1 );

#else
#error Code needed?
		int len = mbstowcs( 0, utf8_string, 0 ) + 1;
		wide = new wchar_t[len];
		mbstowcs( wide, utf8_string, len );
#endif
	}
	~ustring( void ) { delete [] utf8; delete [] ansi; delete [] wide; }
	const char *utf8_str( void )
	{
		if ( ! utf8 && wide )
		{
#ifdef __WIN32__
			int len = WideCharToMultiByte( CP_UTF8, 0, wide, -1, 0, 0, 0, 0 );
			if ( len > 0 )
			{
				utf8 = new char[len + 1];
				WideCharToMultiByte( CP_UTF8, 0, wide, -1, utf8, len + 1, 0, 0 );
			}
#else
#error Code needed?
			int len = wcstombs( 0, wide, 0 ) + 1;
			utf8 = new char[len];
			wcstombs( utf8, wide, len );
#endif
		}
		return utf8;
	}
	const char *ansi_str( void )
	{
#ifdef __WIN32__
		if ( ! ansi && wide )
		{
			int len = WideCharToMultiByte( CP_ACP, 0, wide, -1, 0, 0, 0, 0 );
			if ( len > 0 )
			{
				ansi = new char[len + 1];
				WideCharToMultiByte( CP_ACP, 0, wide, -1, ansi, len + 1, 0, 0 );
			}
		}
		return ansi;
#else
#error Code needed?
		return utf8_str();
#endif
	}
	operator const wchar_t *( void )
	{
		return wide;
	}
};

#endif
