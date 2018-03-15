// Copyright 2011-2018 Carnegie Mellon University.  See LICENSE file for terms.
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
#include "boost/format.hpp"
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/copy.hpp>

#include "BGLogging.hpp"
#include "VarByte.hpp"
#include "PFOR.hpp"

using namespace std;

#include "bgi_header.hpp"

// config options - mainly to get VERSION
#include "bgconfig.h"
using boost::format;


vector< uint32_t > ngrams_from_binascii(
    uint8_t n,
    string &binascii)
{
    vector< uint32_t > ngrams;
    // n == 3 or 4, either way we're converting to 4 byte integers...
    if (binascii.size()%2 != 0) {
        BGERR << "convert from binascii, not even number of hex chars: "
             << binascii;
    } else if ((binascii.size()/2) < n) {
        BGERR << "number of bytes to search (" << binascii.size()/2 <<
            ") is less than n (" << (int)n << ")";
    } else {
        string::iterator iter(binascii.begin());
        uint32_t count((binascii.size()/2));
        vector< uint8_t > bin(count);
        uint32_t i(0);

        while (i < count) {
            string hbyte(binascii.substr(2*i,2));
            BGDEBUG << "  converting binascii byte: " << hbyte;
            errno = 0;
            bin[i] = (uint8_t) strtol(hbyte.c_str(),NULL,16);
            if (errno != 0) {
                BGERR << "strtol error: " << errno << " " << strerror(errno);

            }
            BGDEBUG << "  converted to: " << std::hex << (uint32_t)bin[i]
                    << std::dec;
            ++i;
        }
        // okay, have binary data now, let's convert to ngrams:
        uint32_t ngram_count(count-n+1);
        BGDEBUG << "string '" << binascii << "' has " << ngram_count
                << " ngrams max";
        ngrams.resize(ngram_count,0);
        for (i = 0; i < ngram_count; ++i) {
            if (n == 4) {
                ngrams[i] = *(reinterpret_cast<uint32_t*>(&bin.front() + i));
            } else {
                // must be 3, read 4 bytes at a time for speed, but adjust...
                if (i < (ngram_count-1)) {
                    ngrams[i] = *(reinterpret_cast<uint32_t*>(&bin.front()+i));
                    // because of little endian architecture, so as to not lose
                    // first byte in file, we need to remove the high order
                    // byte in the resulting integer:
                    ngrams[i] &= 0x00FFFFFF;
                } else {
                    // at last ngram, uuh...this code (and the encoding code)
                    // is going to have a problem if the total length
                    // is a single 3-gram
                    ngrams[i] = *(reinterpret_cast<uint32_t*>(&bin.front()
                                                              + i - 1));
                    // and don't walk off the end of the vector!
          // for the last one, we need to be sure and keep the high order
          // byte instead (as it's the last byte in the file) for the same
          // little endian reason:
                    ngrams[i] >>= 8;
                }
            }
            BGDEBUG << "extracted ngram " << std::hex << ngrams[i]
                    << std::dec;
        }
    }
    return ngrams;
}

vector< uint32_t > ngrams_from_v_binascii(
    uint8_t n,
    vector< string >&vbinascii)
{
  // n == 3 or 4, either way we're converting to 4 byte integers...
    vector< uint32_t > ngrams;
    for (uint32_t i(0); i < vbinascii.size();++i) {
        vector< uint32_t > newngrams(ngrams_from_binascii(n,vbinascii[i]));
        ngrams.insert(ngrams.end(),newngrams.begin(),newngrams.end());
    }
    return ngrams;
}

void ngrams_sort_and_uniq(
    vector< uint32_t > &ngrams)
{
    sort(ngrams.begin(),ngrams.end());
    vector< uint32_t >::iterator it;
    it = unique(ngrams.begin(),ngrams.end());
    ngrams.resize(it-ngrams.begin());
}

void version() {

    std::cerr << "bgparse version " << VERSION << std::endl;
    std::cerr << "(c) 2011-2018 Carnegie Mellon University " << std::endl;
    std::cerr << "Government Purpose License Rights (GPLR) pursuant to "
        "DFARS 252.227-7013" << std::endl;
    std::cerr << "Post issues to https://github.com/cmu-sei/BigGrep" << std::endl;
}


