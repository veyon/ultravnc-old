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

#include "ZOutStream.h"
#include "Exception.h"
#ifdef _INTERNALLIB
#include <zlib.h>
#else
#include "../zlib/zlib.h"
#endif


using namespace rdr;

enum { DEFAULT_BUF_SIZE = 16384 };

// adzm - 2010-07 - Custom compression level
ZOutStream::ZOutStream(OutStream* os, int bufSize_, int compressionLevel)
  : underlying(os), bufSize(bufSize_ ? bufSize_ : DEFAULT_BUF_SIZE), offset(0), usedLib(0)
{
  zs = new z_stream;
  zs->zalloc    = Z_NULL;
  zs->zfree     = Z_NULL;
  zs->opaque    = Z_NULL;
  if (deflateInit(zs, compressionLevel) != Z_OK) {
    delete zs;
    throw Exception("ZStream: deflateInit failed");
  }

#ifdef _ZSTD
  zstds = ZSTD_createCStream();
  unsigned int inSize = ZSTD_CStreamInSize();
  unsigned int outSize = ZSTD_CStreamOutSize();
  bufSize = inSize > outSize ? inSize : outSize;
  bufSize = bufSize_ > bufSize ? bufSize_ : bufSize;
  ZSTD_initCStream(zstds, ZSTD_CLEVEL_DEFAULT);
  outBuffer = new ZSTD_outBuffer;
  inBuffer = new ZSTD_inBuffer;
#endif

  ptr = start = new U8[bufSize];
  end = start + bufSize;
}

ZOutStream::~ZOutStream()
{
  try {
    flush();
  } catch (Exception&) {
  }
  delete [] start;
  deflateEnd(zs);
  delete zs;

#ifdef _ZSTD
  ZSTD_freeCStream(zstds);
#endif
}

void ZOutStream::SetUsedLib(int lib)
{
	usedLib = lib;
}

void ZOutStream::setUnderlying(OutStream* os)
{
  underlying = os;
}

int ZOutStream::length()
{
  return (int)(offset + ptr - start);
}


void ZOutStream::flush()
{
	if(usedLib == 0)
	{
		zs->next_in = start;
		zs->avail_in = (uInt)(ptr - start);

		//    fprintf(stderr,"zos flush: avail_in %d\n",zs->avail_in);

		while (zs->avail_in != 0) {

			do {
				underlying->check(1);
				zs->next_out = underlying->getptr();
				zs->avail_out = (uInt)(underlying->getend() - underlying->getptr());

				//        fprintf(stderr,"zos flush: calling deflate, avail_in %d, avail_out %d\n",
				//                zs->avail_in,zs->avail_out);
				int rc = deflate(zs, Z_SYNC_FLUSH);
				if (rc != Z_OK) throw Exception("ZOutStream: deflate failed");

				//        fprintf(stderr,"zos flush: after deflate: %d bytes\n",
				//                zs->next_out-underlying->getptr());

				underlying->setptr(zs->next_out);
			} while (zs->avail_out == 0);
		}
	}
#ifdef _ZSTD
	else if(usedLib == 1)
	{
		inBuffer->src = start;
		inBuffer->size = ptr - start;
		inBuffer->pos = 0;
		unsigned int rc = 0;

		while (inBuffer->size != 0) {

			do {
				underlying->check(1);
				outBuffer->dst = underlying->getptr();
				outBuffer->size = underlying->getend() - underlying->getptr();
				outBuffer->pos = 0;

				rc = ZSTD_compressStream2(zstds, outBuffer, inBuffer, ZSTD_e_flush);
				if (ZSTD_isError(rc))
				{
					auto error = ZSTD_getErrorName(rc);
					throw Exception(error);
				}

				inBuffer->src = (U8*)inBuffer->src + inBuffer->pos;
				inBuffer->size -= inBuffer->pos;
				inBuffer->pos = 0;
				outBuffer->dst = (U8*)outBuffer->dst + outBuffer->pos;
				outBuffer->size -= outBuffer->pos;
				outBuffer->pos = 0;

				underlying->setptr((U8*)outBuffer->dst);
			} while (outBuffer->size == 0);
		}

		if (rc != 0)
			__debugbreak();
	}
#endif
	else
	{
		throw Exception("ZOutStream: unknown z compressor requested");
	}
	
	offset += (int)(ptr - start);
	ptr = start;
}

