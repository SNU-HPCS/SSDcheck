#!/usr/bin/python

import json
import argparse
import os.path
import numpy

def failed(why):
    print "Parse failed because of [%s]"%(why)
    exit(1)

def get_data(objs,target):
    jobs = objs["jobs"]
    res = {}
    for job_elem in jobs:
        if (job_elem["jobname"] == "seq-read"):
            res["seq_r"] = job_elem["read"][target]
        elif (job_elem["jobname"] == "seq-write"):
            res["seq_w"] = job_elem["write"][target]
        elif (job_elem["jobname"] == "rand-read"):
            res["rnd_r"] = job_elem["read"][target]
        elif (job_elem["jobname"].find("rand-write") != -1):
            res["rnd_w"] = job_elem["write"][target]
        else:
            failed("invalid json file jobname:%s"%(job_elem["jobname"]))

    return res

def multiple_write(fd_list,value):
    for fd in fd_list:
        fd.write(value)

def multiple_close(fd_list):
    for fd in fd_list:
        fd.close()

def dump_files(fname_list):
    for (ssd_name, fname) in fname_list:
        print "%s,========== %s =========="%(ssd_name, fname)
        fd = open(fname)
        raw_data = fd.read()
        print raw_data
        fd.close()

def do_iopsbw_parse(base_dir, run_time, task_type, ssd_name, capacity, iter_num):
    bitidxs = range(12,42)      # 12, 13, ... 41
    bitidx_values = range(0,2)  # 0, 1

    #fd_rndw_log = open("%s_%s_rand-write_%s.parsed"%(ssd_name, capacity ,task_type),"w")

    #fd_rndw_log.write(''.join(","+str(e) for e in bitidxs))
    #fd_rndw_log.write("\n")

    total_result = {}
    for bitidx in bitidxs:
        bitidx_dic = {}
        for bitidx_value in bitidx_values:

            average = 0.0
            stderr = 0.0

            tmp_res_list = []
            for _iter in range(1, iter_num + 1):
                bench_name="%s_%s_bitidx-%d_value-%d_runtime-%s"%(
                    ssd_name, capacity, bitidx, bitidx_value, run_time)
                fname = "%s/%s/%s/%s/%s/%s_iter-%d.json"%(
                    base_dir,
                    ssd_name, capacity,
                    "allocation_region",
                    bench_name, bench_name, _iter)
                
                if (os.path.isfile(fname)):
                    fd_data = open(fname)
                    raw_text = fd_data.read()
                    fd_data.close()
                    
                    fail = 0
                    res = None
                    try:
                        res = get_data(json.loads(raw_text), task_type)
                    except ValueError, e:
                        fail = 1
                        print "fail to parse json object for bench_name:%s, iter:%d"%(
                            bench_name, _iter)

                    if (not fail):
                        tmp_res_list.append(res["rnd_w"])

            if (len(tmp_res_list) != iter_num):
                average = 0.0
                stderr = 0.0
            else:
                average = numpy.average(tmp_res_list)
                stddev = numpy.std(tmp_res_list)
                stderr = stddev / pow(iter_num, 0.5)

            bitidx_dic[bitidx_value] = (average,stderr)

        total_result[bitidx] = bitidx_dic

    # dump the results
    print "%s_%s,runtime-%s_iter-%d"%(ssd_name, capacity, run_time, iter_num)
    print ''.join(","+str(e) for e in bitidxs)
    for bitidx_value in bitidx_values:
        line_buf_avg = "%d"%(bitidx_value)
        line_buf_err = ""
        for bitidx in bitidxs:
            line_buf_avg += ",%lf"%(total_result[bitidx][bitidx_value][0])
            line_buf_err += ",%lf"%(total_result[bitidx][bitidx_value][1])

        print line_buf_avg
        print line_buf_err

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    
    #parser.add_argument('-d', '--dir', help='target directory containing raw datas [dir_path] (default: ../../results')
    parser.add_argument('-t', '--type', help='disk type [iops|bw]')
    parser.add_argument('-s', '--ssd', help='ssd name')
    parser.add_argument('-c', '--capacity', help='capacity of target ssd')
    parser.add_argument('-r', '--runtime', help='runtime (ex: 5s, 10s, 15s)')
    parser.add_argument('-i', '--iter_num', help='number of iteration (es: 5)')
    args = parser.parse_args()

    if(args.ssd != None and
       args.capacity != None and
       args.runtime != None and
       args.iter_num != None and
       args.type != None):
        task_type = args.type
        base_dir = "../../results"
        run_time = args.runtime
        ssd_name = args.ssd
        capacity = args.capacity
        iter_num = int(args.iter_num)
        if (task_type != "iops" and
            task_type != "bw"):
            failed("invalid task_type: %s"%(task_type))

        do_iopsbw_parse(base_dir, run_time, task_type, ssd_name, capacity, iter_num)
    else:
        print "Too few arguments, look -h"
