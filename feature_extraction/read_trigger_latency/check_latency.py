#!/usr/bin/python

import json
import argparse
import os.path
import bisect
import numpy

def failed(why):
    print "Parse failed because of [%s]"%(why)
    exit(1)


# only parse read request
def get_lat_data(lat_name):
    lat_dic = {}
    f = open(lat_name)

    req_id=0
    for text in f:
        values = text.split(",")
        elapsed_time = int(values[0])
        latency = int(values[1])
        write_type = int(values[2])
        if (write_type == 0) :
            lat_dic[req_id] = {
                "ts":elapsed_time,
                "lat":latency,
            }
            req_id += 1
        #print "%d, %d" %(lat_dic[req_id-1]["ts"], lat_dic[req_id-1]["lat"])

    #print req_id 
    return lat_dic

def find_io_by_ts(io_lats, io_ts):
    io_reqids = sorted(io_lats.keys())

    for io_reqid in io_reqids:
        if (io_lats[io_reqid]["ts"] > io_ts):
            return ((io_reqid - 1),)

    failed("find_io_by_ts error, Unknown")

def calc_latency(io_lats, lat_threshold, gc_threshold):
#    io_reqids = sorted(io_lats.keys())

    target_ios = []
    for io_reqid in io_lats:
        io_ts = io_lats[io_reqid]["ts"]
        io_lat = io_lats[io_reqid]["lat"]
        if (io_lat < gc_threshold and io_lat> lat_threshold):
            target_ios.append(io_lat)

    min_latency = min(target_ios)
    max_latency = max(target_ios)
    avg_latency = numpy.average(target_ios)
    mid_latency = int(numpy.median(target_ios))

    print "MIN_LATENCY : %d, MAX_LATENCY : %d, AVG_LATENCY: %d, MID_LATENCY: %d\n " %(min_latency, max_latency, avg_latency, mid_latency)


def main_parse(parm_set):
    lat_threshold = parm_set["lat_threshold"]
    gc_threshold = parm_set["gc_threshold"]

    io_lats = get_lat_data(parm_set["io_log"])
    calc_latency(io_lats, lat_threshold, gc_threshold)

def parm_check(parm_set):
    invalid = False
    if (not os.path.isfile(parm_set["io_log"])):
        print "Invalid io_log (%s)"%io_log
        invalid = True

    return invalid


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--io_log', help='[io] log file')
    parser.add_argument('-l', '--lat_threshold', help='latency threshold')
    parser.add_argument('-g', '--gc_threshold', help='GC threshold')
    args = parser.parse_args()

    invalid_args = False
    if ((args.lat_threshold == None) or
        (args.gc_threshold == None)):
        invalid_args = True
        print "threshold(lat:%s, gc:%s) should be given"%(args.lat_threshold, args.gc_threshold)
    
    parm_set = {
        "io_log":args.io_log,
        "lat_threshold":float(args.lat_threshold),
        "gc_threshold":float(args.gc_threshold),
    }

    invalid_args = parm_check(parm_set)
    if (invalid_args):
        print "Too few arguments, please -h"
        exit(1)

    main_parse(parm_set)

