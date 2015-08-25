#! /usr/bin/python

import socket
import sys

UDP_IP = '127.0.0.1'
UDP_PORT = 2003
MESSAGE = sys.argv[1]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(MESSAGE, (UDP_IP, UDP_PORT))
