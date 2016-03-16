// Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

#define __STDC_LIMIT_MACROS // I have to define this to get UINT32_MAX because of the ISO C99 standard, apparently
// #define __STDC_CONSTANT_MACROS // don't really need these right now
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <string.h>

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

#include <sys/time.h>
#include <sys/resource.h> // these two for getrusage calls to track mem consumption...

// not using the macros in endian.h currently, but probaly should to make this
// code and the indexes portable between big endian and little endian
#if 0
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <endian.h> 
#endif

using namespace std;

// using POCO for Thread & ThreadPool:
#include "Poco/Notification.h"
#include "Poco/NotificationQueue.h"
#include "Poco/ThreadPool.h"
#include "Poco/Runnable.h"
#include "Poco/SharedPtr.h"
#include "Poco/AutoPtr.h"

#include "Poco/ScopedLock.h"
#include "Poco/Mutex.h"

// might use the Application class and it's associated OptionSet parsing later to augment/replace the main loop...
//#include "Poco/Util/Application.h"
//#include "Poco/Util/OptionSet.h"
// might use Logging later too?
// #include "Poco/Logger.h

#include "Poco/ConsoleChannel.h"
#include "Poco/FormattingChannel.h"
#include "Poco/PatternFormatter.h"
#include "Poco/Logger.h"
#include "Poco/LogStream.h"

// nice wrapper for logging, except there appears to be some sort of an issue
// using the LogStream interfaces through these macros (the streams themselves
// or my usage of them appear to be not thread safe):
#include "LogUtils.h"

// utility class to do sprintf type work to create a std::string
#include "StrFormat.hpp"

// utility class to do time deltas
#include "Stopwatch.hpp"

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

// to remove the trace stuff entirely:
//#define ALLOW_TRACING
#ifndef ALLOW_TRACING
#undef poco_trace
#define poco_trace(x,y)
#endif

LOG_DEFINE_MODULE_DEFAULT("BGINDEX_TH");
// since I'm not using the stream based stuff in LogUtils.h, I could just do this here:
//static Poco::Logger& logger() { static Poco::Logger& logger = Poco::Logger::get("bgindex_th"); return logger; }

// note, this code only deals w/ 3-grams or 4-grams...this is the default if not passed in:
#define NGRAM_SIZE 3
//#define NGRAM_SIZE 4
uint32_t ngram_size(NGRAM_SIZE);

// The maximum of unique ngrams to allow (default to upper limit of 4-gram space)
uint64_t max_uniq_ngrams(4294967296);

// these are options to:
#define NUM_SHINGLE_THREADS 4
//#define NUM_MERGE_THREADS 5
#define NUM_COMPRESS_THREADS 5

uint32_t num_shingle_threads(NUM_SHINGLE_THREADS);
uint32_t num_compress_threads(NUM_COMPRESS_THREADS);

//#define PFOR_BLOCKSIZE 128
//#define PFOR_MAXEXCEPTIONS 12
// min number of things to consider PFOR encoding:
//#define PFOR_THRESHOLD 32

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
string stats_filename;

// stringification macros:
//#define xstr(s) str(s)
//#define str(s) #s

//Poco::AtomicCounter<uint32_t> merge_counter;
BGAtomicCounter shingle_counter;
BGAtomicCounter compress_counter;
BGAtomicCounter write_counter;

// some useful type definitions:
typedef vector< uint64_t > vec_u64_t;
typedef vector< uint32_t > vec_u32_t;
typedef vector< uint8_t > vec_u8_t;
typedef vector< string > vec_str_t;

//typedef map< uint32_t, vector< uint32_t > > map_u32_to_vu32_t;
//typedef vector< map_u32_to_vu32_t > vec_map_u32_to_vu32_t;
//typedef map_u32_to_vu32_t ngram_to_id_map_t;
//typedef vec_map_u32_to_vu32_t vec_ngram_to_id_map_t;

//typedef map< uint32_t, vector< uint8_t > * > map_u32_to_vec_u8p_t;
//typedef map_u32_to_vec_u8p_t compressed_data_map_t;

typedef map< uint32_t, vec_u32_t * > map_u32_to_vu32p_t;

typedef pair< uint32_t, vec_u32_t * > pair_ngram_to_ids_t;
typedef vector< pair_ngram_to_ids_t > ngrams_to_ids_t;
typedef vector< ngrams_to_ids_t > vec_ngrams_to_ids_t;

vec_str_t id_to_fname;

uint32_t total_unique_ngrams(0);
uint64_t total_extracted_ngrams(0);

// merge sort a vector ([fileid]) of iterators (or pointers to) of sorted ngram (uint32_t) vectors

class LoserTree;
LoserTree *lt(NULL);

// utility function to get maxrss value, converted to GB
double
get_mem_usage(void)
{
  int rc(0);
  struct rusage ru;
  rc = getrusage(RUSAGE_SELF,&ru);
  return float(ru.ru_maxrss)/(1024*1024); // convert the returned value in KB to a floating point answer in GB
}



// FileData holds info about each file id & contents, plus state info for loser tree multiway merge
class FileData
{
public:
  uint32_t id; // file id
  vec_u32_t ngrams; // ngram vector
  //vec_u32_t::iterator cur; // current ngram...used during merge sort
  //vec_u32_t::iterator end; // to keep from calling ngrams.end() all the time during merge sort...
  uint32_t val; // current ngram value
  uint32_t cur_off; // offset into vector of that value
  uint32_t num; // total num
  bool have_vals;
  uint32_t *data_ptr; // to avoid operator[] and iterators, leverage the fact that the underlying data in a vector is contiguous...

  explicit
  FileData(uint32_t _id):
    id(_id), val(0), cur_off(0), num(0), have_vals(false), data_ptr(NULL) // reset when ngrams filled in (call fixup)
  { }

  inline
  void
  fixup()
  {
    num = ngrams.size();
    if (num) // better be...
    {
      data_ptr = &ngrams.front();
      val = data_ptr[0];
      have_vals = true;
    }
  }

  inline
  uint32_t
  peek_val()
  {
    return val;
  }

  inline
  uint32_t
  pop_val()
  {
    uint32_t rval(val);
    if (++cur_off < num)
    {
      // prep the next one
      val = data_ptr[cur_off];
    }
    else
    {
      have_vals = false;
      val = 0;
    }
    return rval;
  }

  inline
  void
  next_val()
  {
    if (++cur_off < num)
    {
      // prep the next one
      val = data_ptr[cur_off];
    }
    else
    {
      have_vals = false;
      val = 0;
    }
  }

};

