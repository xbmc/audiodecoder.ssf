/*
 *      Copyright (C) 2014-2020 Arne Morten Kvarving
 *      Copyright (C) 2019-2020 Team Kodi
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "CircularBuffer.h"

#include <kodi/addon-instance/AudioDecoder.h>
#include <kodi/Filesystem.h>

#include <atomic>
#include <vector>
#include <math.h>
#include <mutex>

#include "sega.h"
#include "dcsound.h"
#include "satsound.h"
#include "yam.h"
#include "psflib.h"

struct sdsf_load_state
{
  std::vector<uint8_t> state;
};

struct psf_info_meta_state
{
  std::string title;
  std::string artist;
  std::string year;
  std::string replaygain;

  bool utf8 = false;

  int tagSongMs = 0;
  int tagFadeMs = 0;
};

class ATTRIBUTE_HIDDEN CSSFCodec : public kodi::addon::CInstanceAudioDecoder
{
public:
  CSSFCodec(KODI_HANDLE instance);
  ~CSSFCodec() override;

  bool Init(const std::string& filename, unsigned int filecache,
            int& channels, int& samplerate,
            int& bitspersample, int64_t& totaltime,
            int& bitrate, AEDataFormat& format,
            std::vector<AEChannel>& channellist) override;
  int ReadPCM(uint8_t* buffer, int size, int& actualsize) override;
  int64_t Seek(int64_t time) override;
  bool ReadTag(const std::string& file, std::string& title,
               std::string& artist, int& length) override;

private:
  static void SSFPrintMessage(void* context, const char* message);

  static bool m_gInitialized;
  static std::mutex m_gSyncMutex;

  bool Load();

  inline uint64_t time_to_samples(double p_time, uint32_t p_sample_rate)
  {
    return (uint64_t)floor((double)p_sample_rate * p_time + 0.5);
  }

  inline void calcfade()
  {
    m_songLength = mul_div(m_tagSongMs-m_posDelta,44100,1000);
    m_fadeLength = mul_div(m_tagFadeMs,44100,1000);
  }

  inline int mul_div(int number, int numerator, int denominator)
  {
    long long ret = number;
    ret *= numerator;
    ret /= denominator;
    return (int) ret;
  }

  int m_cfgDefaultSampleRate = 44100;
  bool m_cfgSuppressOpeningSilence = true;
  bool m_cfgSuppressEndSilence = true;
  int m_cfgEndSilenceSeconds = 5;
  bool m_cfgDry = true;
  bool m_cfgDSP = true;
  bool m_cfgDSPDynamicRec = true;

  bool m_noLoop = true;
  bool m_eof;

  std::vector<uint8_t> m_segaState;
  std::vector<int16_t> m_sampleBuffer;

  circular_buffer<int16_t> m_silenceTestBuffer = 0;

  std::string m_path;

  int m_xsfVersion;

  int m_dataWritten;
  int m_remainder;
  int m_posDelta;
  int m_startSilence;
  int m_silence;

  double m_xsfEmuPosition;

  int m_songLength;
  int m_fadeLength;
  int m_tagSongMs;
  int m_tagFadeMs;
};

