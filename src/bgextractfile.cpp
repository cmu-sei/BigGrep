// Copyright 2018 Carnegie Mellon University.  See LICENSE file for terms.
// BigGrep, bgextractfile: code to extract/remove/replace a file from a biggrep index

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
#include <boost/iostreams/device/file_descriptor.hpp>

#include "BGLogging.hpp"
#include "VarByte.hpp"
#include "PFOR.hpp"

using namespace std;

typedef vector< string > vec_str_t;

vec_str_t extractfiles;

#include "bgi_header.hpp"

// config options - mainly to get VERSION
#include "bgconfig.h"
using boost::format;
namespace io = boost::iostreams;

void version() {

    std::cerr << "bgextractfile version " << VERSION << std::endl;
    std::cerr << "(c) 2017-2018 Carnegie Mellon University " << std::endl;
    std::cerr << "Government Purpose License Rights (GPLR) pursuant to "
        "DFARS 252.227-7013" << std::endl;
    std::cerr << "Post issues to https://github.com/cmu-sei/BigGrep" << std::endl;
}


void help() {

    std::cerr << "Usage:" << std::endl;
    std::cerr << "bgextractfile [OPTS] IDXFILE.BGI" << std::endl << std::endl;
    std::cerr << "  bgextractfile takes an index file and can remove a file from"
        " the index" << std::endl << std::endl;
    std::cerr << "  OPTS can be:" << std::endl;
    std::cerr << "    -x,--extract file\tsearch for the candidate file and "
        "remove it from the index map." << std::endl;
    std::cerr << "    -r,--replace STR\tsearch for the candidate file given "
        "to -x and replace it with the given STRING." << std::endl;
    std::cerr << "    -v,--verbose\tshow some additional info while working"
              << std::endl;
    std::cerr << "    -d,--debug\tshow diagnostic information" << std::endl;
    std::cerr << "    -h,--help\tshow this help message" << std::endl;
    std::cerr << "    -V,--version\tshow version" << std::endl;
    std::cerr << std::endl;
}

uint32_t bgReadFiles()
{
    bool done(false);
    uint32_t numfiles;

    // read list of files from stdin, get count
    while (!done) {
        string fname;
        getline(cin,fname);
        done = cin.eof();
        if (fname != "") {// last line might be blank
            extractfiles.push_back(fname);
        }
    }

    numfiles = extractfiles.size();

    /*if (numfiles < 1) {
        BGERR << "Please provide one or more files for extracting at a time.";
        help();
        return numfiles;
        }*/

    return numfiles;
}

struct option long_options[] =
{
    {"extract",required_argument,0,'x'},
    {"replace",required_argument,0,'r'},
    {"verbose",no_argument,0,'v'},
    {"version",no_argument,0,'V'},
    {"debug",no_argument,0,'d'},
    {"help",no_argument,0,'h'},
    {0,0,0,0}
};