class LoserTreeNode
{
public:
  LoserTreeNode *parent; // can be NULL for top of tree...
  LoserTreeNode *left; // can be NULL (leaf nodes & internal nodes that have been finished)
  LoserTreeNode *right; // same notes as left
  FileData *fd; // leaf nodes, this won't change, but higher in tree it will...
  uint32_t id; // just an id for this node that is only really used for diags (stays constant in tree, as opposed to fd->id)

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
    if (fd && fd->have_vals)
    {
      ret.first = fd->peek_val();
      ret.second = fd->id;
    }
    else
    {
      // return UINT32_MAX for lack of better choice right now, maybe use a custom return class w/ a flag?
      // actually 0 may be better, because that'll probably flag the "hey, we went backwards" check
      ret.first = ret.second = 0;
      poco_trace(logger(),StrFormat("peek_node_data node %u, fd == NULL",id));
    }
    poco_trace(logger(),StrFormat("peek_node_data node %u, ngram %08x, fid %u",id,ret.first,ret.second));
    return ret;
  }

  // find out the ngram & fid for a node, remove it & update tree
  inline
  pair< uint32_t, uint32_t >
  pop_node_data()
  {
    // only call this directly on the leaf nodes:
    //if (left != NULL || right != NULL)
    //{
    //  poco_error(logger(),"loser tree node pop_node_data called on non leaf node!");
    //}
    pair< uint32_t, uint32_t > ret(peek_node_data());
    poco_trace(logger(),StrFormat("pop_node_data node %u, ngram %08x, fid %u",id,ret.first,ret.second));
    if (fd->have_vals)
    {
      fd->next_val();
    }
    else // hit the end of the vals for this id, clean up a little...
    {
      poco_trace(logger(),StrFormat("pop_node_data node %u, at end of ngrams",id));
      vec_u32_t ().swap(fd->ngrams);
      fd->fixup();
      fd = NULL;
    }
    //fix_parent();
    //update_tree(); // this handled in the tree code now...
    return ret;
  }

};

class LoserTree
{
public:
  //vector< vector< T >::iterator * > v_p_iters;
  vector< LoserTreeNode > nodes; // one per file id/ngram vector
  LoserTreeNode *root;

  bool empty;

  uint32_t num;
  uint32_t numnodes;
  uint32_t levels;

  uint32_t last_fid_popped;

  bool
  is_empty()
  {
    if (!empty)
    {
      if (root->left == NULL || root->left->fd == NULL || !root->left->fd->have_vals)
      {
        if (root->right == NULL || root->right->fd == NULL || !root->right->fd->have_vals)
        {
          empty = true;
        }
      }
    }
    return empty;
  }

  inline
  void
  update_tree()
  {
    // now update fd values by calling fix_parent on every other leaf node
    poco_trace(logger(),StrFormat("update_tree called"));
    //nodes[last_fid_popped].fix_parent(true);
    // we'll handle this locally now...
    LoserTreeNode *node (nodes[last_fid_popped].parent);

    do
    {
      // need to look at our children, the left & right nodes...
      // get proper min vals from left & right (barring NULLs)

      FileData *new_fd(NULL);
      if (node->left && node->left->fd && node->left->fd->have_vals)
      {
        if (node->right && node->right->fd && node->right->fd->have_vals)
        {
          if (node->left->fd->val <= node->right->fd->val)
          {
            new_fd = node->left->fd;
          }
          else
          {
            new_fd = node->right->fd;
          }
        }
        else
        {
          new_fd = node->left->fd;
        }
      }
      else
      {
        if (node->right && node->right->fd && node->right->fd->have_vals)
        {
          new_fd = node->right->fd;
        }
        //else
        //{
          // both left & right are NULL, so we become NULL
        //}
      }

      node->fd = new_fd;

      //poco_trace(logger(),StrFormat("update_node, id %d, PROPAGATING!",id));
      node = node->parent; // fix_parent checks for parent != NULL
    } while (node != NULL);
  }

  void
  update_tree_full()
  {
    // now update fd values by calling fix_parent on every other leaf node
    poco_trace(logger(),StrFormat("update_tree_full called"));
    uint32_t mid((num+num%2)/2);
    uint32_t start(0);
    uint32_t end(num);
    for (uint32_t i(start);i<end;i+=2)
    {
      last_fid_popped=i;
      update_tree();
    }
    last_fid_popped = 0;
  }

  void
  build_tree() // better have filled in the leaf node data pointers prior to this...
  {
    // nodes 0 through num-1 are the leaf nodes, node numnodes-1 is the
    // root, so lets build it up from the leaf nodes...
    uint32_t p(num+num%2); // offset of current parent node
    uint32_t cnt(num);
    //bool first_time(true);
    uint32_t curnode(0);
    poco_trace(logger(),StrFormat("build_tree, num = %u, leaf cnt = %u, numnodes = %u",num,cnt,numnodes));
    //if there is only one node, this ugly hack constructs a three node tree (root + phantom + actual)
    /* Caution: this is an incomplete fix that breaks input: file1.exe\nmissing_file.exe\nfile2.exe
    if (cnt == 1)
    {
      nodes[numnodes-1].left= &nodes[0];
      nodes[0].parent = &nodes[numnodes-1];
      nodes[numnodes-1].right= &nodes[1];
      nodes[1].parent = &nodes[numnodes-1];
    }
    */
    while (cnt > 1)
    {
      for (uint32_t i(0);i<cnt;++i,++i)
      {
        poco_trace(logger(),StrFormat("build_tree, node = %u & node = %u parent = %u",curnode,curnode+1,p));
        nodes[curnode].id = curnode;
        nodes[curnode+1].id = curnode+1;
        nodes[curnode].parent = &nodes[p];
        nodes[curnode+1].parent = &nodes[p];
        nodes[curnode].parent->left = &nodes[curnode];
        nodes[curnode].parent->right = &nodes[curnode+1];
        // stick in "temporary" fd pointer for parent so initial tree fixup doesn't drop nodes?
        nodes[curnode].parent->fd = nodes[curnode].fd;
        ++p;
        curnode += 2;
      }
      cnt = (cnt + cnt%2)/2; // step up to next level...
      p += cnt%2;
    }
    // still need to fixup the root?
    //root->left = &nodes[numnodes-3];
    //root->right = &nodes[numnodes-2];
    root->id = nodes.size()-1;

    // make sure all of our fd->cur are correct...
    for (uint32_t i(0);i<num;i++)
    {
      //nodes[i].fd->id = i; // in case id's weren't set...
      //nodes[i+1].fd->id = i+1;
      if (nodes[i].fd != NULL)
        nodes[i].fd->fixup();
    }
    //update_tree();
    update_tree_full();
  }

  // returns: ngram, id then updates the tree...
  inline
  pair< uint32_t, uint32_t >
  get_root_data()
  {
    // better check if not empty before calling this...
    pair< uint32_t, uint32_t > ret(nodes[root->fd->id].pop_node_data());
    last_fid_popped = ret.second;
    update_tree();
    poco_trace(logger(),StrFormat("get_root_data ngram %08x, fid %u",ret.first,ret.second));
    if (root->left == NULL && root->right == NULL)
    {
      empty = true;
    }
    return ret;
  }