void help() {

    std::cerr << "Usage:" << std::endl;
    std::cerr << "bgparse [OPTS] IDXFILE.BGI" << std::endl << std::endl;
    std::cerr << "  bgparse takes an index file and can perform various operations on it, depending on options picked." << std::endl << std::endl;
    std::cerr << "  OPTS can be:" << std::endl;
    std::cerr << "    -s,--search HEXSTR\tsearch for the candidate file ids for this ascii encoded binary string (can have multiple -s options)" << std::endl;
    std::cerr << "    -S,--stats\tDumps the distribution of file ids and info about PFOR/VarByte compression per n-gram" << std::endl;
    std::cerr << "    -V,--verbose\tshow some additional info while working" << std::endl;
    std::cerr << "    -d,--debug\tshow diagnostic information" << std::endl;
    std::cerr << "    -h,--help\tshow this help message" << std::endl;
    std::cerr << "    -v,--version\tshow version" << std::endl;
    std::cerr << std::endl;
}

struct option long_options[] =
{
    {"search",required_argument,0,'s'},
    {"stats",no_argument,0,'S'},
    {"verbose",no_argument,0,'V'},
    {"version",no_argument,0,'v'},
    {"debug",no_argument,0,'d'},
    {"help",no_argument,0,'h'},
    {0,0,0,0}
};

