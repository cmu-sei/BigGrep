// Copyright 2011-2017 Carnegie Mellon University.  See LICENSE file for terms.
// a class to handle a Patched Frame of Reference (PFOR) encoding scheme (decoding too).
//
// Requires BLOCKSIZE (power of 2 value, well, actually a multiple of 8 is
// likely sufficient, default 128) vector of uint32_t or uint64_t values,
// produces a vector of uint8_t values with the compressed data, like so:
//
//    [byte high order nybble: # exceptions][byte low order nybble: b-value][PFOR encoded data (b*BLOCKSIZE/8 bytes)][[varbyte encoded exception offset 1][varbyte encoded exception value 1]...]
//
// This version has a limitation of punting on b values above 15 (0xF),
// because we're storing it in a nybble and we use a b-value of 0 as a special
// value to indicate that the encoded data block of b==1 that is comprised
// completely of ones (ignoring exceptions), and hence ommitted (saving quite
// a bit of space in that situation, 128 bits or 16 bytes for the default
// BLOCKSIZE number of repeated ones in the list, quite common for "properly"
// ordered delta lists like what we are dealing with).
//
// Also, the max exceptions is limited to 15 as well, effectively limiting the
// upper bounds of the blocksize to 128/160 or so if you're shooting for a 10%
// max exception rate.
//
// And I went with varbyte encoding for the exception offsets, because for the
// default blocksize of 128 it'll just be a single byte value anyways (and
// unchanged in value, at least when using my VarByte implementation that has
// the MSB set to indicate overrun instead of the original bgindex code
// variable byte implementation which shifts and uses the LSB to indicate
// overrun), but could potentially be used for larger block sizes too.  Would
// probably have to increase the space allocated to store B and E to really
// allow for that though...
//
// Speaking of Delta lists, this code doesn't explicitly adjust for that,
// assumes that you are passing in exactly what you want compressed and gives
// back the decompressed list just the same, so you'll need to account for
// delta lists externally (same w/ padding out to BLOCKSIZE values if you need
// to do so).  But to help with that, there are two helper methods here to
// convert to/from delta lists (modifies them inline).
//
// I originally envisioned a class that would hold on to the b value &
// exception list and be able to get at that data independently and produce
// the above described data storage format externally, but eh, this is okay
// for now.
//
// Borrowed info from some algorithms at:
//   http://graphics.stanford.edu/~seander/bithacks.html
//   http://aggregate.org/MAGIC/ and
//   and the book Hacker's Delight (www.hackersdelight.org)
// to have a nice fast way to get at the next integer log2 value (rounded up)
// for calculating b.

#ifndef __PFOR_HPP_INCLUDED__
#define __PFOR_HPP_INCLUDED__


#define __STDC_LIMIT_MACROS // I have to define these to get UINT32_MAX because of the ISO C99 standard, apparently
// #define __STDC_CONSTANT_MACROS // don't really need these right now though
#include <stdint.h>

#include <string.h> // memset

#include <vector>
#include <map>

#include "VarByte.hpp"

namespace
{
    template < typename T >
    inline
    uint8_t // max answer for a 64 bit type == 63, so might as well keep that short
    ilog2(T v) // technically floor_ilog2 might be a better name
    {
        // uses a lookup table for speed
        static const uint8_t LogTable256[256] =
            {
                // Note, the first val probably should be -1 (or rather, since we're
                // returning a uint8_t that should be 255) to indicate error, but the
                // usage in this code works better w/ the "wrong" answer of 0;
                0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
                5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
                5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
                6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
                6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
                6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
                6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
            };

        uint8_t r(0); // r will be ilog2(v), went w/ uint8_t because that easily covers through uint64_t values...
        //register T t, tt; // temporaries
        register T tt; // temporaries

        // need to account for uint64_t vs uint32_t, and although I'm sure there's
        // a much more C++-ish way to do this via some funky Template
        // Metaprogramming techniques to calculate the equivallent of this nested
        // if structure at compile time, my mind doesn't quite work that way, so
        // this will have to do for now (and the resulting code should still be
        // pretty fast, so long as branch prediction is working in our favor):
#if 0
        if (sizeof(T) == sizeof(uint64_t))
        {
            if ((tt = v >> 56)) {
                r = 56 + LogTable256[tt];
            } else if ((tt = v >> 48)) {
                r = 48 + LogTable256[tt];
            } else if ((tt = v >> 40)) {
                r = 40 + LogTable256[tt];
            } else if ((tt = v >> 32)) {
                r = 32 + LogTable256[tt];
            } else if ((tt = v >> 24)) {
                r = 24 + LogTable256[tt];
            } else if ((tt = v >> 16)) {
                r = 16 + LogTable256[tt];
            } else if ((tt = v >> 8)) {
                r = 8 + LogTable256[tt];
            } else {
                r = LogTable256[v];
            }
        }
#else
        // for now only using uint32_t and I don't want to see the warning about the above shifts being larger than the size of the data type...
        //std::cout << "ilog2(" << v << ") == ";
        if (0) {}
#endif // 0
        else if ((tt = v >> 24)) {
            r = 24 + LogTable256[tt];
        } else if ((tt = v >> 16)) {
            r = 16 + LogTable256[tt];
        } else if ((tt = v >> 8)) {
            r = 8 + LogTable256[tt];
        } else {
            r = LogTable256[v];
        }

        //std::cout << int(r) << std::endl;
        return r;
    }

