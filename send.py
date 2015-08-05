#! /usr/bin/python

import json
import socket
import sys

UDP_IP = '127.0.0.1'
UDP_PORT = 2003
MESSAGE = sys.argv[1]

json_obj = dict()
json_obj['text'] = '   $rHello, world! '
json_obj['duration'] = 60000.0 # 60s
json_obj['id'] = "semi-important"
json_obj['prio'] = 0
json_obj['cmd'] = "add_text"
json_obj['z_depth'] = 0.0
json_obj['x'] = 0.0
json_obj['y'] = 0.0
json_obj['w'] = 96.0
json_obj['h'] = 32.0
json_obj['pps'] = 50.0
json_obj['repeat_wrap'] = 1
json_obj['move_left'] = 1
json_obj['transparency_color'] = "#000000"
json_obj['antialias'] = 1
json_obj['font_name'] = 'Arial'

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(json.dumps(json_obj), (UDP_IP, UDP_PORT))
