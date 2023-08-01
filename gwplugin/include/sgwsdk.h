/*
	Simplified C Plug-in SDK (C) 2006 GoldWave Inc.
*/

#ifndef _SGWSDH_H_
#define _SGWSDH_H_

#include <windows.h>

/* Error enumeration */
#define gw_eNone		0
#define gw_eCreate		1
#define gw_eOpen		2
#define gw_eRead		3
#define gw_eWrite		4
#define gw_eSeek		5
#define gw_eClose		6
#define gw_eMemory		7
#define gw_eCorrupt		8
#define gw_eReadOnly	9
#define gw_eClash		10
#define gw_eEmpty		11
#define gw_eCodec		12
#define gw_eMath		13
#define gw_eBounds		14
#define gw_eType		15
#define gw_eFormat		16
#define gw_eAbort		17
#define gw_eUnsupported	18
#define gw_eForbidden	19
#define gw_eParameter	20
#define gw_ePermission	21
#define gw_eRange		22

#define gwi_Album		0		/* Album name */
#define gwi_Author		1		/* Author or artist */
#define gwi_Copyright	2		/* Copyright information */
#define gwi_Description	3		/* Description of file contents */
#define gwi_Date		4		/* Date of recording */
#define gwi_Genre		5		/* A short indication of music genre */
#define gwi_Title		6		/* Title of the track/file */
#define gwi_URL			7		/* Url for more information about track/file */
#define gwi_Tool		8		/* Tool used to create the file/track */
#define gwi_TrackNumber	9		/* Track number */
#define gwi_ISRC		10		/* Standard recording number */
#define gwi_Total		11

/* Common functions and structures for all plug-ins */
void * STDMETHODCALLTYPE 		gw_Construct( void );
void STDMETHODCALLTYPE 			gw_Destroy( void *instance );
const char * STDMETHODCALLTYPE	gw_Name( void );
unsigned STDMETHODCALLTYPE 		gw_Ability( void );
int  STDMETHODCALLTYPE			gw_Get( void *instance, void *data, int size );
BOOL STDMETHODCALLTYPE 			gw_Set( void *instance, void *data, int size );

typedef struct taggw_InfoList
{
	void *list;
	const char * CALLBACK (*Get)( void *list, int item );
	BOOL CALLBACK (*Set)( void *list, int item, const char *string );
} gw_InfoList;

typedef struct taggw_Cue
{
	double		position;
	const char	*name, *description;
}gw_Cue;

typedef struct taggw_CueList
{
	void *list;
	BOOL CALLBACK (*Add)( void *list, const gw_Cue *cue );
	BOOL CALLBACK (*Set)( void *list, int index, const gw_Cue *cue );
	BOOL CALLBACK (*Get)( void *list, int index, const gw_Cue *cue );
	BOOL CALLBACK (*Remove)( void *list, int index );
	int CALLBACK (*Count)( void *list );
} gw_CueList;

/* Page enumeration */
#define gw_paApply		1
#define gw_paResize		2
#define gw_paHelp		4

/* Interface functions for Page/Dialog handling */
void * STDMETHODCALLTYPE 		gw_PageConstruct( void *instance );
void STDMETHODCALLTYPE 			gw_PageDestroy( void *page );
HWND STDMETHODCALLTYPE 			gw_PageHandle( void *page, HWND parent );
void STDMETHODCALLTYPE 			gw_PageShow( void *page );
void STDMETHODCALLTYPE 			gw_PageHide( void *page );
const char * STDMETHODCALLTYPE	gw_PageHelp( void *page );
void STDMETHODCALLTYPE			gw_PageUpdate( void *page );
BOOL STDMETHODCALLTYPE			gw_PageApply( void *page );
void STDMETHODCALLTYPE			gw_PageResize( void *page, int width, int height );
int  STDMETHODCALLTYPE			gw_PageWidth( void *page );
int  STDMETHODCALLTYPE			gw_PageHeight( void *page );
unsigned STDMETHODCALLTYPE		gw_PageAbility( void );

/* Plug-in ability flags */
#define gw_baPage		0x01
#define gw_baReserved	0x20

#define gw_AbilityFlag(x)	(gw_baReserved << (x))