    template < typename T >
    inline
    uint8_t
    ceil_ilog2(T x)
    {
        // nice & simple way to go from floor_ilog2 to ceil_ilog2:
        return ((x == 0) ? 0 : (1+ilog2< T >(x-1)));
    }

};


template < typename T >
class PFORUInt
{
private:
    T _blocksize;
    T _maxexceptions;
    uint8_t _maxb;

public:

    // for external acces for diags:
    uint8_t last_exceptions;
    uint8_t last_b;
    bool last_special;
    uint8_t last_errorcode;


    // constructors
    //PFORUInt< T >::PFORUInt(T val):
    PFORUInt(T blocksize = 128, T maxexceptions = 12):
        _blocksize(blocksize), _maxexceptions(maxexceptions), _maxb(16)
    {
        //_maxb = 16; // b < 16, so we can fit the b value in a nybble
        // stored as 1-15, b == 0 sentinel value for "next block was b=1 will all one values, so I didn't include it"
    }

    // copy constructor: default okay?
    // copy assignment operator: default okay?

    // you'll need to free the returned vector pointer...
    std::vector< uint8_t >* encode(typename std::vector< T > &vals)
    {
        return encode(vals.begin(),vals.end());
    }

    // to work w/ a subset of a larger input vector, here's the iterator version:
    std::vector< uint8_t > *encode(typename std::vector< T >::iterator vstart,
                                   typename std::vector< T >::iterator vend)
    {
        last_errorcode = 0;
        // use iterator math to verify size of the block
        // (pad before calling if you need to)
        unsigned numvals(vend-vstart);
        if (numvals != _blocksize) {
            // throw an exception or return NULL?
            last_errorcode = 1;
            return NULL;
        }

        // need to figure out our min b that gives <= maxexceptions values,
        // so how many bits for each value?
        std::vector< uint8_t > minbits(_blocksize);
        std::map< uint8_t, uint8_t > bitcounts;
        for (unsigned i(0);i<_blocksize;++i) {
            //T val(*(vstart+i));
            //minbits[i] = ceil_ilog2< T >(*(vstart+i));
            // whoops, actually, we always need to round up, not ceil,
            // because 32 (2^5) actually needs 6 bits to store it:
            minbits[i] = 1+ilog2< T >(*(vstart+i));
            // do we need to convert 0 to 1 here?
            // I think so, because the values 0
            // & 1 need to be stored in a single bit
            //(and we'll be using b == 0 as a special flag value later)
            //if (minbits[i] == 0)
            //{
            //  minbits[i] = 1;
            //}
            // since rounding up now, don't think minbits[i] can ever be 0,
            //so don't check
            bitcounts[minbits[i]] += 1;
        }

    // okay, minbits vector now tells us the minimum b for each entry, and we
    // kept track of the number of each potential b value in bitcounts, so
    // figure out which to use (lowest b value to get our num exceptions at or
    // below our max allowed).  Possible future improvement: calculate min
    // value of total bits needed instead of "just met threshold" (for
    // instance, if adding one more bit to b makes 12 exeptions become 0, for
    // blocksize 128 of 32 bit input values that's an improvement for that
    // block in total number of bits required)
        unsigned int exceptions(_blocksize);
        uint8_t b(0xFF);
        for (unsigned int i(1);i<_maxb;++i) {
            // we know there are no values in bitcounts[0]
            exceptions -= bitcounts[i];
            if (exceptions <= _maxexceptions) {
                b = i;
                break;
            }
        }

        if (b == 0xFF) {
            // couldn't meet requirements for b < 16 & maxexceptions
            last_b = b;
            last_errorcode = 2;
            return NULL; // or throw?
        }

        // okay, now know what our b is, so we can encode
        // (will convert to vector later):
        uint8_t *outbuf = new uint8_t[sizeof(T)+b*_blocksize/8];
        // pad out sizeof(T) to prevent over reads/writes
        // I thought new should init to 0, but apparently that isn't guaranteed for primitive types, and you can't specify how to call the constructor w/ array new because ISO C++ forbids it?  Sigh...memset it is then...
        memset(outbuf,0,b*_blocksize/8);
        uint32_t bitoffset(0);
        T mask((1 << b)-1);
        // okay, now we cheat a bit working w/ data in 32 bit chunks, knowing we aren't actually going to overwrite the end of the buffer...
        T *outptr;
        bool special(b==1); // to check if this is a special block of b==1 + all bits set
        for (unsigned i(0);i<_blocksize;++i)
        {
            // this likely isn't portable, but for x86 little endian should be fine:
            outptr = (T*)&outbuf[bitoffset/8];
            T thisval(*(vstart+i));
            //std::cout << "encoding " << thisval << " at bit offset " << bitoffset << " outptr byte offset: " << bitoffset/8 << std::endl;
            (*outptr) = (*outptr) | ((thisval & mask) << (bitoffset % 8)); // note, this may "write" beyond the bounds of the array at the end, but since it's oring w/ 0 values due to the mask, it's technically okay, probably, but to avoid we can pad the array on create by sizeof(T)
            // could check if minbits[i] > b and store zero there, but we'll overwrite this on decode anyways
            bitoffset += b;

            // check if still looks like a "special" block...note that this will
            // short circuit if b != 1 or we've hit a non 1 value:
            //special = special && (thisval == 1);
            // minor mod to the special check to ignore exception values:
            if (special && minbits[i] == 1)
            {
                special = special && (thisval == 1);
            }
        }

        // now convert our array into a vector w/ our PFOR encoding format, and
        // add in the exceptions in varbyte encoding at the end:
        std::vector< uint8_t > *retvalptr = new std::vector< uint8_t > (1); // we know we'll have at least the first byte
        std::vector< uint8_t > &retval(*retvalptr); // to access it like it's not a pointer...

        // first entry holds both # exceptions (high nybble) and b (low nybble)
        retval[0] = (exceptions << 4) | (special ? 0 : b);
        // now add the PFOR compressed data if we're not a "special" block:
        if (!special)
        {
            // reserve the additional space to keep reallocations down:
            retval.reserve(1+(b*_blocksize/8));
            // use ptrs as iterators to add to our vector:
            retval.insert(retval.end(),outbuf,outbuf+(b*_blocksize/8));
        }
        // can clean up our memory now
        delete [] outbuf;
        if (exceptions > 0)
        {
            VarByteUInt< T > varbyte(0);
            // add offsets of the exceptions next, varbyte encoded (if blocksize > 128)
            // then add varbyte encoded exception values
            for (unsigned i(0);i<_blocksize;++i)
            {
                if (minbits[i] > b)
                {
                    //std::cout << "encoding exception at " << i << " value " << (*(vstart+i)) << std::endl;
                    std::vector< uint8_t > encoff(varbyte.encode(i));
                    retval.insert(retval.end(),encoff.begin(),encoff.end());
                    std::vector< uint8_t > encval(varbyte.encode(*(vstart+i)));
                    retval.insert(retval.end(),encval.begin(),encval.end());
                }
            }
        }

        // for diags
        last_exceptions = exceptions;
        last_b = b;
        last_special = special;

        return retvalptr;
    }


