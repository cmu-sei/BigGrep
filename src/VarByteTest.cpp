// Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

#include <iostream>

#define __STDC_LIMIT_MACROS // I have to define this to get UINT32_MAX because of the ISO C99 standard?
#define __STDC_CONSTANT_MACROS // ustl wants this
#include <stdint.h>

#include <vector>

#include "VarByte.hpp"
#include "StrFormat.hpp"

using namespace std;

int
main(int argc, char** argv)
{
  uint64_t num1(0);
  uint64_t cnum(0);
  uint8_t arraytest[5] = {0x81,0xeb,0xbe,0xa6,0x01}; // 349156737
  VarByteUInt< uint64_t > varbyte(0);

  for (uint64_t i(73);i < UINT64_MAX/3; i *= 3)
  {
    num1 = i;
    vector< uint8_t > encnum(varbyte.encode(num1));
    cout << endl << num1 << " encoded to " << encnum.size() << " bytes" << endl;
    for (int j(0);j<encnum.size();++j)
    {
      cout << StrFormat("  0x%02x",encnum[j]);
    }
    cout << endl;
    cnum = varbyte.decode();
    cout << "decoded to " << cnum << endl;
    if (cnum != num1)
    {
      cout << "ERROR!" << endl;
    }
  }

  cout << endl << "array (stream) decode test, should decode to 349156737...";
  uint64_t atest = varbyte.decode(&arraytest[0]);
  if (atest == 349156737)
  {
    cout << "and it did!" << endl;
  }
  else
  {
    cout << "but it didn't: " << atest << endl;
    cout << "ERROR" << endl;
  }

  cout << endl << "test done" << endl << endl;

  return 0;
}
