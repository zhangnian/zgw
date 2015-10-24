# -*- coding: utf-8 -*-
from __future__ import division

import os
import zmq
import sys
import time
import struct
import socket

HOST = '58.67.219.143'
PORT = 8191

def main():
    # protocol Client —> ZGW
    # [len:4byte][type:1 byte][body]

    body = "zhangnian"
    body_len = len(body)
    type = 1

    format = 'iB%ds' % len(body)
    bytes = struct.pack(format, body_len, type, body)

    s = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    s.connect((HOST, PORT))

    msg_cnt = 1000

    ts_begin = int(time.time())
    for i in range(0, msg_cnt):
        s.send(bytes)
        s.recv(128)
    ts_end = int(time.time())

    print 'QPS: %.2f' % ( msg_cnt / (ts_end - ts_begin))


if __name__ == '__main__':
    sys.exit(main())