    // you'll need to delete the returned vector pointer...
    std::vector< T > *decode(typename std::vector< uint8_t > encvals, uint32_t *count=NULL)
    {
        last_errorcode = 0;
        if (encvals.size() == 0)
        {
            last_errorcode = 3;
            return NULL; // or throw
        }
        // private copy of encvals, padded out an extra sizeof(T) bytes (simple
        // hack for now, could probably handle in & out of the for loop below more
        // gracefully in the future)
        encvals.resize(encvals.size()+sizeof(T),0);
        // first byte: high nybble == # exceptions, low nybble == b.
        uint8_t exceptions((encvals[0]) >> 4);
        uint8_t b((encvals[0]) & 0x0f);
        bool special(b==0);
        std::vector< T > *retvalptr = new std::vector< T >(_blocksize, (T)special); // init vals to '1' if we're "special"
        std::vector< T > &retval(*retvalptr);
        T mask((1 << b) - 1);
        //std::cout << "decode, b == " << int(b) << " exceptions == " << int(exceptions) << " special == " << int(special) << std::endl;
        if (!special)
        {
            // not special, need to decode the PFOR data:

            // to keep from walking off the end of the encvals data, we need to stop before the end,
            // our b is 1-15, and we need to stop sizeof(T) bytes before the end of encvals, so: sizeof(T) == N*b/8 => N == 8*sizeof(T)/b
            //unsigned stopgap(sizeof(T)*8/b); // int math will round down (floor)
            //for (unsigned i(0);i<(_blocksize-stopgap);++i)
            for (unsigned i(0);i<_blocksize;++i)
            {
                uint8_t off(b*i/8);
                T * valptr((T *)((&encvals[1]) + off));
                // this next part probably only works for x86 little endian arch:
                retval[i] = (((*valptr) >> ((b*i)%8)) & mask); // note, this may "read" beyond the bounds of the array at the end, but since it's anding w/ 0 values due to the mask, it should be technically okay, but to avoid we'll stop before that happens and deal w/ that special stuff outside the for loop...or for now just make a padded local copy of the data...
                //std::cout << "decode, retval " << i << " == " << retval[i] << std::endl;
            }
            // handle the last elements special to avoid appearing to walk off the end:
        }
        // now deal w/ the exceptions, if any:
        uint32_t eoff(special ? 1 : 1+(b*_blocksize/8)); // where do they start in the vector, if any?
        VarByteUInt< T > varbyte(0); // for decoding
        for (unsigned i(0);i<exceptions;++i)
        {
            uint8_t nb(0);
            // get varbyte encoded value of the index of the exception at eoff
            T valoff(varbyte.decode(&encvals[eoff],&nb));
            //std::cout << "decode, handling exception at " << valoff << " eoff == " << eoff << std::endl;
            // now get varbyte encoded exceptional value at eoff+nb
            eoff += nb; // offset to the actual value
            T newval(varbyte.decode(&encvals[eoff],&nb));
            retval[valoff] = newval;
            //std::cout << "decode, new value for exception at " << valoff << " == " << newval << " eoff == "  << eoff << std::endl;
            eoff += nb;
        }
        if (count != NULL)
        {
            (*count) = eoff;
        }

        // for diags
        last_exceptions = exceptions;
        last_b = b;
        last_special = special;

        return retvalptr;
    }