  // CTOR
  LoserTree(uint32_t _num = 0):
    num(_num), numnodes(1), empty(true), root(NULL)
  {
    if (num > 0) // better be! :)
    {
      // I need to be able to easily reference the nodes while I'm building
      // the tree, so I'll use Cory's meothod of dropping them all in the
      // nodes array for the build...  so, need the total count of nodes,
      // which is determined by making sure each level will be an even number
      // (except the root, of course):
      uint32_t cnt(num);
      uint32_t phantoms(0);
      uint32_t i(0);
      poco_debug(logger(),StrFormat("LoserTree for %u leaf nodes requested",num));
      //Special case to handle only a single file/node
      /* Caution: this is an incomplete fix that breaks input: file1.exe\nmissing_file.exe\nfile2.exe
      if (num == 1)
      {
        phantoms = 2;
        numnodes = 3;
      }
      */
      while (cnt > 1)
      {
        uint32_t n(cnt);
        uint32_t p(cnt%2);
        numnodes += n+p;
        cnt = (n+p)/2;
        phantoms += p;
        ++i;
        poco_debug(logger(),StrFormat("LoserTree level %u will have %u nodes and %u phantoms",i,n,p));
      }
      poco_debug(logger(),StrFormat("LoserTree going to have %u nodes (%u phantoms)",numnodes,phantoms));
      nodes.resize(numnodes);
      //build_tree();
      root = &nodes.back();
      empty = false;
    }
  }

  void
  dump_recursive(LoserTreeNode *node,uint32_t lvl,string& out)
  {
    string pad(3*lvl,' ');
    if (node && node->fd && node->fd->have_vals)
    {
      dump_recursive(node->right,lvl+1,out);
      uint32_t ngram(UINT32_MAX);
      uint32_t id(UINT32_MAX);
      ngram = node->fd->val;
      id = node->fd->id;
      string flag;
      if (node->id == root->id)
      {
        flag = "ROOT";
      }
      out += StrFormat("%s %u [%08x %u] %s\n",pad.c_str(),node->id,ngram,id,flag.c_str());
      dump_recursive(node->left,lvl+1,out);
    }
    else
    {
      out += StrFormat("%s N\n",pad.c_str());
    }
  }

  void
  dump()
  {
    if (logger().trace())
    {
      string out;
      dump_recursive(root,0,out);
      poco_debug(logger(),StrFormat("LoserTree dump:"));
      cerr << out << endl;
      poco_debug(logger(),"LoserTree dump done");
    }
  }
};

// Thread Pool & Event classes:

class ShingleNotification: public Poco::Notification
{
public:
  ShingleNotification(uint32_t id, string fname):
    _id(id), _fname(fname)
  {}
  string fname() const
  {
    return _fname;
  }
  uint32_t id() const
  {
    return _id;
  }
private:
  uint32_t _id;
  string _fname;
};

class CompressNotification: public Poco::Notification
{
public:
  CompressNotification(uint32_t ngram_order, uint32_t ngram, vec_u32_t *file_ids): 
    _ngram_order(ngram_order), _ngram(ngram), _file_ids(file_ids) {}
  ~CompressNotification() {}
  uint32_t _ngram_order;
  uint32_t _ngram;
  vec_u32_t *_file_ids;
};

class WriteNotification: public Poco::Notification
{
public:
  WriteNotification(uint32_t ngram_order, uint32_t ngram, vec_u8_t *cdata, uint32_t uncsz, bool pfor): 
    _ngram_order(ngram_order), _ngram(ngram), _cdata(cdata), _uncompressed_size(uncsz), _pfor(pfor) {}
  ~WriteNotification() {}
  uint32_t _ngram_order;
  uint32_t _ngram;
  vec_u8_t *_cdata;
  uint32_t _uncompressed_size;
  bool _pfor;
};

// workers:

class ShingleWorker: public Poco::Runnable
{
public:
  ShingleWorker(Poco::NotificationQueue &squeue):
    _queue(squeue)
  {}

  void run()
  {
    poco_debug(logger(),"ShingleWorker run()");
    //LOG_DEBUG << "ShingleWorker run()" << endl;
    bool done(false);
    while (!done)
    {
      poco_debug(logger(),"waiting for shingle notification");
      //LOG_DEBUG << "waiting for shingle notification" << endl;
      Poco::AutoPtr<Poco::Notification> pNf(_queue.waitDequeueNotification());
      poco_debug(logger(),"got shingle notification");
      //LOG_DEBUG << "got shingle notification" << endl;
      if (pNf)
      {
        ShingleNotification* pShingleNf = dynamic_cast<ShingleNotification*>(pNf.get());
        if (pShingleNf)
        {
          string fname(pShingleNf->fname());
          uint32_t id(pShingleNf->id());
          if (id == UINT32_MAX || fname == "")
          {
            done = true;
            continue;
          }

          Stopwatch stimer;
          poco_debug(logger(),StrFormat("Shingling %s (id: %u, shingle counter: %u)",fname.c_str(),int(id),int(shingle_counter)));
          //LOG_DEBUG << "Shingleing " << fname << " (" << id << ")" << endl;

          // open file (mmap) (would love to use C++ ifstream to open the file
          // & get the size & file descriptor for the mmap call, but sadly
          // there is no standard way to do the latter...sigh...so resorting
          // to the C interfaces for this...
          struct stat sb;
          off_t file_size(0);
          int f;
          unsigned char* fmem = NULL;
          bool missing_file(false);

          if (stat(fname.c_str(), &sb) == -1) {
            //poco_error(logger(),StrFormat("stat failed on '%s' (%d: %s)",fname.c_str(),errno,strerror(errno)));
            char buf[256];
            poco_error(logger(),StrFormat("stat failed on '%s' (%d: %s)",fname.c_str(),errno,strerror_r(errno,buf,256)));
            //exit(11);
            missing_file = true;
            // append ",missing=true" to metadata
            // should be safe to alter the string in id_to_fname multithreaded
            // without lock, because only one thread will ever see each entry...
            id_to_fname[id] = id_to_fname[id] + ",missing=true";
          }
          else
          {
            file_size = sb.st_size;

            // hm...should be able to use 64 bit file size stuff, no?  Why did
            // old code care?  Probably had either int or uint32_t for file size
            // storage somewhere?  Should probably make sure this code can
            // handle bigger files at some point and remove the upper restriction...
            if (file_size < 4 || file_size >= UINT_MAX) {
              poco_error(logger(),StrFormat("issue w/ file size on '%s' (%u)",fname.c_str(),file_size));
              //exit(12);
              missing_file = true; // better to treat as "missing" for now I think...
            }
            else
            {
              f = open(fname.c_str(), O_RDONLY);

              if ((fmem = (unsigned char *)mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, f, 0)) == MAP_FAILED) {
                char buf[256];
                poco_error(logger(),StrFormat("issue w/ mmap on '%s' (%s)",fname.c_str(),strerror_r(errno,buf,256)));
                exit(14);
              }
              // potential speedup by telling how we intend to access the mmapped pages:
              madvise((void*)fmem,file_size,MADV_SEQUENTIAL);
            }
          }
          lt->nodes[id].fd = NULL;
          if (!missing_file) {
            // walk over n-grams (all file size - ngram_size + 1 of them)
            unsigned ngram_count = missing_file ? 0 : (file_size - ngram_size + 1);
            FileData *fd(new FileData(id));
            lt->nodes[id].fd = fd;
            vec_u32_t &ngrams(fd->ngrams);
            ngrams.reserve(ngram_count);

            for (unsigned int fi(0); fi < ngram_count; ++fi)
            {
              uint32_t ngram(0);
              if (ngram_size == 4)
              {
                ngram = (*((uint32_t *)(&fmem[fi])));
              }
              else // must be 3, will read 4 bytes at a time for speed, but adjust...
              {
                // trim to 3 grams...
                if (fi < (ngram_count-1))
                {
                  ngram = (*((uint32_t *)(&fmem[fi])));
                  // because of little endian architecture, so as to not lose
                  // first byte in file, we need to remove the high order byte in
                  // the resulting integer:
                  ngram &= 0x00FFFFFF;
                }
                else
                {
                  ngram = (*((uint32_t *)(&fmem[fi-1]))); // and don't walk off the end of the mmap array!
                  // for the last one, we need to be sure and keep the high order
                  // byte instead (as it's the last byte in the file) for the same
                  // little endian reason:
                  ngram >>= 8;
                }
              }
              //poco_debug(logger(),StrFormat("ShingleWorker got ngram %08x from id %d, bucket %d",ngram,id,i));
              //(*(ngrams[i]))[ngram_cnt[i]] = ngram;
              ngrams.push_back(ngram);
            }

            // now we can clean up a little
            munmap(fmem, file_size);
            close(f);

            // sort & uniq
            sort(ngrams.begin(),ngrams.end());
            vec_u32_t::iterator it;
            it = unique(ngrams.begin(),ngrams.end());

            // can shrink down our ngram vector now, first remove the "non unique" stuff at the bottom of the vector
            ngrams.resize(it-(ngrams.begin()));
            // then, to really toss out extra mem, we have to create a new tmp vector & swap buffers:
            vec_u32_t (ngrams).swap(ngrams);

            fd->fixup();

            unsigned int unique_ngrams(ngrams.size());
            if (max_uniq_ngrams <= unique_ngrams) {
                poco_error(logger(),StrFormat("Shingled file %s contains too many (%u) unique %u-grams, rejecting from index.",fname.c_str(),unique_ngrams,ngram_size));
                missing_file = true;
                lt->nodes[id].fd = NULL;
            }

            poco_information(logger(),StrFormat("Shingled file %s (id %u) contains %u UNIQUE %u-grams out of %u total, took %f sec",fname.c_str(),int(id),unique_ngrams,ngram_size,int(ngram_count),stimer.secondsFromStart()));

            // store the number of unique n-grams in the metadata
            ostringstream numngrams;
            numngrams << ",unique_ngrams=" << unique_ngrams;
            id_to_fname[id] = id_to_fname[id] + numngrams.str();
          }
          // to let main thread know how many have been processed so far:
          ++shingle_counter;
        }
        else
        {
          // why would we ever get here?
          poco_debug(logger(),"ShingleThread null notification?");
        }
      }
      else
      {
        // probably woken up to say that we are done, so check for that...
        done = true;
      }
      poco_debug(logger(),"getting ready to wait for next file to shingle...");
    }
    poco_debug(logger(),"ShingleThread exitting");
  }

  //private:
  Poco::NotificationQueue &_queue;

};