/* Channel enumeration */
#define gw_acfLeft		1
#define gw_acfRight		2
#define gw_acfAll		0xFFFFFFFF

#define gw_MaxAudio 		1
#define gw_MinAudio 		(-1)
#define gw_MaxAudioSamples 	(1E12)
#define gw_MinAudioDelta	(1/(double)(1<<23))

typedef float	gw_audio;

/* Visual SDK */
#define gwv_Samples			2048
#define gwv_FrequencyBits	11
#define gwv_Frequencies		(VisualSamples >> 1)
#define gwv_MinFrequency	-100

/* Full colour magnitudes */
#define gwv_fRed		0x1F
#define gwv_fGreen		0x3F
#define gwv_fBlue		0x1F

/* Mask bits */
#define gwv_mRed		0xF800
#define gwv_mGreen		0x07E0
#define gwv_mBlue		0x001F

/* Draw states */
#define gwv_dsStopped	0
#define gwv_dsPaused	1
#define gwv_dsActive	2
#define gwv_dsInstant	3

/*
#pragma pack(push, 1)
// Make sure that sizeof(gwv_Pixel) == 2 to properly access pixels
typedef struct taggwv_Pixel
{
	unsigned short	b:5, g:6, r:5;
} gwv_Pixel;
#pragma pack(pop)
*/
typedef void *gwv_Pixel;
#define ScanLine( start, row, width, height )\
   (gwv_Pixel*)((char *)start + (((width << 1) + 2) & ~2) * (height - row - 1))


typedef struct taggwv_DrawInfo
{
	HDC				imageDC;
	gwv_Pixel		*pixel;
	int				state;
	double			time;
	float			*waveform;
	float			*frequency;
} gwv_DrawInfo;

/* State flags */
#define gwv_sfWidth			0x001
#define gwv_sfHeight		0x002
#define gwv_sfChannels		0x004
#define gwv_sfSide			0x008
#define gwv_sfSamplingRate	0x010
#define gwv_sfPlaybackState	0x020
#define gwv_sfRecordState	0x040
#define gwv_sfFrameRate		0x080
#define gwv_sfFileInfo		0x100
#define gwv_sfFileCues		0x200
#define gwv_sfAll			0x03FF

/* Side flags */
#define gwv_sLeft			1
#define gwv_sRight			2
#define gwv_sBoth			3

/* Playback states */
#define gwv_pStop			0
#define gwv_pPause			1
#define gwv_pPlay			2
#define gwv_pRewind			3
#define gwv_pFast			4

#define gwv_rStop			0
#define gwv_rRecord			1
#define gwv_rPause			2

#define gwv_aPage			gw_baPage
#define gwv_aStatus			gw_AbilityFlag(1)
#define gwv_aLevel			gw_AbilityFlag(2)
#define gwv_aLeft			gw_AbilityFlag(3)
#define gwv_aRight			gw_AbilityFlag(4)
#define gwv_aEvent			gw_AbilityFlag(5)

#define gwv_aGraph			(gwv_aLeft | gwv_aRight)
#define gwv_aAny			(gwv_aStatus | gwv_aLevel | gwv_aGraph)

typedef struct taggwv_State
{
	int		width, height,
			channels,
			side,
			samplingRate,
			playbackState,
			recordState,
			frameRate;

	gw_InfoList	info;
	gw_CueList	cues;
} gwv_State;

#define gwv_MouseFlag		0x100
#define gwv_MouseEnter		0x101
#define gwv_MouseLeave		0x102
#define gwv_MouseMove		0x103
#define gwv_MouseUp			0x104
#define gwv_MouseDown		0x105

typedef struct taggwv_EventInfo
{
	int	type;
	union
	{
		struct Mouse { int x, y; unsigned buttons; } mouse;
	};
} gwv_EventInfo;

/* Interface functions for Visual plug-ins */
BOOL STDMETHODCALLTYPE	gwv_Draw( void *instance, gwv_DrawInfo *draw );
void STDMETHODCALLTYPE	gwv_SetState( void *instance, gwv_State *state, unsigned flags );
void STDMETHODCALLTYPE	gwv_Event( void *instance, gwv_EventInfo *event );

#endif