    // again, you'll need to delete the returned vector pointer...
    //
    // probably should turn this around at some point and make the vector
    // reference one be the wrapper and the iterator one be the "real"
    // implementation, but for now...
    std::vector< T > *decode(typename std::vector< uint8_t >::iterator encvals_begin,
                             typename std::vector< uint8_t >::iterator encvals_end,
                             uint32_t *count=NULL)
    {
        std::vector< T > *retvalptr(new std::vector< T >());
        std::vector< uint8_t >::iterator iter(encvals_begin);
        std::vector< uint8_t >::iterator end(encvals_end);
        uint32_t cnt(0);
        while (iter < end)
        {
            // create temp copy of data (horrible, I know, will fix in future)
            std::vector< uint8_t > cdata(iter,end);
            uint32_t loopcnt(0);
            std::vector< T >* cur_data(decode(cdata,&loopcnt));
            cnt += loopcnt;
            iter += loopcnt;
            std::copy(cur_data->begin(),cur_data->end(),std::inserter(*retvalptr,retvalptr->end()));
            delete cur_data;
        }
        if (count)
        {
            *count = cnt;
        }
        return retvalptr;
    }

    // for "stream" decoding of a pointer to an array of uint8_t or char:
    //T decode(uint8_t *dp)
    //helper funcs to convert to/from delta lists...you can use the startval
    //if you don't want the first value in the vector to be the reference value
    static void convert_to_deltas(
        typename std::vector< T > &vec,
        T startval = 0)
    {
        for (unsigned i(vec.size()-1);i>0;--i) {
            vec[i] = vec[i] - vec[i-1];
        }
        // left out check for startval > 0, figuring
        // check/jmp might be more expensive than sub of 0
        vec[0] = vec[0] - startval;
    }

    static void convert_from_deltas(typename std::vector< T > &vec, T startval = 0)
    {
        // left out check for startval > 0,
        // figuring check/jmp might be more expensive than add of 0
        vec[0] = vec[0] + startval;
        unsigned numvals(vec.size());
        for (unsigned i(1);i<numvals;++i) {
            vec[i] = vec[i] + vec[i-1];
        }
    }


};


#endif // __PFOR_HPP_INCLUDED__