int main(
    int argc,
    char** argv)
{

    int rc(0);
    vector< string > searchstrs;
    bool dump_stats(false);
    int ch;
    int option_index;
    bool debug(false);

    BGSETPROCESSNAME("bgparse");
    BGSETWARNLOGLEVEL;

    while ((ch = getopt_long(argc,argv,"s:SVvdh", long_options,
                             &option_index)) != -1)
    {
        switch (ch) {
          case 's':
            searchstrs.push_back(optarg);
            break;
          case 'S':
            dump_stats = true;
            break;
          case 'V':
            BGSETINFOLOGLEVEL;
            break;
          case 'd':
            BGWARN << "setting debug log level";
            debug = true;
            BGSETDEBUGLOGLEVEL;
            break;
          case 'h':
            help();
            return 1; // bail
          case 'v':
            version();
            return 1;
          default: // should never get here?
            BGWARN << format("option %c received??") % (char)ch;
            break;
        }
    }

    if ((argc - optind) != 1) { // takes a single index file
        BGERR << "takes a single BGI file";
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
        BGERR << format("issue w/ stat on %s - %d") % fname % strerror(errno);
        exit(11);
    }

    off_t file_size = sb.st_size;

    int f(open(fname.c_str(), O_RDONLY));

    unsigned char* fmem = NULL;
    if ((fmem = (unsigned char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, f, 0)) == MAP_FAILED) {
        BGERR << format("issue w/ mmap on %s - %d") % fname % strerror(errno);
        exit(14);
    }
    // Might be nice to use madvise((void*)fmem,file_size,MADV_SEQUENTIAL);
    // for a speedup if the code were restructured to indeed be accessed
    // sequentially?  Although even if I specified the memory region of just
    // ngram data to be sequentially accessed, it might not actually help given
    // the likely to be sparse search strings, we will be likely to skip large
    // sections so read ahead won't really help, probably.

    // I should probably wrap the open & mmap in a class so I can munmap and
    // close when it goes out of scope, since I'm relying on the normal exit
    // code to clean up for me currently...bad form, I know...

    // parse header
    bgi_header hdr;
    // this should set the hint_type related info correctly for this index
    hdr.read(fmem);
    BGDEBUG << hdr.dump() << endl;

    // quick sanity check, if fileid_map_offset is 0, that means the index file
    // is either bad or (more likely) still in the process of being generated,
    // so we want to stop moving forward in that case...
    if (hdr.fileid_map_offset == 0) {
        BGERR << format("fileid_map_offset not set, index %s still being generated?") % fname;
        exit(22);
    }

    // okay, setup for using the hints data & accessing the ngram data:
    uint64_t *hints_begin(reinterpret_cast<uint64_t*>(fmem + hdr.header_size()));
    uint64_t *hints_end(hints_begin + hdr.num_hints());
    unsigned char* index_begin(reinterpret_cast<unsigned char*>(hints_end));
    unsigned char* index_end(fmem + hdr.fileid_map_offset);

    unsigned char* fileid_map_begin(fmem + hdr.fileid_map_offset);
    unsigned char* fileid_map_end(fmem + file_size);

    if (hdr.fileid_map_offset > file_size) {
        BGERR << format("Index file %s appears to be truncated.") % fname;
        exit(33);
    }
    vector< uint32_t > ngrams(ngrams_from_v_binascii(hdr.N,searchstrs));
    BGDEBUG << format("search strings converted to %d ngrams (not unique)") % ngrams.size();

    ngrams_sort_and_uniq(ngrams);

    // okay, have the ngrams we need to search for...

    BGDEBUG << format("searching for %d  unique ngrams") % ngrams.size();

    //set< uint32_t > found;
    vector< uint32_t > found; // sorted vectors will use less RAM, and easier to intersect than sets??? (sigh)

    for (uint32_t i(0); i<ngrams.size(); ++i) {
        uint32_t ngram(ngrams[i]);
        BGDEBUG << format("searching for ngram %04x") % ngram;
        vector< uint32_t > cur_found;
        BGDEBUG << format("hints_begin %p") % hints_begin;
        BGDEBUG << format("fmem is %p") % (uint64_t*)fmem;
        BGDEBUG << format("headersize %d") % hdr.header_size();
        // find general area in hints
        uint64_t search_start(hints_begin[hdr.ngram_to_hint(ngram)]);
        BGDEBUG << format("hint index: %04x offset: %d")
            % hdr.ngram_to_hint(ngram) % search_start;

        if (search_start == UINT64_MAX) {
            // this ngram can't be in here, time to bail...
            BGDEBUG << "hint index indicates ngram not present";
            found.resize(0);
            break;
        }
        uint8_t *sptr(fmem + search_start);
        // now, surf to the proper ngram:
        uint32_t skip(ngram & hdr.hint_type_mask()); // change mask based on hint_type
        BGDEBUG << format("about to skip %u ngrams worth of data") % skip;
        VarByteUInt< uint32_t > vbyte(0);
        uint8_t bytecount(0);
        while (skip--) {
            // look at current ngram size & skip that amount, size is varbyte
            // encoded and has been shifted left one, LSB set to 1 for PFOR, 0 for
            // VarByte encoding, but we don't care about that here...
            uint32_t size(vbyte.decode(sptr,&bytecount));
            //bool pfor_encoded(size & 0x00000001);
            size >>= 1;
            BGDEBUG << format("skipping %u bytes") % (size+bytecount);
            sptr += (size + bytecount);
        }
        // okay, now at the proper ngram...now we care about the encoding
        BGDEBUG << format("got to ngram %04x at file offset %u") % ngram % (sptr-fmem);
        uint32_t size(vbyte.decode(sptr,&bytecount));
        sptr += bytecount;
        bool pfor_encoded(size & 0x00000001);
        size >>= 1;
        if (size == 0) {
            // this ngram not present
            BGDEBUG << "0 size, ngram not here";
            found.resize(0);
            break;
        }

        BGDEBUG << format("compressed data size == %u")  % size;

        // first value always varbyte encoded:
        uint32_t id(0);
        uint32_t bytes_decompressed(0);
        id = vbyte.decode(sptr,&bytecount);
        cur_found.push_back(id);

        sptr += bytecount;
        bytes_decompressed += bytecount;

        BGDEBUG << format("initial VarByte encoded id: %u, encoded size: %d ")
            % id % (int)bytecount;

        if (pfor_encoded) {
            BGDEBUG << "decoding PFOR encoded data";
            PFORUInt< uint32_t >pfor(hdr.pfor_blocksize,2); // hardcoding max exceptions to 2, because we don't care what it is while decoding....
            vector< uint8_t > cdata(sptr,sptr+(size-bytes_decompressed));
            vector< uint8_t >::iterator cdata_iter(cdata.begin());
            while (bytes_decompressed < size) {
                uint32_t cdataoffset(0);
                vector< uint32_t > *data(pfor.decode(cdata_iter,cdata.end(),&cdataoffset)); // probably should make a pfor decode method that takes ptrs/iterators instead...
                bytes_decompressed += cdataoffset;
                cdata_iter += cdataoffset;
                // okay, delete trailing zeroes, because our delta list may have been padded with them.  And since we won't have any other zeroes in there, we can find the first instance and nuke from there:
                vector< uint32_t >::iterator iter = find(data->begin(),data->end(),0);
                if (iter != data->end()) {
                    BGDEBUG << "PFORDelta encoded data had " << (data->end()-iter) << " 0 pads, trimming" << endl;
                    //data->erase(iter,data->end());
                    // or just resize it down:
                    data->resize(iter-data->begin());
                }
                // actually, could just copy into cur_found without removing the
                // trailing zeroes, as intersect below should remove the duplicate
        // entries that will result from doing the convert_from_deltas on a
        // list w/ trailing zeroes...
                copy(data->begin(),iter,std::inserter(cur_found,cur_found.end()));
                delete data;

            }
        } else { // VarByte
            BGDEBUG << "decoding VarByte encoded data";;
            while (bytes_decompressed < size)
            {
                id = vbyte.decode(sptr,&bytecount);
                cur_found.push_back(id);
                sptr += bytecount;
                bytes_decompressed += bytecount;
                BGDEBUG << format("VarByte encoded id: %u, encoded size: %d")
                    % id % (int)bytecount;
            }
        }

        BGDEBUG << "found " << cur_found.size() << " file ids for this ngram" << endl;

        // convert delta list back to non-delta:
        PFORUInt< uint32_t >::convert_from_deltas(cur_found);

        if (debug) {
            ostringstream ostr;
            for (uint32_t i(0);i<cur_found.size();++i) {
                ostr << cur_found[i] << " ";
            }
            BGDEBUG << ostr.str() << endl;
        }

        if (i == 0) {
            // first time through, current found set is our initial set
            BGDEBUG << "creating initial found set...";
            found.insert(found.end(),cur_found.begin(),cur_found.end());
        } else {
            BGDEBUG << "intersecting current found data w/ previous...";
            // intersect w/ previous found values
            //set< uint32_t > new_found;
            // of course, I used STL set<> because I needed to do set intersection,
            // but the class itself doesn't directly support that, have to use an
            // algorithm, set_intersection (along w/ std::inserter).  Well, might as well use sorted vectors then:
            uint32_t prev_num_found(found.size());
            vector< uint32_t > new_found(prev_num_found); // init w/ bogus values that will be (partly) written over
            vector< uint32_t >::iterator it=set_intersection(found.begin(),found.end(),
                                                             cur_found.begin(),cur_found.end(),
                                                             new_found.begin());
            // note: found examples showing that you didn't need to pre-init the
            // vector if the final parameter to the set_intersection call was
            // std::back_inserter(vec) or std::inserter(vec,vec.end()), but g++
            // didn't like either of those, gave compilation error...so either it's
            // something specific to say VisualStudio C++, or a part of the standard
            // that wasn't enforced well previously maybe?

            uint32_t new_num_found(it-new_found.begin());
            if (new_num_found > prev_num_found) {
                // this should NEVER occur...
                BGERR << format("THIS SHOULD NOT HAPPEN! prev num found: %u, new num found: %u")
                    % prev_num_found % new_num_found;
                exit(99);
            }
            new_found.resize(new_num_found);
            // need to "remove" the preallocated elements that weren't needed...
            // could do the swap trick here to shrink the mem usage too, but likely
            // not that bad, and each successive trip through this loop over the
            // ngrams will keep shrinking it anyways, so forget it for now, probably
            // not worth the extra time

            // now, replace the running list w/ the result of the intersection:
            found.swap(new_found);
            BGDEBUG << "after intersection have found " << found.size() << " candidate file ids" << endl;
        }

        if (found.size() == 0) {
            BGINFO << "set intersection empty, bailing...";
            break;
        } else if (debug) {
            BGDEBUG << "current intersected 'found' list:";
            ostringstream ostr;
            for (uint32_t i(0);i<found.size();++i) {
                ostr << found[i] << " ";
            }
            BGDEBUG << ostr.str();
        }
    }

    // Parse the fileid_map out into an actual map, for convenience...could use
    // boost::split or can use stringstream & getline w/ various delimiters.
    // Think I'll go w/ boost solution...seems cleaner despite my typical desire
    // to avoid Boost. :)

    //map< uint32_t, string > files;
    // actually, a vector should suffice
    vector< string > files;
    BGDEBUG << "parsing file id to file name map";
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

    for (int i(0);i<files.size();++i) {
        if (files[i].size() > 1) { // last line might just be a newline...

            // each line is like so:
            //   NNNNNNNNNN /path/to/file
            // could use split again, but spaces in file names would be a problem,
            // or we could just look to the first space and remove that,
            // and just use the vector position as the number
            // (they should match, and since we are just doing lookups by the number anyways...)
            size_t spacepos(files[i].find(' ',0));
            if (spacepos != string::npos) {
                files[i].erase(0,spacepos+1);
                BGDEBUG << format("file id %d: %s") % i % files[i];
            } else {
                if ( i < hdr.num_files) {
                    BGERR << format("problem parsing fileid map, files[%d] == %s") % i % files[i];
                }
            }
        } else {
            // last line probably empty or just a newline, so remove it:
            files.erase(files.begin()+i);
        }
    }
    if (files.size() != hdr.num_files) {
        BGWARN << format("num_files mismatch, expected %d but found %d")
            % hdr.num_files % files.size();
    }

    for (vector< uint32_t >::iterator iter(found.begin());iter != found.end();++iter)
    {
        cout << files[*iter] << endl;
    }

    if (dump_stats) {
        hdr.dump();
        // starting point from header:
    }


    return rc;
}
