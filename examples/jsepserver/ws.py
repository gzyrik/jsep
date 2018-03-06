# -*- coding: utf-8 -*-

import socket,ssl,os
import base64
import hashlib
import struct
from select import select

class WebSocket():

    @staticmethod
    def handshake(conn):
        key = None 
        origin=''
        data = conn.recv(8192)
        if not len(data):
            return False
        for line in data.split('\r\n\r\n')[0].split('\r\n')[1:]:
            k, v = line.split(': ')
            if k == 'Sec-WebSocket-Key':
                key = base64.b64encode(hashlib.sha1(v + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
            elif k == 'Origin':
                origin = v
        if not key:
            raise Exception("No Sec-WebSocket-Key")

        response = 'HTTP/1.1 101 Switching Protocols\r\n'\
                   'Upgrade: websocket\r\n'\
                   'Connection: Upgrade\r\n'\
                   'Sec-WebSocket-Accept:' + key + '\r\n\r\n'
        conn.send(response)
        return origin

    @staticmethod
    def recv(conn, data, recvsize=8192):
        if recvsize and recvsize > 0:
            data += conn.recv(recvsize)
        size = len(data)
        if size < 2:
            return (False,data)
        op = ord(data[0])&15
        if 8 == op:
            raise Exception("Close")

        ismask = ord(data[1]) & 128 == 128
        length = ord(data[1]) & 127

        if length == 126:
            if size < 2+2: return (False,data)
            length = (ord(data[2])<<8)|ord(data[3])
            pos = 4
        elif length == 127:
            if size < 2+8: return (False,data)
            length = 0
            for i in range(8):
                length = (length << 8) | ord(data[2+i])
            pos = 10
        else:
            pos = 2

        if ismask:
            if size < pos + 4: return (False,data)
            mask = data[pos:pos+4]
            pos += 4

        end = pos + length
        if size < end: 
            return (False,data)

        if ismask:
            ret = ''.join([chr(ord(d) ^ ord(mask[i%4])) for i,d in enumerate(data[pos:end])])
        else:
            ret = data[pos:end]
        if op == 9:
            WebSocket.send(conn, ret, '\x8a')
        return (ret,data[end:])

    @staticmethod
    def send(conn, data, head='\x81'):
        if len(data) < 126:
            head += struct.pack('B', len(data))
        elif len(data) <= 0xFFFF:
            head += struct.pack('!BH', 126, len(data))
        else:
            head += struct.pack('!BQ', 127, len(data))
        conn.send(head+data)

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sockets= {}#套接字->[接收缓冲,名称,地址]的字典
users  = {}#用户名->套接字

def broadcast(conn, data):
    for sock in sockets.keys():
        if sock != server and sock != conn:
            WebSocket.send(sock, data)

def loopback(conn, data):
    WebSocket.send(conn, data)

def call(conn, data):
    fromId=to=sock=msg=None
    try:
        start = data.index('"from":"')+8
        end = data.index('"',start)
        fromId = data[start:end]
        start = data.find('"to":"')+6
        if start > 6:
            to =  data[start:data.index('"',start)]
            start = data.index('"type":"')+8
            end = data.index('"', start)
            msg = data[start:end]
            if to == '*':
                return broadcast(conn, data)
            elif to in users:
                sock = users[to]
            else:
                WebSocket.send(conn, '{"txt":"hangup","type":"cmd"}');
                raise Exception(fromId + " to invalid Peer: " + to)

    except Exception,e:
        print e

    else:
        if sock:
            print "[%s]>[%s]:%s"%(fromId,to,msg)
            WebSocket.send(sock, data)
        else:
            print '+',fromId
            users[fromId]=conn

def main(handle=call,port=7000):
    name = 'WebSocket Server'
    if os.access("cert.pem", os.R_OK) and os.access("key.pem", os.R_OK):
        name = 'WebSocket SSL Server'
    try:
        server.bind(('', port))
        server.listen(100)
    except Exception, e:
        print e
        exit()

    sockets[server] = ['','SERVER']
    print '%s on port %d' % (name, port)
    while True:
        r, w, e = select(sockets.keys(), [], [])
        for sock in r:
            if sock == server:
                conn, addr = sock.accept()
                try:
                    if name == 'WebSocket SSL Server':
                        conn = ssl.wrap_socket(conn, certfile="cert.pem", keyfile="key.pem", server_side=True)
                    origin = WebSocket.handshake(conn)
                    print '~', origin,addr
                    sockets[conn]=['',origin, addr]
                except Exception,e:
                    print addr, e
                    conn.shutdown(2)
                    conn.close()

            else:
                info=sockets[sock]
                try:
                    remain = info[0]
                    data,remain = WebSocket.recv(sock, remain)
                    while data:
                        handle(sock, data)
                        if len(remain) > 2:
                            data,remain = WebSocket.recv(sock, remain, 0)
                        else:
                            break
                    info[0] = remain
                except Exception,e:
                    del sockets[sock]
                    for k,v in users.items():
                        if v == sock:
                            print '-', k, e
                            del users[k]
                            break
                    else:
                        print '-', info[1], info[2], e
                    sock.shutdown(2)
                    sock.close()

if __name__ == '__main__':
    main()
