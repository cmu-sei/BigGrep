// Copyright 2011-2017 Carnegie Mellon University.  See LICENSE file for terms.
#define __STDC_LIMIT_MACROS // I have to define this to get UINT32_MAX because of the ISO C99 standard, apparently
// #define __STDC_CONSTANT_MACROS // don't really need these right now
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <queue>
#include <unistd.h>
//#define GLIBC_MALLOC_BUG_WORKAROUND

#ifdef GLIBC_MALLOC_BUG_WORKAROUND
// in case I need to call mallopt:
#include <malloc.h>
#endif

#include <vector>
#include <algorithm>
#include <map>
#include <list>
#include <set>

#include <iostream>
#include <fstream>
#include <sstream>

#include <memory> // for auto_ptr

#include <getopt.h>

#include <boost/thread/thread.hpp>

// config options - mainly to get VERSION and BG_BOOST_LOCKLESS
#include "bgconfig.h"

/* only available if boost v. >= 1.53 */
#if BG_BOOST_LOCKLESS
#include <boost/lockfree/queue.hpp>
#include <boost/atomic.hpp>
#endif

#include <boost/timer/timer.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/bind.hpp>
#include <boost/optional.hpp>
#include <boost/format.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/copy.hpp>

// these two for getrusage calls to track mem consumption...
#include <sys/time.h>
#include <sys/resource.h>

// not using the macros in endian.h currently, but probaly should to make this
// code and the indexes portable between big endian and little endian
#if 0
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <endian.h>
#endif

using namespace std;
namespace io = boost::iostreams;

// utility class to do sprintf type work to create a std::string
#include "StrFormat.hpp"
// utility class to do time deltas
#include "bgstopwatch.hpp"
// class to do variable byte encoding & decoding:
#include "VarByte.hpp"
// class to do PFOR (Patched Frame Of Reference) encoding & decoding:
#include "PFOR.hpp"
// API for dealing w/ different BGI file header versions
#include "bgi_header.hpp"
// for boost string split function:
#include "boost/algorithm/string.hpp"
// to replace Poco::AtomicCounter
#include "BGAtomicCounter.hpp"
#include "BGLogging.hpp"

using boost::timer::cpu_timer;
using boost::timer::cpu_times;
using boost::format;

// note, this code only deals w/ 3-grams or 4-grams...
// this is the default if not passed in:
#define NGRAM_SIZE 3
//#define NGRAM_SIZE 4
uint32_t ngram_size(NGRAM_SIZE);

// The maximum of unique ngrams to allow
// (default to upper limit of 4-gram space)
uint64_t max_uniq_ngrams(4294967296);

// these are options to:
#define NUM_SHINGLE_THREADS 4
//#define NUM_MERGE_THREADS 5
#define NUM_COMPRESS_THREADS 5

uint32_t num_shingle_threads(NUM_SHINGLE_THREADS);
uint32_t num_compress_threads(NUM_COMPRESS_THREADS);

// actually, experimenting w/ different block sizes (64/4/16, 32/2/8, 16/2/4,
// 8/1/4, etc) showed that for a lot of our data, smaller block sizes are a a
// lot better: 16/2/4 seemed best for 4 grams, 32/3/8 edged that out slightly
// for 3 grams, but not by very much):
#define PFOR_BLOCKSIZE 32
#define PFOR_MAXEXCEPTIONS 3
// min number of things to consider PFOR encoding:
#define PFOR_THRESHOLD 8

uint32_t PFOR_blocksize(PFOR_BLOCKSIZE);
uint32_t PFOR_maxexceptions(PFOR_MAXEXCEPTIONS);
uint32_t PFOR_threshold(PFOR_THRESHOLD);

uint8_t hint_type(0);

string index_prefix("index");
bool shingle_and_exit(false);
bool compress_data(false);
FILE *bglog = NULL;
FILE *overflow = NULL;
bool run_lock_free(false);

BGAtomicCounter shingle_counter;
BGAtomicCounter compress_counter;
BGAtomicCounter write_counter;

#if BG_BOOST_LOCKLESS
boost::atomic<bool> writerdone (false);
boost::atomic<bool> compressdone (false);
boost::atomic<bool> shingledone (false);
#else
bool writerdone(false);
bool compressdone(false);
bool shingledone(false);
#endif

// some useful type definitions:
typedef vector< uint64_t > vec_u64_t;
typedef vector< uint32_t > vec_u32_t;
typedef vector< uint8_t > vec_u8_t;
typedef vector< string > vec_str_t;

typedef map< uint32_t, vec_u32_t * > map_u32_to_vu32p_t;
typedef pair< uint32_t, vec_u32_t * > pair_ngram_to_ids_t;
typedef vector< pair_ngram_to_ids_t > ngrams_to_ids_t;
typedef vector< ngrams_to_ids_t > vec_ngrams_to_ids_t;

vec_str_t id_to_fname;

uint64_t total_extracted_ngrams(0);

class bgWriteNotification {
public:
    uint32_t ngram_order;
    uint32_t ngram;
    vec_u8_t *cdata;
    uint32_t uncompressed_size;
    bool     pfor;

    bgWriteNotification(uint32_t ngram_order, uint32_t ngram,
                      vec_u8_t *cdata, uint32_t uncompressed_size,
                      bool pfor)
        : ngram_order(ngram_order), ngram(ngram), cdata(cdata), uncompressed_size(uncompressed_size), pfor(pfor) {}

    ~bgWriteNotification() {}
};


class bgCompressNotification {
public:
    uint32_t   ngram_order;
    uint32_t   ngram;
    vec_u32_t  *fids;

    bgCompressNotification(uint32_t ngram_order, uint32_t ngram,
                         vec_u32_t *fids)
        : ngram_order(ngram_order), ngram(ngram), fids(fids) {}
    ~bgCompressNotification() {}
};


class FileData;

class bgShingleNotification {
public:
    string     fname;
    uint32_t   id;
    FileData   *fd;
    bgShingleNotification(string fname, uint32_t id, FileData *fd)
        : fname(fname), id(id), fd(fd) {}
    ~bgShingleNotification() {}
};

# if BG_BOOST_LOCKLESS
/* initialized to 10000 but will allocate more when needed */
boost::lockfree::queue<bgWriteNotification*> writeQueue(65535);
boost::lockfree::queue<bgCompressNotification*> compressQueue(65535);
boost::lockfree::queue<bgShingleNotification*> shingleQueue(65535);
#endif

template <typename T>
class bgQueue
{
private:
    std::queue<T> bgqueue;
    mutable boost::mutex bgqmutex;
    boost::condition_variable bgqcond;
#if BG_BOOST_LOCKLESS
    boost::atomic<bool> queuedone;
#else
    bool queuedone;
#endif
public:
    bgQueue() {
        queuedone = false;
    }
    void push(T const& data) {
        boost::mutex::scoped_lock lock(bgqmutex);
        bgqueue.push(data);
        lock.unlock();
        bgqcond.notify_one();
    }

    bool empty() const{
        boost::mutex::scoped_lock lock(bgqmutex);
        return bgqueue.empty();
    }

    void quit() {
        queuedone = true;
        bgqcond.notify_all();
    }

    bool try_pop(T& value) {
        boost::mutex::scoped_lock lock(bgqmutex);
        if (bgqueue.empty()) {
            return false;
        }

        value = bgqueue.front();
        bgqueue.pop();
        return true;
    }

    void wait_and_pop(T& value) {
        boost::mutex::scoped_lock lock(bgqmutex);
        while (bgqueue.empty() && !queuedone) {
            bgqcond.wait(lock);
        }
        if (queuedone) {
            value = NULL;
            return;
        }
        value = bgqueue.front();
        bgqueue.pop();
    }
};


