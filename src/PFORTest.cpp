// Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

#include <iostream>

#define __STDC_LIMIT_MACROS // I have to define this to get UINT32_MAX because of the ISO C99 standard?
#define __STDC_CONSTANT_MACROS // ustl wants this
#include <stdint.h>

#include <cstdlib>

#include <vector>
#include <memory> // for auto_ptr
#include <algorithm> // for equal


#include "PFOR.hpp"
#include "VarByte.hpp"
#include "StrFormat.hpp"

using namespace std;

void
dump_byte_vector_as_bits(vector< uint8_t > &vec)
{
  cout << "dump vector bits: ";
  for (unsigned i(0);i<vec.size();++i)
  {
    for (int b(7);b>=0;--b)
    {
      cout << ((vec[i] & (1 << b)) != 0);
    }
    cout << " ";
  }
  cout << endl;
}

template <typename T>
void
dump_vector(vector< T > &vec)
{
  cout << "dump vector: ";
  for (unsigned i(0);i<vec.size();++i)
  {
    cout << vec[i] << " ";
  }
  cout << endl;
}

int
main(int argc, char** argv)
{
  // scale down 128 blocksize w/ 12 max exceptions to smaller stuff for our tests:
  uint32_t blocksize(16);
  uint32_t maxexceptions(1);
  bool failed(false);

  // C++ 2011 standard, gcc 4.4 & up, allows for array style init of vectors, woohoo!  But you have to compile w/ the flag --std=c++0x
  vector< vector< uint32_t > > testdata1 = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, // special
    {1,1,1,1,1,1,1,1,1,1,1,1024,1,1,1,1}, // special w/ one exception
    {1,1,1,1,1000,1,1,1,1,1,1,1000,1,1,1,1}, // almost, but not quite special, b == 10 will be used, 0 exceptions
    {1,1,1,30,32,1,1,1,1,1,1,1000,0,0,0,0}, // b == 5, 1 exceptions
  };
  vector < uint32_t > testdata2 = {
    // and now for a bigger list, multiple blocks, w/ a special one in the middle:
    100,83,12,1,0,60,70,1,1,1,1,1,1,1,1,1024, // b == 7, & one exception
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 
    32,31,33,1,1,1,1,1,1,1,1,1,1,1,1,1 // b == 5 w/ one exception
  };

  vector < uint32_t > testdata3(16); // we'll fill it w/ random data?

  VarByteUInt< uint32_t > varbyte(0);

  cout << "Testing static vector encode/decode..." << endl << endl;

  for (unsigned i(0);i < testdata1.size();++i)
  {
    PFORUInt< uint32_t > pfor(blocksize,maxexceptions);
    cout << ">>> encoding testdata1 vector " << i << ", " << testdata1[i].size()*sizeof(uint32_t) << " bytes of data" << endl;
    dump_vector< uint32_t >(testdata1[i]);
    auto_ptr< vector< uint8_t > > encdataptr(pfor.encode(testdata1[i]));
    vector< uint8_t > &encdata(*encdataptr);
    
    cout << "  - encoded to " << encdata.size() << " bytes, b == " << int(pfor.last_b) << ", execptions == " << int(pfor.last_exceptions) << ", special == " << pfor.last_special << endl;
    dump_byte_vector_as_bits(encdata);
    auto_ptr< vector< uint32_t > > decdataptr(pfor.decode(encdata));
    vector< uint32_t > &decdata(*decdataptr);
    
    dump_vector< uint32_t >(decdata);
    if (equal(decdata.begin(),decdata.end(),testdata1[i].begin()))
    {
      cout << "  - decode check passed" << endl;
    }
    else
    {
      cout << "  - decode check FAILED" << endl;
      failed = true;
    }

  }

  cout << endl << endl << "Testing 1000 random vectors encode/decode w/ larger blocksize..." << endl << endl;
  //blocksize = 128;
  //maxexceptions = 12;
  blocksize = 64;
  maxexceptions = 6;

  uint64_t total_uncompressed_size(0);
  uint64_t total_compressed_size(0);

  srandom(time(NULL)%UINT32_MAX);

  for (unsigned i(0);i < 10000;++i)
  {
    bool sorted(false);
    bool deltas(false);
    PFORUInt< uint32_t > pfor(blocksize,maxexceptions);
    // generate a random vector:
    vector< uint32_t > randvector(blocksize);
    // randomly determine our max bounds for each vector:
    int max_rand_val(32768/(1+random()%256)); // vary, but keep within the maxb of 15 range...

    for (unsigned j(0);j<blocksize;++j)
    {
      randvector[j] = random() % max_rand_val;
    }
    // randomly decide to sort (should I uniq & pad too?) or not:
    if ((random() % 2) == 1)
    {
      sorted = true;
      sort(randvector.begin(),randvector.end());
      // if sorted, randomly decide to convert to delta:
      if ((random() %2) == 1)
      {
        pfor.convert_to_deltas(randvector);
        deltas = true;
      }
    }

    total_uncompressed_size += randvector.size()*sizeof(uint32_t);
    cout << ">>> encoding random vector " << i << ", " << randvector.size()*sizeof(uint32_t) << " bytes of data, sorted == " << int(sorted) << ", deltas == " << int(deltas) << ", max value == " << max_rand_val << endl;
    //dump_vector< uint32_t >(randvector);
    auto_ptr< vector< uint8_t > > encdataptr(pfor.encode(randvector));
    vector< uint8_t > &encdata(*encdataptr);
    
    cout << "  - encoded to " << encdata.size() << " bytes, b == " << int(pfor.last_b) << ", execptions == " << int(pfor.last_exceptions) << ", special == " << pfor.last_special << endl;
    total_compressed_size += encdata.size();
    //dump_byte_vector_as_bits(encdata);
    auto_ptr< vector< uint32_t > > decdataptr(pfor.decode(encdata));
    vector< uint32_t > &decdata(*decdataptr);
    
    //dump_vector< uint32_t >(decdata);
    if (equal(decdata.begin(),decdata.end(),randvector.begin()))
    {
      cout << "  - decode check passed" << endl;
    }
    else
    {
      cout << "  - decode check FAILED" << endl;
      dump_vector< uint32_t >(randvector);
      dump_byte_vector_as_bits(encdata);
      dump_vector< uint32_t >(decdata);
      failed = true;
    }

  }


  cout << endl << "test done, failed == " << int(failed) << ", avg compressed output rate (lower == better) == " << (total_compressed_size/float(total_uncompressed_size)) << endl << endl;

  return int(failed);
}
