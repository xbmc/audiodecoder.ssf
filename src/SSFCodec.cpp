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

#include "libXBMC_addon.h"
#include "sega.h"
#include "dcsound.h"
#include "satsound.h"
#include "yam.h"
#include "psflib.h"

extern "C" {
#include <stdio.h>
#include <stdint.h>

#include "kodi_audiodec_dll.h"

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  return ADDON_STATUS_OK;
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  XBMC=NULL;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  return ADDON_STATUS_OK;
}

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
  return  XBMC->OpenFile(uri, 0);
}

static size_t psf_file_fread( void * buffer, size_t size, size_t count, void * handle )
{
  return XBMC->ReadFile(handle, buffer, size*count);
}

static int psf_file_fseek( void * handle, int64_t offset, int whence )
{
  return XBMC->SeekFile(handle, offset, whence) > -1?0:-1;
}

static int psf_file_fclose( void * handle )
{
  XBMC->CloseFile(handle);
  return 0;
}

static long psf_file_ftell( void * handle )
{
  return XBMC->GetFilePosition(handle);
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

void* Init(const char* strFile, unsigned int filecache, int* channels,
           int* samplerate, int* bitspersample, int64_t* totaltime,
           int* bitrate, AEDataFormat* format, const AEChannel** channelinfo)
{
  SSFContext* result = new SSFContext;
  result->pos = 0;
  if ((result->version=psf_load(strFile, &psf_file_system, 0, 0, 0, 0, 0, 0)) <= 0 ||
      !(result->version == 0x11 || result->version == 0x12))
    return NULL;

  if (psf_load(strFile, &psf_file_system, result->version,
               0, 0, psf_info_meta, result, 0) <= 0)
  {
    delete result;
    return NULL;
  }

  if (psf_load(strFile, &psf_file_system, result->version,
               sdsf_load, &result->state, 0, 0, 0) < 0)
  {
    delete result;
    return NULL;
  }

  sega_init();
  result->sega_state.resize(sega_get_state_size(result->version-0x10));
  void* emu = &result->sega_state[0];
  sega_clear_state(emu, result->version-0x10);
  sega_enable_dry(emu, 0);
  sega_enable_dsp(emu, 1);
  sega_enable_dsp_dynarec(emu, 1);

  void * yam = 0;
  if (result->version == 0x12)
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

  uint32_t start = get_le32(&result->state.state[0]);
  size_t length = result->state.state.size();
  size_t max_length = ( result->version == 0x12 ) ? 0x800000 : 0x80000;
  if ((start + (length-4)) > max_length)
  {
    length = max_length - start + 4;
  }
  sega_upload_program(emu, &result->state.state[0], length );
  
  *totaltime = result->len;
  static enum AEChannel map[3] = {
    AE_CH_FL, AE_CH_FR, AE_CH_NULL
  };
  *format = AE_FMT_S16NE;
  *channelinfo = map;
  *channels = 2;
  *bitspersample = 16;
  *bitrate = 0.0;
  *samplerate = result->sample_rate = 44100;
  result->len = result->sample_rate*4*(*totaltime)/1000;

  return result;
}

int ReadPCM(void* context, uint8_t* pBuffer, int size, int *actualsize)
{
  SSFContext* ssf = (SSFContext*)context;
  if (ssf->pos >= ssf->len)
    return 1;

  *actualsize = size/4;
  int err = sega_execute(&ssf->sega_state[0], 0x7FFFFFFF,
                         (int16_t*)pBuffer, (unsigned int*)actualsize);
  if (err < 0)
    return 1;
  *actualsize *= 4;
  ssf->pos += *actualsize;
  return 0;
}

int64_t Seek(void* context, int64_t time)
{
  SSFContext* ssf = (SSFContext*)context;
  if (time*ssf->sample_rate*4/1000 < ssf->pos)
  {
    void* emu = &ssf->sega_state[0];
    uint32_t start = get_le32((uint32_t*)(&ssf->state.state[0]));
    size_t length = ssf->state.state.size();
    size_t max_length = ( ssf->version == 0x12 ) ? 0x800000 : 0x80000;
    if ((start + (length-4)) > max_length)
    {
      length = max_length - start + 4;
    }
    sega_upload_program(emu, &ssf->state.state[0], length );
    ssf->pos = 0;
  }
  
  int64_t left = time*ssf->sample_rate*4/1000-ssf->pos;
  while (left > 1024)
  {
    unsigned int chunk=1024;
    int rtn = sega_execute(&ssf->sega_state[0], 0x7FFFFFFF, 0, &chunk);
    ssf->pos += chunk*2;
    left -= chunk*2;
  }

  return ssf->pos/(ssf->sample_rate*4)*1000;
}

bool DeInit(void* context)
{
  SSFContext* ssf = (SSFContext*)context;
  void * yam = 0;
  if (ssf->version == 0x12)
  {
    void * dcsound = sega_get_dcsound_state(&ssf->state.state[0]);
    yam = dcsound_get_yam_state( dcsound );
  }
  else
  {
    void * satsound = sega_get_satsound_state(&ssf->state.state[0]);
    yam = satsound_get_yam_state( satsound );
  }
  if (yam)
    yam_unprepare_dynacode(yam);
  delete ssf;

  return true;
}

bool ReadTag(const char* strFile, char* title, char* artist, int* length)
{
  SSFContext* ssf = new SSFContext;

  if (psf_load(strFile, &psf_file_system, 0x11, 0, 0, psf_info_meta, ssf, 0) <= 0 &&
      psf_load(strFile, &psf_file_system, 0x12, 0, 0, psf_info_meta, ssf, 0) <= 0)
  {
    delete ssf;
    return false;
  }

  strcpy(title, ssf->title.c_str());
  strcpy(artist, ssf->artist.c_str());
  *length = ssf->len/1000;

  delete ssf;
  return true;
}

int TrackCount(const char* strFile)
{
  return 1;
}
}