class LoserTree;
LoserTree *lt(NULL);

void help();

// utility function to get maxrss value, converted to GB
double
get_mem_usage(void)
{
    int rc(0);
    struct rusage ru;
    rc = getrusage(RUSAGE_SELF,&ru);
    // convert the returned value in KB to a floating point answer in GB
    return float(ru.ru_maxrss)/(1024*1024);
}


// FileData holds info about each file id & contents,
// plus state info for loser tree multiway merge
class FileData {
public:
    uint32_t id; // file id
    vec_u32_t ngrams; // ngram vector
    uint32_t val; // current ngram value
    uint32_t cur_off; // offset into vector of that value
    uint32_t num; // total num
    bool missing; //label missing
    bool hit_ngram_limit; //surpass ngram limit -M
    bool have_vals;
    // to avoid operator[] and iterators, leverage the fact
    //that the underlying data in a vector is contiguous...
    uint32_t *data_ptr;

    explicit
    FileData(uint32_t _id):
        id(_id), val(0), cur_off(0), num(0), have_vals(false),
        missing(false), hit_ngram_limit(false), data_ptr(NULL)
        // reset when ngrams filled in (call fixup)
    { }

    inline void fixup()
    {
        num = ngrams.size();
        if (num) { // better be...
            data_ptr = &ngrams.front();
            val = data_ptr[0];
            have_vals = true;
        }
    }

    inline uint32_t peek_val()
    {
        return val;
    }

    uint32_t pop_val()
    {
        uint32_t rval(val);
        if (++cur_off < num) {
            // prep the next one
            val = data_ptr[cur_off];
        } else {
            have_vals = false;
            val = 0;
        }
        return rval;
    }

    inline void next_val()
    {
        if (++cur_off < num) {
            // prep the next one
            val = data_ptr[cur_off];
        } else {
            have_vals = false;
            val = 0;
        }
    }
};

vector<FileData*> fds;

class LoserTreeNode {
public:
    /* can be NULL for top of tree */
    LoserTreeNode *parent;
    /* can be NULL (leaf nodes & internal nodes that have been finished*/
    LoserTreeNode *left;
    // same notes as left
    LoserTreeNode *right;
    // leaf nodes, this won't change, but higher in tree it will
    FileData *fd;
    // just an id for this node that is only really used for diags
    //(stays constant in tree, as opposed to fd->id)
    uint32_t id;

    // CTOR
    LoserTreeNode():
        parent(NULL),left(NULL),right(NULL),fd(NULL),id(0)
    {}

    // find out the ngram & fid for a node:
    inline
    pair< uint32_t, uint32_t >
    peek_node_data()
    {
        pair< uint32_t, uint32_t > ret;
        if (fd && fd->have_vals) {
            ret.first = fd->peek_val();
            ret.second = fd->id;
            //ret.second = fd->reorg_id;
        } else {
            // return UINT32_MAX for lack of better choice right now,
            // maybe use a custom return class w/ a flag?
            // actually 0 may be better, because that'll probably
            // flag the "hey, we went backwards" check
            ret.first = ret.second = 0;
            BGTRACE << format("peek_node_data node %u, fd == NULL") % id;
        }
        BGTRACE << format("peek_node_data node %u, ngram %08x, fid %u")
            % id % ret.first % ret.second;
        return ret;
    }

    // find out the ngram & fid for a node, remove it & update tree
    inline pair< uint32_t, uint32_t > pop_node_data()
    {
        pair< uint32_t, uint32_t > ret(peek_node_data());
        BGTRACE << format("pop_node_data node %u, ngram %08x, fid %u")
            % id % ret.first % ret.second;
        if (fd->have_vals) {
            fd->next_val();
        } else { // hit the end of the vals for this id, clean up a little...
            BGTRACE << format("pop_node_data node %u, at end of ngrams") % id;
            vec_u32_t ().swap(fd->ngrams);
            fd->fixup();
            fd = NULL;
        }
        return ret;
    }
};

class LoserTree {
public:
    //vector< vector< T >::iterator * > v_p_iters;
    vector< LoserTreeNode > nodes; // one per file id/ngram vector
    LoserTreeNode *root;

    bool empty;

    uint32_t num; /* number of files */
    uint32_t numnodes;
    uint32_t levels;

    uint32_t last_fid_popped;

    bool
    is_empty()
    {
        if (!empty) {
            if (root->left == NULL || root->left->fd == NULL ||
                !root->left->fd->have_vals)
            {
                if (root->right == NULL || root->right->fd == NULL ||
                    !root->right->fd->have_vals) {
                    empty = true;
                }
            }
        }
        return empty;
    }

    inline void update_tree()
    {
        // now update fd values by calling fix_parent on every other leaf node
        LoserTreeNode *node (nodes[last_fid_popped].parent);
        BGTRACE << "update_tree called";

        do {
            // need to look at our children, the left & right nodes...
            // get proper min vals from left & right (barring NULLs)

            FileData *new_fd(NULL);
            if (node->left && node->left->fd && node->left->fd->have_vals) {
                if (node->right && node->right->fd &&
                    node->right->fd->have_vals)
                {
                    if (node->left->fd->val <= node->right->fd->val) {
                        new_fd = node->left->fd;
                    } else {
                        new_fd = node->right->fd;
                    }
                } else {
                    new_fd = node->left->fd;
                }
            } else {
                if (node->right && node->right->fd &&
                    node->right->fd->have_vals)
                {
                    new_fd = node->right->fd;
                } // else both left & right are NULL, so we become NULL
            }

            node->fd = new_fd;
            node = node->parent; // fix_parent checks for parent != NULL
        } while (node != NULL);
    }

    void
    update_tree_full()
    {
        // now update fd values by calling fix_parent on every other leaf node
        BGTRACE << "update_tree_full called";
        uint32_t mid((num+num%2)/2);
        uint32_t start(0);
        uint32_t end(num);
        for (uint32_t i(start); i < end; i += 2) {
            last_fid_popped=i;
            update_tree();
        }
        last_fid_popped = 0;
    }

    // better have filled in the leaf node data pointers prior to this...
    void build_tree()
    {
        // nodes 0 through num-1 are the leaf nodes, node numnodes-1 is the
        // root, so lets build it up from the leaf nodes...
        uint32_t p(num+num%2); // offset of current parent node
        uint32_t cnt(num);
        uint32_t curnode(0);

        BGDEBUG << format("build_tree, num = %u, leaf cnt = %u, numnodes = %u")
            % num % cnt % numnodes;

        while (cnt > 1) {
            for (uint32_t i(0); i < cnt; ++i,++i) {
                BGTRACE << format("build_tree, node = %u & node =%u "
                                  "parent = %u") % curnode % (curnode+1) % p;
                nodes[curnode].id = curnode;
                nodes[curnode+1].id = curnode+1;
                nodes[curnode].parent = &nodes[p];
                nodes[curnode+1].parent = &nodes[p];
                nodes[curnode].parent->left = &nodes[curnode];
                nodes[curnode].parent->right = &nodes[curnode+1];
                // stick in "temporary" fd pointer for parent
                //so initial tree fixup doesn't drop nodes?
                nodes[curnode].parent->fd = nodes[curnode].fd;
                ++p;
                curnode += 2;
            }
            cnt = (cnt + cnt%2)/2; // step up to next level...
            p += cnt%2;
        }
        // still need to fixup the root?
        root->id = nodes.size()-1;

        // make sure all of our fd->cur are correct...
        for (uint32_t i(0); i < num; i++) {
            if (nodes[i].fd != NULL) {
                nodes[i].fd->fixup();
            }
        }
        update_tree_full();
    }

