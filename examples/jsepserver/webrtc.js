var WebRTC = function(config, trace) {
    'use strict';
    if (!this || this == window) return "17120702";
    if (typeof config != 'object') throw new Error("invalid config");
    var _html_getsourceid = 'https://www.webrtc-experiment.com/getSourceId/';
    var _id = config.id;
    var _url = config.url;
    var _httpRetry = (config.__HTTP_RETRY__ || 3);
    var _httpInterval = (config.__HTTP_INTERVAL__ || 1000)
    if (config.getsourceid && config.getsourceid.indexOf("https://") == 0)
        _html_getsourceid =  config.getsourceid;
    function isstring(str)  { return typeof str == 'string' && str.length > 0; }
    if (!isstring(_id)) _id = Math.random().toString(36).substr(2,4);
    if (!trace) trace = console.debug;
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    var _ws, _pc, _xhr, _answer;
    var _rtc = this;
    _rtc.onerror = console.error;
    function onerror(error, message) {
        if (message) {
            var e = new Error(message);
            e.number = error;
            _rtc.onerror(e);
        }
        else if (typeof error == 'string')
            _rtc.onerror(new Error(error));
        else
            _rtc.onerror(error);
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    if (_url.indexOf("ws") == 0) {
        try { _ws = new WebSocket(_url); } catch(e){ return null; }
        _ws.onerror = function(e) { if (_ws) return onerror("websocket failed"); };
        _ws.onopen = function () { trace('ws onopen'); _ws.send(JSON.stringify({'from':_id}));
            if (_ws.heart) clearTimeout(_ws.heart); _ws.heart = null;
            _rtc.call = call;
            _rtc.send = sendCustomMessage;
            _rtc.onopen(_id);
        };
        _ws.onmessage = function (e) { onSignalingMessage(e.data); }
        _ws.onclose = function (e) { if (!_ws) return; trace('ws onclose');
            _rtc.call = _rtc.send = null;
            _rtc.onclose();
        };
        _ws.heart = setTimeout(function(){ return onerror('ws timeout');}, 10000);
    }
    else if (_url.indexOf("http") == 0 && typeof config.confProps == 'object') {
        try {
            var confProps = config.confProps;
            if (!isstring(confProps.roomId)) throw new Error('invalid confProps.roomId');
            if (!isstring(confProps.accountName)) throw new Error('invalid confProps.accountName');
            if (!isstring(confProps.password)) throw new Error('invalid confProps.password');
            if (!isstring(confProps.appKey)) throw new Error('invalid confProps.appKey');
            if (!isstring(confProps.regionId)) throw new Error('invalid confProps.regionId');
            var url = _url+"/v1/conference/"+confProps.roomId;
            delete confProps.roomId;
            var data = JSON.stringify(confProps);

            _xhr = new XMLHttpRequest();
            _xhr._data = [];
            _xhr._datasets = 0;
            _xhr._trycount = 0;
            _xhr.onreadystatechange = function () { if (!_xhr || _xhr.readyState != 4 ) return;
                if (_xhr.status != 200)
                    return onerror(_xhr.status, "http-conference failed");
                var data = JSON.parse(_xhr.responseText);
                if (data.ret != 0)
                    return onerror(data.ret, _xhr.responseText);
                if (!isstring(data.data.instanceId))
                    return onerror("invalid instanceId");
                if (!isstring(data.data.confNum))
                    return onerror("invalid confNum");
                trace('http onopen');
                _xhr.confNum = data.data.confNum;
                _rtc.call = call;
                _rtc.send = sendCustomMessage;
                _xhr.config = data.data,
                _xhr.instanceId = data.data.instanceId;
                _rtc.onopen(_id, data.data);
                delete _xhr.config.confNum;
                delete _xhr.config.instanceId;
                _xhr.heart = setTimeout(getData, _httpInterval);
            };
            _xhr.open("POST", url);
            _xhr.setRequestHeader("Content-type", "application/json; charset=utf-8");
            trace("POST "+url+"\n"+data);
            if (config.__TEST_INSTANCE__) {
                trace("TEST_INSTANCE="+config.__TEST_INSTANCE__);
                _xhr.setRequestHeader("TEST_INSTANCE", config.__TEST_INSTANCE__);
            }
            _xhr.send(data);
        } catch(e) { throw e; }
    }
    else
        throw new Error('invalid url');
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    function _dosetdata() {try {
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange =  function () { if (xhr.readyState != 4 || !_xhr) return;
            if (xhr.status != 200){
                _xhr._datasets = 0;
                _xhr._trycount++;
                if (_xhr._trycount >= _httpRetry)
                    return onerror(xhr.status, "http-set failed");
                else
                    return trace("ERROR: http-set try " + _xhr._trycount + ", status is " + xhr.status);
            }
            _xhr._trycount = 0;
            _xhr._data.splice(0,_xhr._datasets);
            _xhr._datasets = 0;
            var data = JSON.parse(xhr.responseText);
            if (data.ret != 0)
                return onerror(data.ret, xhr.responseText);
            else if (_xhr._data.length > 0)
                return _dosetdata();

        }
        var url = _url+"/v1/set/"+_xhr.confNum+"/"+_xhr.instanceId;
        trace("POST " + url);
        xhr.open("POST", url);
        _xhr._datasets = _xhr._data.length;
        xhr.setRequestHeader("Content-type", "application/json; charset=utf-8");
        xhr.send(JSON.stringify({data:_xhr._data}));
    } catch(e){return onerror(e);}
    }
    function sendSingalingMessage(json, peer) {
        json["from"]=_id;
        if (_ws){
            if (!peer) throw new Error("invalid peer");
            json["to"]= peer;
            _ws.send(JSON.stringify(json));
        }
        else if (_xhr) { 
            json["to"]= peer || _xhr.instanceId;
            _xhr._data.push(JSON.stringify(json));
            if (_xhr._datasets == 0) _dosetdata();
        }
        else
            return onerror("invalid state");
        if (json.txt)
            trace("&gt&gt" + json.txt);
        else if (json.ice && json.ice.candidate)
            trace("&gt&gt"+json.ice.candidate);
        else
            trace("&gt&gt"+json.type);
    }
    function sendCustomMessage(text, peer) {
        if (_pc && _pc.dc)
            _pc.dc.send(text);
        else
            sendSingalingMessage({'txt':text,'type':'msg'}, peer);
    }
    function _hungup(){
        if (_pc.heart) clearTimeout(_pc.heart);
        var streams = _pc.getLocalStreams();
        for (var i=0,len=streams.length;i<len;++i){
            var tracks = streams[i].getTracks();
            for(var j=0,len2=tracks.length;j<len2;++j) tracks[j].stop();
            if (_pc.removeStream) try{
                _pc.removeStream(streams[i]); 
            } catch(e){}
        }
        if (_pc.senders) try{
            for(var i=0,len=_pc.senders.length;i<len;++i) _pc.removeTrack(_pc.senders[i]);
        } catch(e){}
        _pc.close();
        _answer = _pc = null;
        _rtc.share = null;
        _rtc.update = null;
        _rtc.mute = null;
        trace('hangup ok');
    }
    function onSignalingMessage(str) {
        var json = JSON.parse(str);
        if (!json) return trace('invalid msg: ' + str);
        if (json.type == "msg") return _rtc.onmessage(json.txt, json.from);
        else if (json.type =='cmd'){
            trace("&lt&lt" + json.txt);
            if (json.txt == "hangup") {
                if (_pc) {
                    _hungup();
                    _rtc.onhangup();
                }
            }
            return;
        }
        if (json.ice && json.ice.candidate)
            trace("&lt&lt"+json.ice.candidate);
        else
            trace("&lt&lt" + json.type);
        try {
            if (!_pc) {
                _answer = true;
                _rtc.oncall(json.from);
            }
            if (!_pc) return;
            if (json.ice)
                _pc.addIceCandidate(new RTCIceCandidate(json.ice));
            if (json.sdp){
                if (isstring(json.shareid)) _pc.remoteStreamTypes[json.shareid]='peershare';
                var desc = new RTCSessionDescription(json.sdp);
                _pc.setRemoteDescription(desc, function(){onSetSdpSuccess(desc.type, "remote", desc);}, onerror);
            }
        } catch(e) { return onerror(e); }
    }

    function getData() { if (!_xhr) return; try {
        var xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function () { if (xhr.readyState != 4 || !_xhr) return;
            if (xhr.status != 200)
                trace("ERROR: http-get status " + xhr.status);
            else {
                var data = JSON.parse(xhr.responseText);
                if (data.ret != 0)
                    return onerror(data.ret, xhr.responseText);

                var msg = data.data;
                if (Array.isArray(msg) && msg.length > 0)
                    for(var i=0,len=msg.length;i<len;++i) onSignalingMessage(msg[i]);
            }
            _xhr.heart = setTimeout(getData, _httpInterval);
        };
        var url = _url+"/v1/get/"+_xhr.confNum+"/"+_xhr.instanceId;
        //trace("GET " + url);
        xhr.open("GET", url);
        xhr.send();
    } catch(e) { return onerror(e); }
        if (_xhr._datasets == 0 && _xhr._data.length > 0)
            _dosetdata();
    }

    function onSetSdpSuccess(type, locate, desc) {
        trace("set "+type+" into " + locate + " ok");
        if (locate == "local")
            sendSingalingMessage({'sdp':desc,'type':'sdp', 'shareid':_pc.localShareId}, _pc.peer);
        if (type == "offer" && locate == "remote"){
            if (_pc.localVideoStream) _pc.onnegotiationneeded();
        }
    }
    function onCreateSdpSuccess(desc) {
        _pc.setLocalDescription(desc, function(){ onSetSdpSuccess(desc.type, "local", desc);}, onerror);
    }
    function hangup() {
        if (!_pc) return;
        sendSingalingMessage({'txt':'hangup','type':'cmd'}, _pc.peer);
        _hungup();
    }
    function disconnect() {
        hangup();
        if (_ws) {
            if (_ws.heart) clearTimeout(_ws.heart);
            _ws.close();
            _ws = null;
        }
        if (_xhr) {
            if (_xhr.heart) clearTimeout(_xhr.heart);
            _xhr.abort();
            var url;
            if (_xhr.instanceId && _xhr.confNum)
                url = _url+"/v1/release/"+_xhr.confNum+"/"+_xhr.instanceId;
            _xhr = null;
            if (url){
                var xhr = new XMLHttpRequest();
                xhr.onreadystatechange = function () { if (xhr.readyState != 4 ) return;
                    trace("http-delete " + (xhr.status == 200 ? "ok" :  "failed"));
                };
                trace("DELETE " + url);
                xhr.open("DELETE", url, true);
                xhr.send();
            }
        }
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    function onDataChannel(dc) {
        dc.onmessage = function (event) { if (_pc) _rtc.onmessage(event.data, _pc.peer); };
        dc.onopen = function () {trace('dc onopen'); _pc.dc = dc; };
        dc.onclose = function () { trace('dc onclose'); if (_pc) _pc.dc = null; };
    }
    function onNegotiationNeeded() {
        if (!_pc) return;
        if (_pc.heart) clearTimeout(_pc.heart);
        _pc.heart = null;
        var state = _pc.signalingState;
        if (state == "have-remote-offer")
            _pc.createAnswer(onCreateSdpSuccess, onerror);
        else if (state == "stable")
            _pc.createOffer(onCreateSdpSuccess, onerror,{'offerToReceiveAudio':true,'offerToReceiveVideo':true});
        else if (state != "closed" && state != "have-local-offer")
            _pc.heart = setTimeout(onNegotiationNeeded, 1000);
    }
    function _resetLocalStream() {
        if (!_pc) return;
        if (!_pc.localVideoStream){
            _pc.localVideoStream = new MediaStream();
            if (!_answer) onDataChannel(_pc.createDataChannel("data channel"));
        }
        else {
            _pc.dtmf = null;
            _rtc.dtmf = null;
            if (_pc.localVideoStream.getVideoTracks().length > 0)
                _rtc.onremovestream(_pc.localVideoStream, 'localvideo');
            var streams = _pc.getLocalStreams();
            for (var i=0,len=streams.length;i<len;++i){
                var s = streams[i];
                if (_shareStream != s) {
                    var tracks = s.getTracks();
                    for(var j=0,len2=tracks.length;j<len2;++j) {
                        var t = tracks[j];
                        t.stop();
                        _pc.localVideoStream.removeTrack(t);
                    }
                    if (_pc.removeStream) _pc.removeStream(s);
                }
            }
            if (_pc.senders){
                for(var i=0,len=_pc.senders.length;i<len;++i) _pc.removeTrack(_pc.senders[i]);
                _pc.senders = [];
            }
        }
    }
    function _mute(audio, video) {
        if (!_pc) return;
        var streams = _pc.getLocalStreams();
        for (var i=0,len=streams.length;i<len;++i){
            var tracks = streams[i].getAudioTracks();
            for(var j=0,len2=tracks.length;j<len2;++j)
                tracks[j].enabled = !audio;
            tracks = streams[i].getVideoTracks();
            for(var j=0,len2=tracks.length;j<len2;++j)
                tracks[j].enabled = !video;
        }
    }
    function _doupdate(stream) {
        if (!_pc){
            var tracks = stream.getTracks();
            for(var i=0,len=tracks.length;i<len;++i) tracks[i].stop();
            return;
        }
        _resetLocalStream();
        if (_pc.removeStream)
            _pc.addStream(stream);
        else {
            _pc.senders = [];
            var tracks=stream.getTracks(),len=tracks.length;
            for (var i=0;i<len;++i) _pc.senders.push(_pc.addTrack(tracks[i],stream));
        }
        var audioTracks = stream.getAudioTracks();
        if (audioTracks.length > 0){
            if (_pc.getSenders)
                _pc.dtmf = _pc.getSenders()[0].dtmf;
            else
                _pc.dtmf = _pc.createDTMFSender(audioTracks[0]);
            if (_pc.dtmf) {
                _pc.dtmf.ontonechange = function(e) { trace("dtmf: " + e.tone); }
                _rtc.dtmf = function(tones,duration,interToneGap) {
                    if (!_pc || !_pc.dtmf) return;
                    _pc.dtmf.insertDTMF(tones, duration, interToneGap);
                }
            }
        }
        var videoTracks = stream.getVideoTracks(),len=videoTracks.length;
        if (len > 0) {
            for (var i=0;i<len;++i) _pc.localVideoStream.addTrack(videoTracks[i]);
            _rtc.onaddstream(_pc.localVideoStream, 'localvideo');
        }
        _pc.onnegotiationneeded();
    }
    function _update(constraints) {
        if (typeof constraints == 'object') {
            if (typeof constraints.getTracks == 'function'
                && typeof constraints.getAudioTracks == 'function'
                && typeof constraints.getVideoTracks == 'function'){
                trace("media object: " + constraints);
                _doupdate(constraints);
                return;
            }
            else if (constraints.audio || constraints.video) {
                trace("media constraints: " + JSON.stringify(constraints));
                navigator.mediaDevices.getUserMedia(constraints).then(_doupdate).catch(onerror);
                return;
            }
        }
        trace("media closed");
        _resetLocalStream();
        if (_pc) _pc.onnegotiationneeded();
    }
    function call(config, constraints, peer) {
        if (typeof _rtc.onaddstream != 'function') throw new Error("invalid onaddstream");
        if (typeof _rtc.onremovestream != 'function') throw new Error("invalid onremovestream");
        if (!config && _xhr && _xhr.config) config = _xhr.config;
        if (typeof config != 'object') throw new Error("invalid call arguments");

        trace('call ' + JSON.stringify(config));
        _pc = new RTCPeerConnection(config);
        _pc.peer = peer;
        _pc.localShareId="";
        _pc.remoteStreamTypes={};
        _pc.onicecandidate = function(e) { if (!e|| !e.candidate || !_pc) return;
            sendSingalingMessage({'ice':e.candidate,'type':'ice', 'shareid':_pc.localShareId}, peer);
        }
        _pc.onaddstream = function (e) { if (!_pc) return;
            var type = _pc.remoteStreamTypes[e.stream.id];
            if (!type) {
                if (e.stream.getVideoTracks().length > 0)
                    type = 'peervideo';
                else if (e.stream.getAudioTracks().length > 0)
                    type = 'peeraudio';
                else
                    type = 'peerdata';
                _pc.remoteStreamTypes[e.stream.id] = type;
            }
            _rtc.onaddstream(e.stream, type);
        };
        _pc.onremovestream = function(e) { if (!_pc) return;
            var type = _pc.remoteStreamTypes[e.stream.id];
            if (!type) return;
            delete _pc.remoteStreamTypes[e.stream.id];
            _rtc.onremovestream(e.stream,type);
        }
        _pc.oniceconnectionstatechange = function(e) {
            var state = e.currentTarget.iceConnectionState;
            trace("ICE:" + state);
            if (_rtc.oniceconnectionstatechange) _rtc.oniceconnectionstatechange(state);
        }
        _pc.onsignalingstatechange = function(e) {
            var state = e.currentTarget.signalingState;
            trace("signaling:" + state);
            if (_rtc.onsignalingstatechange) _rtc.onsignalingstatechange(state);
        }
        _pc.ondatachannel = function(e) { onDataChannel(e.channel); }
        _pc.onnegotiationneeded = function() {
            if (_pc && !_pc.heart) _pc.heart = setTimeout(onNegotiationNeeded, 1000);
        }
        _update(constraints);
        _rtc.share = sharescreen;
        _rtc.update = _update;
        _rtc.mute = _mute;
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    _rtc.close = disconnect;
    _rtc.hangup = hangup;
    _rtc.onopen = trace;
    _rtc.onclose = disconnect;
    _rtc.oncall = call;
    _rtc.onhangup = hangup;
    _rtc.onmessage = trace;
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    var _shareStream;
    function sharescreen(share, mediaSource) {
        if (!_pc) return;
        if (!share){
            if (_shareStream && _pc.localShareId == _shareStream.id) {
                _pc.removeStream(_shareStream);
                _rtc.onremovestream(_shareStream, 'localshare');
                _pc.localShareId = "";
            }
        }
        else if (_shareStream && _pc.localShareId != _shareStream.id) {
            _pc.localShareId = _shareStream.id;
            _pc.addStream(_shareStream);
            _rtc.onaddstream(_shareStream, 'localshare');
        }
        else {
            getScreenId(mediaSource, function (error, sourceId, screen_constraints) {
                if (!_pc) return;
                if (error) {
                    if (_rtc.onremovestream) _rtc.onremovestream(_shareStream, 'localshare');
                    return trace(error);
                }
                if (_shareStream) return;
                navigator.mediaDevices.getUserMedia(screen_constraints).then(function (stream) {
                    _shareStream = stream;
                    stream.getVideoTracks()[0].onended = function() {
                        _shareStream = null;
                        if (_pc && _pc.localShareId == stream.id) {
                            _pc.removeStream(stream);
                            if (_rtc.onremovestream) _rtc.onremovestream(stream, 'localshare');
                            _pc.localShareId = "";
                        }
                    }
                    if (_pc) {
                        _pc.localShareId = stream.id;
                        _pc.addStream(stream);
                        if (_rtc.onaddstream) _rtc.onaddstream(_shareStream, 'localshare');
                    }
                }).catch(onerror);
            });
        }
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    function getScreenConstraints(error, sourceId) {
        var screen_constraints = {
            audio: false,
            video: {
                mandatory: {
                    chromeMediaSource: error ? 'screen' : 'desktop',
                    maxWidth: window.screen.width > 1920 ? window.screen.width : 1920,
                    maxHeight: window.screen.height > 1080 ? window.screen.height : 1080
                },
                optional: []
            }
        };

        if (sourceId) {
            screen_constraints.video.mandatory.chromeMediaSourceId = sourceId;
        }

        return screen_constraints;
    }
    var iframe;
    function loadIFrame(loadCallback) {
        if (iframe) {
            loadCallback();
            return;
        }

        iframe = document.createElement('iframe');
        iframe.onload = function() {
            iframe.isLoaded = true;

            loadCallback();
        };
        iframe.src = _html_getsourceid;
        iframe.style.display = 'none';
        (document.body || document.documentElement).appendChild(iframe);
    }
    function postGetSourceIdMessage() {
        if (!iframe) {
            loadIFrame(postGetSourceIdMessage);
            return;
        }

        if (!iframe.isLoaded) {
            setTimeout(postGetSourceIdMessage, 100);
            return;
        }

        iframe.contentWindow.postMessage({
            captureSourceId: true
        }, '*');
    }
    var getScreenId = function(mediaSource, callback) {
        if (!!navigator.mozGetUserMedia) {
            //<p>In <a href=\"about:config\">about:config</a>,
            //please enable media.getusermedia.screensharing.enabled<br> and add this" +
            //" site's domain name to media.getusermedia.screensharing.allowed_domains in about:config
            callback(null, 'firefox', {
                video: {
                    mozMediaSource: mediaSource || 'window',
                    mediaSource: mediaSource || 'window'
                }
            });
            return;
        }

        window.addEventListener('message', onIFrameCallback);

        function onIFrameCallback(event) {
            if (!event.data) return;

            if (event.data.chromeMediaSourceId) {
                if (event.data.chromeMediaSourceId === 'PermissionDeniedError') {
                    callback('permission-denied');
                } else callback(null, event.data.chromeMediaSourceId, getScreenConstraints(null, event.data.chromeMediaSourceId));
            }
            window.removeEventListener('message', onIFrameCallback);
        }

        setTimeout(postGetSourceIdMessage, 100);
    };

}