class CompressWorker: public Poco::Runnable
{
public:
  CompressWorker(Poco::NotificationQueue &queue, Poco::NotificationQueue &write_queue):
    _queue(queue), _write_queue(write_queue)
  {}

  void run()
  {
    poco_debug(logger(),"CompressWorker run()");
    bool done(false);
    while (!done)
    {
      Poco::AutoPtr<Poco::Notification> pNf(_queue.waitDequeueNotification());
      if (pNf)
      {
        CompressNotification* pCompressNf = dynamic_cast<CompressNotification*>(pNf.get());
        if (pCompressNf)
        {
          uint32_t ngram_order(pCompressNf->_ngram_order);
          uint32_t ngram(pCompressNf->_ngram);
          vec_u32_t &file_ids(*(pCompressNf->_file_ids));

          vec_u8_t *cdataptr(new vec_u8_t()); // compressed data will go here, ptr to reduce copies, writer will free
          vec_u8_t &cdata(*cdataptr); // ref version for local ease of access via . instead of ->

          VarByteUInt< uint32_t > vbyte(0); // we'll reuse this a bit...

          // ngrams.dat format:
          //  [4 byte ngram][4 byte size of compressed data][id#1 varbyte][rest of ids in delta format, compressed PFOR or VarByte]

          // make sure the file ids are sorted:
          sort(file_ids.begin(),file_ids.end());
          // then modify file id data to be deltas after the first value (for nicer compression characteristics)
          uint32_t num_files(file_ids.size());
          poco_debug(logger(),StrFormat("CompressThread, ngram %08x %u files", ngram, num_files));
          if (num_files == 0)
          {
            // not sure how this could happen...
            poco_error(logger(),"zero files??? something seriously wrong, bailing");
            exit(99);
          }

          PFORUInt< uint32_t > pfor(PFOR_blocksize,PFOR_maxexceptions);

          if (num_files > 1)
          {
            pfor.convert_to_deltas(file_ids);
          }

          uint32_t uncsz(num_files*sizeof(uint32_t));

          // let's preallocate some space in the compressed data vector, just
          // to hopefully save a little time on reallocating at the beginning
          // of processing each list, maybe 50% of the uncompressed size:
          cdata.reserve(uncsz/2);

          // now compress...first value is always VarByte encoded:
          vec_u8_t &vbyte_data(vbyte.encode(file_ids[0]));
          cdata.insert(cdata.end(),vbyte_data.begin(),vbyte_data.end());
          uint32_t size_of_initial_varbyte_value(cdata.size());

          poco_debug(logger(),StrFormat("CompressThread, ngram %08x first file id (%u) VarByte compressed to %u bytes", ngram, file_ids[0], size_of_initial_varbyte_value));

          // then for the remainder attempt PFOR first then fall back to
          // VarByte if that fails.  As an all or nothing situation that kind
          // of sucks, would probably be better to be able to blend
          // compression techniques for the same list, but that requires
          // additional overhead for each block that for the "normal" case we
          // really don't need...
          bool pfor_encoded(false);
          uint32_t num_pads(0);
          // but first...is the block big enough to bother w/ PFOR encoding?
          if ((num_files-1) >= PFOR_threshold)
          {
            uint32_t numblocks((num_files-1)/PFOR_blocksize);
            poco_debug(logger(),StrFormat("CompressThread, ngram %08x candadite for PFOR encoding (%u blocks?)", ngram,numblocks));
            // and do we need to pad w/ zeros (works for us becuase we are
            // doing a delta encoding) to get to the blocksize?
            if (((num_files-1) % PFOR_blocksize) != 0)
            {
              ++numblocks;
              num_pads = PFOR_blocksize - ((num_files-1) % PFOR_blocksize);
              poco_debug(logger(),StrFormat("CompressThread, ngram %08x padding file_ids (only %u in last block, adding %u pads) for PFOR encoding (%u blocks total)", 
                                            ngram, (num_files-1) % PFOR_blocksize, num_pads, numblocks));
              num_files += num_pads;
              file_ids.resize(num_files, 0);
            }

            // now need to deal w/ each block, and concat the data if successful
            vec_u32_t::iterator iter(++file_ids.begin()); // first one varbyte encoded, start w/ the 2nd one
            uint32_t blockcount(0);
            while (blockcount < numblocks)
            {
              auto_ptr < vec_u8_t > block_data(pfor.encode(iter,iter+PFOR_blocksize));
              // if we got a NULL back from that, there was a problem meeting our
              // constraints, so fall back to VarByte
              pfor_encoded = (block_data.get() != NULL);
              if (pfor_encoded)
              {
                // append to our PFOR encoded data to the cdata object we'll pass of to the write thread later
                cdata.insert(cdata.end(),(*block_data).begin(),(*block_data).end());
              }
              else
              {
                if (num_pads)
                {
                  // need to back that padding out...
                  num_files -= num_pads;
                  file_ids.resize(num_files);
                }
                poco_debug(logger(),StrFormat("CompressThread, ngram %08x block %u failed PFOR encoding (errorcode == %u)", ngram, blockcount, pfor.last_errorcode));
                // well, something went wrong, so nuke the PFOR cdata contents, as
                // we'll be refilling it w/ VarByte below...
                cdata.resize(size_of_initial_varbyte_value);
                break;
              }
              ++blockcount;
              iter += PFOR_blocksize;
            }
          }
          else
          {
            poco_debug(logger(),StrFormat("CompressThread, ngram %08x not enough file_ids for PFOR encoding (%u)",
                                          ngram, num_files));
          }
          if (!pfor_encoded && file_ids.size() > 1)
          {
            poco_debug(logger(),StrFormat("CompressThread, ngram %08x falling back to VarByte encoding",ngram));
            for (vec_u32_t::iterator iter(++file_ids.begin());iter != file_ids.end(); ++iter)
            {
              vec_u8_t &vbyte_data(vbyte.encode(*iter));
              cdata.insert(cdata.end(),vbyte_data.begin(),vbyte_data.end());
            }
          }
          poco_debug(logger(),StrFormat("CompressThread, ngram %08x, %u file ids, compressed size %u (%4.2f%% smaller, used %s)",
                                        ngram,
                                        file_ids.size(),
                                        cdata.size(),
                                        100.0*(((4.0*file_ids.size())-cdata.size())/(4.0*file_ids.size())),
                                        (pfor_encoded ? "PFOR":"VarByte")));

          // clean up a little
          delete pCompressNf->_file_ids;

          // send compressed data pointer to write thread:
          _write_queue.enqueueNotification(new WriteNotification(ngram_order,ngram,cdataptr,uncsz,pfor_encoded));

          // let main thread know we finished via counter increment:
          ++compress_counter;
        }
        else
        {
          // why would we ever get here?
          poco_debug(logger(),"CompressThread NULL notification?");
        }
      }
      else
      {
        poco_debug(logger(),"CompressThread pNf was NULL?");
        done = true;
      }
    }
    poco_debug(logger(),"CompressThread exitting");
  }

//private:
  Poco::NotificationQueue &_queue;
  Poco::NotificationQueue &_write_queue;
};

