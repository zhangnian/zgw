# -*- coding: utf-8 -*-
import os
import zmq
import sys
import time
import multiprocessing

import logging

logging.basicConfig(level=logging.DEBUG,
                format='%(asctime)s %(filename)s[line:%(lineno)d] %(levelname)s %(message)s',
                datefmt='%a, %d %b %Y %H:%M:%S')



g_msg_recvd = 0
g_msg_sent = 0


def do_work(msg, push_sock):
    logging.info(u'进程: %d收到消息：%s' % (os.getpid(), msg))
    push_sock.send(msg)
    logging.info(u'进程: %d回复消息' % os.getpid())
    global g_msg_recvd, g_msg_sent
    g_msg_recvd = g_msg_recvd + 1
    g_msg_sent = g_msg_sent + 1

    logging.debug('g_msg_recvd: %d, g_msg_sent: %d', g_msg_recvd, g_msg_sent)


def proc_main(zgw_push, zgw_pull):
    logging.info(u'进程: %d启动成功' % os.getpid())
    ctx = zmq.Context()

    push_sock = ctx.socket(zmq.PUSH)
    push_sock.connect('tcp://%s' % zgw_push)

    pull_sock = ctx.socket(zmq.PULL)
    pull_sock.connect('tcp://%s' % zgw_pull)


    while True:
        data = pull_sock.recv()
        do_work(data, push_sock)



def main():
    """
    if len(sys.argv) != 5:
        print 'usage: %s zgw_push zgw_pull svr_type proc_cnt' % sys.argv[0]
        return -1

    zgw_push = sys.argv[1]          #用于向ZGW发送数据
    zgw_pull = sys.argv[2]          #用于从ZGW接收数据
    svr_type = sys.argv[3]          #进程组类型
    proc_cnt = int(sys.argv[4])     #进程组中的进程数

    """

    zgw_push = '58.67.219.143:8899'
    zgw_pull = '58.67.219.143:11111'
    svr_type = 'chat'
    proc_cnt = 4


    procs = []
    for i in range(0, proc_cnt):
        p = multiprocessing.Process(target=proc_main, args=(zgw_push, zgw_pull))
        procs.append(p)

    for proc in procs:
        proc.start()

    for proc in procs:
        proc.join()


if __name__ == '__main__':
    sys.exit(main())