    // returns: ngram, id then updates the tree...
    inline pair< uint32_t, uint32_t > get_root_data()
    {
        // better check if not empty before calling this...
        pair< uint32_t, uint32_t > ret(nodes[root->fd->id].pop_node_data());

        last_fid_popped = ret.second;
        update_tree();
        BGTRACE << format("get_root_data ngram %08x, fid %u")
            % ret.first % ret.second;

        if (root->left == NULL && root->right == NULL) {
            empty = true;
        }

        return ret;
    }

    // CTOR
    LoserTree(uint32_t _num = 0):
        num(_num), numnodes(1), empty(true), root(NULL)
    {
        if (num > 0) { // better be! :)
          // I need to be able to easily reference the nodes while I'm building
            // the tree, so I'll use Cory's meothod of dropping them all in the
            // nodes array for the build...  so, need the total count of nodes,
            // which is determined by making sure each level will be an even
            // number (except the root, of course):
            uint32_t cnt(num);
            uint32_t phantoms(0);
            uint32_t i(0);

            BGDEBUG << format("LoserTree for %u leaf nodes requested") % num;

            //Special case to handle only a single file/node

            while (cnt > 1) {
                uint32_t n(cnt);
                uint32_t p(cnt%2);
                numnodes += n+p;
                cnt = (n+p)/2;
                phantoms += p;
                ++i;
                BGDEBUG << format("LoserTree level %u will have %u nodes "
                                  "and %u phantoms") % i % n % p;
            }
            BGDEBUG << format("LoserTree going to have %u nodes (%u phantoms)")
                % numnodes % phantoms;
            nodes.resize(numnodes);
            root = &nodes.back();
            empty = false;
        }
    }

   void
   dump_recursive(LoserTreeNode *node,uint32_t lvl,string& out)
    {
        string pad(3*lvl,' ');
        if (node && node->fd && node->fd->have_vals) {
            dump_recursive(node->right,lvl+1,out);
            uint32_t ngram(UINT32_MAX);
            uint32_t id(UINT32_MAX);
            ngram = node->fd->val;
            id = node->fd->id;
            string flag;
            if (node->id == root->id) {
                flag = "ROOT";
            }
            out += StrFormat("%s %u [%08x %u] %s\n",
                             pad.c_str(),node->id,ngram,id,flag.c_str());
            dump_recursive(node->left,lvl+1,out);
        } else {
            out += StrFormat("%s N\n",pad.c_str());
        }
    }

    void
    dump()
    {
        /*        if (logger().trace()) {
            string out;
            dump_recursive(root,0,out);
            BGDEBUG << "LoserTree dump: ";
            //poco_debug(logger(),StrFormat("LoserTree dump:"));
            cerr << out << endl;
            BGDEBUG << "LoserTree dump done";
            //poco_debug(logger(),"LoserTree dump done");
            }*/
    }
};

void bgShingle(
    bgShingleNotification *note
               )
{

    string fname = note->fname;
    uint32_t id = note->id;
    bgstopwatch stimer;
    struct stat sb;
    off_t file_size(0);
    int f;
    unsigned char* fmem = NULL;
    //    FileData *fd(new FileData(id));
    FileData *fd = note->fd;

    //fds.push_back(fd);

    BGDEBUG << format("Shingleing %s (id: %u, shingle "
                      "counter: %u)") % fname.c_str()
        % int(id) % int(shingle_counter);

    // open file (mmap) (would love to use C++ ifstream to open the file
    // & get the size & file descriptor for the mmap call, but sadly
    // there is no standard way to do the latter...sigh...so resorting
    // to the C interfaces for this...

    if (stat(fname.c_str(), &sb) == -1) {
        char buf[256];
        BGERR << format("stat failed on '%s' (%d:%s)")
            % fname.c_str() % errno %
            strerror_r(errno,buf,256);
        fd->missing = true;
        // append ",missing=true" to metadata
        // should be safe to alter the string in id_to_fname multithreaded
        // without lock, because only one thread will ever see each entry...
        //id_to_fname[id] = id_to_fname[id] + ",missing=true";
    } else {
        file_size = sb.st_size;

        // hm...should be able to use 64 bit file size stuff, no?  Why did
        // old code care?  Probably had either int or uint32_t for file size
        // storage somewhere?  Should probably make sure this code can
        // handle bigger files at some point and remove the upper restriction...
        if (file_size < 4 || file_size >= UINT_MAX) {
            BGERR << format("issue w/ file size on '%s' (%u)")
                % fname.c_str() % file_size;
            // better to treat as "missing" for now I think...
            fd->missing = true;
        } else {
            f = open(fname.c_str(), O_RDONLY);

            if ((fmem =
                 (unsigned char *)mmap(NULL, file_size, PROT_READ,
                                       MAP_PRIVATE, f, 0)) == MAP_FAILED)
            {
                char buf[256];
                BGERR << format("issue w/ mmap on '%s' (%s)") %
                    fname.c_str() % strerror_r(errno,buf,256);
                exit(14);
            }
            // potential speedup by telling how we intend
            // to access the mmapped pages:
            madvise((void*)fmem,file_size,MADV_SEQUENTIAL);
        }
    }

    //lt->nodes[id].fd = NULL;

    if (!fd->missing) {
        // walk over n-grams (all file size - ngram_size + 1 of them)
        unsigned ngram_count = file_size - ngram_size + 1;
        //lt->nodes[id].fd = fd;
        vec_u32_t &ngrams(fd->ngrams);
        ngrams.reserve(ngram_count);

        for (unsigned int fi(0); fi < ngram_count; ++fi) {
            uint32_t ngram(0);
            if (ngram_size == 4) {
                ngram = (*((uint32_t *)(&fmem[fi])));
            } else {
                // must be 3, will read 4 bytes
                //at a time for speed, but adjust...
                // trim to 3 grams...
                if (fi < (ngram_count-1)) {
                    ngram = (*((uint32_t *)(&fmem[fi])));
                    // because of little endian architecture, so as to not lose
                    // first byte in file, we need to remove the high order
                    // byte in the resulting integer:
                    ngram &= 0x00FFFFFF;
                } else {
                    ngram = (*((uint32_t *)(&fmem[fi-1])));
                    // and don't walk off the end of the mmap array!
                    // for the last one, we need to be sure and keep the high
                    // order byte instead (as it's the last byte in the file)
                    // for the same little endian reason:
                    ngram >>= 8;
                }
            }
            ngrams.push_back(ngram);
        }

        // now we can clean up a little
        munmap(fmem, file_size);
        close(f);
        // sort & uniq
        sort(ngrams.begin(),ngrams.end());
        vec_u32_t::iterator it;
        it = unique(ngrams.begin(),ngrams.end());

        // can shrink down our ngram vector now, first remove the
        //"non unique" stuff at the bottom of the vector
        ngrams.resize(it-(ngrams.begin()));
        // then, to really toss out extra mem, we have to create
        //a new tmp vector & swap buffers:
        vec_u32_t (ngrams).swap(ngrams);

        unsigned int unique_ngrams(ngrams.size());
        if (max_uniq_ngrams <= unique_ngrams) {
            BGERR << format("Shingled file %s contains too "
                            "many (%u) unique %u-grams, "
                            "rejecting from index.")
                % fname.c_str() % unique_ngrams % ngram_size;
            fd->hit_ngram_limit = true;
            //lt->nodes[id].fd = NULL;
            if (overflow) {
                fputs(fname.c_str(),overflow);
                fputs("\n",overflow);
            }
        } else {
            // store the number of unique n-grams in the metadata
            ostringstream numngrams;
            numngrams << ",unique_ngrams=" << unique_ngrams;
            id_to_fname[id] = id_to_fname[id] + numngrams.str();
            fd->fixup();

            BGINFO << format("Shingled file %s (id %u) contains "
                             "%u UNIQUE %u-grams out of %u total,"
                             " took %f sec") % fname.c_str() %
                int(id) % unique_ngrams %
                ngram_size % int(ngram_count) %
                stimer.secondsFromStart();
        }

    }
    // to let main thread know how many have been processed so far:
    //BGDEBUG << "ShingleThread Exitting";
}

