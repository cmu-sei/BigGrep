#!/usr/bin/env python

# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2011-2016 by Carnegie Mellon University
# @author:    Charles Hines, Matt Coates <mc-contact@cert.org>

# This script looks in the specified directories for all .bgi files,
# preprocesses the search terms, and invokes bgparse on each of them in
# parallel using the multiprocessing modules.  Shows progress using cheap
# terminal tricks (likely works only under Linux under certain terminal
# types).

import sys
import struct
import os
import binascii
import optparse
import string
import mmap
import multiprocessing
import glob
import subprocess
import signal

import logging

def setQuiet():
    logger.setLevel(logging.ERROR)

def setVerbose():
    logger.setLevel(logging.INFO)

def setDebug():
    logger.setLevel(logging.DEBUG)


# code to convert various search string inputs:
def ConvertSearchTerm(term, type='detect'):
    is_hex = False
    is_unicode = False

    if type == 'detect':
        is_hex = all(c in "0123456789abcdefABCDEF" for c in term)
        is_hex = is_hex and len(term) % 2 == 0
    elif type == 'hexadecimal':
        is_hex = all(c in "0123456789abcdefABCDEF" for c in term)
        if not is_hex:
            logger.error("term '%s' contains invalid characters." % (term))
            sys.exit(1)
        is_hex = is_hex and len(term) % 2 == 0
        if not is_hex:
            logger.error("term '%s' has odd number of characters." % (term))
            sys.exit(1)
        is_unicode = False
    elif type == 'unicode':
        ustr = unicode(term)
        #print "Unicode term: '%s'" % ustr
        term = ustr.encode('utf-16')[2:]
        #print "Encoded term: '%s'" % term
        is_hex = False
        is_unicode = True
    elif type == 'ascii':
        is_hex = False
        is_unicode = False

    if is_hex:
        # print binascii.a2b_hex(term)
        return term
    else:
        return binascii.b2a_hex(term)


