package com.webrtc.jsep;
public class RTCPeerConnection
{
    private long handle;
    private native long createPeerConnection(String config, String constraints);
    private native void releasePeerConnection(long);
    RTCPeerConnection(String config, String constraints){
        handle = createPeerConnection(config, constraints);
    }
    public void release() { handle = releasePeerConnection(handle); }
    protected void finalize() { release() }
    public native int addIceCandidate(String candidate);
    public native int addLocalStream (String streamId,  boolean[] bAudioVideo, String constraints);
    public native void closeChannel (String channelId);
    public native int createChannel (String channelId, String constraints);
    public native int createAnswer (String constraints);
    public native int createOffer (String constraints);
    public native int getStats (String statsType, boolean bDebug);
    public native int insertDtmf(String tones, int duration, int inter_tone_gap);
    public native int publishRemoteStream(String streamId, int renderOrCapturerBits, int videoTrackMask);
    public native void removeLocalStream (String streamId);
    public native int sendMessage(String channelId, String buffer);
    public native int setLocalDescription (String desc);
    public native int setRemoteDescription (String desc);
};