class bgShingleClass
{
    bgQueue<bgShingleNotification*>& bgqueue;
public:
    bgShingleClass(bgQueue<bgShingleNotification*>& queue) : bgqueue(queue) {}

#if BG_BOOST_LOCKLESS
    void runlockfree() {
        bgShingleNotification *note;

        while (!shingledone) {
            BGDEBUG << "Lockfree shingler waiting for items in queue...";

            while (shingleQueue.pop(note)) {
                bgShingle(note);
                ++shingle_counter;
                delete note;
            }
        }

        BGDEBUG <<format("ShingleWorker shingle counter == %u")
            % (unsigned int)shingle_counter;

        while (shingleQueue.pop(note)) {
            bgShingle(note);
            ++shingle_counter;
            delete note;

            BGDEBUG <<format("ShingleWorker shingle counter == %u")
                % (unsigned int)shingle_counter;
        }

        BGDEBUG << format("ShingleThread exitting %d") % shingle_counter;
    }
#endif

    void run() {

        bgShingleNotification *note;

        while (!shingledone) {
            BGDEBUG << "ShingleWorker waiting for items in queue...";

            //while (shingleQueue.pop(note)) {
            bgqueue.wait_and_pop(note);
            if (note) {
                bgShingle(note);
                ++shingle_counter;
                delete note;
            }
        }

        BGDEBUG <<format("ShingleWorker shingle counter == %u")
            % (unsigned int)shingle_counter;

        while (bgqueue.try_pop(note)) {
            bgShingle(note);
            ++shingle_counter;
            delete note;

            BGDEBUG <<format("ShingleWorker shingle counter == %u")
                % (unsigned int)shingle_counter;
        }

        BGDEBUG << format("ShingleThread exitting %d") % shingle_counter;
    }
};





class bgCompressClass
{
    bgQueue<bgCompressNotification*>& bgqueue;
    bgQueue<bgWriteNotification*>& bgwritequeue;
public:
    bgCompressClass(bgQueue<bgCompressNotification*>& queue,
                    bgQueue<bgWriteNotification*>& wqueue)
        : bgqueue(queue), bgwritequeue(wqueue) {}

    void bgCompress(
        bgCompressNotification *cnote)
    {
        uint32_t   ngram_order(cnote->ngram_order);
        uint32_t   ngram(cnote->ngram);
        vec_u32_t  *fids(cnote->fids);
        vec_u32_t &file_ids(*fids);
        // compressed data will go here, ptr to reduce copies, writer will free
        vec_u8_t *cdataptr(new vec_u8_t());
        // ref version for local ease of access via . instead of ->
        vec_u8_t &cdata(*cdataptr);
        // we'll reuse this a bit...
        VarByteUInt< uint32_t > vbyte(0);
        uint32_t num_files(file_ids.size());
        PFORUInt< uint32_t > pfor(PFOR_blocksize,PFOR_maxexceptions);
        uint32_t uncsz(num_files*sizeof(uint32_t));

        //  [4 byte ngram][4 byte size of compressed data]              \
        // [id#1 varbyte][rest of ids in delta format, compressed PFOR or VarByte]
        // make sure the file ids are sorted:
        sort(file_ids.begin(),file_ids.end());
        // then modify file id data to be deltas after the first value
        // (for nicer compression characteristics)

        BGDEBUG << format("CompressThread, ngram %08x %u files") %
            ngram % num_files;

        if (num_files == 0) {
            // not sure how this could happen...
            BGERR << "zero files? something seriously wrong, bailing";
            exit(99);
        }

        if (num_files > 1) {
            pfor.convert_to_deltas(file_ids);
        }
        // let's preallocate some space in the compressed data vector, just
        // to hopefully save a little time on reallocating at the beginning
        // of processing each list, maybe 50% of the uncompressed size:
        cdata.reserve(uncsz/2);

        // now compress...first value is always VarByte encoded:
        vec_u8_t &vbyte_data(vbyte.encode(file_ids[0]));
        cdata.insert(cdata.end(),vbyte_data.begin(),vbyte_data.end());
        uint32_t size_of_initial_varbyte_value(cdata.size());

        BGDEBUG << format("CompressThread, ngram %08x first file id"
                          " (%u) VarByte compressed to %u bytes")
            % ngram % file_ids[0] % size_of_initial_varbyte_value;

        // then for the remainder attempt PFOR first then fall back to
        // VarByte if that fails.  As an all or nothing situation that kind
        // of sucks, would probably be better to be able to blend
        // compression techniques for the same list, but that requires
        // additional overhead for each block that for the "normal" case we
        // really don't need...

        bool pfor_encoded(false);
        uint32_t num_pads(0);

        // but first...is the block big enough to bother w/ PFOR encoding?
        if ((num_files-1) >= PFOR_threshold) {
            uint32_t numblocks((num_files-1)/PFOR_blocksize);
            BGDEBUG << format("CompressThread, ngram %08x candidate "
                              "for PFOR encoding (%u blocks?)")% ngram % numblocks;
            // and do we need to pad w/ zeros (works for us because we are
            // doing a delta encoding) to get to the blocksize?
            if (((num_files-1) % PFOR_blocksize) != 0) {
                ++numblocks;
                num_pads = PFOR_blocksize - ((num_files-1) % PFOR_blocksize);
                BGDEBUG << format("CompressThread, ngram %08x "
                                  "padding  file_ids (only %u in"
                                  " last block, adding %u pads) for"
                                  " PFOR encoding (%u blocks total)")
                    % ngram % ((num_files-1) % PFOR_blocksize) %num_pads % numblocks;
                num_files += num_pads;
                file_ids.resize(num_files, 0);
            }
            // now need to deal w/ each block, and concat the data if successful
            vec_u32_t::iterator iter(++file_ids.begin());
            // first one varbyte encoded, start w/ the 2nd one
            uint32_t blockcount(0);

            while (blockcount < numblocks) {
                auto_ptr < vec_u8_t > block_data(pfor.encode(
                                                    iter,iter+PFOR_blocksize));
                // if we got a NULL back from that, there was a problem
                // meeting our constraints, so fall back to VarByte
                pfor_encoded = (block_data.get() != NULL);
                if (pfor_encoded) {
                    // append to our PFOR encoded data to the cdata object
                    //we'll pass of to the write thread later
                    cdata.insert(cdata.end(), (*block_data).begin(),
                                 (*block_data).end());
                } else {
                    if (num_pads) {
                        // need to back that padding out...
                        num_files -= num_pads;
                        file_ids.resize(num_files);
                    }
                    BGDEBUG << format("CompressThread, ngram %08x "
                                      "block %u failed PFOR encoding"
                                      " (errorcode == %u)")
                        % ngram % blockcount % pfor.last_errorcode;
                    // well, something went wrong, so nuke the PFOR cdata contents,
                    // as we'll be refilling it w/ VarByte below...
                    cdata.resize(size_of_initial_varbyte_value);
                    break;
                }
                ++blockcount;
                iter += PFOR_blocksize;
            }
        } else {
            BGDEBUG << format("CompressThread, ngram %08x not enough file_ids "
                              "for PFOR encoding (%u)") % ngram % num_files;
        }

        if (!pfor_encoded && file_ids.size() > 1) {
            BGDEBUG << format("CompressThread, ngram %08x falling "
                              "back to VarByte encoding") % ngram;
            for (vec_u32_t::iterator iter(++file_ids.begin());
                 iter != file_ids.end(); ++iter)
            {
                vec_u8_t &vbyte_data(vbyte.encode(*iter));
                cdata.insert(cdata.end(), vbyte_data.begin(), vbyte_data.end());
            }
        }

        BGDEBUG << format("CompressThread, ngram %08x, %u file ids,"
                          " compressed size %u (%4.2f%% smaller, used %s)")
            % ngram % file_ids.size() % cdata.size() %
            (100.0 * (((4.0 * file_ids.size()) - cdata.size())
                      /(4.0 * file_ids.size())))
            % (pfor_encoded ? "PFOR":"VarByte");

        // clean up a little
        //delete fids;
        delete cnote->fids;
        delete cnote;
        // send compressed data pointer to write thread:
        bgWriteNotification *note = new bgWriteNotification(ngram_order, ngram,
                                                            cdataptr, uncsz,
                                                            pfor_encoded);
        if (run_lock_free) {
#if BG_BOOST_LOCKLESS
            writeQueue.push(note);
#endif
        } else {
            bgwritequeue.push(note);
        }
    }

#if BG_BOOST_LOCKLESS
    void runlockfree() {

        bgCompressNotification *note;

        while (!compressdone) {

            while (compressQueue.pop(note)) {
                bgCompress(note);
                ++compress_counter;
            }
        }

        BGDEBUG <<format("CompressWorker compress counter == %u")
            % (unsigned int)compress_counter;

        while (compressQueue.pop(note)) {
            bgCompress(note);
            ++compress_counter;

            BGDEBUG <<format("CompressWorker compress counter == %u")
                % (unsigned int)compress_counter;
        }

        BGDEBUG << format("CompressThread exitting %d") % compress_counter;
    }
#endif

