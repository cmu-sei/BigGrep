// Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

// File:
//   bgi_header.hpp
//
// Description:
//   A class to consolidate interactions w/ the BGI index file format header,
//   to make working w/ multiple versions of it "easier"?
//

#ifndef __BGI_HEADER__
#define __BGI_HEADER__

// structure for the .bgi file header info
class bgi_header
{
public:
  char magic[8]; // 'BIGGREP\n'
  uint8_t fmt_major;
  uint8_t fmt_minor;
  uint8_t N; // will be 3 or 4
  uint8_t hint_type; // added in format 2.1: 0 = "last" byte trimmed, 1 =
                     // "last" nybble trimmed, 2 = nothing trimmed/whole ngram
                     // used
  uint8_t pfor_blocksize;
  uint32_t num_ngrams;
  uint32_t num_files;
  uint64_t fileid_map_offset;

  // "v1" == 2.0
  // "v2" == 2.1, adds one additional byte to the header (the hint_type field)
  //bgi_header_v1(uint8_t _n):
  // going to handle more generically for now:
  bgi_header(uint8_t _n = 3, uint8_t _fmt_maj = 2, uint8_t _fmt_min = 1):
    //magic({'B','I','G','G','R','E','P','\n'}),
    fmt_major(_fmt_maj),fmt_minor(_fmt_min), // 2.1 is the new default, change to old 2.0 if need be
    N(_n),hint_type(0),pfor_blocksize(0),num_ngrams(0),num_files(0),fileid_map_offset(0) // fill these in properly before writing real data...
  {
    // couldn't figure out how/if possible to init magic like you would an array defined inline normally, so have to resort to this:
    strncpy(magic,"BIGGREP\n",sizeof(magic));
  }

  inline
  bool
  has_hint_type()
  {
    if (fmt_major >= 2 && fmt_minor >= 1)
    {
      return true;
    }
    return false;
  }

  inline
  uint32_t
  header_size()
  {
    //return 28; // probably should calculate via adding sizeof(), but eh, this okay for now...
    // v2 == 29, account for that:
    uint32_t rc(28);
    if (has_hint_type())
    {
      ++rc;
    }
    return rc;
  }

  inline
  uint32_t
  num_hints()
  {
    //return 1<<(8*(N-1));
    // again, now based on hint_type:
    uint32_t nh(0);
    switch (hint_type)
    {
      case 0: // v1 will default to this
        nh = 1<<(8*(N-1)); // N=3 -> 64K, N=4 -> 16M
        break;
      case 1:
        nh = 1<<(8*N-4); // N=3 -> 1M, N=4 -> 256M
        break;
      case 2:
        nh = 1<<(8*N); // N=3 -> 16M, N=4 -> 4G
        break;
      default: // should never get here, be dramatic if we do:
        //throw std::exception("wrong hint type");
        throw std::exception();
        break;
    }
    return nh;
  }

  inline
  uint32_t
  hints_size()
  {
    // sizeof(uint64_t)*sizeof(ngram hints range) [8 x 2^(8*(N-1))]
    //return 8*(1<<(8*(N-1)));
    // this now returns a different value based on N and the hint_type:
    // type 0 (trim LS byte)   N=3 -> 512KB, N=4 -> 128MB
    // type 1 (trim LS nybble) N=3 -> 8MB,   N=4 -> 2GB
    // type 2 (trim nothing)   N=3 -> 128MB, N=4 -> 32GB
    return sizeof(uint64_t)*num_hints();
  }

  inline
  uint32_t
  hint_type_mask()
  {
    // 0x000000FF for type 0, 0x0000000F for type 1, 0x00000000 for type 2
    uint32_t rc(0x000000FF>>(4*hint_type));
    return rc;
  }

  inline
  uint32_t
  ngram_to_hint(uint32_t ngram)
  {
    uint32_t rc(ngram>>(4*(2-hint_type)));
    return rc;
  }

  void
  write(ostream& out)
  {
    out.write(reinterpret_cast<const char*>(&magic),sizeof(magic));
    out.write(reinterpret_cast<const char*>(&fmt_major),sizeof(fmt_major));
    out.write(reinterpret_cast<const char*>(&fmt_minor),sizeof(fmt_minor));
    // note, those above will actually set/reset the has_hint_type trigger too
    out.write(reinterpret_cast<const char*>(&N),sizeof(N));
    if (has_hint_type())
    {
      out.write(reinterpret_cast<const char*>(&hint_type),sizeof(hint_type));
    }
    out.write(reinterpret_cast<const char*>(&pfor_blocksize),sizeof(pfor_blocksize));
    out.write(reinterpret_cast<const char*>(&num_ngrams),sizeof(num_ngrams));
    out.write(reinterpret_cast<const char*>(&num_files),sizeof(num_files));
    out.write(reinterpret_cast<const char*>(&fileid_map_offset),sizeof(fileid_map_offset));
  }

  void
  read(void *mptr) // takes a mmap pointer
  {
    const char *ptr((const char*)mptr);
    memcpy(magic,ptr,sizeof(magic));
    ptr += sizeof(magic);
    fmt_major = *(uint8_t*)ptr;
    ptr += sizeof(fmt_major);
    fmt_minor = *(uint8_t*)ptr;
    ptr += sizeof(fmt_minor);
    N = *(uint8_t*)ptr;
    ptr += sizeof(N);
    if (has_hint_type())
    {
      hint_type = *(uint8_t*)ptr;
      ptr += sizeof(hint_type);
    }
    pfor_blocksize = *(uint8_t*)ptr;
    ptr += sizeof(pfor_blocksize);
    num_ngrams = *(uint32_t*)ptr;
    ptr += sizeof(num_ngrams);
    num_files = *(uint32_t*)ptr;
    ptr += sizeof(num_files);
    fileid_map_offset = *(uint64_t*)ptr;
  }

  std::string
  dump()
  {
    std::ostringstream oss;
    oss << "BGI Header:" << std::endl;
    oss << "  magic == " << string(magic,magic+sizeof(magic)) << std::endl;
    oss << "  fmt_major == " << (uint32_t)fmt_major << std::endl;
    oss << "  fmt_minor == " << (uint32_t)fmt_minor << std::endl;
    oss << "  N == " << (uint32_t)N << std::endl;
    oss << "    hint_type == " << (uint32_t)hint_type << std::endl;
    oss << "    num_hints == " << num_hints() << std::endl;
    oss << "    hints size == " << hints_size() << std::endl;
    oss << "  pfor_blocksize == " << (uint32_t)pfor_blocksize << std::endl;
    oss << "  num_ngrams == " << num_ngrams << std::endl;
    oss << "  num_files == " << num_files << std::endl;
    oss << "  fileid_map_offset == 0x" << std::hex << fileid_map_offset << std::dec << std::endl;
    return oss.str();
  }
};

//typedef class bgi_header_v1 bgi_header;
typedef class bgi_header bgi_header;

#endif // __BGI_HEADER__
