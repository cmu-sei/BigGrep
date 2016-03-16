# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2013 by Carnegie Mellon University
# @author:    Matt Coates <mc-contact@cert.org>

import subprocess
import signal
import sys
import re
import time
import binascii
import tempfile

def search(search_terms,index_files,results_callback,status_callback,verify=False,filter_criteria=None,numprocs=12,limit=15000,verbose=False,debug=False,logger=False,yara_file=None,throttleat=10000):
    import jobdispatch
    from bgsearch_jobmanager import BgSearchJobManager
    from bgsearch_processor import BgSearchProcessor
    search_starttime = time.time()
    ret=0
    vp=None
    if yara_file:
        verify=True
        from bgverify_processor import VerifyYaraProcessor
        vp = VerifyYaraProcessor(("bgsearch","verify"))
        vp.yara_file = yara_file
    else:
        from bgverify_processor import BgVerifyProcessor
        vp = BgVerifyProcessor(("bgsearch","verify"))
    if not logger:
        import logging
        logger = logging.getLogger(__name__)
    num_index_files = len(index_files)
    #vjm = bgsearch_jobmanager.BgVerifyJobManager()
    # Don't enforce candidate limit if no verification is to be done.
    if not verify:
        limit = 0
    sjm = BgSearchJobManager(limit,filter_criteria,verify)
    sjd = jobdispatch.JobDispatcher(sjm)
    #vjd = jobdispatch.JobDispatcher(vjm)
    sp = BgSearchProcessor(("bgsearch","search"))
    sp.verbose=verbose
    sp.debug=debug
    vp.verbose=verbose
    vp.debug=debug
    s_engines = []
    v_engines = []
    parsing_halted = False
    for i in range(numprocs):
        s_engines.append(sjd.addProcessor(sp))
        if verify:
            vpi=sjd.addProcessor(vp)
            vpi.pause()
            v_engines.append(vpi)
    if numprocs > 1:
        s_engines[0].pause()
        if verify:
            v_engines[0].resume()
        

    try:
        totaljobs = 0
        for idx in index_files:
            logger.debug("Adding job for index file %s"%idx)
            sjm.addJob(search_terms,idx,verify)
            totaljobs += 1

        sjd.start()
        enabled_verifiers = 1
        
        while(sjm.working or (len(sjm.completed_jobs) > 0) or (sjm.searched_count != totaljobs) or (verify and sjm.candidate_count-sjm.verify_checked_count != 0)):
            # fetch a completed job, and process it
            job = sjm.getCompletedJob()
            if job == None:
                logger.debug("No completed jobs")
                time.sleep(1)
            else:
                logger.debug("Processing results...")
                for r,m in job.result_tuples:
                    logger.debug("Result %s"%r)
                    results_callback(r,m)
                    #quiet_status.force = True
            quiet_status(status_callback,num_index_files,len(sjm.sjobs),sjm.candidate_count,sjm.verify_checked_count,sjm.verified_count)
            if sjm.candidate_limit_reached:
                logger.error("Candidate limit reached (%d %.2f%%), exiting..."%(sjm.candidate_count,100*float(num_index_files-len(sjm.sjobs))/num_index_files))
                sjd.live=False
                ret = 2
                break
            if sjd.live and not sjd.isAlive():
                logger.error("The job dispatcher failed, exiting...")
                sjd.signal_children_exit()
                ret = 1
                break
            #throttle parsers/verifiers
            if verify:
                if parsing_halted:
                    if sjm.candidate_count-sjm.verify_checked_count < (throttleat-500):
                        parsing_halted=False
                        for p in v_engines:
                            p.pause()
                        for p in s_engines:
                            p.resume()
                        v_engines[0].resume()


                elif throttleat > 0 and sjm.candidate_count-sjm.verify_checked_count > throttleat:
                    parsing_halted = True
                    for p in s_engines:
                        p.pause()
                    for p in v_engines:
                        p.resume()
                else:
                    # add more verifiers if we have room in the thread pool
                    target_verifiers = numprocs - len(sjm.sjobs)
                    if target_verifiers > 0 and target_verifiers > enabled_verifiers:
                        logger.debug("Targeting %d verifiers ev:%d sjs:%d\n"%(target_verifiers,enabled_verifiers,len(sjm.sjobs)))
                        for i in range(target_verifiers):
                            logger.debug("Enabling verifier %d\n"%(i))
                            v_engines[i].resume()
                        enabled_verifiers = target_verifiers


    except KeyboardInterrupt:
        logger.error("keyboard interrupt, exiting...")
        sjd.live=False
        raise

    wall_clock = time.time() - search_starttime
    logger.debug("bgsearch actual time %0.2f seconds looking for %s; threads spent %0.2f seconds searching, %0.2f seconds verifying"%(wall_clock, search_terms, sjm.search_duration, sjm.verify_duration))
    logger.debug("Asking jobdispatcher to stop.")
    sjd.live=False
    sys.stderr.write("\n")
    return ret
 
