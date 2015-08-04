#! /bin/sh

./send.py '{ "cmd":"add_text", "x":20, "y":10, "w":25, "h":12, "text" : "123 test", "pps":40, "duration":0, "z_depth":1, "prio":0, "repeat_wrap":1, "move_left":0, "id":"small_right" }'

./send.py '{ "cmd":"add_text", "x":0, "y":0, "w":64, "h":34, "text":"#40ff40test 123", "pps":40, "duration":0, "z_depth":0, "prio":0, "repeat_wrap":1, "move_left":1, "id":"big_left" }'

./send.py '{ "cmd":"add_text", "x":0, "y":24, "w":64, "h":10, "text" : "#ff4040yeah bitches!!! ", "pps":40, "duration":0, "z_depth":2, "prio":0, "repeat_wrap":1, "move_left" : 1, "id":"bottom" }'
