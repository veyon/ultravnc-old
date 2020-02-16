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


//
// ZOutStream streams to a compressed data stream (underlying), compressing
// with zlib or zstd on the fly.
//

#ifndef __RDR_ZOUTSTREAM_H__
#define __RDR_ZOUTSTREAM_H__

#include "OutStream.h"

#ifdef _ZSTD
#ifdef _INTERNALLIB
#include <zstd.h>
#else
#include "../zstd/lib/zstd.h"
#endif
#endif

struct z_stream_s;

namespace rdr {

  class ZOutStream : public OutStream {

  public:

    // adzm - 2010-07 - Custom compression level
	  ZOutStream(OutStream* os=0, int bufSize=0, int compressionLevel=-1); // Z_DEFAULT_COMPRESSION
    virtual ~ZOutStream();

    void setUnderlying(OutStream* os);
    void flush();
    int length();
	void SetUsedLib(int lib);

  private:

    int overrun(int itemSize, int nItems);

	int usedLib;

    OutStream* underlying;
    int bufSize;
    int offset;
    z_stream_s* zs;
    U8* start;

#ifdef _ZSTD
	ZSTD_outBuffer* outBuffer;
	ZSTD_inBuffer* inBuffer;
	ZSTD_CStream* zstds;
#endif
  };

} // end of namespace rdr

#endif