    void run() {

        bgCompressNotification *note;

        while (!compressdone) {
            //while (compressQueue.pop(note)) {
            //while (bgqueue.wait_and_pop(note)) {
            bgqueue.wait_and_pop(note);

            if (note) {
                bgCompress(note);
                ++compress_counter;
            }
        }

        BGINFO <<format("CompressWorker compress counter == %u")
            % (unsigned int)compress_counter;

        //bgqueue.wait_and_pop(note);
        //while (bgqueue.wait_and_pop(note)) {
        if (bgqueue.try_pop(note)) {
            bgCompress(note);
            ++compress_counter;

            BGDEBUG <<format("CompressWorker compress counter == %u")
                % (unsigned int)compress_counter;
        }

        BGDEBUG << format("CompressThread exitting %d") % compress_counter;
    }
    //private:


};



class WriteWorker
{
    uint32_t total_ngrams;
    uint32_t total_pfor_encoded;
    uint64_t total_uncompressed_bytes;
    uint64_t total_compressed_bytes;
    uint32_t last_ngram_written;
    bool first_write;
    bgQueue<bgWriteNotification*> *bgwritequeue;
public:
    bool error;
    WriteWorker(bgQueue<bgWriteNotification*> *wqueue) {
        _next_ngram_counter = 0;
        _last_index = 0;
        total_ngrams = 0;
        total_pfor_encoded = 0;
        total_uncompressed_bytes = 0;
        total_compressed_bytes = 0;
        last_ngram_written = 0;
        first_write = true;
        error = false;
        bgwritequeue = wqueue;
        BGDEBUG << "opening index output file";

        //don't init the ostringstream, because the first << overwrites that...
        ostringstream fnm;
        fnm << index_prefix << ".bgi";
        _index_dat.open(fnm.str().c_str());
        if (!_index_dat) {
            /* Could not open output file, exit */
            BGERR << format("Could not open output file %s. Exiting.") %
                fnm.str().c_str();
            error = true;
        }
    }

    class WriteBufferData
    {
    public:
        uint32_t ngram;
        bool pfor_encoded;
        vec_u8_t *cdataptr;
        uint32_t uncompressed_size;
    };


    void writer(
        bgWriteNotification   *note,
        bgi_header            *header)
    {
        uint32_t ngram_order(note->ngram_order);
        uint32_t ngram(note->ngram);
        uint32_t uncompressed_size(note->uncompressed_size);
        vec_u8_t *cdataptr(note->cdata);
        bool pfor_encoded(note->pfor);
        WriteBufferData write_data;
        bool keepwriting(true);

        BGDEBUG << format("WriteWorker got ngram_order %u (%08x)")
            % ngram_order % ngram;

        write_data.ngram = ngram;
        write_data.pfor_encoded = pfor_encoded;
        write_data.cdataptr = cdataptr;
        write_data.uncompressed_size = uncompressed_size;
        _write_buffer[ngram_order] = write_data;

        while (keepwriting) {
            map< uint32_t, WriteBufferData >::iterator iter(
                _write_buffer.begin());

            if (iter != _write_buffer.end() &&
                iter->first == _next_ngram_counter)
            {

                WriteBufferData wbdata(iter->second);
                uint32_t ngram(wbdata.ngram);
                // calc hint index based on hint type
                uint32_t idx(header->ngram_to_hint(ngram));
                vec_u8_t &cdata(*(wbdata.cdataptr));
                uint32_t uncompressed_size(wbdata.uncompressed_size);
                bool pfor_encoded(wbdata.pfor_encoded);
                uint32_t sz(cdata.size());

                total_ngrams += 1;
                total_pfor_encoded += int(pfor_encoded);
                total_uncompressed_bytes += uncompressed_size;
                total_compressed_bytes += sz;

                if (sz >= 0x80000000) {
                    // too big for us to deal with, bail
                    BGERR << "size of compressed data too big!";
                    exit(1);
                }
                // set high order bit of sz if we are pfor encoded... actually,
                // let's switch to LSB of sz instead, so we can VarByte encode
                // it better
                sz <<= 1;
                if (pfor_encoded) {
                    sz |= 0x00000001;
                }

                if ((total_ngrams % 0xFFFFF) == 0) {
                    BGINFO << format("WriteWorker writing ngram %08x") % ngram;
                }

                VarByteUInt< uint32_t > vbyte(sz);
                vec_u8_t &cszdata(vbyte.encode());

                // write entry to ngrams.dat, old format:
                //  [4 byte ngram]
                //  [4 byte size of compressed data w/ MSB set for PFOR]
                //  [id#1 varbyte]
                //  [rest of ids in delta format, compressed PFOR or VarByte]
                // new format:
                //  [VarByte encoded size, shifted left 1 w/ LSB set for PFOR]
                //  [id#1 Varbyte][delta list, PFOR/VarByte]

                vec_u8_t zbuf;
                if (!first_write) { // most common case
                    if (ngram != (last_ngram_written+1)) {
                        // pad out
                        zbuf.resize(ngram - (last_ngram_written + 1) , 0);
                    }
                } else {
                    first_write = false;
                    // and pad out if ngram is > 0
                    if (ngram > 0) {
                        zbuf.resize(ngram,0);
                    }
                }

                if (zbuf.size() > 0) {
                    BGDEBUG << format("WriteWorker padding missing"
                                      " ngrams between last ngram"
                                      " %08x and current ngram "
                                      "%08x (%u of them)")
                        % last_ngram_written % ngram % zbuf.size();
                    _index_dat.write(reinterpret_cast<char const *>
                                     (&zbuf.front()),zbuf.size());
                }

                // if necessary, write entry to index.dat
                if (_last_index != idx ) {
                    _hints[idx] = _index_dat.tellp();
                    // need current file offset here...
                    // actually,need to adjust that if it really is in
                    //the middle of padding we are adding...
                    //uint32_t lsbyte(ngram & 0x000000ff);
                    // this might be a nybble now, or zero:
                    uint32_t lsbyte(ngram & header->hint_type_mask());
                    if (zbuf.size() != 0 && lsbyte != 0) {
                        BGDEBUG << format("WriteWorker hint index"
                                          " entry padding adjustment: -%u") %
                            lsbyte;
                        _hints[idx] -= lsbyte;
                    }
                    BGDEBUG << format("WriteWorker adding hint "
                                      "index entry %08x: %016x")
                        % idx % _hints[idx];
                    _last_index = idx;
                }

                BGDEBUG << format("WriteWorker writing ngram %08x"
                                  " data size %u at offset %016x")
                    % ngram % cszdata.size()
                    % (uint64_t)_index_dat.tellp();

                _index_dat.write(reinterpret_cast<char const *>
                                (&cszdata.front()),cszdata.size());
                // can't just do << vector unfortunately,
                // and it'd probably give the wrong data anyways:
                _index_dat.write(reinterpret_cast<char const *>
                                 (&(cdata[0])),cdata.size());

                last_ngram_written = ngram;

                // clean up the compressed data memory
                delete wbdata.cdataptr;
                // remove the entry from the buffer
                _write_buffer.erase(iter);

                ++_next_ngram_counter;

            } else {
                keepwriting = false;
            }
        }
    }