if __name__ == '__main__':
    ###########################################################################
    #
    # set up some logging functions
    #
    ###########################################################################
    
    logging.basicConfig(level=logging.WARNING,
                        #filename="/tmp/funcdump_log.txt", # uncomment, overrides stream setting
                        format='%(asctime)s %(name)s [%(levelname)s] %(message)s',
                        datefmt='%Y%m%d %H:%M:%S',
                        stream=sys.stderr)
    global logger
    logger = logging.getLogger('BGSEARCH')
    
    
    parser = optparse.OptionParser(usage="usage: %prog [options] term term term...")
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
    parser.add_option('-l', '--limit', action='store', type='int', default=15000, 
                      help='do not verify above this number of candidates (default %default)')
    
    parser.add_option('-n', '--numprocs', action='store', type='int', default=12, 
                      help='number of simultaneous .bgi files to search (default %default)')

    
    parser.add_option('-V', '--verbose', action='store_true', default=False, help='verbose output')
    parser.add_option('-D', '--debug', action='store_true', default=False, help='diagnostic output')
    
    global options
    
    (options, args) = parser.parse_args()
    
    #if options.quiet:
    #    # set proper logging level (error messages only [overrides warnings on by default])
    #    setQuiet()
    if options.verbose:
        # set proper logging level (overrides quiet, info & above)
        setVerbose()
    if options.debug:
        # set proper logging level (debug implies & overrides verbose)
        setDebug()

    if len(options.directory) < 1:
        logger.error("no directories specified")
        sys.exit(1)

    # I suppose I could add this as an actual option if needed, but detecting
    # should be sufficient methinks:
    options.bgbindir = os.path.dirname(sys.argv[0])
    if options.bgbindir:
        if options.bgbindir != "" and options.bgbindir[-1] != os.path.sep:
            options.bgbindir += os.path.sep
    else:
        options.bgbindir = ""

    searchterms = []
    for arg in options.binary:
        searchterms.append(ConvertSearchTerm(arg, 'hexadecimal'))
    for arg in options.ascii:
        searchterms.append(ConvertSearchTerm(arg, 'ascii'))
    for arg in options.unicode:
        searchterms.append(ConvertSearchTerm(arg, 'unicode'))
    for arg in args:
        searchterms.append(ConvertSearchTerm(arg))
    
    logger.info("searching for %d search terms: %s" % (len(searchterms),searchterms))
    
    # find all .bgi files in the directory/ies pointed to by config.
    
    index_files = []
    for d in options.directory:
        for f in glob.glob('%s/*.bgi'%d):
            index_files.append(f)
    num_index_files = len(index_files)

    if num_index_files < 1:
        logger.error("no index files found")
        sys.exit(1)
    
    # find bgparse executable alongside bgsearch.py
    
    def bgsearch(index_file):
        global options
        logger.debug("bgsearch: %s"%index_file)
        results = []
        try:
            #cmd = ["./bgparse"]
            cmd = [ options.bgbindir+"bgparse"]
            for s in searchterms:
                cmd += ['-s',s]
            if options.debug and options.verbose:
                cmd += ['-d']
            cmd += [index_file]
            logger.debug("executing: %s"%cmd)
            #results = subprocess.check_output(cmd).split()
            # subprocess.check_output only available in Python 2.7+, so Popen+communicate it is:
            proc = subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            output, errors = proc.communicate()
            if proc.returncode != 0:
                logger.warning("bgparse exitted with an error or a signal")
                # dump the stderr which will have logging info
                for l in errors.split('\n'):
                    logger.warning("bgparse stderr: %s"%l)
            else:
                logger.debug("bgparse exitted okay?")
            results = output.split('\n')
            if options.debug and options.verbose:
                # dump the stderr which will have logging info
                for l in errors.split('\n'):
                    logger.debug("bgparse debug: %s"%l)
            # last one can be blank
            if results[-1] == "":
                del results[-1]
            
        except Exception,e:
            logger.error("exeception caught while doing bgparse on %s: %s"%(index_file,e))
        return index_file,results

    #def bgverify_single_file(check_this_file):
    #    global options
    #    # run bgverify on this file:
    #    #cmd = ['./bgverify']
    #    cmd = [options.bgbindir+'bgverify']
    #    for s in searchterms:
    #        cmd += [s]
    #    logger.debug("executing: %s"%cmd)
    #    proc = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    #    # send file(s) to stdin
    #    #proc.stdin.write("%s\n"%check_this_file)
    #    #proc.stdin.close()
    #    #proc.wait()
    #    # actually, communicate can do this and help hide stdout/stderr:
    #    sout,serr = proc.communicate("%s\n"%check_this_file)
    #    proc.wait()
    #    rc = (proc.returncode == 0)
    #    return check_this_file,rc
    
    def bgverify(check_these_files):
        try:
            #signal.signal(signal.SIGTERM, signal_handler)
            # okay, the above signal handler should result in an Exception
            # being thrown if it happened, but apparently this try/except
            # block was failing to catch it properly and we still see a stack
            # trace during cleanup of the child processes (only sometimes,
            # unfortunately it's not consistent), so using a handler that just
            # exits instead seems to work better...
            signal.signal(signal.SIGTERM, signal_term_handler)
            global options
            # run bgverify on these files:
            bsize = len(check_these_files)
            verified = []
            #cmd = ['./bgverify']
            cmd = [options.bgbindir+'bgverify']
            for s in searchterms:
                cmd += [s]
            logger.debug("executing: '%s', batch size %d"%(cmd,bsize))
            logger.debug("on files: %s"%check_these_files)
            proc = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            sout,serr = proc.communicate('\n'.join(check_these_files))
            proc.wait()
            # sout will have "filename: matches" for all the hits:
            for l in sout.split('\n'):
                # last one can be blank:
                if l != "":
                    pos = l.rfind(':')
                    verified += [l[:pos]]
            # serr may have error messages (like for missing files) or
            # possibly diagnostics (if I make this code pass the debug flag
            # along at some point), and unlike bgparse bgverify might not
            # return a non zero rc to indicate problems as it tries to march
            # on with multiple verifications, so I should probably add code
            # here to parse it or at least dump it if there is anything in it:
            for l in serr.split('\n'):
                if " (E) " in l:
                    logger.error("ERROR from bgverify: %s"%(l))
                elif " (W) " in l:
                    logger.warning("WARNING from bgverify: %s"%(l))
                else:
                    if options.debug and options.verbose:
                        logger.debug("found in stderr of bgverify: %s"%(l))
            return bsize,verified
        except Exception, e:
            # right now, don't care, but probably should 
            return 0,0

    def init_worker():
        # need the children not to catch SIGINT so parent sees the ^C instead
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        # need the children to exit gracefully on SIGTERM
        signal.signal(signal.SIGTERM, signal_term_handler)

    # code to terminate child processes cleanly & bail if certain signals
    # received by throwing & catching an unknown/unexpected exception:
    def signal_handler(sig_num, frm):
        raise Exception("Got a signal (%d)!"%sig_num)

    def signal_term_handler(sig_num, frm):
        logger.debug("child got SIGTERM, bailing")
        sys.exit()

    def print_result(r):
        if options.metadata:
            print(r)
        else:
            # need to strip off metadata from the csv output, if any
            print(r.split(',')[0])
    
    # do options.numprocs parallel searches via multiprocess and subprocess.Popen/os.system,
    pool = multiprocessing.Pool(options.numprocs,init_worker)
    piter = pool.imap(bgsearch,index_files)
    completed = 0
    candidate_results = []
    metadata = {}

    # make sure we cleanup from a 'kill' nicely:
    signal.signal(signal.SIGTERM, signal_handler)

    while completed < num_index_files:
        try:
            (idx_file,batch_results) = piter.next(None)
            logger.debug("index file %s returned %u results"%(idx_file,len(batch_results)))
            candidate_results += batch_results
            if not options.verify:
                for r in batch_results:
                    print_result(r)
            completed += 1
            sys.stderr.write("%10d %6.2f%%\r" % (len(candidate_results), 100*float(completed) / num_index_files))
            sys.stderr.flush()
        except KeyboardInterrupt:
            logger.error("keyboard interrupt, exitting...")
            pool.terminate()
            sys.exit(1)
        except StopIteration:
            break
        except Exception, e:
            logger.error("got an unexpected exception (%s), cleaning up and exitting..."%e)
            pool.terminate()
            sys.exit(1)

    # now clean up the pool
    try:
        pool.close()
    except Exception, e:
        pass

    #print results
    sys.stderr.write('\n')
    
    if options.verify:
        # find bgverify alongside this script
        num_candidates = len(candidate_results)
        if num_candidates > options.limit:
            logger.error("%d candidates, above limit of %d, not verifying"%(num_candidates,options.limit))
            # should I print anyway?  Probably not.
            #for fn in candidate_results:
            #    print_result(fn)
            # should I return/exit with a non-zero return code?  Probably.
            sys.exit(1)
        else:
            logger.info("%d candidates, chunking for verification..."%num_candidates)
            # first, need to save off the metadata, if any:
            metadata = {}
            for x in range(0,num_candidates):
                c_file = candidate_results[x].split(',')[0]
                # save metadata if need be:
                if options.metadata:
                    c_metadata = candidate_results[x][len(c_file):] # rest of line
                    logger.debug("file %s metadata: %s"%(c_file,c_metadata))
                    metadata[c_file] = c_metadata # note, will have leading comma if not empty
                candidate_results[x] = c_file
            # let's use the pool again...
            chunksize=100 # each bgverify will get up to 100 files on stdin
            #piter = pool.imap(bgverify,candidate_results,chunksize=chunksize)
            # sigh...apparently the chunksize parameter doesn't work the way I
            # thought it would/should, so need to split up the list into lists of
            # lists to create the chunks ourselves...
            chunked_candidates = []
            for x in range(0,num_candidates,chunksize):
                chunked_candidates.append(candidate_results[x:x+chunksize])
            sys.stderr.write("%d candidates, verifying...\n"%num_candidates)
            # and make a new pool...
            pool = multiprocessing.Pool(1+options.numprocs/4,init_worker)
            piter = pool.imap(bgverify,chunked_candidates)
            completed = 0
            verified_results = []
            while completed < num_candidates:
                try:
                    bsize,verified = piter.next(None)
                    verified_results += verified
                    for fn in verified:
                        # put metadata back if need be
                        if options.metadata and fn in metadata and metadata[fn] != "":
                            fn = fn + metadata[fn]
                        print_result(fn) 
                    completed += bsize
                    sys.stderr.write("%10d/%10d %6.2f%%\r" % (len(verified_results),num_candidates, 100*float(completed) / num_candidates))
                    sys.stderr.flush()
                except KeyboardInterrupt:
                    logger.error("keyboard interrupt, exitting...")
                    pool.terminate()
                    sys.exit(1)
                except StopIteration:
                    break
                except Exception, e:
                    logger.error("got an unexpected exception (%s), cleaning up and exitting..."%e)
                    pool.terminate()
                    sys.exit(1)
            # now clean up the pool
            try:
                pool.close()
            except Exception, e:
                pass
        sys.stderr.write('\n')

    
    sys.exit(0)
