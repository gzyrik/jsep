/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "audio_device_zmf.h"

#include <string.h>
#include <stdlib.h>
#include "webrtc/typedefs.h"
#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/common_audio/resampler/include/resampler.h"

typedef int(*ZmfAudioOutputCallback)(void* pUser, const char* outputId, int iSampleRateHz, int iChannels,
    unsigned char *buf, int len);
typedef void(*ZmfAudioInputCallback)(void* pUser, const char* inputId, int iSampleRateHz, int iChannels,
    unsigned char *buf, int len, int *micLevel,
    int playDelayMS, int recDelayMS, int clockDrift);
int(*Zmf_AudioInputGetCount)(void);
int(*Zmf_AudioInputGetName)(int iIndex, char acId[512], char acName[512]);
int(*Zmf_AudioOutputGetCount)(void);
int(*Zmf_AudioOutputGetName)(int iIndex, char acId[512], char acName[512]);
void(*Zmf_AudioOutputRequestStart)(const char *outputId, int iSampleRateHz, int iChannels);
void(*Zmf_AudioOutputRequestStop)(const char *outputId);
void(*Zmf_AudioInputRequestStart)(const char *inputId, int iSampleRateHz, int iChannels, int bAEC, int bAGC);
void(*Zmf_AudioInputRequestStop)(const char *inputId);
int(*Zmf_AudioOutputAddCallback)(void *pUser, ZmfAudioOutputCallback pfnCb);
int(*Zmf_AudioOutputRemoveCallback)(void *pUser);
int(*Zmf_AudioInputAddCallback)(void *pUser, ZmfAudioInputCallback pfnCb);
int(*Zmf_AudioInputRemoveCallback)(void *pUser);

namespace webrtc {
class ResamplerQueue : public Resampler {
public:
  // Asynchronous resampling, input
  int Insert(int16_t* samplesIn, size_t lengthIn);