    void run()
    {
        BGINFO << "WriteWorker run()";

        bgWriteNotification *note;
        bgi_header header(ngram_size);

        header.hint_type = hint_type; // set the hint type appropriately
        // if in debug mode, dump initial header:
        BGDEBUG << format("%s") % header.dump();
        // default val 0xFFFFFFFFFFFFFFFF to indicate that prefix not in index
        _hints.resize(header.num_hints(),UINT64_MAX);

        // write out empty header data for spacer, fill in real data later,
        // rewind, and write again...
        header.write(_index_dat);
        // write out empty hints data for spacer,
        // rewind & rewrite real data later...
        _index_dat.write(reinterpret_cast<const char*>(
                             &_hints.front()),_hints.size()*sizeof(uint64_t));
        // now we're in a (file) position to write out the actual index data...
        // record where that starts:
        _hints[0] = _index_dat.tellp(); // sizeof(header) + sizeof(_hints)

        while (!writerdone) {

            if (run_lock_free) {
#if BG_BOOST_LOCKLESS
                while (writeQueue.pop(note)) {
                    writer(note, &header);
                    ++write_counter;
                    delete note;
                }
#endif
            } else {
                bgwritequeue->wait_and_pop(note);
                if (note) {
                    writer(note, &header);
                    // let main thread know we finished one via counter increment:
                    ++write_counter;
                    delete note;
                }

            }
        }

        BGINFO <<format("WriteWorker write counter == %u")
            % (unsigned int)write_counter;

        if (run_lock_free) {
#if BG_BOOST_LOCKLESS
            while (writeQueue.pop(note)) {
                writer(note, &header);
                // let main thread know we finished one via counter increment:
                ++write_counter;
                BGDEBUG <<format("WriteWorker write counter after done  == %u")
                    % (unsigned int)write_counter;
                delete note;
            }
#endif
        } else {
            while(bgwritequeue->try_pop(note)) {
                writer(note, &header);
                // let main thread know we finished one via counter increment:
                ++write_counter;
                BGDEBUG <<format("WriteWorker write counter after done  == %u")
                    % (unsigned int)write_counter;
                delete note;
            }
        }

        BGINFO << format("WriteThread done w/ ngrams.dat, wrote %u ngrams,"
                         " %u pfor encoded, %lu uncompressed,"
                         " %lu compressed (ratio %f)")
            % total_ngrams % total_pfor_encoded % total_uncompressed_bytes
            % total_compressed_bytes
            % (float(total_compressed_bytes)/total_uncompressed_bytes);

        // write out final index data now, first correct header values:
        header.fileid_map_offset = _index_dat.tellp();
        header.num_ngrams = write_counter;
        header.num_files = id_to_fname.size();
        if (compress_data) {
            header.fmt_minor = 2;
        }
        header.pfor_blocksize = PFOR_blocksize;
        BGDEBUG << "WriteThread saving fileid map";

        { /* need this curly brace here so that filtering_streambuf deconstructs
             before the file is closed */
            io::filtering_streambuf<io::output> out;
            if (compress_data) {
                out.push(io::zlib_compressor());
                out.push(_index_dat);
            }

            for (uint32_t i(0); i < header.num_files; ++i) {
                BGDEBUG << format("about to call StrFormat on %d") % i;
                string msg(StrFormat("%010u %s",i,id_to_fname[i].c_str()));
                BGDEBUG << msg;
                if (compress_data) {
                    ostream os(&out);
                    os << msg << endl;
                } else {
                    _index_dat << msg << endl;
                }
            }
        }

        uint64_t final_index_size(_index_dat.tellp());

        BGDEBUG << "WriteThread fixing header data";

        _index_dat.seekp(0,ios::beg);
        header.write(_index_dat);
        header.dump();

        BGDEBUG << "WriteThread saving index hint data";
        _index_dat.write(reinterpret_cast<const char*>
                         (&_hints.front()),_hints.size()*sizeof(uint64_t));

        _index_dat.close();
        BGDEBUG << "WriteThread done saving index data, exitting";
    }

    // private:
    // uh, how is this different than my total_ngrams counter?
    uint32_t _next_ngram_counter;
    uint32_t _last_index;
    vec_u64_t _hints;
    map< uint32_t, WriteBufferData > _write_buffer;
    ofstream _index_dat;
};

void version()
{

    std::cerr << "bgindex version " << VERSION << std::endl;
    std::cerr << "(c) 2011-2017 Carnegie Mellon University " << std::endl;
    std::cerr << "Government Purpose License Rights (GPLR) pursuant to "
        "DFARS 252.227-7013" << std::endl;
    std::cerr << "Post issues to https://github.com/cmu-sei/BigGrep" << std::endl;
}



struct option long_options[] =
    {
        {"ngram",required_argument,0,'n'},
        {"hint-type",required_argument,0,'H'},
        {"blocksize",required_argument,0,'b'},
        {"exceptions",required_argument,0,'e'},
        {"minimum",required_argument,0,'m'},
        {"max-unique-ngrams",required_argument,0,'M'},
        {"overflow",required_argument,0,'O'},
        {"prefix",required_argument,0,'p'},
        {"stats",required_argument,0,'s'},
        {"sthreads",required_argument,0,'S'},
        {"cthreads",required_argument,0,'C'},
        {"stats",required_argument,0,'s'},
        {"count",no_argument,0,'c'},
        {"compress",no_argument,0,'z'},
        {"verbose",no_argument,0,'v'},
        {"debug",no_argument,0,'d'},
        {"log",required_argument,0,'l'},
        {"version",no_argument,0,'V'},
#if BG_BOOST_LOCKLESS
        {"lock",no_argument,0,'L'},
#endif
#ifdef ALLOW_TRACING
        {"trace",no_argument,0,'t'},
#endif
        {"help",no_argument,0,'h'},
        {0,0,0,0}
    };