#def verify_only(search_terms,results_callback,status_callback,numprocs=12,verbose=False,debug=False,logger=False,yara_file=None):
#    import jobdispatch
#    from bgsearch_jobmanager import BgSearchJobManager
#    ret=0
#    vp=None
#    if yara_file:
#        verify=True
#        from bgverify_processor import VerifyYaraProcessor
#        vp = VerifyYaraProcessor(("bgsearch","verify"))
#        vp.yara_file = yara_file
#    else:
#        from bgverify_processor import BgVerifyProcessor
#        vp = BgVerifyProcessor(("bgsearch","verify"))
#    if not logger:
#        import logging
#        logger = logging.getLogger(__name__)
#
#    sjm = BgSearchJobManager(0)
#    sjd = jobdispatch.JobDispatcher(sjm)
#    vp.verbose=verbose
#    vp.debug=debug
#    for i in range(numprocs):
#        sjd.addProcessorEngine(jobdispatch.ProcessorEngine(vp))
#
#    try:
#        totaljobs = 0
#        for idx in index_files:
#            logger.debug("Adding job for index file %s"%idx)
#            sjm.addJob(search_terms,idx,verify)
#            totaljobs += 1
#
#        sjd.start()
#        verifiers = 0
#        
#        while(sjm.working or (len(sjm.completed_jobs) > 0) or ( sjm.candidate_count-sjm.verify_checked_count != 0)):
#
#            line = sys.stdin.readline()
#            sjm.addJob(search_terms,line.strip())
#            
#            # fetch a completed job, and process it
#            job = sjm.getCompletedJob()
#            if job == None:
#                logger.debug("No completed jobs")
##                continue
#            else:
#                logger.debug("Processing results...")
#                for r in job.results:
#                    logger.debug("Result %s"%r)
#                    results_callback(r,sjm.candidate_metadata[r])
#                    quiet_status.force = True
#            quiet_status(status_callback,num_index_files,len(sjm.sjobs),sjm.candidate_count,sjm.verify_checked_count,sjm.verified_count)
#            if sjm.candidate_limit_reached:
#                logger.error("Candidate limit reached, exiting...")
#                sjd.live=False
#                ret = 2
#                break
#            if sjd.live and not sjd.isAlive():
#                logger.error("The job dispatcher failed, exiting...")
#                sjd.signal_children_exit()
#                ret = 1
#                break
#    except KeyboardInterrupt:
#        logger.error("keyboard interrupt, exiting...")
#        sjd.live=False
#        raise
#
#    logger.debug("Asking jobdispatcher to stop.")
#    sjd.live=False
#    sys.stderr.write("\n")
#    return ret
 