  // Asynchronous resampling output, remaining samples are buffered
  int Pull(int16_t* samplesOut, size_t desiredLen, size_t &outLen);
  bool Stereo() const { return num_channels_ == 2; }
private:
  rtc::CriticalSection _critSect;
};
int ResamplerQueue::Insert(int16_t* samplesIn, size_t lengthIn) {
  rtc::CritScope lock(&_critSect);
  const int channels = Stereo() ? 2 : 1;
  size_t sizeNeeded, tenMsblock;

  // Determine need for size of outBuffer
  sizeNeeded = out_buffer_size_ + ((lengthIn + in_buffer_size_) * my_out_frequency_khz_)
          / my_in_frequency_khz_;
  if (sizeNeeded > out_buffer_size_max_)
  {
      // Round the value upwards to complete 10 ms blocks
      tenMsblock = my_out_frequency_khz_ * channels  * 10;
      sizeNeeded = (sizeNeeded / tenMsblock + 1) * tenMsblock;
      out_buffer_ = (int16_t*)realloc(out_buffer_, sizeNeeded * sizeof(int16_t));
      out_buffer_size_max_ = sizeNeeded;
  }

  // If we need to use inBuffer, make sure all input data fits there.

  tenMsblock = my_in_frequency_khz_ * channels *  10;
  if (in_buffer_size_ || (lengthIn % tenMsblock))
  {
      // Check if input buffer size is enough
      if ((in_buffer_size_ + lengthIn) > in_buffer_size_max_)
      {
          // Round the value upwards to complete 10 ms blocks
          sizeNeeded = ((in_buffer_size_ + lengthIn) / tenMsblock + 1) * tenMsblock;
          in_buffer_ = (int16_t*)realloc(in_buffer_,
                                               sizeNeeded * sizeof(int16_t));
          in_buffer_size_max_ = sizeNeeded;
      }
      // Copy in data to input buffer
      memcpy(in_buffer_ + in_buffer_size_, samplesIn, lengthIn * sizeof(int16_t));
      in_buffer_size_ += lengthIn;

      // Resample all available 10 ms blocks
      const int dataLenToResample = (in_buffer_size_ / tenMsblock) * tenMsblock;
      if (dataLenToResample > 0) 
      {
          size_t lenOut = 0;
          Push(in_buffer_, dataLenToResample, out_buffer_ + out_buffer_size_,
              out_buffer_size_max_ - out_buffer_size_, lenOut);
          out_buffer_size_ += lenOut;

          // Save the rest
          memmove(in_buffer_, in_buffer_ + dataLenToResample,
              (in_buffer_size_ - dataLenToResample) * sizeof(int16_t));
          in_buffer_size_ -= dataLenToResample;
      }
  } else
  {
      // Just resample
      size_t lenOut = 0;
      Push(samplesIn, lengthIn, out_buffer_ + out_buffer_size_,
           out_buffer_size_max_ - out_buffer_size_, lenOut);
      out_buffer_size_ += lenOut;
  }

  return 0;
}

int ResamplerQueue::Pull(int16_t* samplesOut, size_t desiredLen, size_t &outLen) {
  rtc::CritScope lock(&_critSect);
  // Check that we have enough data
  if (desiredLen <= out_buffer_size_)
  {
      // Give out the date
      memcpy(samplesOut, out_buffer_, desiredLen * sizeof(int16_t));

      // Shuffle down remaining
      memmove(out_buffer_, out_buffer_ + desiredLen,
              (out_buffer_size_ - desiredLen) * sizeof(int16_t));

      // Update remaining size
      out_buffer_size_ -= desiredLen;

      return 0;
  } else
  {
      return -1;
  }

}
static int  OnSpkFillBuf(void* user, const char *outputId, int sampleRateHz, int iChannels,
                         unsigned char *buf, int len)
{
    AudioDeviceZmf* self = (AudioDeviceZmf*)user;
    return self->PlayoutProcess(outputId, sampleRateHz, iChannels, buf, len);
}

static void OnMicReceive(void* user, const char* inputId, int sampleRateHz, int iChannels, 
                         unsigned char *buf, int len, int *micLevel,
                         int playDelayMS, int recDelayMS, int clockDrift)
{
    AudioDeviceZmf* self = (AudioDeviceZmf*)user;
    return self->RecordProcess(inputId, sampleRateHz, iChannels, buf, len, micLevel,
                               playDelayMS, recDelayMS, clockDrift);
}
// ============================================================================
//                            Construction & Destruction
// ============================================================================

// ----------------------------------------------------------------------------
//  AudioDeviceZmf() - ctor
// ----------------------------------------------------------------------------

AudioDeviceZmf::AudioDeviceZmf(const int32_t id, const char* deviceName) :
    _playoutAutoDevice(!deviceName),
    _recordAutoDevice(!deviceName),
    _ptrAudioBuffer(NULL),
    _initialized(false),
    _recording(false),
    _playing(false),
    _recIsInitialized(false),
    _playIsInitialized(false),
    _speakerIsInitialized(false),
    _microphoneIsInitialized(false),
    _bAEC(false),
    _bAGC(false),
    _bNS(false),
    _lastPlayoutMs(-1),
    _lastRecordMs(-1)
{
    LOG(INFO) << __FUNCTION__;
    if (deviceName)
    {
        strcpy(_playoutDeviceId, deviceName);
        strcpy(_recordDeviceId, deviceName);
    }else
    {
        _recordDeviceId[0] = '\0';
        _playoutDeviceId[0] = '\0';
    }
}

// ----------------------------------------------------------------------------
//  AudioDeviceZmf() - dtor
// ----------------------------------------------------------------------------

AudioDeviceZmf::~AudioDeviceZmf()
{
    LOG(INFO) << __FUNCTION__;

    Terminate();

    _ptrAudioBuffer = NULL;
}

// ============================================================================
//                                     API
// ============================================================================

// ----------------------------------------------------------------------------
//  AttachAudioBuffer
// ----------------------------------------------------------------------------

void AudioDeviceZmf::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer)
{

    _ptrAudioBuffer = audioBuffer;
    // Inform the AudioBuffer about default settings for this implementation.
    _ptrAudioBuffer->SetRecordingSampleRate(16000);
    _ptrAudioBuffer->SetPlayoutSampleRate(16000);
    _ptrAudioBuffer->SetRecordingChannels(1);
    _ptrAudioBuffer->SetPlayoutChannels(1);
}

// ----------------------------------------------------------------------------
//  ActiveAudioLayer
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::ActiveAudioLayer(
    AudioDeviceModule::AudioLayer& audioLayer) const
{
    audioLayer = AudioDeviceModule::kDummyAudio;
    return 0;
}

// ----------------------------------------------------------------------------
//  Init
// ----------------------------------------------------------------------------

