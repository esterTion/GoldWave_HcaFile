/*
	Audio File Plug-in base type (C) 2002 GoldWave Inc.

	Purpose:
		Native audio type used for all internal processing and conversions.
 */

#ifndef _GAUDIOTYPE_H_
#define _GAUDIOTYPE_H_

#pragma pack(push, 4)

typedef float	audio;
struct audioStereo
{
	audio	l, r;	// Shows storage order (left first, right second)
};

enum AudioChannelFlags { acfLeft = 1, acfRight = 2, acfAll = 0xFFFFFFFF };

#define MaxAudio 1
#define MinAudio (-1)
#define MaxAudioSamples (1E12)
#define MinAudioDelta (1/(double)(1<<23))

#pragma pack(pop)

#endif

