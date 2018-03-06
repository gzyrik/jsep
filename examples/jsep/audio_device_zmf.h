/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_ZMF_H
#define WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_ZMF_H

#include <stdio.h>
#include <memory>
#include <map>
#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/modules/audio_device/audio_device_generic.h"
#include "webrtc/base/criticalsection.h"

namespace webrtc {
class EventWrapper;
class ThreadWrapper;
class ResamplerQueue;
class AudioDeviceZmf : public AudioDeviceGeneric
{
public:
    AudioDeviceZmf(const int32_t id, const char* deviceName);
    ~AudioDeviceZmf();

    // Retrieve the currently utilized audio layer
    virtual int32_t ActiveAudioLayer(AudioDeviceModule::AudioLayer& audioLayer) const;

    // Main initializaton and termination
    virtual InitStatus Init();
    virtual int32_t Terminate();
    virtual bool Initialized() const;

    // Device enumeration
    virtual int16_t PlayoutDevices();
    virtual int16_t RecordingDevices();
    virtual int32_t PlayoutDeviceName(
        uint16_t index,
        char name[kAdmMaxDeviceNameSize],
        char guid[kAdmMaxGuidSize]);
    virtual int32_t RecordingDeviceName(
        uint16_t index,
        char name[kAdmMaxDeviceNameSize],
        char guid[kAdmMaxGuidSize]);

    // Device selection
    virtual int32_t SetPlayoutDevice(uint16_t index);
    virtual int32_t SetPlayoutDevice(
        AudioDeviceModule::WindowsDeviceType device);
    virtual int32_t SetRecordingDevice(uint16_t index);
    virtual int32_t SetRecordingDevice(
        AudioDeviceModule::WindowsDeviceType device);
    virtual int32_t GetCurrentPlayoutDevice(char strGuidUTF8[512]);
    virtual int32_t GetCurrentRecordingDevice(char strGuidUTF8[512]);
    // Audio transport initialization
    virtual int32_t PlayoutIsAvailable(bool& available);
    virtual int32_t InitPlayout();
    virtual bool PlayoutIsInitialized() const;
    virtual int32_t RecordingIsAvailable(bool& available);
    virtual int32_t InitRecording();
    virtual bool RecordingIsInitialized() const;

    // Audio transport control
    virtual int32_t StartPlayout();
    virtual int32_t StopPlayout();
    virtual bool Playing() const;
    virtual int32_t StartRecording();
    virtual int32_t StopRecording();
    virtual bool Recording() const;

    // Microphone Automatic Gain Control (AGC)
    virtual int32_t SetAGC(bool enable) override;
    virtual bool AGC() const override;
    virtual bool BuiltInAECIsAvailable() const override;
    virtual int32_t EnableBuiltInAEC(bool enable) override;
    virtual bool BuiltInAGCIsAvailable() const override;
    virtual int32_t EnableBuiltInAGC(bool enable) override;
    virtual bool BuiltInNSIsAvailable() const override;
    virtual int32_t EnableBuiltInNS(bool enable) override;

    // Volume control based on the Windows Wave API (Windows only)
    virtual int32_t SetWaveOutVolume(
        uint16_t volumeLeft, uint16_t volumeRight);
    virtual int32_t WaveOutVolume(
        uint16_t& volumeLeft, uint16_t& volumeRight) const;

    // Audio mixer initialization
    virtual int32_t SpeakerIsAvailable(bool& available);
    virtual int32_t InitSpeaker();
    virtual bool SpeakerIsInitialized() const;
    virtual int32_t MicrophoneIsAvailable(bool& available);
    virtual int32_t InitMicrophone();
    virtual bool MicrophoneIsInitialized() const;

    // Speaker volume controls
    virtual int32_t SpeakerVolumeIsAvailable(bool& available);
    virtual int32_t SetSpeakerVolume(uint32_t volume);
    virtual int32_t SpeakerVolume(uint32_t& volume) const;
    virtual int32_t MaxSpeakerVolume(uint32_t& maxVolume) const;
    virtual int32_t MinSpeakerVolume(uint32_t& minVolume) const;
    virtual int32_t SpeakerVolumeStepSize(uint16_t& stepSize) const;