AudioDeviceGeneric::InitStatus AudioDeviceZmf::Init()
{

    rtc::CritScope lock(&_critSect);
    _initialized = true;
    _playoutSampleRateHz = 0;
    _playoutBufferUsed = 0;
    _playoutBuffer = NULL;
    _recordSampleRateHz = 0;
    _recordBufferUsed = 0;
    _recordBuffer = NULL;

    return InitStatus::OK;
}

// ----------------------------------------------------------------------------
//  Terminate
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::Terminate()
{

    rtc::CritScope lock(&_critSect);
    _initialized = false;
    if (_playoutBuffer) {
        free (_playoutBuffer);
        _playoutBuffer = NULL;
    }
    if (_recordBuffer) {
        free (_recordBuffer);
        _recordBuffer = NULL;
    }
    return 0;
}

// ----------------------------------------------------------------------------
//  Initialized
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::Initialized() const
{
    return (_initialized);
}

// ----------------------------------------------------------------------------
//  SpeakerIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SpeakerIsAvailable(bool& available)
{
    available = true;
    return 0;
}

// ----------------------------------------------------------------------------
//  InitSpeaker
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::InitSpeaker()
{

    rtc::CritScope lock(&_critSect);
	_speakerIsInitialized = true;

	return 0;
}

// ----------------------------------------------------------------------------
//  MicrophoneIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneIsAvailable(bool& available)
{
    rtc::CritScope lock(&_critSect);
    available = true;

    return 0;
}

// ----------------------------------------------------------------------------
//  InitMicrophone
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::InitMicrophone()
{

    rtc::CritScope lock(&_critSect);

    _microphoneIsInitialized = true;

    return 0;
}

// ----------------------------------------------------------------------------
//  SpeakerIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::SpeakerIsInitialized() const
{
    return (_speakerIsInitialized);
}

// ----------------------------------------------------------------------------
//  MicrophoneIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::MicrophoneIsInitialized() const
{
    return (_microphoneIsInitialized);
}

