// Copyright 2011-2017 Carnegie Mellon University.  See LICENSE file for terms.
// BigGrep, bgparse: code to parse the BGI file format to do searches
// (bgsearch.py will call this), extract stats, etc.

#define __STDC_LIMIT_MACROS // I have to define this to get UINT32_MAX because of the ISO C99 standard, apparently
// #define __STDC_CONSTANT_MACROS // don't really need these right now
#include <stdint.h>
#include <limits.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <string>

#include <cstring>
#include <cstdlib>

#include <iostream>
#include <sstream>

#include <string.h>
#include <errno.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include <getopt.h> // for getopt_long

#include "boost/algorithm/string.hpp"
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/copy.hpp>

#include "Logger.hpp" // my simple logging module (to stderr)

#include "VarByte.hpp"
#include "PFOR.hpp"

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif


#if HAVE_ENDIAN_H
#include <endian.h>
#else
#if HAVE_MACHINE_ENDIAN_H
#include <machine/endian.h>
#endif
#endif

using namespace std;
#include "bgi_header.hpp"


vector< uint32_t >
ngrams_from_binascii(
    uint8_t n,
    string &binascii)
{
    vector< uint32_t > ngrams;
    // n == 3 or 4, either way we're converting to 4 byte integers...
    if (binascii.size()%2 != 0) {
        LERR << "convert from binascii, not even number of hex chars: "
             << binascii << endl;
    } else if ((binascii.size()/2) < n) {
        LERR << "number of bytes to search (" << binascii.size()/2 << ") is less than n (" << (int)n << ")" << endl;
    } else {
        string::iterator iter(binascii.begin());
        uint32_t count((binascii.size()/2));
        vector< uint8_t > bin(count);
        uint32_t i(0);
        while (i < count) {
            string hbyte(binascii.substr(2*i,2));
            LDEBUG << "  converting binascii byte: " << hbyte << endl;
            errno = 0;
            bin[i] = (uint8_t) strtol(hbyte.c_str(),NULL,16);
            if (errno != 0) {
                LERR << "strtol error: " << errno << " " <<
                    strerror(errno) << endl;
            }
            LDEBUG << "  converted to: " << std::hex <<
                (uint32_t)bin[i] << std::dec << endl;
            ++i;
        }
        // okay, have binary data now, let's convert to ngrams:
        uint32_t ngram_count(count-n+1);
        LDEBUG << "string '" << binascii << "' has " << ngram_count <<
            " ngrams max" << endl;
        ngrams.resize(ngram_count,0);

        for (i = 0; i < ngram_count; ++i) {
            if (n == 4) {
                ngrams[i] = *(reinterpret_cast<uint32_t*>(&bin.front() + i));
            } else {
                // must be 3, read 4 bytes at a time for speed, but adjust.
                if (i < (ngram_count-1)) {
                    ngrams[i] = *(reinterpret_cast<uint32_t*>(&bin.front() + i));
                    // because of little endian architecture, so as to not lose
                    // first byte in file, we need to remove the high order byte in
                    // the resulting integer:
                    ngrams[i] &= 0x00FFFFFF;
                } else {
                    // at last ngram, uuh...this code (and the encoding code) is going
                    // to have a problem if the total length is a single 3-gram
                    ngrams[i] = *(reinterpret_cast<uint32_t*>(&bin.front() + i - 1)); // and don't walk off the end of the vector!
                    // for the last one, we need to be sure and keep the high order
                    // byte instead (as it's the last byte in the file) for the same
                    // little endian reason:
                    ngrams[i] >>= 8;
                }
            }
            LDEBUG << "extracted ngram " << std::hex << ngrams[i] <<
                std::dec << endl;
        }
    }
    return ngrams;
}