class WriteWorker: public Poco::Runnable
{
public:
  WriteWorker(Poco::NotificationQueue &queue):
    _queue(queue)
  {
    _next_ngram_counter = 0;
    _last_index = 0;

    // don't init the ostringstream, because the first << overwrites that...
    ostringstream fnm;
    fnm << index_prefix << ".bgi";
    _index_dat.open(fnm.str().c_str());
    if (!_index_dat)
    { /* Could not open output file, exit */
      poco_error(logger(),StrFormat("Could not open output file %s. Exiting.",fnm.str().c_str()));
      exit(1);
    }
  }

  // here's a helper class for holding the write buffer data
  class WriteBufferData
  {
  public:
    uint32_t ngram;
    bool pfor_encoded;
    vec_u8_t *cdataptr;
    uint32_t uncompressed_size;
  };

  void run()
  {
    poco_debug(logger(),"WriteWorker run()");
    bool done(false);
    uint32_t total_ngrams(0);
    uint32_t total_pfor_encoded(0);
    uint64_t total_uncompressed_bytes(0);
    uint64_t total_compressed_bytes(0);
    uint32_t last_ngram_written(0);
    bool first_write(true);

    bgi_header header(ngram_size);
    header.hint_type = hint_type; // set the hint type appropriately
    // if in debug mode, dump initial header:
    poco_debug(logger(),header.dump());

    _hints.resize(header.num_hints(),UINT64_MAX); // default val 0xFFFFFFFFFFFFFFFF to indicate that prefix not in index

    // write out empty header data for spacer, fill in real data later, rewind, and write again...
    header.write(_index_dat);
    // write out empty hints data for spacer, rewind & rewrite real data later...
    _index_dat.write(reinterpret_cast<const char*>(&_hints.front()),_hints.size()*sizeof(uint64_t));
    // now we're in a (file) position to write out the actual index data...record where that starts:
    _hints[0] = _index_dat.tellp(); // sizeof(header) + sizeof(_hints)

    while (!done)
    {
      poco_debug(logger(),"WriteWorker waiting for notification...");
      Poco::AutoPtr<Poco::Notification> pNf(_queue.waitDequeueNotification());
      if (pNf)
      {
        WriteNotification* pWriteNf = dynamic_cast< WriteNotification* >(pNf.get());
        if (pWriteNf)
        {
          uint32_t ngram_order(pWriteNf->_ngram_order);
          uint32_t ngram(pWriteNf->_ngram);
          vec_u8_t *cdataptr(pWriteNf->_cdata);
          uint32_t uncompressed_size(pWriteNf->_uncompressed_size);
          bool pfor_encoded(pWriteNf->_pfor);

          poco_debug(logger(),StrFormat("WriteWorker got ngram_order %u (%08x)",ngram_order,ngram));

          // add this data to our buffer
          WriteBufferData write_data;
          write_data.ngram = ngram;
          write_data.pfor_encoded = pfor_encoded;
          write_data.cdataptr = cdataptr;
          write_data.uncompressed_size = uncompressed_size;
          _write_buffer[ngram_order] = write_data;

          bool keepwriting(true);
          while (keepwriting)
          {
            map< uint32_t, WriteBufferData >::iterator iter(_write_buffer.begin());
            if (iter != _write_buffer.end() && iter->first == _next_ngram_counter)
            {
              WriteBufferData wbdata(iter->second);
              uint32_t ngram(wbdata.ngram);
              //uint32_t idx(ngram >> 8);
              // calc hint index based on hint type
              uint32_t idx(header.ngram_to_hint(ngram));
              vec_u8_t &cdata(*(wbdata.cdataptr));
              uint32_t uncompressed_size(wbdata.uncompressed_size);
              bool pfor_encoded(wbdata.pfor_encoded);
              uint32_t sz(cdata.size());

              total_ngrams += 1;
              total_pfor_encoded += int(pfor_encoded);
              total_uncompressed_bytes += uncompressed_size;
              total_compressed_bytes += sz;

              if (sz >= 0x80000000)
              {
                // too big for us to deal with, bail
                poco_error(logger(),StrFormat("size of compressed data too big!"));
                exit(1);
              }
              // set high order bit of sz if we are pfor encoded...  actually,
              // let's switch to LSB of sz instead, so we can VarByte encode
              // it better
              sz <<= 1;
              if (pfor_encoded)
              {
                //sz |= 0x80000000;
                sz |= 0x00000001;
              }

              //if ((total_ngrams % 100000) == 0)
              if ((total_ngrams % 0xFFFFF) == 0)
              {
                poco_information(logger(),StrFormat("WriteWorker writing ngram %08x",ngram));
              }

              VarByteUInt< uint32_t > vbyte(sz);
              vec_u8_t &cszdata(vbyte.encode());

              // write entry to ngrams.dat, old format:
              //  [4 byte ngram][4 byte size of compressed data w/ MSB set for PFOR][id#1 varbyte][rest of ids in delta format, compressed PFOR or VarByte]
              // new format:
              //  [VarByte encoded size, shifted left 1 w/ LSB set for PFOR][id#1 Varbyte][delta list, PFOR/VarByte]

              vec_u8_t zbuf;
              if (!first_write) // most common case
              {
                if (ngram != (last_ngram_written+1))
                {
                  // pad out
                  zbuf.resize(ngram-(last_ngram_written+1),0);
                }
              }
              else
              {
                first_write = false;
                // and pad out if ngram is > 0
                if (ngram > 0)
                {
                  zbuf.resize(ngram,0);
                }
              }

              if (zbuf.size() > 0)
              {
                poco_debug(logger(),StrFormat("WriteWorker padding missing ngrams between last ngram %08x and current ngram %08x (%u of them)",last_ngram_written,ngram,zbuf.size()));
                _index_dat.write(reinterpret_cast<char const *>(&zbuf.front()),zbuf.size());
              }

              // if necessary, write entry to index.dat
              if (_last_index != idx )
              {
                _hints[idx] = _index_dat.tellp(); // need current file offset here...
                // actually,need to adjust that if it really is in the middle of padding we are adding...
                //uint32_t lsbyte(ngram & 0x000000ff);
                // this might be a nybble now, or zero:
                uint32_t lsbyte(ngram & header.hint_type_mask());
                if (zbuf.size() != 0 && lsbyte != 0)
                {
                  poco_debug(logger(),StrFormat("WriteWorker hint index entry padding adjustment: -%u",lsbyte));
                  _hints[idx] -= lsbyte;
                }
                poco_debug(logger(),StrFormat("WriteWorker adding hint index entry %08x: %016x",idx,_hints[idx]));
                _last_index = idx;
              }

              poco_debug(logger(),StrFormat("WriteWorker writing ngram %08x data size %u at offset %016x",ngram,cszdata.size(),(uint64_t)_index_dat.tellp())); // older gcc versions (4.4.x and earlier) require the cast of the tellp value in order to not hit an illegal instruction somewhere in the Poco libs

              //_index_dat.write(reinterpret_cast<char const *>(&sz),sizeof(sz));
              // we now VarByte encode the sz:
              _index_dat.write(reinterpret_cast<char const *>(&cszdata.front()),cszdata.size());
              // can't just do << vector unfortunately, and it'd probably give the wrong data anyways:
              _index_dat.write(reinterpret_cast<char const *>(&(cdata[0])),cdata.size());

              last_ngram_written = ngram;

              // clean up the compressed data memory
              delete wbdata.cdataptr;
              // remove the entry from the buffer
              _write_buffer.erase(iter);

              ++_next_ngram_counter;

              // let main thread know we finished one via counter increment:
              ++write_counter;
              poco_debug(logger(),StrFormat("WriteWorker write counter == %u",(unsigned int)write_counter));
            }
            else
            {
              keepwriting = false; // go back to getting new notifications
              // probably need to do something more drastic to "really" clean that memory usage up, the swap trick:
              //map< uint32_t, WriteBufferData > (_write_buffer).swap(_write_buffer);
            }
          }
        }
        else
        {
          // why would we ever get here?
          poco_debug(logger(),"WriteThread NULL notification?");
        }
      }
      else
      {
        poco_debug(logger(),"WriteThread pNf was NULL?");
        done = true;
      }
    }

    poco_information(logger(),StrFormat("WriteThread done w/ ngrams.dat, wrote %u ngrams, %u pfor encoded, %lu uncompressed, %lu compressed (ratio %f)",total_ngrams,total_pfor_encoded, total_uncompressed_bytes, total_compressed_bytes, float(total_compressed_bytes)/total_uncompressed_bytes));

    // write out final index data now, first correct header values:
    header.fileid_map_offset = _index_dat.tellp();
    header.num_ngrams = write_counter;
    header.num_files = id_to_fname.size();
    header.pfor_blocksize = PFOR_blocksize;

    poco_debug(logger(),"WriteThread saving fileid map");
    for (uint32_t i(0);i<header.num_files;++i)
    {
      poco_debug(logger(),StrFormat("about to call StrFormat on %d",i));
      string msg(StrFormat("%010u %s",i,id_to_fname[i].c_str()));
      poco_debug(logger(),msg);
      _index_dat << msg << endl;
    }
    uint64_t final_index_size(_index_dat.tellp());

    poco_debug(logger(),"WriteThread fixing header data");
    _index_dat.seekp(0,ios::beg);
    header.write(_index_dat);
    header.dump();

    poco_debug(logger(),"WriteThread saving index hint data");
    //uint32_t i(0);
    //for (vec_u64_t::iterator iter(_hints.begin());iter != _hints.end();++iter)
    //{
    //  uint64_t offset(*iter);
    //  //poco_debug(logger(),StrFormat("index.dat: %06x %016lx",i++,offset));
    //  //_hints_dat << offset; // this gives totally the wrong data in the file
    //  _hints_dat.write(reinterpret_cast<char const *>(&offset),sizeof(offset)); // thankfully, this works
    //}
    // vector contiguous, so can just re-write that out wholesale:
    _index_dat.write(reinterpret_cast<const char*>(&_hints.front()),_hints.size()*sizeof(uint64_t));

    _index_dat.close();
    poco_debug(logger(),"WriteThread done saving index data, exitting");
  }

//private:
  Poco::NotificationQueue &_queue;
  uint32_t _next_ngram_counter; // uh, how is this different than my total_ngrams counter?
  uint32_t _last_index;
  vec_u64_t _hints;
  map< uint32_t, WriteBufferData > _write_buffer;
  ofstream _index_dat;
};