// ----------------------------------------------------------------------------
//  SpeakerVolumeIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SpeakerVolumeIsAvailable(bool& available)
{
    available = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetSpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetSpeakerVolume(uint32_t volume)
{
	return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SpeakerVolume(uint32_t& volume) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  SetWaveOutVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetWaveOutVolume(uint16_t volumeLeft, uint16_t volumeRight)
{
    return -1;
}

// ----------------------------------------------------------------------------
//  WaveOutVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::WaveOutVolume(uint16_t& volumeLeft, uint16_t& volumeRight) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  MaxSpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MaxSpeakerVolume(uint32_t& maxVolume) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  MinSpeakerVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MinSpeakerVolume(uint32_t& minVolume) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerVolumeStepSize
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SpeakerVolumeStepSize(uint16_t& stepSize) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerMuteIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SpeakerMuteIsAvailable(bool& available)
{

    available = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetSpeakerMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetSpeakerMute(bool enable)
{
    return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SpeakerMute(bool& enabled) const
{
    if (_lastPlayoutMs < 0)
        return -1;
    enabled = (rtc::TimeMillis() - _lastPlayoutMs > 1000);
    return 0;
}

// ----------------------------------------------------------------------------
//  MicrophoneMuteIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneMuteIsAvailable(bool& available)
{
    available = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetMicrophoneMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetMicrophoneMute(bool enable)
{
    return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneMute
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneMute(bool& enabled) const
{
    if (_lastRecordMs < 0)
        return -1;
    enabled = (rtc::TimeMillis() - _lastRecordMs > 1000);
    return 0;
}

// ----------------------------------------------------------------------------
//  MicrophoneBoostIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneBoostIsAvailable(bool& available)
{
    available = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetMicrophoneBoost
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetMicrophoneBoost(bool enable)
{
    return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneBoost
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneBoost(bool& enabled) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  StereoRecordingIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StereoRecordingIsAvailable(bool& available)
{

    available = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetStereoRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetStereoRecording(bool enable)
{

    rtc::CritScope lock(&_critSect);
    if (enable)
        return -1;
    return 0;
}

// ----------------------------------------------------------------------------
//  StereoRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StereoRecording(bool& enabled) const
{
    enabled = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  StereoPlayoutIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StereoPlayoutIsAvailable(bool& available)
{

    available = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetStereoPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetStereoPlayout(bool enable)
{
    if (enable)
        return -1;
    return 0;
}

// ----------------------------------------------------------------------------
//  StereoPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StereoPlayout(bool& enabled) const
{
    enabled = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetAGC
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetAGC(bool enable)
{
    _bAGC = enable;
    return 0;
}

// ----------------------------------------------------------------------------
//  AGC
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::AGC() const
{
    return true;
}
bool AudioDeviceZmf::BuiltInAECIsAvailable() const
{
    return _bAEC;
}
int32_t AudioDeviceZmf::EnableBuiltInAEC(bool enable)
{
    _bAEC = enable;
    return 0;
}
bool AudioDeviceZmf::BuiltInAGCIsAvailable() const {
    return _bAGC;
}
int32_t AudioDeviceZmf::EnableBuiltInAGC(bool enable) {
    _bAGC = enable;
    return 0;
}
bool AudioDeviceZmf::BuiltInNSIsAvailable() const
{
    return _bNS;
}
int32_t AudioDeviceZmf::EnableBuiltInNS(bool enable) {
    _bNS = enable;
    return 0;
}
// ----------------------------------------------------------------------------
//  MicrophoneVolumeIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneVolumeIsAvailable(bool& available)
{
    available = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  SetMicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetMicrophoneVolume(uint32_t volume)
{
    return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneVolume(uint32_t& volume) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  MaxMicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MaxMicrophoneVolume(
    uint32_t& maxVolume) const
{
    return -1;
}

// ----------------------------------------------------------------------------
//  MinMicrophoneVolume
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MinMicrophoneVolume(
    uint32_t& minVolume) const
{

    return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneVolumeStepSize
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::MicrophoneVolumeStepSize(
    uint16_t& stepSize) const
{

    return -1;
}

// ----------------------------------------------------------------------------
//  PlayoutDevices
// ----------------------------------------------------------------------------

int16_t AudioDeviceZmf::PlayoutDevices()
{
    return Zmf_AudioOutputGetCount();
}

// ----------------------------------------------------------------------------
//  SetPlayoutDevice I (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetPlayoutDevice(uint16_t index)
{
    char name[512];
    //_playoutAutoDevice = (index == 0);
    return Zmf_AudioOutputGetName(index, _playoutDeviceId, name);
}

// ----------------------------------------------------------------------------
//  SetPlayoutDevice II (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetPlayoutDevice(
    AudioDeviceModule::WindowsDeviceType device)
{
    //_playoutAutoDevice = true;
    return 0;
}

// ----------------------------------------------------------------------------
//  PlayoutDeviceName
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::PlayoutDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize])
{
    return Zmf_AudioOutputGetName (index, guid, name);
}

// ----------------------------------------------------------------------------
//  RecordingDeviceName
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::RecordingDeviceName(
    uint16_t index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize])
{
    return Zmf_AudioInputGetName (index, guid, name);
}

// ----------------------------------------------------------------------------
//  RecordingDevices
// ----------------------------------------------------------------------------

int16_t AudioDeviceZmf::RecordingDevices()
{
    return Zmf_AudioInputGetCount();
}

// ----------------------------------------------------------------------------
//  SetRecordingDevice I (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetRecordingDevice(uint16_t index)
{
    char name[512];
    //_recordAutoDevice = (index == 0);
    return Zmf_AudioInputGetName(index, _recordDeviceId, name);
}

// ----------------------------------------------------------------------------
//  SetRecordingDevice II (II)
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetRecordingDevice(
    AudioDeviceModule::WindowsDeviceType device)
{
    //_recordAutoDevice = true;
    return 0;
}

int32_t AudioDeviceZmf::GetCurrentPlayoutDevice(char strGuidUTF8[512])
{
    if (strGuidUTF8) strcpy(strGuidUTF8, _playoutDeviceId);
    return 0;
}
int32_t AudioDeviceZmf::GetCurrentRecordingDevice(char strGuidUTF8[512])
{
    if (strGuidUTF8) strcpy(strGuidUTF8, _recordDeviceId);
    return 0;
}
// ----------------------------------------------------------------------------
//  PlayoutIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::PlayoutIsAvailable(bool& available)
{

    available = true;
    return 0;
}

// ----------------------------------------------------------------------------
//  RecordingIsAvailable
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::RecordingIsAvailable(bool& available)
{
    available = true;

    return 0;
}

// ----------------------------------------------------------------------------
//  InitPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::InitPlayout()
{

    rtc::CritScope lock(&_critSect);

    _playIsInitialized = true;
    return 0;
}

// ----------------------------------------------------------------------------
//  InitRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::InitRecording()
{

    rtc::CritScope lock(&_critSect);

    _recIsInitialized = true;
    return 0;

}

// ----------------------------------------------------------------------------
//  StartRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StartRecording()
{

    rtc::CritScope lock(&_critSect);

    if (_recording)
        return 0;

    Zmf_AudioInputRequestStart(_recordDeviceId, 16000, 1, _bAEC, _bAGC);
    Zmf_AudioInputAddCallback (this, OnMicReceive);
    _lastRecordMs = 0;
    _recording = true;
    _recordSampleRateHz = 0;
    return 0;
}

// ----------------------------------------------------------------------------
//  StopRecording
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StopRecording()
{

    rtc::CritScope lock(&_critSect);

    if (_recording) {
        Zmf_AudioInputRemoveCallback (this);
        Zmf_AudioInputRequestStop(_recordDeviceId);
        _recording = false;
    }
    _recordSampleRateHz = 0;
    _lastRecordMs = -1;
    _recIsInitialized = false;
    {
        rtc::CritScope lock(&_critResampler);
        _resamplers.clear();
        _micInputId.clear();
    }
    return 0;
}

// ----------------------------------------------------------------------------
//  RecordingIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::RecordingIsInitialized() const
{
    return (_recIsInitialized);
}

// ----------------------------------------------------------------------------
//  Recording
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::Recording() const
{
    return (_recording);
}

// ----------------------------------------------------------------------------
//  PlayoutIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::PlayoutIsInitialized() const
{

    return (_playIsInitialized);
}

// ----------------------------------------------------------------------------
//  StartPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StartPlayout()
{

    rtc::CritScope lock(&_critSect);

    if (_playing)
        return 0;

    Zmf_AudioOutputRequestStart(_playoutDeviceId, 16000, 1);
    Zmf_AudioOutputAddCallback (this, OnSpkFillBuf);
    _lastPlayoutMs = 0;
    _playing = true;
    _playoutSampleRateHz = 0;

    return 0;
}

// ----------------------------------------------------------------------------
//  StopPlayout
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::StopPlayout()
{
    rtc::CritScope lock(&_critSect);

    if (_playing) {
        Zmf_AudioOutputRemoveCallback (this);
        Zmf_AudioOutputRequestStop(_playoutDeviceId);
    }
    _lastPlayoutMs = -1;
    _playing = false;
    _playIsInitialized = false;
    return 0;
}

// ----------------------------------------------------------------------------
//  PlayoutDelay
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::PlayoutDelay(uint16_t& delayMS) const
{
    rtc::CritScope lock(&_critSect);
    delayMS = 0;
    return 0;
}

// ----------------------------------------------------------------------------
//  RecordingDelay
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::RecordingDelay(uint16_t& delayMS) const
{
    rtc::CritScope lock(&_critSect);
    delayMS = 0;
    return 0;
}

// ----------------------------------------------------------------------------
//  Playing
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::Playing() const
{
    return (_playing);
}
// ----------------------------------------------------------------------------
//  SetPlayoutBuffer
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::SetPlayoutBuffer(
    const AudioDeviceModule::BufferType type, uint16_t sizeMS)
{
    return 0;
}

// ----------------------------------------------------------------------------
//  PlayoutBuffer
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::PlayoutBuffer(
    AudioDeviceModule::BufferType& type, uint16_t& sizeMS) const
{
    type = AudioDeviceModule::kAdaptiveBufferSize;
    sizeMS = 0;
    return 0;
}

// ----------------------------------------------------------------------------
//  CPULoad
// ----------------------------------------------------------------------------

int32_t AudioDeviceZmf::CPULoad(uint16_t& load) const
{

    load = 0;

    return 0;
}

// ----------------------------------------------------------------------------
//  PlayoutWarning
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::PlayoutWarning() const
{
    return false;
}

// ----------------------------------------------------------------------------
//  PlayoutError
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::PlayoutError() const
{
    return false;
}

// ----------------------------------------------------------------------------
//  RecordingWarning
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::RecordingWarning() const
{
    return false;
}

// ----------------------------------------------------------------------------
//  RecordingError
// ----------------------------------------------------------------------------

bool AudioDeviceZmf::RecordingError() const
{
    return false;
}

// ----------------------------------------------------------------------------
//  ClearPlayoutWarning
// ----------------------------------------------------------------------------

void AudioDeviceZmf::ClearPlayoutWarning()
{
}

// ----------------------------------------------------------------------------
//  ClearPlayoutError
// ----------------------------------------------------------------------------

void AudioDeviceZmf::ClearPlayoutError()
{
}

// ----------------------------------------------------------------------------
//  ClearRecordingWarning
// ----------------------------------------------------------------------------

void AudioDeviceZmf::ClearRecordingWarning()
{
}

// ----------------------------------------------------------------------------
//  ClearRecordingError
// ----------------------------------------------------------------------------

void AudioDeviceZmf::ClearRecordingError()
{
}

int AudioDeviceZmf::PlayoutProcess(const char* inputId, int sampleRateHz, int iChannels,
                                   unsigned char *buf, int len)
{
    if (!inputId || !inputId[0]) return 0;
    if (_playoutAutoDevice && inputId[0] == ' ')
        return 0;
    if (!_playoutAutoDevice && strcmp(_playoutDeviceId, inputId) != 0)
        return 0;
    int offset = 0;
    const int nSamples = sampleRateHz/100;
    const int recBlockSize = iChannels * nSamples * 2;

    _lastPlayoutMs = rtc::TimeMillis();
    if (sampleRateHz != _playoutSampleRateHz) {
        strcpy(_playoutDeviceId, inputId);
        _ptrAudioBuffer->SetPlayoutSampleRate(sampleRateHz);
        _ptrAudioBuffer->SetPlayoutChannels(iChannels);
        _playoutSampleRateHz = sampleRateHz;
        _playoutBufferUsed = 0;
        _playoutBuffer = (char *)realloc(_playoutBuffer, recBlockSize);
    }
    else {
        if (_playoutBufferUsed > 0) {
            offset = recBlockSize - _playoutBufferUsed;
            const void *b = _playoutBuffer+_playoutBufferUsed;
            if (offset <= len) {
                memcpy (buf, b, offset);
                _playoutBufferUsed = 0;
            }
            else {
                memcpy (buf, b, len);
                _playoutBufferUsed += len;
                if (_playoutBufferUsed >= recBlockSize)
                    _playoutBufferUsed = 0;
                return len;
            }
        }
    }

    while (offset + recBlockSize <= len) {
        _ptrAudioBuffer->RequestPlayoutData(nSamples);
        _ptrAudioBuffer->GetPlayoutData(buf+offset);
        offset += recBlockSize;
    }
    if (offset < len) {
        _ptrAudioBuffer->RequestPlayoutData(nSamples);
        _ptrAudioBuffer->GetPlayoutData(_playoutBuffer);
        _playoutBufferUsed = len - offset;
        memcpy (buf+offset, _playoutBuffer, _playoutBufferUsed);
    }
    return len;
}
static void Mix(int16_t target[], const int16_t source[], int32_t nSamples, bool dst_stereo=false, bool src_stereo=false)
{
    int32_t temp(0);
    if (dst_stereo && src_stereo){
        nSamples <<=1;
        dst_stereo = src_stereo = false;
    }
    for (int i = 0; i < nSamples; ++i) {
        int16_t& dst = dst_stereo ? target[i<<1] : target[i];
        const int16_t& src = src_stereo ? source[i<<1] : source[i];
        temp = src + dst;
        if (temp > 32767)
            dst = 32767;
        else if (temp < -32768)
            dst = -32768;
        else
            dst = (int16_t) temp;
    }
}

void AudioDeviceZmf::MixAudio(unsigned char *buf, const int nSamples, const int iChannels)
{
    if (!_resamplers.size()) return;
    ResamplerMap tmp;
    {
        rtc::CritScope lock(&_critResampler);
        tmp = _resamplers;
    }
    size_t outLen;
    int16_t pcm[441*6*2];//stereo 44.1KHz 60ms 2*44.1*60
    for(auto iter : tmp)
    {
        if (iter.second->Pull(pcm, nSamples*iChannels, outLen) == 0)
            Mix((int16_t*)buf, pcm, nSamples, iChannels > 1, iter.second->Stereo());
    }
}
void AudioDeviceZmf::RecordProcess(const char* inputId, int sampleRateHz, int iChannels,
                                   unsigned char *buf, int len, int *micLevel,
                                   int playDelayMS, int recDelayMS, int clockDrift)
{
    if (!inputId || !inputId[0]) return;
    if (!buf || !len) {
        rtc::CritScope lock(&_critResampler);
        if (_micInputId.compare(inputId) == 0)
            _micInputId.clear();
        _resamplers.erase(inputId);
        return;
    }

    if (_recordAutoDevice && inputId[0] == ' ')
        return;
    if (!_recordAutoDevice && strcmp(_recordDeviceId, inputId) != 0)
        return;

    if (!_micInputId.size() || (!_micCtrlLevel && micLevel)){
        rtc::CritScope lock(&_critResampler);
        _micSampleRate = sampleRateHz;
        _micCtrlLevel = (micLevel!=0);
        _resamplers.erase(inputId);
        _micInputId.assign(inputId);
    } else if (_recordAutoDevice && _micInputId.compare(inputId) != 0) {
        ResamplerPtr resampler;
        {
            rtc::CritScope lock(&_critResampler);
            ResamplerMap::iterator iter = _resamplers.find(inputId);
            if (iter == _resamplers.end())
                iter = _resamplers.insert(ResamplerMap::value_type(inputId, std::make_shared<ResamplerQueue>())).first;
            resampler = iter->second;
        }
        resampler->ResetIfNeeded(sampleRateHz,  _micSampleRate, iChannels);
        resampler->Insert((int16_t*)buf, len>>1);
        return;
    }
    int offset = 0;
    const int nSamples = sampleRateHz/100;
    const int recBlockSize = iChannels * nSamples * 2;
    _lastRecordMs = rtc::TimeMillis();

    if (micLevel)
        _ptrAudioBuffer->SetCurrentMicLevel(*micLevel);

    if (sampleRateHz != _recordSampleRateHz) {
        strcpy(_recordDeviceId, inputId);
        _ptrAudioBuffer->SetRecordingSampleRate(sampleRateHz);
        _ptrAudioBuffer->SetRecordingChannels(iChannels);
        _recordSampleRateHz = sampleRateHz;
        _recordBufferUsed = 0;
        _recordBuffer = (unsigned char *)realloc(_recordBuffer, recBlockSize);
    }
    else {
        if (_recordBufferUsed > 0) {
            if (_recordBufferUsed + len >= recBlockSize) {
                offset = recBlockSize - _recordBufferUsed;
                memcpy (_recordBuffer+_recordBufferUsed, buf, offset);
                _recordBufferUsed = 0;
                if (_recordAutoDevice) MixAudio(_recordBuffer, nSamples, iChannels);
                _ptrAudioBuffer->SetRecordedBuffer(_recordBuffer, nSamples);
                int bufDelayMs = len - offset - recBlockSize;
                bufDelayMs = bufDelayMs > recBlockSize ? bufDelayMs/recBlockSize * 10 : 0;
                _ptrAudioBuffer->SetVQEData(playDelayMS, recDelayMS+bufDelayMs, clockDrift);
                _ptrAudioBuffer->DeliverRecordedData();
            }
            else {
                memcpy (_recordBuffer+_recordBufferUsed, buf, len);
                _recordBufferUsed += len;
                if (micLevel)
                    *micLevel = _ptrAudioBuffer->NewMicLevel();
                return;
            }
        }
    }

    while (offset + recBlockSize <= len) {
        if (_recordAutoDevice) MixAudio(buf+offset, nSamples, iChannels);
        _ptrAudioBuffer->SetRecordedBuffer(buf+offset, nSamples);
        int bufDelayMs = len - offset - recBlockSize;
        bufDelayMs = bufDelayMs > recBlockSize ? bufDelayMs/recBlockSize * 10 : 0;
        _ptrAudioBuffer->SetVQEData(playDelayMS, recDelayMS+bufDelayMs, clockDrift);
        _ptrAudioBuffer->DeliverRecordedData();
        offset += recBlockSize;
    }
    if (offset < len) {
        _recordBufferUsed = len - offset;
        memcpy (_recordBuffer, buf+offset, _recordBufferUsed);
    }

    if (micLevel)
        *micLevel = _ptrAudioBuffer->NewMicLevel();
}

}  // namespace webrtc