int bgParseOptions(
    int argc,
    char **argv)
{
    int ch;
    int option_index;
    bool got_hint_type(false);

    BGSETPROCESSNAME("bgindex");
    BGSETWARNLOGLEVEL;

    while ((ch = getopt_long(argc, argv, "n:b:e:m:M:O:vzLVdthcp:S:C:l:",
                             long_options,&option_index)) != -1)
    {
        switch (ch)
        {
          case 'n':
            ngram_size = atoi(optarg);
            // better be 3 or 4
            if (ngram_size < 3 || ngram_size > 4) {
                // error
                BGERR << "Invalid NGRAM size: "
                    "BGINDEX only handles 3 or 4 grams";
                help();
                return 1; // bail
            }
            break;
          case 'H':
            got_hint_type = true;
            hint_type = atoi(optarg);
            // 0-2
            if (hint_type < 0 || hint_type > 2) {
                // error
                BGERR << "Invalid Hint type: "
                    "BGINDEX only handles hint type of 0-2";
                help();
                return 1; // bail
            }
            break;
          case 'b':
            PFOR_blocksize = atoi(optarg);
            // better be a power of 2
            break;
          case 'e':
            PFOR_maxexceptions = atoi(optarg);
            break;
          case 'm':
            PFOR_threshold = atoi(optarg);
            break;
          case 'M':
            max_uniq_ngrams = atoi(optarg);
            break;
          case 'O':
            overflow = fopen(optarg, "w");
            if (overflow == NULL) {
                BGERR << "Can not open overflow file " << optarg;
                return 1;
            }
            break;
          case 'S':
            num_shingle_threads = atoi(optarg);
            break;
          case 'C':
            num_compress_threads = atoi(optarg);
            break;
          case 'p':
            index_prefix = optarg;
            break;
          case 'v':
            BGSETINFOLOGLEVEL;
            break;
          case 'd':
            BGSETDEBUGLOGLEVEL;
            break;
          case 'z':
            compress_data = true;
            break;
          case 'c':
            shingle_and_exit = true;
            break;
#ifdef ALLOW_TRACING
          case 't':
            BGSETTRACELOGLEVEL;
            break;
#endif
          case 'l':
            bglog = fopen(optarg, "a");
            if (bglog == NULL) {
                BGERR << "Can not open file " << optarg;
                return 1;
            }
            BGLog2File::Stream() = bglog;
            break;
          case 'V':
            version();
            return 1;
#if BG_BOOST_LOCKLESS
          case 'L':
            run_lock_free = true;
            break;
#endif
          case 'h':
            help();
            return 1; // bail
          default: // should never get here?
            break;
        }
    }

    if ((argc - optind) != 0) {
        /* doesn't take any parameters other than options right now
           ...otherwise they'd be at argv[optind]*/
        help();
        return 1;
    }

    // set default hint_type appropriately if not given one:
    if (!got_hint_type && ngram_size==3) {
        hint_type = 1;
    }

    string program = argv[0];
    string execname = program.substr(program.find_last_of("b"));
    if (execname.compare("bgcount") == 0) {
        shingle_and_exit = true;
    }

    return 0;

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
            id_to_fname.push_back(fname);
        }
    }

    numfiles = id_to_fname.size();

    if (numfiles < 2) {
        BGERR << "Please provide two or more files for indexing at a time.";
        help();
        return numfiles;
    }

    return numfiles;
}



void
help()
{
    std::cerr << "Usage:" << std::endl;
    std::cerr << "bgindex [OPTS]" << std::endl << std::endl;
    std::cerr << "  bgindex reads a list of files on stdin to process,"
        " produces an N-gram inverted index" << std::endl << std::endl;
    std::cerr << "  OPTS can be:" << std::endl;
    std::cerr << "    -n, --ngram #\tDefine N for the N-gram "
        "(3 or 4, 3 is default)" << std::endl;
    std::cerr << "    -H, --hint-type #\tSpecify hint type "
        "(0-2, default 0 for n==4, 1 for n==3 )" << std::endl;
    std::cerr << "    -b, --blocksize #\tPFOR encoding blocksize "
        "(multiple of 8, default 32)" << std::endl;
    std::cerr << "    -e, --exceptions #\tPFOR encoding max exceptions"
        " per block (default 2)" << std::endl;
    std::cerr << "    -m, --minimum #\tPFOR encoding minimum number "
        "of entries to consider PFOR (default 4)" << std::endl;
    std::cerr << "    -M, --max-unique-ngrams #\tMaximum number of "
        "unique n-grams allowed per file" << std::endl;
    std::cerr << "    -O, --overflow FILE\tWrite filenames that "
        "surpass max-unique-ngram limit to given filename" << std::endl;
    std::cerr << "    -p, --prefix STR\tA prefix for the index file(s) "
        "(directory and/or partial filename)" << std::endl;
    std::cerr << "    -S, --sthreads #\tNumber of threads to use for "
        "shingling (default 4)" << std::endl;
    std::cerr << "    -z, --compress \tCompress file and metadata info"
              << std::endl;
    std::cerr << "    -C, --cthreads #\tNumber of threads to use for "
        "compression (default 5)" << std::endl;
#if BG_BOOST_LOCKLESS
    std::cerr << "    -L, --lock\t\tUse the Boost lockless queue "
        "implementation instead of the standard locking queue implementation." << std::endl;
#endif
    std::cerr << "    -v, --verbose\tShow some additional info while "
        "working" << std::endl;
    std::cerr <<"    -d, --debug\t\tShow more diagnostic information" <<std::endl;
#ifdef ALLOW_TRACING
    std::cerr << "    -t, --trace\tShow WAY TOO MUCH diagnostics "
        "(if compiled in)" << std::endl;
#endif
    std::cerr << "    -l, --log FILE\tProvide a log file for"
        " processing information" << std::endl;
    std::cerr<<  "    -V, --version\tReport version and exit"<< std::endl;

    std::cerr << "    -h, --help\t\tShow this help" << std::endl << std::endl;

}

