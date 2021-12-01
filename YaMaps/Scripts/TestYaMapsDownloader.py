# Python 2.5

import os
import sys
import _socket
import select

realpath = os.path.realpath('..')
#print realpath
filename = os.path.join(realpath, "Data\\conf.ini")
#print filename

config = open(filename, "r")
settings = config.read().splitlines()
config.close()
#print settings
#Path = settings[7]

def getImage():
    
    cords = settings[1]
    pt = settings[1]
    label = settings[2]
    z_value = settings[3]
    layers = settings[4]
    #mode = settings[5]
    #trf = settings[6]
    img_size = u"600,450"

    map_request = "static-maps.yandex.ru"
    map_params = "/1.x/?ll=%s&pt=%s,%s&z=%s&size=%s&l=%s"%((cords,pt,label,z_value,img_size,layers))
    #print map_request, map_params

    # --- socket ---
    s = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    s.connect((map_request, 80))
    request = "GET %s HTTP/1.1\r\nHOST: %s\r\n\r\n"%((map_params,map_request))
    #send_request = ''.join(['%08d'%int(bin(ord(i))[2:]) for i in request])
    s.sendall(request)

    reply = ""

    while select.select([s], [], [], 3)[0]:
        data = s.recv(2048)
        if not data: break
        reply += data
    
    headers = reply.split('\r\n\r\n')[0]
    image = reply[len(headers)+4:]

    # --- save file ----
    map_file = os.path.join(realpath,"Images\\map.png")
    file = open(map_file, 'wb')
    file.write(image)
    file.close()

getImage()
