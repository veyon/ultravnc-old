//
// Copyright (C) 2002 RealVNC Ltd.  All Rights Reserved.
//
// This is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this software; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
// USA.

#include "ZInStream.h"
#include "Exception.h"
#ifdef _INTERNALLIB
#include <zlib.h>
#else
#include "../zlib/zlib.h"
#endif


using namespace rdr;

enum { DEFAULT_BUF_SIZE = 16384 };

ZInStream::ZInStream(int bufSize_)
	: underlying(0), bufSize(bufSize_ ? bufSize_ : DEFAULT_BUF_SIZE), offset(0),
	bytesIn(0), usedLib(0)
{
	zlibs = new z_stream;
	zlibs->zalloc = Z_NULL;
	zlibs->zfree = Z_NULL;
	zlibs->opaque = Z_NULL;
	zlibs->next_in = Z_NULL;
	zlibs->avail_in = 0;
	if (inflateInit(zlibs) != Z_OK) {
		delete zlibs;
		throw Exception("ZlibInStream: inflateInit failed");
	}

#ifdef _ZSTD
	zstds = ZSTD_createDStream();
	unsigned int inSize = ZSTD_DStreamInSize();
	unsigned int outSize = ZSTD_DStreamOutSize();
	bufSize = inSize > outSize ? inSize : outSize;
	bufSize = bufSize_ > bufSize ? bufSize_ : bufSize;
	ZSTD_initDStream(zstds);
	outBuffer = new ZSTD_outBuffer;
	inBuffer = new ZSTD_inBuffer;	
#endif

	ptr = end = start = new U8[bufSize];
}

ZInStream::~ZInStream()
{
	delete[] start;
	inflateEnd(zlibs);
	delete zlibs;

#ifdef _ZSTD
	ZSTD_freeDStream(zstds);
#endif
}

void ZInStream::SetUsedLib(int lib)
{
	usedLib = lib;
}

void ZInStream::setUnderlying(InStream* is, int bytesIn_)
{
	underlying = is;
	bytesIn = bytesIn_;
	ptr = end = start;
}

int ZInStream::pos()
{
	return (int)(offset + ptr - start);
}

void ZInStream::reset()
{
	ptr = end = start;
	if (!underlying) return;

	while (bytesIn > 0) {
		decompress();
		end = start; // throw away any data
	}
	underlying = 0;
}

int ZInStream::overrun(int itemSize, int nItems)
{
	if (itemSize > bufSize)
		throw Exception("ZlibInStream overrun: max itemSize exceeded");
	if (!underlying)
		throw Exception("ZlibInStream overrun: no underlying stream");

	if (end - ptr != 0)
		memmove(start, ptr, end - ptr);

	offset += (int)(ptr - start);
	end -= ptr - start;
	ptr = start;

	while (end - ptr < itemSize) {
		decompress();
	}

	if (itemSize * nItems > end - ptr)
		nItems = (int)((end - ptr) / itemSize);

	return nItems;
}

// decompress() calls the decompressor once.  Note that this won't necessarily
// generate any output data - it may just consume some input data.

void ZInStream::decompress()
{
	if(usedLib == 0)
	{
		zlibs->next_out = (U8*)end;
		zlibs->avail_out = (uInt)(start + bufSize - end);

		underlying->check(1);
		zlibs->next_in = (U8*)underlying->getptr();
		zlibs->avail_in = (uInt)(underlying->getend() - underlying->getptr());
		if ((int)zlibs->avail_in > bytesIn)
			zlibs->avail_in = bytesIn;

		int rc = inflate(zlibs, Z_SYNC_FLUSH);
		if (rc != Z_OK) {
			throw Exception("ZlibInStream: inflate failed");
		}

		bytesIn -= (int)(zlibs->next_in - underlying->getptr());
		end = zlibs->next_out;
		underlying->setptr(zlibs->next_in);
	}

#ifdef _ZSTD
	else if(usedLib == 1)
	{
		outBuffer->dst = (U8*)end;
		outBuffer->size = start + bufSize - end;
		outBuffer->pos = 0;

		underlying->check(1);
		inBuffer->src = underlying->getptr();
		inBuffer->size = underlying->getend() - underlying->getptr();
		inBuffer->pos = 0;

		if (inBuffer->size > bytesIn)
			inBuffer->size = bytesIn;

		auto result = ZSTD_decompressStream(zstds, outBuffer, inBuffer);
		if (ZSTD_isError(result))
			throw Exception(ZSTD_getErrorName(result));

		bytesIn -= (int)(((U8*)inBuffer->src + inBuffer->pos) - (U8*)underlying->getptr());
		end = ((U8*)outBuffer->dst + outBuffer->pos);
		underlying->setptr((U8*)inBuffer->src + inBuffer->pos);
	}
#endif
	else
	{
		throw Exception("ZInStream: unknown z compressor requested");
	}
}
