#!/usr/bin/python

from rgbmatrix import Adafruit_RGBmatrix

# Rows and chain length are both required parameters:
matrix = Adafruit_RGBmatrix(32, 2)

matrix.Fill(0xFF0000)
time.sleep(0.1)
matrix.Clear()