    // Microphone volume controls
    virtual int32_t MicrophoneVolumeIsAvailable(bool& available);
    virtual int32_t SetMicrophoneVolume(uint32_t volume);
    virtual int32_t MicrophoneVolume(uint32_t& volume) const;
    virtual int32_t MaxMicrophoneVolume(uint32_t& maxVolume) const;
    virtual int32_t MinMicrophoneVolume(uint32_t& minVolume) const;
    virtual int32_t MicrophoneVolumeStepSize(
        uint16_t& stepSize) const;

    // Speaker mute control
    virtual int32_t SpeakerMuteIsAvailable(bool& available);
    virtual int32_t SetSpeakerMute(bool enable);
    virtual int32_t SpeakerMute(bool& enabled) const;

    // Microphone mute control
    virtual int32_t MicrophoneMuteIsAvailable(bool& available);
    virtual int32_t SetMicrophoneMute(bool enable);
    virtual int32_t MicrophoneMute(bool& enabled) const;

    // Microphone boost control
    virtual int32_t MicrophoneBoostIsAvailable(bool& available);
    virtual int32_t SetMicrophoneBoost(bool enable);
    virtual int32_t MicrophoneBoost(bool& enabled) const;

    // Stereo support
    virtual int32_t StereoPlayoutIsAvailable(bool& available);
    virtual int32_t SetStereoPlayout(bool enable);
    virtual int32_t StereoPlayout(bool& enabled) const;
    virtual int32_t StereoRecordingIsAvailable(bool& available);
    virtual int32_t SetStereoRecording(bool enable);
    virtual int32_t StereoRecording(bool& enabled) const;

    // Delay information and control
    virtual int32_t SetPlayoutBuffer(
        const AudioDeviceModule::BufferType type, uint16_t sizeMS);
    virtual int32_t PlayoutBuffer(
        AudioDeviceModule::BufferType& type, uint16_t& sizeMS) const;
    virtual int32_t PlayoutDelay(uint16_t& delayMS) const;
    virtual int32_t RecordingDelay(uint16_t& delayMS) const;

    // CPU load
    virtual int32_t CPULoad(uint16_t& load) const;

    virtual bool PlayoutWarning() const;
    virtual bool PlayoutError() const;
    virtual bool RecordingWarning() const;
    virtual bool RecordingError() const;
    virtual void ClearPlayoutWarning();
    virtual void ClearPlayoutError();
    virtual void ClearRecordingWarning();
    virtual void ClearRecordingError();

    virtual void AttachAudioBuffer(AudioDeviceBuffer* audioBuffer);

    int  PlayoutProcess(const char* outputId, int sampleRateHz,int iChannels,unsigned char *buf,int len);
    void RecordProcess(const char* inputId, int sampleRateHz,int iChannels, unsigned char *buf,int len, int *micLevel,
                       int playDelayMS, int recDelayMS, int clockDrift);

private:
    int _playoutSampleRateHz;
    int _playoutBufferUsed;
    char *_playoutBuffer;
    char _playoutDeviceId[512];
    const bool _playoutAutoDevice;

    int _recordSampleRateHz;
    int _recordBufferUsed;
    unsigned char *_recordBuffer;
    char _recordDeviceId[512];
    const bool _recordAutoDevice;

    AudioDeviceBuffer* _ptrAudioBuffer;
    rtc::CriticalSection _critSect;

    bool _initialized;
    bool _recording;
    bool _playing;
    bool _recIsInitialized;
    bool _playIsInitialized;
    bool _speakerIsInitialized;
    bool _microphoneIsInitialized;
    bool _bAEC;
    bool _bAGC;
    bool _bNS;
    int64_t _lastPlayoutMs;
    int64_t _lastRecordMs;
    typedef std::shared_ptr<ResamplerQueue> ResamplerPtr;
    typedef std::map<std::string, ResamplerPtr> ResamplerMap;
    ResamplerMap _resamplers;
    rtc::CriticalSection _critResampler;
    std::string _micInputId;
    bool _micCtrlLevel;
    int _micSampleRate;
    void MixAudio(unsigned char *buf, const int nSamples, const int iChannels);
};

}  // namespace webrtc

#endif  // WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_ZMF_H
