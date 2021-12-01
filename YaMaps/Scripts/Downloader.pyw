# PythonCE 2.5

import sys
import _socket
import select

script, url, url_params, file_path = sys.argv

def getImage():
    # --- socket ---
    try:
        s = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
        s.connect((url, 80))
        request = "GET %s HTTP/1.1\r\nHOST: %s\r\n\r\n"%((url_params,url))
        s.sendall(request)

        reply = ""

        while select.select([s], [], [], 3)[0]:
            data = s.recv(2048)
            if not data: break
            reply += data
        
        headers = reply.split('\r\n\r\n')[0]
        image = reply[len(headers)+4:]
    except:
        print 'Error file download'
        return
    # --- save file ----
    try:
        file = open(file_path, 'wb')
        file.write(image)
        file.close()
    except:
        print 'Error file save'
        return
    
getImage()
