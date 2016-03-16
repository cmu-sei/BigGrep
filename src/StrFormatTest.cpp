// Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

// simple std::string formatter function using vsnprintf, because I don't want
// to use boost::format, Loki::SafeFormat, or fastformat just yet...

#include "StrFormat.hpp"

#include <iostream>

int
main(int argc, char ** argv)
{
  std::cout << StrFormat("This is %s #%d attempt","my",1) << std::endl;
  std::cout << StrFormat("And this is %s #%0200d attempt that will attempt to deal with a really really really really really really long string that might actually force the reallocator to reallocate the static buffer and make sure that we don't leak memory because of that...","my",1) << std::endl;
  return 0;
}