def parse(searchterms,index_file,logger,debug=False,verbose=False):
    #logger = search.get_logger()
    #if verbose:
    #    logger.setLevel(logging.INFO)
    #if debug:
    #    logger.setLevel(logging.DEBUG)
    starttime=time.time()
    logger.debug("bgsearch: %s"%index_file)
    results = []
    cmd = [ "bgparse"]
    for s in searchterms:
        cmd += ['-s',s]
    if debug and verbose:
        cmd += ['-d']
    cmd += [index_file]
    logger.debug("executing: %s"%cmd)
    #results = subprocess.check_output(cmd).split()
    # subprocess.check_output only available in Python 2.7+, so Popen+communicate it is:
    stdout = tempfile.SpooledTemporaryFile(max_size=1073741824)
    stderr = tempfile.SpooledTemporaryFile()
    proc = subprocess.Popen(cmd,stdout=stdout,stderr=stderr,preexec_fn=init_worker)
    proc.wait()
    stdout.seek(0)
    stderr.seek(0)
    #proc = subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,preexec_fn=init_worker)
    #output, errors = proc.communicate()
    if proc.returncode != 0:
        logger.warning("bgparse exited with an error or a signal")
        # dump the stderr which will have logging info
        #for l in errors.split('\n'):
        for l in stderr.readlines():
            l = l.rstrip()
            logger.warning("bgparse stderr: %s"%l)
    else:
        logger.debug("bgparse exited okay?")
    for r in stdout.readlines():
        r = r.rstrip()
        if r != "":
            c_file = r.split(',')[0]
            c_metadata = r[len(c_file):] # rest of line
            results.append((c_file,c_metadata))
    if debug and verbose:
        # dump the stderr which will have logging info
        for l in stderr.readlines():
            l = l.rstrip()
            logger.debug("bgparse debug: %s"%l)
    stdout.close()
    stderr.close()
    bsize = len(results)
    duration = time.time() - starttime
    logger.debug("parse took %0.2f seconds to identify %d candidates"%(duration,bsize))
    return index_file,results,duration

def verify(searchterms,check_these_files,logger,debug=False,verbose=False):
    #if verbose:
    starttime = time.time()
    bsize = len(check_these_files)
    verified = []
    #no actual work...
    if bsize == 0:
        return (bsize,verified,0)
    cmd = ['bgverify']
    for s in searchterms:
        cmd += [s]
    logger.debug("executing: '%s', batch size %d"%(cmd,bsize))
    logger.debug("on files: %s"%check_these_files)
    #proc = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,preexec_fn=init_worker)
    proc = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,close_fds=True)
    sout,serr = proc.communicate('\n'.join(check_these_files))
                 
    # sout will have "filename: matches" for all the hits:
    for l in sout.split('\n'):
        # last one can be blank:
        if l != "":
            logger.debug("bgverify out: %s"%l)
            pos = l.rfind(':')
            verified += [(l[:pos],None)]
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
            if debug and verbose:
                logger.debug("found in stderr of bgverify: %s"%(l))
    logger.debug("bgverify returning %s,%s"%(bsize,verified))
    duration = time.time() - starttime
    logger.debug("verify took %0.2f seconds to validate %d files (%d successful)"%(duration,bsize,len(verified)))
    return bsize,verified,duration

def verify_yara(yara_file,check_these_files,logger,debug=False,verbose=False):
    starttime = time.time()
    import yara
    bsize = len(check_these_files)
    verified = []
    try:
        rules = yara.compile(yara_file)
        for f in check_these_files:
            try:
                matches = None
                matches = rules.match(f)
                if (matches):
                    verified.append((f,matches))
            except:
                logger.error("Yara was unable to verify %s, removing from results."%f)
    except:
        logger.error("Yara was unable to use the supplied Yara input.")
        raise
    logger.debug("yara returning %s,%s"%(bsize,verified))
    duration = time.time() - starttime
    logger.debug("verify_yara took %0.2f seconds to validate %d files (%d successful)"%(duration,bsize,len(verified)))
    return bsize,verified,duration

def verify_yara_cli(yara_file,check_these_files,logger,debug=False,verbose=False):
    #if verbose:
    starttime = time.time()
    bsize = len(check_these_files)
    verified = []
    #no actual work...
    if bsize == 0:
        return (bsize,verified)
    cmd = ['yara']
    cmd += [yara_file]
    cmd += check_these_files
    logger.debug("executing: '%s', batch size %d"%(cmd,bsize))
    logger.debug("on files: %s"%check_these_files)
    #proc = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,preexec_fn=init_worker)
    proc = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,close_fds=True)
    sout,serr = proc.communicate()
    matches={}
    # sout will have "matches filename" for all the hits:
    for l in sout.split('\n'):
        # last one can be blank:
        if l != "":
            logger.debug("yara out: %s"%l)
            pos = l.find(' ')
            file = l[(pos+1):]
            match = l[:pos]
            try:
                matches[file]+=[match]
            except KeyError:
                matches[file]=[match]
    for k in matches.keys():
        verified.append((k,matches[k]))
    for l in serr.split('\n'):
        if debug and verbose:
            logger.debug("found in stderr of yara: %s"%(l))
    logger.debug("yara returning %s,%s"%(bsize,verified))
    duration = time.time() - starttime
    logger.debug("verify_yara_cli took %0.2f seconds to validate %d files (%d successful)"%(duration,bsize,len(verified)))
    return bsize,verified,duration


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
    sys.stderr.write("child got SIGTERM, bailing")
    #sys.exit()