int main(
    int argc,
    char** argv)
{

    int rc(0);
    string replacestr;
    int num_file_search(0);
    int ch;
    int option_index;
    bool debug(false);

    BGSETPROCESSNAME("bgextractfile");
    BGSETWARNLOGLEVEL;

    while ((ch = getopt_long(argc,argv,"x:r:Vvdh", long_options,
                             &option_index)) != -1)
    {
        switch (ch) {
          case 'x':
            extractfiles.push_back(optarg);
            num_file_search = 1;
            break;
          case 'r':
            replacestr = optarg;
            break;
          case 'v':
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
          case 'V':
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

    if (extractfiles.size() == 0) {
        num_file_search = bgReadFiles();
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

    int f(open(fname.c_str(), O_RDWR));

    unsigned char* fmem = NULL;
    if ((fmem = (unsigned char *)mmap(NULL, file_size, PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE, f, 0)) == MAP_FAILED)
    {
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
        BGERR << format("fileid_map_offset not set, index %s still being "
                        "generated?") % fname;
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

    // Parse the fileid_map out into an actual map

    vector< string > files;
    BGDEBUG << "parsing file id to file name map";
    string mapdata(fileid_map_begin,fileid_map_end);

    if (hdr.compressed()) {
        //must decompress
        std::stringstream compressed(mapdata);
        std::ostringstream decompressed;
        boost::iostreams::filtering_streambuf<boost::iostreams::input> in;
        in.push(boost::iostreams::zlib_decompressor());
        in.push(compressed);
        boost::iostreams::copy(in, decompressed);
        mapdata = decompressed.str();
    }
    size_t fileid_size_before(mapdata.size());

    boost::split(files,mapdata,boost::is_any_of("\n"));
    uint32_t found(0);
    /* keep track of the size of files removed so we can pad later */
    int removed_files(0);

    for (int i(0);i<files.size();++i) {
        if (files[i].size() > 1) { // last line might just be a newline...
            // each line is like so:
            //   NNNNNNNNNN /path/to/file
            // or we could just look to the first space and remove that,
            // and just use the vector position as the number
            BGDEBUG << format("file id %d: %s") % i % files[i];
            size_t spacepos(files[i].find(' ',0));
            size_t commapos(files[i].find(',',spacepos));
            string filecmp(files[i].substr(spacepos+1,(commapos-spacepos-1)));
            for (int k(0); k<extractfiles.size(); k++) {
                if (filecmp.compare(extractfiles[k]) != 0) {
                    continue;
                } else {
                    BGINFO << format("Extracting: file id %d: %s") % i % files[i];
                    found++;
                    removed_files += files[i].size() + 1;
                    if (!replacestr.empty()) {
                        files[i] = files[i].substr(0, spacepos+1) + replacestr;
                        removed_files -= files[i].size() - 1;
                    } else {
                        files[i] = files[i].substr(0, spacepos+1);
                        removed_files -= files[i].size() - 1;
                    }
                    break;
                }
            }
            if (found == num_file_search) {
                break;
            }
        } else {
            // last line probably empty or just a newline, so remove it:
            files.erase(files.begin()+i);
            removed_files++;
        }
    }

    BGWARN << format("Extracted %d out of %d files requested.")
        % found % num_file_search;

    if (found) {
        /* rewrite header with new num files */

        if (lseek(f, hdr.fileid_map_offset, SEEK_SET) < 1) {
            BGERR << "problem with lseek";
            exit(1);
        }
        if (hdr.compressed()) {
            io::filtering_streambuf<io::output> out;
            out.push(io::zlib_compressor());
            out.push(io::file_descriptor_sink(f, io::never_close_handle));

            ostream os(&out);
            for (int i(0); i < files.size(); ++i) {
                os << files[i].c_str() << endl;
            }

        } else {
            for (int i(0); i < files.size(); i++) {
                if (write(f, files[i].c_str(), files[i].size()) != files[i].size()) {
                    BGERR << "problem with write";
                }
                if (write(f, "\n", 1) != 1) {
                    BGERR << "problem with writing new line";
                }
            }
            if (removed_files > 0) {
                BGDEBUG << format("Need to pad file %d bytes") % removed_files;
                for (int i(0); i < removed_files; i++) {
                    if (write(f, "\0", 1) != 1) {
                        BGERR << "problem with writing padding";
                    }
                }
            }
        }

        /* not rewriting header right now bc bgparse requires files to be there */
        /*if (replacestr.empty()) {
            if (hdr.has_hint_type()) {
                if (lseek(f, 17, SEEK_SET) < 1) {
                    BGERR << "problem with lseek x";
                    BGERR << errno;
                    exit(1);
                }
            } else {
                if (lseek(f, 16, SEEK_SET) < 1) {
                    BGERR << "problem with lseek y";
                    exit(1);
                }
            }
            uint32_t new_num_files = hdr.num_files - found;
            BGINFO << format("rewriting header with %d files") % new_num_files;

            if (write(f, reinterpret_cast<const char*>(&new_num_files),
                      sizeof(new_num_files)) != sizeof(new_num_files))
            {
                BGERR << "problem rewriting header";
            }
            }*/

    } else {
        BGWARN << format("no files found");
    }

    close(f);

    return rc;
}
