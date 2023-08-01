/*
	General Library (C) 2002-2009 GoldWave Inc.

	Purpose:
		General error class for passing application exceptions.
*/

#ifndef _GERROR_H_
#define _GERROR_H_

#pragma pack(push, 4)

namespace Gerr
{
	enum Error
	{
		eNone, eCreate, eOpen, eRead, eWrite, eSeek, eClose, eMemory,
		eCorrupt, eReadOnly, eClash, eEmpty, eCodec, eMath, eBounds, eType,
		eFormat, eAbort, eUnsupported, eForbidden, eParameter, ePermission,
		eRange, eException, eUnknown
	};
	struct Exception
	{
		Error error;
		Exception( Error e ) { error = e; }
	};
}

#pragma pack(pop)

#endif