uint32_t bgCompressAndWrite(
                            )
{

    boost::thread_group threadpool;
    bgQueue<bgCompressNotification*> compressLockQueue;
    bgQueue<bgWriteNotification*> writeLockQueue;
    uint32_t total_unique_ngrams(0);

    compress_counter = 0;
    write_counter = 0;
    compressdone = false;
    writerdone = false;

    for (int i(0); i < num_compress_threads; ++i) {
        bgCompressClass *compressor = new bgCompressClass(compressLockQueue, writeLockQueue);

        if (run_lock_free) {
#if BG_BOOST_LOCKLESS
            threadpool.create_thread(boost::bind(&bgCompressClass::runlockfree, compressor));
#endif
        } else {
            threadpool.create_thread(boost::bind(&bgCompressClass::run, compressor));
        }
    }

    boost::thread *write_thread;

    WriteWorker *writer = new WriteWorker(&writeLockQueue);
    if (writer->error) {
        compressdone = true;
        if (!run_lock_free) {
            compressLockQueue.quit();
        }
        threadpool.join_all();
        exit(1);
    }

    write_thread = new boost::thread(boost::bind(&WriteWorker::run, writer));

    // LoserTree based merge here!
    lt->build_tree();
    BGINFO << "Built Loser Tree, Pulling data";
    lt->dump();

    vec_u32_t* fids(new vec_u32_t());
    uint32_t last_ngram(UINT32_MAX);

    if (!lt->empty) {
        last_ngram = lt->root->peek_node_data().first;
    }
    BGDEBUG << format("Loser Tree first root ngram %08x") % last_ngram;

    uint32_t num_ngrams_processed(0);
    while (! lt->is_empty()) {
        lt->dump(); // diags, only in trace mode
        // get root ngram & file id (will update tree as well)
        pair< uint32_t, uint32_t> rdata(lt->get_root_data());
        uint32_t ngram(rdata.first);
        uint32_t id(rdata.second);
        BGDEBUG << format("Loser Tree root ngram %08x from file id %u")
            % ngram % id;
        if (ngram != last_ngram) { // a new ngram?
            if (ngram < last_ngram) {
                BGWARN << format("merge megafail, current ngram (%08x) < "
                                 "last ngram (%08x)") % ngram % last_ngram;
                exit(88);
            }
            if ((num_ngrams_processed++ % 0xFFFFF) == 0) {
                BGINFO << format("merge sending ngram %08x to compress"
                                 " (%u file ids)")
                    % last_ngram % fids->size();
            }

            BGTRACE << format("Merge sending ngram %08x to compress "
                              "(%u file ids)") % last_ngram % fids->size();
            //Wait for the write queue to catch up a bit
            if (compress_counter > write_counter + 50000) {
                BGINFO << format("Waiting for writer to catch up c:%d "
                                 "w:%d")
                    % (int)compress_counter % (int)write_counter;

                while (compress_counter > write_counter + 10000) {
                    usleep(10);
                }
                BGDEBUG << format("writer has caught up c:%d w:%d") %
                    (int)compress_counter % (int)write_counter;
            }

            bgCompressNotification* note = new bgCompressNotification(
                total_unique_ngrams, last_ngram, fids);
            if (run_lock_free) {
#if BG_BOOST_LOCKLESS
                compressQueue.push(note);
#endif
            } else {
                compressLockQueue.push(note);
            }
            fids = new vec_u32_t();
            last_ngram = ngram;
            ++total_unique_ngrams;
        }

        fids->push_back(id);
    }

    if (fids->size() > 0) { // should always be true
        // send to compress queue
        BGINFO << format("merge sending FINAL ngram %08x to compress "
                          "(%u file ids)") % last_ngram % fids->size();
        // current total ngrams == current ngram order

        bgCompressNotification *note = new bgCompressNotification(
            total_unique_ngrams, last_ngram, fids);
        if (run_lock_free) {
#if BG_BOOST_LOCKLESS
            compressQueue.push(note);
#endif
        } else {
            compressLockQueue.push(note);
        }
        ++total_unique_ngrams;
    }

    compressdone = true;
    if (!run_lock_free) {
        compressLockQueue.quit();

    }

    BGINFO << "waiting for compress & write threads to finish...";
    threadpool.join_all();

    writerdone = true;
    if (!run_lock_free) {
        writeLockQueue.quit();
    }
    write_thread->join();

    delete writer;

    return total_unique_ngrams;

}




int main(
    int argc,
    char** argv)
{
    bgstopwatch stimer;
    uint32_t numfiles;
    int total_files;
    int max_ngram_files(0);
    int missing_files(0);
    uint32_t total_unique_ngrams(0);
    boost::thread_group threadpool;

#ifdef GLIBC_MALLOC_BUG_WORKAROUND
    // Chuck's MALLOC bug fix
    int mrc = mallopt(M_MMAP_MAX,0);
    if (mrc != 1) {
        perror("mallopt error?\n");
        exit(1);
    }
#endif // GLIBC_MALLOC_BUG_WORKAROUND

    if (bgParseOptions(argc, argv)) {
        return 1;
    }

    stimer.restart();

    numfiles = bgReadFiles();

    BGDEBUG << boost::format("took %f sec to read %u file names")
        % stimer.secondsFromStart() % numfiles;

    shingle_counter = 0;

    fds.reserve(numfiles);

    bgQueue<bgShingleNotification*> shingleLockQueue;

    for (int i(0); i < num_shingle_threads; ++i) {
        bgShingleClass *newworker = new bgShingleClass(shingleLockQueue);
        if (run_lock_free) {
#if BG_BOOST_LOCKLESS
            threadpool.create_thread(boost::bind(&bgShingleClass::runlockfree, newworker));
#endif
        } else {
            threadpool.create_thread(boost::bind(&bgShingleClass::run, newworker));
        }
    }

    for (unsigned int i(0); i < numfiles; ++i) {
        // strip off metadata from the filename before passing along (it'll be
        // written out from id_to_fname at the end):
        vector< string >fname_and_metadata;
        boost::split(fname_and_metadata,id_to_fname[i],boost::is_any_of(","));
        BGDEBUG << boost::format("file name '%s' metadata length: %d")
            % fname_and_metadata[0].c_str() % (fname_and_metadata.size()-1);
        FileData *fd(new FileData(i));
        fds.push_back(fd);
        bgShingleNotification *snote = new bgShingleNotification(
            fname_and_metadata[0], i, fd);
        if (run_lock_free) {
#if BG_BOOST_LOCKLESS
            shingleQueue.push(snote);
#endif
        } else {
            shingleLockQueue.push(snote);
        }
    }

    shingledone = true;
    if (!run_lock_free) {
        shingleLockQueue.quit();
    }

    BGDEBUG << format("Took %f sec to start threads and enqueue %u files")
        % stimer.secondsFromLast() % numfiles;

    threadpool.join_all();

    BGWARN << format("MAIN: shingling appears done, took %f sec, "
                     "starting compress & write threads")
        % stimer.secondsFromLast();


    // close overflow file now
    if (overflow) {
        fclose(overflow);
    }

    uint32_t newid;
    for (int i(0); i < numfiles; i++) {
        if (fds[i]->missing ) {
            missing_files++;
            id_to_fname[i]="";
        } else if (fds[i]->hit_ngram_limit) {
            max_ngram_files++;
            id_to_fname[i]="";
        } else {
            newid = i - (missing_files + max_ngram_files);
            if (newid != i) {
                id_to_fname[newid] = id_to_fname[i];
                id_to_fname[i]="";
            }
        }
    }

    total_files = numfiles - (missing_files + max_ngram_files);

    LoserTree llt(total_files);
    lt = &llt;

    int fc(0);
    for (int i(0); i < numfiles; i++) {
        if (!fds[i]->missing && !fds[i]->hit_ngram_limit) {
            lt->nodes[fc].fd = fds[i];
            lt->nodes[fc].fd->id = fc;
            fc++;
        }
    }

    if (total_files < 2) {
        BGERR << format("%d of %d files found.  Please provide two or more "
                        "files for indexing at a time.")
            % total_files % numfiles;
        BGERR << format("%d files missing. %d files > %d")
            % missing_files % max_ngram_files % max_uniq_ngrams;
        exit(1);
    }

    id_to_fname.resize(total_files);

    BGWARN << format("%d files valid after shingling") % total_files;

    if (shingle_and_exit) {
        BGINFO << "Exiting due to --count switch";
        return 0;
    }

    total_unique_ngrams = bgCompressAndWrite();

    BGWARN << format("Compress and Write Threads done, took %f sec to merge %u files") %
        stimer.secondsFromLast() % total_files;

    BGWARN << format("%u unique ngrams total") % total_unique_ngrams;

    float total_time = stimer.secondsFromStart();

    BGWARN << format("DONE! Total runtime %f sec (%f min), estimated RSS size %f GB")
        % total_time % (total_time/60.0) % get_mem_usage();

    for (int i(0); i < numfiles; i++) {
        delete fds[i];
    }

    return 0;


}
