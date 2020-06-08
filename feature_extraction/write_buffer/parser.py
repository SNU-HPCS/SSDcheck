#!/usr/bin/python

import json
import argparse
import os.path
import bisect
import numpy

def failed(why):
    print "Parse failed because of [%s]"%(why)
    exit(1)

def get_lat_data(lat_name):
    lat_dic = {}
    f = open(lat_name)

    req_id=0
    for text in f:
        values = text.split(",")
        elapsed_time = int(values[0])
        latency = int(values[1])
        lat_dic[req_id] = {
            "ts":elapsed_time,
            "lat":latency,
        }
        req_id += 1

    return lat_dic

def find_write_by_ts(write_lats, read_ts):
    write_reqids = sorted(write_lats.keys())

    for write_reqid in write_reqids:
        if (write_lats[write_reqid]["ts"] >= read_ts):
            return ((write_reqid - 1),)

    failed("find_write_by_ts error, Unknown")

def calc_buffer_size(read_lats, write_lats,
                     lat_threshold, gc_threshold):
    read_reqids = sorted(read_lats.keys())
    write_reqids = sorted(write_lats.keys())

    target_writes = []
    for read_reqid in read_reqids:
        read_ts = read_lats[read_reqid]["ts"]
        read_lat = read_lats[read_reqid]["lat"]
        #print read_reqid
        if (read_lat > lat_threshold):
            #print read_ts
            if (read_ts < 4900):
                (reqid, ) = find_write_by_ts(write_lats, read_ts)
                target_writes.append((reqid, read_lat))

    # cal interval
    for i in range(len(target_writes) - 1):
        print "[%3d] read_lat:%d, buffer_size:%d "%(target_writes[i][0],
                                   target_writes[i][1], (4096*(target_writes[i+1][0]-target_writes[i][0])))

    
    # above returns req_id in target_writes[i][0]

def calc_buffer_size2(read_time_max_list, write_latency_max, lat_threshold, gc_threshold, bs_size):
    flush_latency_max = 0
    flush_time_list = []
    read_elapsed_list = sorted(read_time_max_list.keys())
    for i in range(len(read_elapsed_list)):
#        if (read_time_max_list[i] >= lat_threshold):
        
        if (read_time_max_list[i] >= lat_threshold and read_time_max_list[i] < gc_threshold):
            flush_time_list.append(i)
            if (flush_latency_max < read_time_max_list[i]):
                flush_latency_max = read_time_max_list[i]

    print flush_time_list

    major_interval_value = 0
    major_interval_count = 0
    flush_interval_list = []
    flush_interval_dic = {}
    for i in range(len(flush_time_list)-1):
        if i >= 1:
            interval = flush_time_list[i+1] - flush_time_list[i]
#            if interval >= 1:
            if interval >= 2:
                flush_interval_list.append(interval)
                if interval in flush_interval_dic:
                    flush_interval_dic[interval] += 1
                    if major_interval_count < flush_interval_dic[interval]:
                        major_interval_count = flush_interval_dic[interval]
                        major_interval_value = interval
                else:   
                    flush_interval_dic[interval] = 1

    min_interval = min(flush_interval_list)
    max_interval = max(flush_interval_list)
    avg_interval = numpy.average(flush_interval_list)
    mid_interval = int(numpy.median(flush_interval_list))
    std_interval = numpy.std(flush_interval_list)

    print flush_interval_dic
#    print sorted(flush_interval_list)
#    print flush_time_list
#    print "Flush interval min: %d, max: %d, avg: %d mid: %d / max read latency: %d us / max write latency: %d us" \
#        %(min_interval, max_interval, avg_interval, mid_interval, flush_latency_max, write_latency_max)

#    buffer_size = mid_interval * bs_size # kb
    buffer_size = major_interval_value * bs_size # kb
    print "Block size: %2d KB --> Write buffer size: %d KB (interval - avg: %.1lf, std dev: %.1lf) / max read latency: %d us\n" \
        %(bs_size, buffer_size, avg_interval, std_interval, flush_latency_max)


def main_parse(parm_set):
    lat_threshold = parm_set["lat_threshold"]
    gc_threshold = parm_set["gc_threshold"]

    read_lats = get_lat_data(parm_set["read_log"])
    write_lats = get_lat_data(parm_set["write_log"])

    calc_buffer_size(read_lats, write_lats,
                     lat_threshold, gc_threshold)

def parm_check(parm_set):
    invalid = False
    if (not os.path.isfile(parm_set["read_log"])):
        print "Invalid read log (%s)"%read_log
        invalid = True

    if (not os.path.isfile(parm_set["write_log"])):
        print "Invalid write log (%s)"%write_log
        invalid = True

    return invalid


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--read_log', help='[read] log file')
    parser.add_argument('--write_log', help='[write] log file')
    parser.add_argument('-l', '--lat_threshold', help='latency threshold')
    parser.add_argument('-g', '--gc_threshold', help='GC threshold')
    args = parser.parse_args()

    invalid_args = False
    if ((args.lat_threshold == None) or
        (args.gc_threshold == None)):
        invalid_args = True
        print "threshold(lat:%s, gc:%s) should be given"%(args.lat_threshold, args.gc_threshold)
    
    parm_set = {
        "read_log":args.read_log,
        "write_log":args.write_log,
        "lat_threshold":float(args.lat_threshold),
        "gc_threshold":float(args.gc_threshold),
    }

    invalid_args = parm_check(parm_set)
    if (invalid_args):
        print "Too few arguments, please -h"
        exit(1)

    main_parse(parm_set)

