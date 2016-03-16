#!/usr/bin/env python
# -*- python -*-

# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2011-2016 by Carnegie Mellon University
# @author:    Charles Hines, Matt Coates <mc-contact@cert.org>

# A little script to facillitate fixup the fileid map section of already
# generated .bgi files.  Can extract & replace the fileid map section.  Does
# minimal checking (only for same number of lines)

import sys
import re
import string
import logging
import os
import struct

from optparse import OptionParser

__version__ = '0.1'


###########################################################################
#
# set up some logging functions
#
###########################################################################

logging.basicConfig(level=logging.INFO, # let's default to verbose instead of normal logging.WARNING level
                    #filename="/tmp/foo_log.txt", # uncomment, overrides stream setting
                    format='%(asctime)s [%(levelname)s] %(message)s',
                    #format='%(asctime)s %(name)s [%(levelname)s] %(message)s',
                    datefmt='%Y%m%d %H:%M:%S',
                    stream=sys.stderr)
logger = logging.getLogger('FILEIDEX')

def setQuiet():
    logger.setLevel(logging.ERROR)

def setVerbose():
    logger.setLevel(logging.INFO)

def setDebug():
    logger.setLevel(logging.DEBUG)

def read_config(args):
    # setup the cmd line parameters (matches possible contents of config file):
    usage="Usage: %prog [options] <file(s)>"
    version="%prog version " + __version__
    epilog="%prog is used to extract (default) or replace the fileid map portion of .bgi files, does minimal checking on replace (same number of lines) so don't mess it up!  Uses/assumes 'bgifilename.bgi.fileidmap.txt' as the filename for extracted data or target to replace from, and the syntax of the text is '%010u file_path_and_name[,meta=data,meta=data]' with the numbers starting at 0 and staying in the same increasing order!"
    parser = OptionParser(usage=usage,version=version,epilog=epilog)
    parser.add_option("-v", "--verbose", help="verbose", action="store_true", dest="verbose", default=False)
    parser.add_option("-d", "--debug", help="debug", action="store_true", dest="debug", default=False)
    parser.add_option("-x", "--extract", help="extracting fileid map (default)", action="store_true", dest="extract", default=True)
    parser.add_option("-r", "--replace", help="replacing fileid map", action="store_false", dest="extract")

    global options
    global files
    (options, files) = parser.parse_args(args)

    if files is None or len(files) == 0:
        logger.error("give me one or more .bgi files to work with, please")
        sys.exit(1)

    #if options.quiet:
    #    # set proper logging level (error messages only [overrides warnings on by default])
    #    setQuiet()
    if options.verbose:
        # set proper logging level (overrides quiet, info & above)
        setVerbose()
    if options.debug:
        # set proper logging level (debug implies & overrides verbose)
        setDebug()

    # check to make sure python 2.6 or better:
    if sys.hexversion < 0x02060000:
        logger.error("requires python 2.6 or better please...")
        sys.exit(3)

def process_file(f):
    global options

    d = os.path.dirname(f)
    if d is not None and d != "":
        d += os.path.sep
    fbase = os.path.basename(f)
    fileidmapfile = d + fbase + ".fileidmap.txt"

    if options.extract:
        opt = 'w'
    else:
        if not os.path.exists(fileidmapfile):
            logger.error(" - fileid map file %s appears to be missing, skipping"%fileidmapfile)
            return
        opt = 'r'
    with open(fileidmapfile,opt) as fidf:
        if options.extract:
            opt = 'rb'
        else:
            opt = 'rb+'
        with open(f,opt) as fd:
            # Index version | fileid map offset (size) | # of files (size)
            # v2.0          | 20 bytes (64 bit)        | 16 bytes (8 bit)
            # v2.1          | 21 bytes (64 bit)        | 17 bytes (8 bit)
            fd.seek(8)
            fmt_major = struct.unpack('<B',fd.read(1))[0]
            fmt_minor = struct.unpack('<B',fd.read(1))[0]
            logger.debug(" - Index format v%u.%u"%(fmt_major,fmt_minor))
            hint_type = None
            if (fmt_major == 2 and fmt_minor == 0):
                fd.seek(16)
            elif (fmt_major == 2 and fmt_minor == 1):
                fd.seek(1,1)
                hint_type = struct.unpack('<B',fd.read(1))[0]
                logger.debug(" - hint type %u"%hint_type)
                fd.seek(17)
            else:
                logger.error(" - Index format is not supported by this tool, exiting...")
                exit(1)
            fmnum = struct.unpack('<L',fd.read(4))[0]
            fmoff = struct.unpack('<Q',fd.read(8))[0]
            logger.debug(" - appears to have %u fileids starting at offset %u (0x%x)"%(fmnum,fmoff,fmoff))
            if fmnum == 0 or fmoff == 0:
                logger.error(" - zero values in header indicating that the index is corrupt/incomplete, bailing")
                return
            fd.seek(fmoff)
            if options.extract:
                nread = 0
                for l in fd:
                    logger.debug(" - found: %s"%(l.rstrip()))
                    fidf.write(l)
                    nread += 1
                if nread != fmnum:
                    logger.error(" - line number mismatch (%u vs expected %u), bailing"%(nread,fmnum))
                    return
                else:
                    logger.debug(" - appear to have found them all...")
            else:
                # first, check number of lines in fidf:
                nread = 0
                for l in fidf:
                    nread += 1
                if nread != fmnum:
                    logger.error(" - line number mismatch (%u vs expected %u), bailing"%(nread,fmnum))
                    return
                else:
                    logger.debug(" - appear to have found them all...")
                # fd already at proper position to overwrite, but need to reset fidf
                fidf.seek(0)
                for l in fidf:
                    fd.write(l)
                # now, one of the runs I accidentally appended to the end of
                # the previous fileid map, so now I need to truncate to make
                # sure I remove the 2nd copy I had on there...
                #fd.truncate(fd.tell())
                fd.truncate()

def main(args):
    global options
    global files
    read_config(args)

    for f in files:
        logger.info("processing file '%s', %sing fileid map"%(f,"extract" if options.extract else "replac"))
        process_file(f)
        logger.info("done %sing file '%s' fileid map"%("extract" if options.extract else "replac", f))

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
