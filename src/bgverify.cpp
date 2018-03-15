// Copyright 2011-2018 Carnegie Mellon University.  See LICENSE file for terms.
// BigGrep, bgverify: uses a C++ implementation of Boyer-Moore-Horspool fast
// string searching to do a "binary grep" to do a simplistic verification of
// potential matches from bgsearch

#include <iostream>
#include <iomanip>
#include <vector>
#include <list>
#include <sys/mman.h>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <getopt.h> // for getopt_long

#include <boost/foreach.hpp>

#include "Logger.hpp" // my simple logging module (to stderr)
// config options - mainly to get VERSION
#include "bgconfig.h"

class BMH
{
    static const int MAXCHAR = 256;
    int skip[MAXCHAR];
    int m; // len(pattern)
    std::vector< unsigned char > & pattern;
public:
    BMH(std::vector< unsigned char > & pat);
    int find(unsigned char *txt, int len);
    std::list< int > find(unsigned char *txt, int len, bool onlyone);
};

BMH::BMH(std::vector< unsigned char > & pat)
    :pattern(pat), m(pat.size())
{
    for (int k(0); k < MAXCHAR; ++k)
        skip[k] = m;
    for (int k(0); k < m-1; ++k)
        skip[pattern[k]] = m - k - 1;
}

std::list< int >
BMH::find(unsigned char *txt, int len, bool onlyone)
{
    std::list< int > retval;

    if (m <= len) {
        int k(m - 1);
        bool done(false);

        while (k < len && !done) {
            int j(m - 1);
            int i(k);

            while (j >= 0 && txt[i] == pattern[j]) {
                --j;
                --i;
            }

            if (j == -1) {
                retval.push_back(i+1);
                if (onlyone) {
                    done = true;
                }
            }
            k += skip[txt[k]];
        }
    }
    return retval;
}


void version() {

    std::cerr << "bgverify version " << VERSION << std::endl;
    std::cerr << "(c) 2011-2018 Carnegie Mellon University " << std::endl;
    std::cerr << "Government Purpose License Rights (GPLR) pursuant to "
        "DFARS 252.227-7013" << std::endl;
    std::cerr << "Post issues to https://github.com/cmu-sei/BigGrep" <<
        std::endl;
}


void help() {

    std::cerr << "Usage:" << std::endl;
    std::cerr << "bgverify [OPTS] STR [STR2 STR3 ...]" << std::endl << std::endl;
    std::cerr << "  bgverify takes a list of files on stdin, the result of a bgsearch, and verifies that the STR patterns (binary hex values) are really present and not false positives" << std::endl << std::endl;
    std::cerr << "  OPTS can be:" << std::endl;
    // right now bgsearch.py translates everything to binascii for us, so we
    // don't need to repeat the logic here:
    //std::cerr << "    -a,--ascii STR\tSTR is an ASCII string (default is to guess)" << std::endl;
    //std::cerr << "    -u,--unicode STR\tSTR is UTF-16 encoded ascii chars (default is to guess)" << std::endl;
    //std::cerr << "    -b,--binary STR\tSTR is HEX chars (default is to guess)" << std::endl;
    std::cerr << "    -o,--offsets\tShow all hits w/ locations instead of just a pass/fail match answer" << std::endl;
    std::cerr << "    -V,--verbose\tShow some additional info while working" << std::endl;
    std::cerr << "    -D,--debug\t\tShow diagnostic information" << std::endl;
    std::cerr << "    -h,--help\t\tShow this help message" << std::endl;
    std::cerr << "    -v,--version\tShow version and exit" << std::endl;
    std::cerr << std::endl;
}

struct option long_options[] =
{
    //{"ascii",required_argument,0,'a'},
    //{"unicode",required_argument,0,'u'},
    //{"binary",required_argument,0,'b'},
    {"offsets",no_argument,0,'o'},
    {"verbose",no_argument,0,'V'},
    {"version",no_argument,0,'v'},
    {"debug",no_argument,0,'D'},
    {"help",no_argument,0,'h'},
    {0,0,0,0}
};