// main code

void
help()
{
  std::cerr << "Usage:" << std::endl;
  std::cerr << "bgindex [OPTS]" << std::endl << std::endl;
  std::cerr << "  bgindex reads a list of files on stdin to process, produces an N-gram inverted index" << std::endl << std::endl;
  std::cerr << "  OPTS can be:" << std::endl;
  std::cerr << "    -n, --ngram #\tDefine N for the N-gram (3 or 4, 3 is default)" << std::endl;
  std::cerr << "    -H, --hint-type #\tSpecify hint type (0-2, default 0 for n==4, 1 for n==3 )" << std::endl;
  std::cerr << "    -b, --blocksize #\tPFOR encoding blocksize (multiple of 8, default 32)" << std::endl;
  std::cerr << "    -e, --exceptions #\tPFOR encoding max exceptions per block (default 2)" << std::endl;
  std::cerr << "    -m, --minimum #\tPFOR encoding minimum number of entries to consider PFOR (default 4)" << std::endl;
  std::cerr << "    -M, --max-unique-ngrams #\tMaximum number of unique n-grams allowed per file" << std::endl;
  std::cerr << "    -p, --prefix STR\tA prefix for the index file(s) (directory and/or partial filename)" << std::endl;
  std::cerr << "    -s, --stats FILE\tA file to save some statistics to (not implemented yet)" << std::endl;
  std::cerr << "    -S, --sthreads #\tNumber of threads to use for shingling (default 4)" << std::endl;
  std::cerr << "    -C, --cthreads #\tNumber of threads to use for compression (default 5)" << std::endl;
  std::cerr << "    -v, --verbose\tshow some additional info while working" << std::endl;
  std::cerr << "    -d, --debug\tshow more diagnostic information" << std::endl;
#ifdef ALLOW_TRACING
  std::cerr << "    -t, --trace\tshow WAY TOO MUCH diagnostics (if compiled in)" << std::endl;
#endif
  std::cerr << "    -h, --help\tshow this help" << std::endl << std::endl;
  std::cerr << "Version: 2.6" << std::endl;

}