vector< uint32_t >
ngrams_from_v_binascii(
    uint8_t n,
    vector< string >&vbinascii)
{
    // n == 3 or 4, either way we're converting to 4 byte integers...
    vector< uint32_t > ngrams;
    for (uint32_t i(0);i<vbinascii.size();++i) {
        vector< uint32_t > newngrams(ngrams_from_binascii(n,vbinascii[i]));
        ngrams.insert(ngrams.end(),newngrams.begin(),newngrams.end());
    }

    return ngrams;
}

void
ngrams_sort_and_uniq(
    vector< uint32_t > &ngrams)
{
    sort(ngrams.begin(),ngrams.end());
    vector< uint32_t >::iterator it;
    it = unique(ngrams.begin(),ngrams.end());
    ngrams.resize(it-ngrams.begin());
}


void
help()
{
    std::cerr << "Usage:" << std::endl;
    std::cerr << "bgdump [OPTS] IDXFILE.BGI" << std::endl << std::endl;
    std::cerr << "  bgdump takes an index file and can perform various operations on it, depending on options picked." << std::endl << std::endl;
    std::cerr << "  OPTS can be:" << std::endl;
    std::cerr << "    -f,--file-list\tPrints the file id index for this index file." << std::endl;
    std::cerr << "    -h,--help\tshow this help message" << std::endl;
    std::cerr << "    -d,--debug\t debug logging" << std::endl;
    std::cerr << "    -v,--verbose\t verbose logging" << std::endl;
    std::cerr << std::endl;
}

struct option long_options[] =
    {
        {"file-list",no_argument,0,'f'},
        {"help",no_argument,0,'h'},
        {"verbose",no_argument,0,'v'},
        {"debug",no_argument,0,'d'},
       {0,0,0,0}
    };