# code to convert various search string inputs:
def ConvertSearchTerm(logger, term, type='detect'):
    is_hex = False
    is_unicode = False

    if type == 'detect':
        looks_like_hex = all(c in "0123456789abcdefABCDEF" for c in term)
        is_hex = looks_like_hex and len(term) % 2 == 0
        if (looks_like_hex and not is_hex):
            logger.warning("term '%s' appears to be hexadecimal, but has an odd number of characters and will be treated as an ascii string."%term)
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

def pf(text):
    sys.stderr.write(text)
    sys.stderr.flush()

def ps(tsj,sj,cc,vcf,vf):
    #tsj=total search jobs
    #sj=unfinished search jobs
    #cc=candidate count
    #vf=verified files
    percent_verified=0
    if cc > 0:
        percent_verified = 100*float(vcf)/cc
    status = "Search:%d %.2f%% Verify:%d/%d %.2f%%" % (
                              cc,
                              100*float(tsj-sj) / tsj,
                              vf,
                              cc,
                              percent_verified)
    #status += " tsj=%d, sj=%d, cc=%d, vcf=%d, vf=%d"%(tsj,sj,cc,vcf,vf)
    sd = ps.osl - len(status)
    if sd > 0:
        for i in range(sd):
            status+=" "
    pf(status+'\r')
    ps.osl = len(status)
    return status
ps.osl = 0

def quiet_status(status_callback,num_index_files,search_count,candidate_count,verify_checked_count,verified_count):
    if (search_count == quiet_status.search_count):
        if (verify_checked_count == quiet_status.verify_checked_count):
            if (candidate_count == quiet_status.candidate_count):
                if (verified_count == quiet_status.verified_count):
                    if (num_index_files == quiet_status.num_index_files):
                        if (not quiet_status.force):
                            return
    quiet_status.force = False
    return status_callback(num_index_files,search_count,candidate_count,verify_checked_count,verified_count)
quiet_status.force = False
quiet_status.num_index_files = 0
quiet_status.search_count = 0
quiet_status.candidate_count = 0
quiet_status.verify_checked_count = 0
quiet_status.verified_count = 0

def split_typeify(s):
    (k,v) = s.split('=')
    return (k,typeify(v))

def typeify(v):
    try:
        return int(v)
    except ValueError:
        return v

def filter_metadata(criteria,metadata):
    missing_keys=[]
    match = False
    for c in criteria:
        cmatch = False
        m = re.match('(\w+)\s*([><=!~]{1,2})\s*([\w\/\*\-\+]+)',c)
        if m:
            key = m.group(1)
            if not metadata.has_key(key):
                missing_keys.append(key)
                cmatch=True
            else:
                operation = m.group(2)
                value = typeify(m.group(3))
                if operation == "=" or operation == "==":
                    try:
                        if (value.endswith('*')):
                            if (metadata[key].startswith(value[:-1])):
                                cmatch = True
                    except AttributeError:
                        #not a string, oops
                        pass
                    if (metadata[key] == value):
                        cmatch = True
                elif operation == ">":
                    if (metadata[key] > value):
                        cmatch = True
                elif operation == ">=":
                    if (metadata[key] >= value):
                        cmatch = True
                elif operation == "<":
                    if (metadata[key] < value):
                        cmatch = True
                elif operation == "<=":
                    if (metadata[key] <= value):
                        cmatch = True
                elif operation == "!=":
                    if (metadata[key] != value):
                        cmatch = True
                elif operation == "~":
                    if (re.match(value,metadata[key])):
                        cmatch = True

        else:
            return (None,missing_keys)
        if cmatch:
            match = True
        else:
            return (False,missing_keys)
    return (match,missing_keys)


