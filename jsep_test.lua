local cpath = package.cpath
local ext = string.match(cpath, '.+(%..*)')
package.cpath = (package.config:sub(1,1) == '/' and 'bin/lib?' or 'bin/?') .. ext
local jp=require'jsep'
assert(jp, 'no jsep library!')
package.cpath = cpath
setmetatable(_G, {__index=jp})
local _myId, _toId = "lua", "240"
local _ws, _pc, isAnswer
local function tojson(tbl)  
    local tmp = {}  
    local k,v = next(tbl)
    while k do
        local k_type = type(k)  
        local v_type = type(v)  
        local key = (k_type == "string" and "\"" .. k .. "\":") or (k_type == "number" and "")  
        local value = (v_type == "table" and serialize(v))  
        or (v_type == "boolean" and tostring(v))  
        or (v_type == "string" and "\"" .. v .. "\"")  
        or (v_type == "number" and v)  
        tmp[#tmp + 1] = key and value and tostring(key) .. tostring(value) or nil  
        k,v = next(tbl, k)
    end  
    if #tbl == 0 then  
        return "{" .. table.concat(tmp, ",") .. "}"  
    else  
        return "[" .. table.concat(tmp, ",") .. "]"  
    end  
end
local function sendSignaling(kind, key, val, toId)
    --print ('>>' .. kind)
    if not toId then toId = _toId end
    if type(val) == 'table' then
        val = tojson(val)
    elseif type(val) == 'string' and string.byte(val, 1) ~= 123 then
        val = '"' .. val .. '"'
    end
    local json = string.format([[{
        "type":"%s",
        "from":"%s",
        "to":"%s",
        "shareid":"%s",
        "%s":%s
    }]], kind, _myId, toId or _toId, _shareid, key, val)
    RTCSocket_Send(_ws, json, 0);
end
local function onSignaling(json)
    --print ('<<' .. json.type)
    if json.type == 'cmd' then
        if json.txt == "hangup" then
            JSEP_Release(_pc);
            _pc = nil
            print"hangup"
        end
    elseif json.type == 'ice' then
        ret = JSEP_AddIceCandidate(_pc, tojson(json.ice))
        assert(ret == 0)
    elseif json.type == 'sdp' then
        ret = JSEP_SetRemoteDescription(_pc, tojson(json.sdp))
        assert(ret == 0)
    end
end
local function onsession(evt, msg)
    local ret
    local json = Json2Table(msg)
    assert(json)
    if evt == RTCSessionEvent_RenegotiationNeeded then
        ret = JSEP_CreateOffer(_pc, 0)
        assert(ret == 0)
    elseif evt == RTCSessionEvent_IceCandidate then
        sendSignaling("ice", "ice", json.JsepIceCandidate)
    elseif evt ==  RTCSessionEvent_IceConnectionStateChange then
        print ("Connection: " .. json.JsepIceConnectionState)
    elseif evt == RTCSessionEvent_SignalingChange then
        print ("Signaling: " .. json.JsepSignalingState)
    elseif evt == RTCSessionEvent_CreateDescriptionSuccess then
        json.JsepSdp = tojson(json.JsepSdp)
        ret = JSEP_SetLocalDescription(_pc, json.JsepSdp);
        assert(ret == 0, JSEP_LastErrorDescription())
    elseif evt == RTCSessionEvent_SetDescriptionFailure then
        print(string.format('** %s ERROR: %s', json.JsepSdpType, json.JsepReason))
    elseif evt == RTCSessionEvent_CreateDescriptionFailure then
        print(string.format('** %s ERROR: %s', json.JsepSdpType, json.JsepReason))
    elseif evt == RTCSessionEvent_SetDescriptionSuccess then
        if json.JsepSdpLocation == "local" then
            sendSignaling("sdp", "sdp", json.JsepSdp);
        elseif json.JsepSdpType == "offer" and json.JsepSdpLocation == "remote" then
            JSEP_CreateAnswer(_pc);
        end
    elseif evt == RTCSessionEvent_AddRemoteStream then
    elseif evt == RTCSessionEvent_RemoveRemoteStream then
    elseif evt == RTCSessionEvent_ToneChange then
    elseif evt == RTCSessionEvent_StatsReport then
    elseif evt == RTCSessionEvent_DataChannelOpen then
    elseif evt == RTCSessionEvent_DataChannelMessage then
    elseif evt == RTCSessionEvent_DataChannelClose then
    end
end
local function update(constraints)
    assert(_pc)
    if type(constraints) == 'boolean' then
        constraints = '{"audio":true}'
    end
    local audio, video = true, false
    local ret, audio, video = JSEP_AddLocalStream(_pc, "local", audio, video, constraints)
    assert(ret == 0)
end
local function call(stunUrl, password, bVideo, toId)
    local configuration = string.format([[{
        "iceServers":[{"credential":"%s","urls":["%s"]}],
        "constraints":{"minPort":8000,"maxPort":9000}
    }]], password, stunUrl)
    local constraints = '{"audio":true}'
    _pc = JSEP_RTCPeerConnection(configuration, false, not isAnswer, onsession)
    assert(_pc)
    update(constraints)
end
local function onmessage(ws, msg, evt)
    if evt == RTCSocketEvent_StateChange then
        local s, v = string.match(msg, '^(%w*)%s*(.*)$')
        if s == 'closed' then
            print'** ERROR: closed by server'
            return MainQuit()
        elseif s == 'open' then
            print'WS onopen'
            RTCSocket_Send(_ws, string.format('{"from":"%s"}', _myId), 0)
            call("stun:test@115.29.4.150:3478", "", false, "240")
        end
    elseif evt == RTCSocketEvent_Message then
        onSignaling(Json2Table(msg))
    end
end
_ws=WebSocket_Connect('ws://192.168.0.240:7000', string.format('{"origin":"%s"}', mId), onmessage);
if not _ws then return print'** ERROR: invalid WS' end
MainLoop()
RTCSocket_Close(_ws)
Terminate()
