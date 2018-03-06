import SimpleHTTPServer
import SocketServer
import ssl

httpd = SocketServer.TCPServer(('', 8000), SimpleHTTPServer.SimpleHTTPRequestHandler)
httpd.socket = ssl.wrap_socket (httpd.socket, certfile='cert.pem', keyfile="key.pem", server_side=True)
httpd.serve_forever()