int
main(
    int argc,
    char** argv) {

    int rc(0);

    bool file_list(false);

    int ch;
    int option_index;

    LSETLOGPREFIX("BGDUMP");

    while ((ch = getopt_long(argc,argv,"fhvd",long_options,&option_index)) != -1)
    {
        switch (ch)
        {
          case 'f':
            file_list = true;
            break;
          case 'h':
            help();
            return 1; // bail
          case 'v':
            LSETLOGLEVEL(Logger::INFO);
            break;
          case 'd':
            LSETLOGLEVEL(Logger::DEBUG);
            break;
          default: // should never get here?
            LWARN << "option '" << (char)ch << "' received??" << std::endl;
            break;
        }
    }

    if ((argc - optind) != 1) // takes a single index file
    {
        LERR << "takes a single BGI file" << std::endl;
        help();
        return 1;
    }

    string fname(argv[optind]);

    // open file (mmap) (would love to use C++ ifstream to open the file
    // & get the size & file descriptor for the mmap call, but sadly
    // there is no standard way to do the latter...sigh...so resorting
    // to the C interfaces for this...
    struct stat sb;
    if (stat(fname.c_str(), &sb) == -1) {
        LERR << "issue w/ stat on " << fname << " - " << strerror(errno)
             << endl;
        exit(11);
    }

    off_t file_size = sb.st_size;

    LDEBUG << "file_size is " << file_size << endl;

    int f(open(fname.c_str(), O_RDONLY));

    unsigned char* fmem = NULL;

    if ((fmem = (unsigned char *)mmap(NULL, file_size, PROT_READ,
                                      MAP_PRIVATE, f, 0)) == MAP_FAILED)
    {
        LERR << "issue w/ mmap on " << fname << " - " <<
            strerror(errno) << endl;
        exit(14);
    }
    // Might be nice to use madvise((void*)fmem,file_size,MADV_SEQUENTIAL); here
    // for a speedup if the code were restructured to indeed be accessed
    // sequentially?  Although even if I specified the memory region of just the
    // ngram data to be sequentially accessed, it might not actually help given
    // the likely to be sparse search strings, we will be likely to skip large
    // sections so read ahead won't really help, probably.

    // I should probably wrap the open & mmap in a class so I can munmap and
    // close when it goes out of scope, since I'm relying on the normal exit
    // code to clean up for me currently...bad form, I know...

    // parse header
    bgi_header hdr;
    hdr.read(fmem);

    // quick sanity check, if fileid_map_offset is 0, that means the index file
    // is either bad or (more likely) still in the process of being generated,
    // so we want to stop moving forward in that case...
    if (hdr.fileid_map_offset == 0) {
        LERR << "fileid_map_offset not set, index still being generated?"
             << endl;
        exit(22);
    }

    LDEBUG << "BGI Header:" << endl;
    LDEBUG << "  magic == " << string(hdr.magic,hdr.magic+sizeof(hdr.magic)) << endl;
    LDEBUG << "  fmt_major == " << (uint32_t)hdr.fmt_major << endl;
    LDEBUG << "  fmt_minor == " << (uint32_t)hdr.fmt_minor << endl;
    LDEBUG << "  N == " << (uint32_t)hdr.N << endl;
    LDEBUG << "    (type == "<< (uint32_t)hdr.hint_type << ") hints size == " <<
        hdr.hints_size() << endl;
    LDEBUG << "  pfor_blocksize == " << (uint32_t)hdr.pfor_blocksize << endl;
    LDEBUG << "  num_ngrams == " << hdr.num_ngrams << endl;
    LDEBUG << "  num_files == " << hdr.num_files << endl;
    LDEBUG << "  fileid_map_offset == 0x" << std::hex << hdr.fileid_map_offset \
           << std::dec << endl;

    // okay, setup for using the hints data & accessing the ngram data:
    uint64_t *hints_begin(reinterpret_cast<uint64_t*>(fmem + hdr.header_size()));
    uint64_t *hints_end(hints_begin + hdr.num_hints());
    unsigned char* index_begin(reinterpret_cast<unsigned char*>(hints_end));
    unsigned char* index_end(fmem + hdr.fileid_map_offset);

    unsigned char* fileid_map_begin(fmem + hdr.fileid_map_offset);
    unsigned char* fileid_map_end(fmem + file_size);

    // Parse the fileid_map out into an actual map, for convenience...could use
    // boost::split or can use stringstream & getline w/ various delimiters.
    // Think I'll go w/ boost solution...seems cleaner despite my typical
    // desire to avoid Boost. :)

    // map< uint32_t, string > files;
    // actually, a vector should suffice
    vector< string > files;

    string mapdata(fileid_map_begin,fileid_map_end);

    if (hdr.fmt_minor == 2) {
        //must decompress
        std::stringstream compressed(mapdata);
        std::ostringstream decompressed;
        boost::iostreams::filtering_streambuf<boost::iostreams::input> in;
        in.push(boost::iostreams::zlib_decompressor());
        in.push(compressed);
        boost::iostreams::copy(in, decompressed);
        mapdata = decompressed.str();
    }

    boost::split(files,mapdata,boost::is_any_of("\n"));

    LDEBUG << "Number of files after split: " << files.size() << std::endl;
    for (int i(0); i < files.size(); ++i) {
        if (file_list) {
            std::cout << files[i] << endl;
        }
        if (files[i].size() > 1) { // last line might just be a newline...
            // each line is like so:
            //   NNNNNNNNNN /path/to/file
            // could use split again, but spaces in file names
            // would be a problem, so a regex is probably in order...
            // or we could just look to the first space and remove that,
            // and just use the vector position as the number
            // (they should match, and since we are just doing lookups by the number anyways...)
            size_t spacepos(files[i].find(' ',0));
            if (spacepos != string::npos) {
                files[i].erase(0,spacepos+1);
                LDEBUG << "file id " << i << " is " << files[i] << endl;
            } else {
                LERR << "problem parsing fileid map, files[" << i << "] == " << files[i] << endl;
            }
        } else {
            // last line probably empty or just a newline, so remove it:
            files.erase(files.begin()+i);
        }
    }
    if (files.size() != hdr.num_files) {
        LERR << "num_files mismatch, expected " << hdr.num_files
             << " but found " << files.size() << endl;
    }

    return rc;
}
