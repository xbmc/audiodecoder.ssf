/*
 *      Copyright (C) 2014 Arne Morten Kvarving
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <kodi/addon-instance/AudioDecoder.h>
#include <kodi/Filesystem.h>
#include "sega.h"
#include "dcsound.h"
#include "satsound.h"
#include "yam.h"
#include "psflib.h"

struct sdsf_load_state
{
  std::vector<uint8_t> state;
};

struct SSFContext
{
  sdsf_load_state state;
  int64_t len;
  int sample_rate;
  int64_t pos;
  std::string title;
  std::string artist;
  std::vector<uint8_t> sega_state;
  int version;
};

extern "C"
{

inline unsigned get_le32( void const* p )
{
    return (unsigned) ((unsigned char const*) p) [3] << 24 |
            (unsigned) ((unsigned char const*) p) [2] << 16 |
            (unsigned) ((unsigned char const*) p) [1] << 8 |
            (unsigned) ((unsigned char const*) p) [0];
}

static int sdsf_load(void * context, const uint8_t * exe, size_t exe_size,
                     const uint8_t * reserved, size_t reserved_size)
{
  if ( exe_size < 4 )
    return -1;

  sdsf_load_state * state = ( sdsf_load_state * ) context;

  std::vector<uint8_t> & dst = state->state;

  if ( dst.size() < 4 )
  {
    dst.resize( exe_size );
    memcpy( &dst[0], exe, exe_size );
    return 0;
  }

  uint32_t dst_start = get_le32(&dst[0]);
  uint32_t src_start = get_le32(exe);
  dst_start &= 0x7FFFFF;
  src_start &= 0x7FFFFF;
  size_t dst_len = dst.size() - 4;
  size_t src_len = exe_size - 4;
  if ( dst_len > 0x800000 ) dst_len = 0x800000;
  if ( src_len > 0x800000 ) src_len = 0x800000;

  if ( src_start < dst_start )
  {
    size_t diff = dst_start - src_start;
    dst.resize( dst_len + 4 + diff );
    memmove( &dst[0] + 4 + diff, &dst[0] + 4, dst_len );
    memset( &dst[0] + 4, 0, diff );
    dst_len += diff;
    dst_start = src_start;
    *(uint32_t*)(&dst[0]) = get_le32(&dst_start);
  }
  if ( ( src_start + src_len ) > ( dst_start + dst_len ) )
  {
    size_t diff = ( src_start + src_len ) - ( dst_start + dst_len );
    dst.resize( dst_len + 4 + diff );
    memset( &dst[0] + 4 + dst_len, 0, diff );
    dst_len += diff;
  }

  memcpy( &dst[0] + 4 + ( src_start - dst_start ), exe + 4, src_len );

  return 0;
}

static void * psf_file_fopen( const char * uri )
{
  kodi::vfs::CFile* file = new kodi::vfs::CFile;
  if (!file->OpenFile(uri, 0))
  {
    delete file;
    return nullptr;
  }

  return file;
}

static size_t psf_file_fread( void * buffer, size_t size, size_t count, void * handle )
{
  kodi::vfs::CFile* file = static_cast<kodi::vfs::CFile*>(handle);
  return file->Read(buffer, size*count);
}

static int psf_file_fseek( void * handle, int64_t offset, int whence )
{
  kodi::vfs::CFile* file = static_cast<kodi::vfs::CFile*>(handle);
  return file->Seek(offset, whence) > -1 ? 0 : -1;
}

static int psf_file_fclose( void * handle )
{
  delete static_cast<kodi::vfs::CFile*>(handle);

  return 0;
}

static long psf_file_ftell( void * handle )
{
  kodi::vfs::CFile* file = static_cast<kodi::vfs::CFile*>(handle);
  return file->GetPosition();
}

const psf_file_callbacks psf_file_system =
{
  "\\/",
  psf_file_fopen,
  psf_file_fread,
  psf_file_fseek,
  psf_file_fclose,
  psf_file_ftell
};

#define BORK_TIME 0xC0CAC01A
static unsigned long parse_time_crap(const char *input)
{
  if (!input) return BORK_TIME;
  int len = strlen(input);
  if (!len) return BORK_TIME;
  int value = 0;
  {
    int i;
    for (i = len - 1; i >= 0; i--)
    {
      if ((input[i] < '0' || input[i] > '9') && input[i] != ':' && input[i] != ',' && input[i] != '.')
      {
        return BORK_TIME;
      }
    }
  }
  std::string foo = input;
  char *bar = (char *) &foo[0];
  char *strs = bar + foo.size() - 1;
  while (strs > bar && (*strs >= '0' && *strs <= '9'))
  {
    strs--;
  }
  if (*strs == '.' || *strs == ',')
  {
    // fraction of a second
    strs++;
    if (strlen(strs) > 3) strs[3] = 0;
    value = atoi(strs);
    switch (strlen(strs))
    {
      case 1:
        value *= 100;
        break;
      case 2:
        value *= 10;
        break;
    }
    strs--;
    *strs = 0;
    strs--;
  }
  while (strs > bar && (*strs >= '0' && *strs <= '9'))
  {
    strs--;
  }
  // seconds
  if (*strs < '0' || *strs > '9') strs++;
  value += atoi(strs) * 1000;
  if (strs > bar)
  {
    strs--;
    *strs = 0;
    strs--;
    while (strs > bar && (*strs >= '0' && *strs <= '9'))
    {
      strs--;
    }
    if (*strs < '0' || *strs > '9') strs++;
    value += atoi(strs) * 60000;
    if (strs > bar)
    {
      strs--;
      *strs = 0;
      strs--;
      while (strs > bar && (*strs >= '0' && *strs <= '9'))
      {
        strs--;
      }
      value += atoi(strs) * 3600000;
    }
  }
  return value;
}

static int psf_info_meta(void* context,
                         const char* name, const char* value)
{
  SSFContext* ssf = (SSFContext*)context;
  if (!strcasecmp(name, "length"))
    ssf->len = parse_time_crap(value);
  if (!strcasecmp(name, "title"))
    ssf->title = value;
  if (!strcasecmp(name, "artist"))
    ssf->artist = value;

  return 0;
}

}

class ATTRIBUTE_HIDDEN CSSFCodec : public kodi::addon::CInstanceAudioDecoder
{
public:
  CSSFCodec(KODI_HANDLE instance) :
    CInstanceAudioDecoder(instance) {}

  virtual ~CSSFCodec()
  {
    if (ctx.sega_state.empty())
      return;

    void * yam = 0;
    if (ctx.version == 0x12)
    {
      void * dcsound = sega_get_dcsound_state(&ctx.sega_state[0]);
      yam = dcsound_get_yam_state( dcsound );
    }
    else
    {
      void * satsound = sega_get_satsound_state(&ctx.sega_state[0]);
      yam = satsound_get_yam_state( satsound );
    }
    if (yam)
      yam_unprepare_dynacode(yam);
  }

  bool Init(const std::string& filename, unsigned int filecache,
            int& channels, int& samplerate,
            int& bitspersample, int64_t& totaltime,
            int& bitrate, AEDataFormat& format,
            std::vector<AEChannel>& channellist) override
  {
    ctx.pos = 0;
    if ((ctx.version=psf_load(filename.c_str(), &psf_file_system, 0, 0, 0, 0, 0, 0)) <= 0 ||
        !(ctx.version == 0x11 || ctx.version == 0x12))
      return false;

    if (psf_load(filename.c_str(), &psf_file_system, ctx.version,
          0, 0, psf_info_meta, &ctx, 0) <= 0)
      return false;

    if (psf_load(filename.c_str(), &psf_file_system, ctx.version,
                 sdsf_load, &ctx.state, 0, 0, 0) < 0)
      return false;

    sega_init();
    ctx.sega_state.resize(sega_get_state_size(ctx.version-0x10));
    void* emu = &ctx.sega_state[0];
    sega_clear_state(emu, ctx.version-0x10);
    sega_enable_dry(emu, 0);
    sega_enable_dsp(emu, 1);
    sega_enable_dsp_dynarec(emu, 1);

    void * yam = 0;
    if (ctx.version == 0x12)
    {
      void * dcsound = sega_get_dcsound_state(emu);
      yam = dcsound_get_yam_state( dcsound );
    }
    else
    {
      void * satsound = sega_get_satsound_state(emu);
      yam = satsound_get_yam_state( satsound );
    }
    if (yam)
      yam_prepare_dynacode(yam);

    uint32_t start = get_le32(&ctx.state.state[0]);
    size_t length = ctx.state.state.size();
    size_t max_length = ( ctx.version == 0x12 ) ? 0x800000 : 0x80000;
    if ((start + (length-4)) > max_length)
    {
      length = max_length - start + 4;
    }
    sega_upload_program(emu, &ctx.state.state[0], length );

    totaltime = ctx.len;
    format = AE_FMT_S16NE;
    channellist = { AE_CH_FL, AE_CH_FR };
    channels = 2;
    bitspersample = 16;
    bitrate = 0.0;
    samplerate = ctx.sample_rate = 44100;
    ctx.len = ctx.sample_rate*4*totaltime/1000;

    return true;
  }

  int ReadPCM(uint8_t* buffer, int size, int& actualsize) override
  {
    if (ctx.pos >= ctx.len)
      return 1;

    actualsize = size/4;
    int err = sega_execute(&ctx.sega_state[0], 0x7FFFFFFF,
                           (int16_t*)buffer, (unsigned int*)&actualsize);
    if (err < 0)
      return 1;
    actualsize *= 4;
    ctx.pos += actualsize;
    return 0;
  }

  int64_t Seek(int64_t time) override
  {
    if (time*ctx.sample_rate*4/1000 < ctx.pos)
    {
      void* emu = &ctx.sega_state[0];
      uint32_t start = get_le32((uint32_t*)(&ctx.state.state[0]));
      size_t length = ctx.state.state.size();
      size_t max_length = ( ctx.version == 0x12 ) ? 0x800000 : 0x80000;
      if ((start + (length-4)) > max_length)
      {
        length = max_length - start + 4;
      }
      sega_upload_program(emu, &ctx.state.state[0], length );
      ctx.pos = 0;
    }

    int64_t left = time*ctx.sample_rate*4/1000-ctx.pos;
    while (left > 1024)
    {
      unsigned int chunk=1024;
      int rtn = sega_execute(&ctx.sega_state[0], 0x7FFFFFFF, 0, &chunk);
      ctx.pos += chunk*2;
      left -= chunk*2;
    }

    return ctx.pos/(ctx.sample_rate*4)*1000;
  }

  bool ReadTag(const std::string& file, std::string& title,
               std::string& artist, int& length) override
  {
    SSFContext ssf;

    if (psf_load(file.c_str(), &psf_file_system, 0x11, 0, 0, psf_info_meta, &ssf, 0) <= 0 &&
        psf_load(file.c_str(), &psf_file_system, 0x12, 0, 0, psf_info_meta, &ssf, 0) <= 0)
      return false;

    title = ssf.title;
    artist = ssf.artist;
    length = ssf.len/1000;

    return true;
  }

private:
  SSFContext ctx;
};


class ATTRIBUTE_HIDDEN CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() = default;
  ADDON_STATUS CreateInstance(int instanceType, std::string instanceID, KODI_HANDLE instance, KODI_HANDLE& addonInstance) override
  {
    addonInstance = new CSSFCodec(instance);
    return ADDON_STATUS_OK;
  }
  virtual ~CMyAddon() = default;
};


ADDONCREATOR(CMyAddon);
