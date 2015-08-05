Required packages:

- libfontconfig1-dev
- libfreetype6-dev
- libjansson-dev


Note: the low-level code for pushing bits to the panels is (C) Henner Zeller <h.zeller@acm.org>.
Only the code that displays the scroll texts and videostreams etc is (C) folkert van heusden <mail@vanheusden.com>.
Maybe some of this all is copyrighted by Adafruit.

Note: you need to change the lib/Makefile file to suit your setup. For example the adafruit "hat" requires a special compile switch. When not doing so, you may not see anything on your led-matrix.

You can send json-encoded commands via udp or udp to this program:

	#! /usr/bin/python

	import json
	import socket
	import sys

	UDP_IP = 'localhost'
	UDP_PORT = 2003
	MESSAGE = sys.argv[1]

	json_obj = dict()
	json_obj['text'] = '#40ff40' + MESSAGE
	json_obj['duration'] = 60100.0 # 60s
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
	json_obj['font_name'] = 'Arial' # 'Comic Sans MS'

	sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	sock.sendto(json.dumps(json_obj), (UDP_IP, UDP_PORT))


This script picks a regular text from its parameter (e.g. ./send.py "hello world") and sens it to the matrix-server. In this case it is running on the same server (localhost) and listening on port 2003 (see the -P parameter). I also configured it to use the TTF "Arial" font. You need to have this font installed. Or use an other font of course.
