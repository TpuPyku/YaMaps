# Python 2.5

import os
import sys
import _socket
import select
import Tkinter

realpath = os.path.realpath('..')
#print realpath
filename = os.path.join(realpath, "YaMaps\\Data\\conf.ini")
#print filename

def getImage():
    config = open(filename, "r")
    settings = config.read().splitlines()
    config.close()

    global ptx, pty, label, z_value, layers, img_size, map_request, map_params
    ptx = settings[1]
    pty = settings[2]
    label = settings[3]
    z_value = settings[4]
    layers = settings[5]
    img_size = u"600,450"

    map_request = "static-maps.yandex.ru"
    map_params = "/1.x/?pt=%s,%s,%s&z=%s&size=%s&l=%s"%((ptx,pty,label,z_value,img_size,layers))

    # --- socket ---
    s = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    s.connect((map_request, 80))
    request = "GET %s HTTP/1.1\r\nHOST: %s\r\n\r\n"%((map_params,map_request))
    s.sendall(request)
    reply = ""
    while select.select([s], [], [], 3)[0]:
        data = s.recv(2048)
        if not data: break
        reply += data
    headers = reply.split('\r\n\r\n')[0]
    image = reply[len(headers)+4:]

    # --- save file ----
    global map_file
    map_file = os.path.join(realpath,"YaMaps\\Images\\map.png")
    file = open(map_file, 'wb')
    file.write(image)
    file.close()

def up_panel():
    img_map2 = Tkinter.PhotoImage(file=map_file)
    panel.configure(image=img_map2)
    panel.image = img_map2

def keyUpdate(event):
    my_step = 0.0025
    if event.num == 1: # Minus zoom
        z_value -= 1
    elif event.keycode == 38:  # UP
        pty += my_step * math.pow(2, 15 - z_value)
    elif event.keycode == 40:  # DOWN
        pty -= my_step * math.pow(2, 15 - z_value)
    elif event.keycode == 37:  # LEFT_ARROW
        ptx -= my_step * math.pow(2, 15 - z_value)
    elif event.keycode == 39:  # RIGHT_ARROW
        ptx += my_step * math.pow(2, 15 - z_value)
    up_panel()

root = Tkinter.Tk()
root.title("YaMaps")
root.geometry("800x480")
getImage()

#img_map = Tkinter.PhotoImage(file=map_file)
panel = Tkinter.Label(root)  #, image=img_map)
panel.pack(side="bottom", fill="both", expand="yes")

bp_file=os.path.join(realpath,'YaMaps\\Images\\Bplus.png')
#imagePlus = Tkinter.PhotoImage(file=bp_file)
buttonPlus = Tkinter.Button(root, text='Plus')  #image=imagePlus)
buttonPlus.bind('<Button-1>')
buttonPlus.pack(side=Tkinter.LEFT)

#imageMinus = Tkinter.PhotoImage(file=realpath+'\\YaMaps\\Images\\Bminus.png')
buttonMinus = Tkinter.Button(root, text="Minus")  #, image=imageMinus)
buttonMinus.bind('<Button-1>',)
buttonMinus.pack(side=Tkinter.LEFT)

root.bind('<Up>', keyUpdate)
root.bind('<Down>', keyUpdate)
root.bind('<Right>', keyUpdate)
root.bind('<Left>', keyUpdate)
root.mainloop()