struct option long_options[] = 
{
  {"ngram",required_argument,0,'n'},
  {"hint-type",required_argument,0,'H'},
  {"blocksize",required_argument,0,'b'},
  {"exceptions",required_argument,0,'e'},
  {"minimum",required_argument,0,'m'},
  {"max-unique-ngrams",required_argument,0,'M'},
  {"prefix",required_argument,0,'p'},
  {"stats",required_argument,0,'s'},
  {"sthreads",required_argument,0,'S'},
  {"cthreads",required_argument,0,'C'},
  {"stats",required_argument,0,'s'},
  {"verbose",no_argument,0,'v'},
  {"debug",no_argument,0,'d'},
#ifdef ALLOW_TRACING
  {"trace",no_argument,0,'t'},
#endif
  {"help",no_argument,0,'h'},
  {0,0,0,0}
};


int
main(int argc, char** argv)
{
#ifdef GLIBC_MALLOC_BUG_WORKAROUND
  // Chuck's MALLOC bug fix
  int mrc = mallopt(M_MMAP_MAX,0);
  if (mrc != 1) {
    perror("mallopt error?\n");
    exit(1);
  }
#endif // GLIBC_MALLOC_BUG_WORKAROUND

  // apparently the default ConsoleChannel really goes to /dev/null, so we
  // need to set up some formatting for it, etc.
  Poco::AutoPtr<Poco::ConsoleChannel> pCons(new Poco::ConsoleChannel);
  Poco::AutoPtr<Poco::PatternFormatter> pPF(new Poco::PatternFormatter);
  pPF->setProperty("pattern", "%Y%m%d%H%M%S %s t%I (%q): %t");
  Poco::AutoPtr<Poco::FormattingChannel> pFC(new Poco::FormattingChannel(pPF, pCons));
  logger().setChannel(pFC);

  bool got_hint_type(false);

  // should read cmd line options here...
  int ch;
  int option_index;
  while ((ch = getopt_long(argc,argv,"n:b:e:m:M:vdthp:s:S:C:",long_options,&option_index)) != -1)
  {
    switch (ch)
    {
      case 'n':
        ngram_size = atoi(optarg);
        // better be 3 or 4
        if (ngram_size < 3 || ngram_size > 4)
        {
          // error
          poco_error(logger(),"sorry, this code only handles 3 or 4 grams");
          help();
          return 1; // bail
        }
        break;
      case 'H':
        got_hint_type = true;
        hint_type = atoi(optarg);
        // 0-2
        if (hint_type < 0 || hint_type > 2)
        {
          // error
          poco_error(logger(),"sorry, this code only handles hint type of 0-2");
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
      case 'S':
        num_shingle_threads = atoi(optarg);
        break;
      case 'C':
        num_compress_threads = atoi(optarg);
        break;
      case 'p':
        index_prefix = optarg;
        break;
      case 's':
        stats_filename = optarg;
        break;
      case 'v':
        //LSETLOGLEVEL(Logger::INFO);
        break;
      case 'd':
        logger().setLevel(Poco::Message::PRIO_DEBUG);
        break;
#ifdef ALLOW_TRACING
      case 't':
        logger().setLevel(Poco::Message::PRIO_TRACE);
        break;
#endif
      case 'h':
        help();
        return 1; // bail
      default: // should never get here?
        //LWARN << "option '" << (char)ch << "' recieved??" << std::endl;
        break;
    }
  }

  if ((argc - optind) != 0)
  {
    // doesn't take any parameters other than options right now...otherwise they'd be at argv[optind]
    help();
    return 1;
  }

  // set default hint_type appropriately if not given one:
  if (!got_hint_type && ngram_size==3)
  {
    hint_type = 1;
  }

  Stopwatch stimer;
  // read list of files from stdin, get count
  bool done(false);
  while (!done)
  {
    string fname;
    getline(cin,fname);
    done = cin.eof();
    if (fname != "") // last line might be blank
    {
      poco_debug_f1(logger(),"got fname: %s",fname);
      id_to_fname.push_back(fname);
    }
  }
  uint32_t numfiles(id_to_fname.size());

  if (numfiles < 2)
  {
    poco_error(logger(),"Please provide two or more files for indexing at a time.");
    help();
    exit(1);
  }
  poco_debug(logger(),StrFormat("took %f sec to read %u file names",stimer.secondsFromLast(),numfiles));
  //poco_debug_f1(logger(),"got this many fnames: %d",int(numfiles));

  LoserTree llt(numfiles);
  lt = &llt; // set the global var ptr...the shingling threads are gonna need it (non conflicting write access to the internal node array, shouldn't need a mutex or anything like that...)

  shingle_counter = 0;
  compress_counter = 0;
  write_counter = 0;

  Poco::NotificationQueue shingle_queue;

  // now create a thread pool to shingle those files
  Poco::ThreadPool shingle_pool(num_shingle_threads,num_shingle_threads); // 
  vector< ShingleWorker* > shinglers;
  for(int i(0);i<num_shingle_threads;++i)
  {
    ShingleWorker *shingler = new ShingleWorker(shingle_queue);
    shinglers.push_back(shingler); // copy of this shingler gets pushed into vector...
    shingle_pool.start(*shinglers[i]);
  }

  // add each file to the shingle queue
  for (unsigned int i(0);i<numfiles;++i)
  {
    // strip off metadata from the filename before passing along (it'll be
    // written out from id_to_fname at the end):
    vector< string >fname_and_metadata;
    boost::split(fname_and_metadata,id_to_fname[i],boost::is_any_of(","));
    poco_debug(logger(),StrFormat("file name '%s' metadata length: %d",fname_and_metadata[0].c_str(),fname_and_metadata.size()-1));
    shingle_queue.enqueueNotification(new ShingleNotification(i,fname_and_metadata[0]));
  }
  // also add NUM_SHINGLE_THREADS blank file names & UINT32_MAX as id for signalling done
  for(int i(0);i<num_shingle_threads;++i)
  {
    shingle_queue.enqueueNotification(new ShingleNotification(UINT32_MAX,""));
  }
  poco_debug(logger(),StrFormat("took %f sec to start threads and endque %u files",stimer.secondsFromLast(),numfiles));

  // wait for shingle threads to finish
  shingle_pool.joinAll();
  poco_information(logger(),StrFormat("MAIN: shingling appears done, took %f sec, starting compress & write threads...",stimer.secondsFromLast()));
  // determine how many files we have actually shingled
  int num_nonmissing_files = 0;
  for (int i=0; i<numfiles; i++)
    if (lt->nodes[i].fd != NULL)
      num_nonmissing_files++;
    else
      id_to_fname[i]="";
  if (num_nonmissing_files < 2)
  {
    poco_error(logger(),StrFormat("%d of %d files found.  Please provide two or more files for indexing at a time.",num_nonmissing_files,numfiles));
    exit(1);
  } 
  // create a thread pool for doing the sort+compress of the resulting file id data for each ngram
  Poco::ThreadPool compress_pool(num_compress_threads,num_compress_threads); // pool to sort & compress the data
  vector < CompressWorker* > compressors;
  Poco::NotificationQueue compress_queue;
  Poco::NotificationQueue write_queue;
  for (int i(0);i<num_compress_threads;++i)
  {
    CompressWorker *compressor = new CompressWorker(compress_queue,write_queue);
    compressors.push_back(compressor);
    compress_pool.start(*compressors[i]);
  }

  // single thread for writing batches of data post compression:
  Poco::Thread write_thread;
  WriteWorker writer(write_queue);
  write_thread.start(writer);

  //poco_debug(logger(),StrFormat("took %f sec to create the comprss & write threads",stimer.secondsFromLast()));

  poco_information(logger(),"Started Compress and Write threads...now starting merge...");

  // LoserTree based merge here!
  lt->build_tree();
  poco_information(logger(),"built Loser Tree, pulling data");
  lt->dump(); // diagnostics (only in trace mode)

 // pull from top of loser tree, when ngram changes send current list to compress queue.
  vec_u32_t* fids(new vec_u32_t());
  uint32_t last_ngram(UINT32_MAX);
  if (!lt->empty)
  {
    last_ngram = lt->root->peek_node_data().first;
    //total_unique_ngrams = 1;
  }
  poco_debug(logger(),StrFormat("Loser Tree first root ngram %08x",last_ngram));
  uint32_t num_ngrams_processed(0);
  while (! lt->is_empty())
  {
    lt->dump(); // diags, only in trace mode

    // get root ngram & file id (will update tree as well)
    pair< uint32_t, uint32_t> rdata(lt->get_root_data());
    uint32_t ngram(rdata.first);
    uint32_t id(rdata.second);
    poco_debug(logger(),StrFormat("Loser Tree root ngram %08x from file id %u",ngram,id));

    if (ngram != last_ngram) // a new ngram?
    {
      if (ngram < last_ngram)
      {
        poco_critical(logger(),StrFormat("merge megafail, current ngram (%08x) < last ngram (%08x)",ngram,last_ngram));
        exit(88);
      }
      //if (fids->size() > 0) // this should always be true..
      {
        //if ((num_ngrams_processed++ % 0x00FF0000) == 0)
        //if ((num_ngrams_processed++ % 1) == 0)
        if ((num_ngrams_processed++ % 0xFFFFF) == 0)
        {
          poco_information(logger(),StrFormat("merge sending ngram %08x to compress (%u file ids)",last_ngram,fids->size()));
        }
        poco_debug(logger(),StrFormat("merge sending ngram %08x to compress (%u file ids)",last_ngram,fids->size()));
        //Wait for the write queue to catch up a bit
        if (compress_counter > write_counter + 10000)
        {
          poco_information(logger(),StrFormat("Waiting for writer to catch up c:%d w:%d",(int)compress_counter, (int)write_counter));
          while (compress_counter > write_counter + 1000)
            sleep(1);
          poco_information(logger(),StrFormat("writer has caught up c:%d w:%d",(int)compress_counter, (int)write_counter));
        }
        // send to compress queue
        compress_queue.enqueueNotification(new CompressNotification(total_unique_ngrams,last_ngram,fids)); // current total ngrams == current ngram order

        fids = new vec_u32_t();
      }
      last_ngram = ngram;
      ++total_unique_ngrams;
    }

    fids->push_back(id);

    // append current id to currend file id list
    // else
    // if file id list .size() > 0, send to compress queue
    // ++total_unique_ngrams;
  }
  // send the final batch over to compression...
  if (fids->size() > 0) // should always be true
  {
    // send to compress queue
    poco_debug(logger(),StrFormat("merge sending FINAL ngram %08x to compress (%u file ids)",last_ngram,fids->size()));
    compress_queue.enqueueNotification(new CompressNotification(total_unique_ngrams,last_ngram,fids)); // current total ngrams == current ngram order
    ++total_unique_ngrams;
  }



  poco_information(logger(),StrFormat("took %f sec to merge %u files",stimer.secondsFromLast(),numfiles));

  poco_information(logger(),StrFormat("%u unique ngrams total",total_unique_ngrams));


  //for (unsigned int i(0);i<total_unique_ngrams;++i)
  //{
  //  ngrams_to_ids_t::iterator iter(ngrams_to_ids.begin());
  //  // iter->first is the ngram value, iter->second is the vector of file id's
  //  // create a local allocated copy of the vector, to limit us to a single
  //  // copy being made in the course of handing this around, the thread will
  //  // free it when done.
  //  vec_u32_t *file_ids = iter->second;
  //  compress_queue.enqueueNotification(new CompressNotification(i,iter->first,file_ids));
  //  // now okay free up the mem for that entry...
  //  ngrams_to_ids.erase(iter);
  //}


  poco_debug(logger(),"waiting for compress & write threads to finish...");

  uint32_t cnt(0);
  while (compress_counter != total_unique_ngrams)
  {
    if ((++cnt)%25 == 0)
    {
      poco_information(logger(),StrFormat("compress counter == %u",(unsigned int)compress_counter));
    }
    Poco::Thread::sleep(20);
  }

  //poco_debug(logger(),StrFormat("compress threads done, took %f sec",stimer.secondsFromLast()));

  Poco::Thread::sleep(50);
  compress_queue.wakeUpAll();
  compress_pool.joinAll();

  cnt = 0;
  while (write_counter != total_unique_ngrams)
  {
    if (++cnt%25 == 0)
    {
      poco_information(logger(),StrFormat("write counter == %u",(unsigned int)write_counter));
    }
    Poco::Thread::sleep(20);
  }

  poco_information(logger(),StrFormat("Compress & Write threads done, took %f sec",stimer.secondsFromLast()));
  //poco_debug(logger(),StrFormat("write threads done, took %f sec after compress threads finished",stimer.secondsFromLast()));
  Poco::Thread::sleep(50);
  write_queue.wakeUpAll();
  write_thread.join();

  // sleep to look at RAM usage while debugging:
  //Poco::Thread::sleep(5000);

  poco_information(logger(),StrFormat("done, total runtime %f sec, estimated RSS size %f GB",stimer.secondsFromStart(),get_mem_usage()));

  return 0;
}
