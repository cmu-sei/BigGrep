#!/usr/bin/env python

# BigGrep
#
# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2011-2017 by Carnegie Mellon University
# @author:    Charles Hines, Matt Coates <mc-contact@cert.org>

# -*- python -*-
# Note that the line above ("#!/usr/bin/env python2.6") is to ensure that in
# our mixed environment (RHEL 5, RHEL 6, and Ubuntu x.y) that this script runs
# on all of the choices (RHEL 5 being the problem where the default Python is
# 2.4).  I would really prefer to have a better way of making sure that this
# script is using Python 2.6 OR ABOVE since 2.7 works fine too, but for now
# the hardcoded use of 2.6 is likely safest in our setup.  Feel free to remove
# the version number to use the system default Python if your system is at 2.6
# or above already...

# This script looks in the specified directories for all .bgi files,
# preprocesses the search terms, and invokes bgparse on each of them in
# parallel.  Shows progress using cheap terminal tricks (likely works only
# under Linux under certain terminal types).

import sys
import optparse
import ConfigParser
import glob
import biggrep.bgsearch
import os
import fnmatch

import logging
import logging.handlers

if __name__ == '__main__':
    ###########################################################################
    #
    # set up some logging functions
    #
    ###########################################################################
    #sys.stderr.write("###########################################################################\n")
    #sys.stderr.write("##                                                                       ##\n")
    #sys.stderr.write("## THIS IS A DEVELOPMENT VERSION OF BIGGREP, DON'T EXPECT SANE RESULTS!! ##\n")
    #sys.stderr.write("##                                                                       ##\n")
    #sys.stderr.write("###########################################################################\n")
    
    logging.basicConfig(level=logging.WARNING,
                        #filename="/tmp/funcdump_log.txt", # uncomment, overrides stream setting
                        format='%(asctime)s %(name)s [%(levelname)s] %(message)s',
                        datefmt='%Y%m%d %H:%M:%S',
                        stream=sys.stderr)
    #logger = logging.getLogger('BGSEARCH')
    logger = logging.getLogger()
    jdlogger = logging.getLogger('jobdispatch')
    #jdlogger.setLevel(logging.ERROR)
    #ch = logging.StreamHandler()
    #formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    #ch.setFormatter(formatter)
    #ch.setLevel(logging.DEBUG)

    parser = optparse.OptionParser(usage="usage: %prog [options] term term term...",version="2.7")
    parser.add_option('-a', '--ascii', action='append', type='string', default=[], 
                      help='ascii string search term')
    parser.add_option('-b', '--binary', action='append', type='string', default=[], 
                      help='binary hexadecimal string search term')
    parser.add_option('-u', '--unicode', action='append', type='string', default=[], 
                      help='unicode string search term')
    
    parser.add_option('-d', '--directory', action='append', type='string', default=[], 
                      help='directory to look for .bgi files in')

    parser.add_option('-r', '--recursive', action='store_true', default=False, 
                      help='recurse down the directories looking for .bgi files')
    
    #parser.add_option('-m', '--metadata', action='store_true', default=False, 
    #                  help='show metadata associated with each result, if available')
    # think we actually want metadata to be the default:
    parser.add_option('-M', '--no-metadata', action='store_false', dest='metadata', default=True,
                      help='do not show metadata associated with each result, if available')

    parser.add_option('-v', '--verify', action='store_true', default=False, 
                      help='invoke bgverify on candidate answers')
    parser.add_option('-y', '--yara', action='store', type='string', default=None, 
                      help='invoke yara on the specified file to verify candidate answers')
    parser.add_option('-l', '--limit', action='store', type='int', default=15000, 
                      help='do not verify above this number of candidates (default %default)')
    

    parser.add_option('-f', '--filter', action='append', type='string', default=[], 
                      help='metadata filter criteria. Supported operators: =,<,>,<=,>=,!= (eg. size>=1024)')

    #parser.add_option('-c','--celery', action='store', type='string', default=None, help='Celery config file')
    parser.add_option('-c','--celery', action='store', type='string', default=None, help=optparse.SUPPRESS_HELP)

    parser.add_option('-n', '--numprocs', action='store', type='int', default=12, 
                      help='number of simultaneous .bgi files to search (default %default)')
    parser.add_option('--banner', action='store', type='string', help='text file to display as a banner/MOTD')
    parser.add_option('-i', '--index-order', action='store', type="string", default="undefined", help='Alter the order in which index files are searched.  Possible values are: "shuffle" and "alpha"  "alpha" will sort all index files from all directories in alphabetic order based on file name.  "shuffle" will shuffle the order in a pseudo-random manner.  The order in which index files are searched is not random, and may be the same between runs.')
    parser.add_option('-t', '--throttle', action='store', default=10000, type='int', help='throttle index parsing when this many candidates are buffered. (default %default)')
    parser.add_option('-V', '--verbose', action='store_true', default=False, help='verbose output')
    parser.add_option('-D', '--debug', action='store_true', default=False, help='diagnostic output')
    parser.add_option('--syslog', action='store', type='string', help='log output to syslog, e.g. facility[@address]')
    parser.add_option('--metrics', action='store_true', default=False, help='display per-directory timing metrics')

    cfg_options = []
    try:
        with open("/etc/biggrep/biggrep.conf") as config_file:
            for line in config_file:
                if line.startswith("#"):
                    continue
                if line.strip() == "":
                    continue
                if line.find("=") == -1:
                    cfg_options.append("--%s" % line.strip())
                else:
                    k,v = line.split("=",1)
                    k=k.strip()
                    v=v.strip()
                    cfg_options.extend(["--%s" % k, v])
    except IOError:
        pass
    (options, args) = parser.parse_args(args=cfg_options)
    cfg_dirs = []
    if len(options.directory) > 0:
        cfg_dirs = list(options.directory)
    (options, args) = parser.parse_args(args = sys.argv[1:], values=options)
    if len(options.directory)>len(cfg_dirs):
        for i in cfg_dirs:
            options.directory.remove(i)
   
    if options.banner:
        with open(options.banner) as f:
            sys.stderr.write(f.read()) 
    if options.metrics:
        # set proper logging level (show per-directory search metrics)
        logger.setLevel(logging.WARNING)
        jdlogger.setLevel(logging.WARNING)
    if options.verbose:
        # set proper logging level (overrides quiet, info & above)
        logger.setLevel(logging.INFO)
        jdlogger.setLevel(logging.INFO)
    if options.debug:
        # set proper logging level (debug implies & overrides verbose)
        logger.setLevel(logging.DEBUG)
        jdlogger.setLevel(logging.DEBUG)

    if options.syslog:
        # choices: local6, local6@localhost, local6@/dev/log, local6@foo:10514
        # split into facility@address
        if "@" in options.syslog:
            fac, addr = options.syslog.split("@",1)
        else:
            fac = options.syslog
            addr = "/dev/log"
        if "/" not in addr:
            if ":" in addr:
                host,port = addr.split(":",1)
                addr = (host,int(port))
            else:
                addr = (addr,514)
        handler = logging.handlers.SysLogHandler(facility=fac, address=addr)
        logger.addHandler(handler)
        jdlogger.addHandler(handler)
        
    if len(options.directory) < 1:
        logger.error("no directories specified")
        sys.exit(1)

    if options.yara:
        try:
            import yara
            yara.compile(options.yara)
        except ImportError:
            logger.error("Yara python module not installed.")
            sys.exit(1)
        except Exception as e:
            logger.error("The supplied Yara file is unusable: %s"%str(e))
            sys.exit(1)

    searchterms = []
    for arg in options.binary:
        searchterms.append(biggrep.bgsearch.ConvertSearchTerm(logger, arg, 'hexadecimal'))
    for arg in options.ascii:
        searchterms.append(biggrep.bgsearch.ConvertSearchTerm(logger, arg, 'ascii'))
    for arg in options.unicode:
        searchterms.append(biggrep.bgsearch.ConvertSearchTerm(logger, arg, 'unicode'))
    for arg in args:
        searchterms.append(biggrep.bgsearch.ConvertSearchTerm(logger, arg))

    if len(searchterms) < 1:
        logger.error("No search terms specified.")
        sys.exit(1)    

    logger.info("searching for %d search terms: %s" % (len(searchterms),searchterms))
    
    # find all .bgi files in the directory/ies pointed to by config.
    
    index_files = []
    for d in options.directory:
        if options.recursive:
            for (root, dirnames, filenames) in os.walk(d):
                for filename in fnmatch.filter(filenames, '*.bgi'):
                    index_files.append(os.path.join(root, filename))
        else:
            for f in fnmatch.filter(os.listdir(d), '*.bgi'):
                index_files.append(os.path.join(d,f))
    if options.index_order:
        if options.index_order == "shuffle":
            import random
            # Set the random seed to a known value to ensure the indecies are
            # searched in the same order for each run.  This is critical for 
            # performance measurement. 
            random.seed(1) 
            random.shuffle(index_files)
        elif options.index_order == "alpha":
            index_files.sort(key=lambda x: os.path.basename(x))

        logger.debug("Index file search queue order: %s"%str(index_files))


    num_index_files = len(index_files)

    if num_index_files < 1:
        logger.error("no index files found")
        sys.exit(1)

    global printed
    printed=0
    def pr(file,metadata):
        if options.metadata:
            print "%s%s"%(file, metadata)
        else:
            print "%s"%(file)
        global printed
        if printed < 10:
            printed += 1
            sys.stdout.flush()

    ret = 0
    try:
        if options.celery:
            import biggrep.bgcelery
            ret=biggrep.bgcelery.search(searchterms,index_files,pr,biggrep.bgsearch.ps,options.verify,options.filter,options.limit)
        else:
            ret=biggrep.bgsearch.search(searchterms,index_files,pr,biggrep.bgsearch.ps,options.verify,options.filter,options.numprocs,options.limit,options.verbose,options.debug,logger,options.yara,options.throttle,options.metrics,options.directory)
    except KeyboardInterrupt:
        sys.exit(1)

    sys.stderr.write("\n")
    sys.exit(ret)
