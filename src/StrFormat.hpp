// Copyright 2011-2017 Carnegie Mellon University.  See LICENSE file for terms.
// simple std::string formatter function using vsnprintf, because I don't want
// to use boost::format, Loki::SafeFormat, or fastformat just yet...

#include <cstdarg>
#include <string>
#include <stdint.h>
#include <cstdio>

#define STRFORMAT_DEFAULT_BUF_SIZE 256

inline
std::string StrFormat(const char* fmt, ...)
{
  std::string retstr;
  char buffer[STRFORMAT_DEFAULT_BUF_SIZE];
  int buflen(STRFORMAT_DEFAULT_BUF_SIZE);
  char *buf(buffer);
  int required_len;

  va_list args;

  bool done(false);
  while (!done)
  {
    va_start(args,fmt);
    required_len = std::vsnprintf(buf,buflen,fmt,args);
    if (required_len >= buflen)
    {
      if (buf && buf != buffer)
      {
        delete [] buf;
      }
      buf = new char[required_len+1];
      buflen = required_len+1;
    }
    else
    {
      done = true;
    }
    va_end(args);
  }

  retstr = buf;


  // free up alloced mem if necessary, should probably do this w/ a scoped
  // class, but this will work for now:
  if (buf && buf != buffer)
  {
    delete [] buf;
  }

  return retstr;
}