int main(
    int argc,
    char** argv)
{

    bool showall(false);

    std::vector< std::vector < unsigned char > > patterns;

    int ch;
    int option_index;
    while ((ch = getopt_long(argc,argv,"oVvDh",long_options,
                             &option_index)) != -1)
    {
        switch (ch)
        {
          case 'o':
            showall = true;
            break;
          case 'V':
            LSETLOGLEVEL(Logger::INFO);
            break;
          case 'D':
            LSETLOGLEVEL(Logger::DEBUG);
            break;
          case 'h':
            help();
            return 1; // bail
          case 'v':
            version();
            return 1;
          default: // should never get here?
            LERR << "unknown option '" << (char)ch << "' recieved??" << std::endl;
            help();
            return 1;
            break;
        }
    }

    if ((argc - optind) < 1) {
        LERR << "not enough parameters given (no search strings?)" <<std::endl;
        help();
        return 1;
    }

    int hoff(optind);
    LDEBUG << "optind == " << optind << std::endl;

    // get all the search strings now
    while (hoff < argc) {

    // we should probably allow for ASCII strings in addition to HEX, but for
    // now having bgsearch.py transform them for us is sufficient...
        bool lookslikehex(true);
        int coff(0);

        LDEBUG << "examining string for hex-ness: " << argv[hoff] << std::endl;
        while (lookslikehex && argv[hoff][coff] != '\0')
        {
            lookslikehex = (isxdigit(argv[hoff][coff]) != 0);
            ++coff;
        }

        LDEBUG << "looks like hex == " << lookslikehex << std::endl;

        std::string hexchars(argv[hoff]);
        LDEBUG << hexchars << std::endl;
        std::vector< unsigned char > pat(hexchars.size()/2);
        // convert argv[hoff] into bytes:
        if (!lookslikehex || (strlen(argv[hoff])%2 != 0 || argv[hoff][0] == '\0'))
        {
            LERR << "HEX STR #" << hoff << " not even # of chars (or empty string or bad hex chars)!" << std::endl;
            return 2;
        }

        for (int i(0); i < hexchars.size(); ++i,++i) {
            pat[i/2] = strtol((hexchars.substr(i,2)).c_str(),NULL,16);
        }
        patterns.push_back(pat);
        ++hoff;
    }
    LDEBUG << "got " << patterns.size() << " patterns to search for" << std::endl;

    int rc(0);

    std::list< std::string > files;
    LINFO << "reading file names from stdin" << std::endl;
    bool done(false);
    while (!done) {
        std::string fname;
        std::getline(std::cin,fname);
        done = std::cin.eof();
        if (fname != "") { // last line might be blank
            files.push_back(fname);
        }
    }

    if (files.size() == 0) {
        LERR << "ERROR: no files specified?" << std::endl;
    }

    int num_matches(0);
    int num_file_matches(0);

    for (std::list< std::string >::iterator iter(files.begin()); iter != files.end(); ++iter) {
        LINFO << "searching file " << (*iter) << std::endl;
        int fd(open((*iter).c_str(),O_RDONLY));
        struct stat sb;
        if (fstat(fd,&sb) == -1) {
            LERR << "stat fail on " << (*iter) << ", skipping" << std::endl;
            //return 4;
            continue;
        }
        int fdlen(sb.st_size);
        unsigned char *addr = static_cast<unsigned char*>(mmap(NULL,fdlen,PROT_READ,MAP_PRIVATE,fd,0));
        // potential speedup by telling how we intend to access the
        // mmapped pages:
        madvise((void*)addr,fdlen,MADV_SEQUENTIAL);

        // would like to be smarter at dealing w/ multiple search terms in the
        // future (try and do searches simultaneously so only one pass through
        // the file front to back, allow for arbitrary combinations of
        // AND/OR/NOT for terms, etc), but for now lets just search for each
        // of them individually with an implicit AND:
        std::list< std::list < int > > results;
        bool gotmatch(false);
        for (int i(0); i < patterns.size(); ++i) {
            BMH bmh(patterns[i]);
            std::list< int > tmpresults(bmh.find(addr,fdlen,!showall));
            if (tmpresults.size() > 0) {
                LDEBUG << "found STR #" << i << std::endl;
                results.push_back(tmpresults);
            } else {
                LDEBUG << "sorry, no results STR #" << i << " this file" << std::endl;
                // and if we had started getting results,
                // we no longer care due to implicit AND behavior...
                if (! results.empty()) {
                    results.clear();
                }
                break;
            }
        }

        if (!results.empty()) {
            ++num_file_matches;

            while (results.size() != 0) {

                if (showall) {

                    BOOST_FOREACH(int i, results.front())
                    {
                        std::cout << (*iter) << ": " << std::setw(8) <<
                            std::setfill('0') << std::hex << i << std::dec
                                  << std::endl;
                        ++num_matches;
                    }
                } else {
                    std::cout << (*iter) << ": matches" << std::endl;
                    ++num_matches;
                    break;
                }
                results.pop_front(); // will be only entry if !showall
            }
        }
        munmap(addr,fdlen);
        close(fd);
    }

    LINFO << num_matches << " matches across " << num_file_matches <<
        " files (out of " << files.size() << ")" << std::endl;
    return (num_matches == 0);
}
