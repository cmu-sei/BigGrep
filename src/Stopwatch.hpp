// Copyright 2011-2016 Carnegie Mellon University.  See LICENSE file for terms.

// class to calc timing detlas, etc
// uses POCO Timestamp

#include "Poco/Timestamp.h"

#define MICROSEC_TO_SEC 0.000001

class Stopwatch
{
public:
  // CTOR
  Stopwatch() { }
  float secondsFromLast()
  {
    Poco::Timestamp current;
    Poco::Timestamp::TimeDiff delta (current - _prev);
    _prev = current;
    return delta*MICROSEC_TO_SEC;
  }
  float secondsFromStart()
  {
    Poco::Timestamp current;
    Poco::Timestamp::TimeDiff delta (current - _start);
    _prev = current;
    return delta*MICROSEC_TO_SEC;
  }
private:
  Poco::Timestamp _start; // start time
  Poco::Timestamp _prev;  // last time an interval was checked
};