int ZOutStream::overrun(int itemSize, int nItems)
{
//    fprintf(stderr,"ZOutStream overrun\n");

  if (itemSize > bufSize)
    throw Exception("ZOutStream overrun: max itemSize exceeded");

  if (usedLib == 0)
  {
	  while (end - ptr < itemSize) {
		  zs->next_in = start;
		  zs->avail_in = (uInt)(ptr - start);

		  do {
			  underlying->check(1);
			  zs->next_out = underlying->getptr();
			  zs->avail_out = (uInt)(underlying->getend() - underlying->getptr());

			  //        fprintf(stderr,"zos overrun: calling deflate, avail_in %d, avail_out %d\n",
			  //                zs->avail_in,zs->avail_out);

			  int rc = deflate(zs, 0);
			  if (rc != Z_OK) throw Exception("ZOutStream: deflate failed");

			  //        fprintf(stderr,"zos overrun: after deflate: %d bytes\n",
			  //                zs->next_out-underlying->getptr());

			  underlying->setptr(zs->next_out);
		  } while (zs->avail_out == 0);

		  // output buffer not full

		  if (zs->avail_in == 0) {
			  offset += (int)(ptr - start);
			  ptr = start;
		  }
		  else {
			  // but didn't consume all the data?  try shifting what's left to the
			  // start of the buffer.
			  fprintf(stderr, "z out buf not full, but in data not consumed\n");
			  memmove(start, zs->next_in, ptr - zs->next_in);
			  offset += (int)(zs->next_in - start);
			  ptr -= zs->next_in - start;
		  }
	  }
  }
#ifdef _ZSTD
  else if(usedLib == 1)
  {
	  while (end - ptr < itemSize) {
		  inBuffer->src = start;
		  inBuffer->size = ptr - start;
		  inBuffer->pos = 0;

		  do {
			  underlying->check(1);
			  outBuffer->dst = underlying->getptr();
			  outBuffer->size = underlying->getend() - underlying->getptr();
			  outBuffer->pos = 0;
		  	
			  auto rc = ZSTD_compressStream(zstds, outBuffer, inBuffer);
			  if (ZSTD_isError(rc))
			  {
				  auto error = ZSTD_getErrorName(rc);
				  throw Exception(error);
			  }				  

			  inBuffer->src = (U8*)inBuffer->src + inBuffer->pos;
			  inBuffer->size -= inBuffer->pos;
			  inBuffer->pos = 0;
			  outBuffer->dst = (U8*)outBuffer->dst + outBuffer->pos;
			  outBuffer->size -= outBuffer->pos;
			  outBuffer->pos = 0;
			 
			  underlying->setptr((U8*)outBuffer->dst);
		  } while (outBuffer->size == 0);

		  // output buffer not full

		  if (inBuffer->size == 0) {
			  offset += (int)(ptr - start);
			  ptr = start;
		  }
		  else {
			  // but didn't consume all the data?  try shifting what's left to the
			  // start of the buffer.
			  fprintf(stderr, "z out buf not full, but in data not consumed\n");
			  memmove(start, inBuffer->src, ptr - inBuffer->src);
			  offset += (int)((U8*)inBuffer->src - start);
			  ptr -= (U8*)inBuffer->src - start;
		  }
	  }
  }
#endif
  else
  {
	  throw Exception("ZOutStream: unknown z compressor requested");
  }


  if (itemSize * nItems > end - ptr)
    nItems = (int)((end - ptr) / itemSize);

  return nItems;
}
