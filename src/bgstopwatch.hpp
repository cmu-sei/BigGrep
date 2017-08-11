// class to calc timing detlas, etc
// uses Boost Timer

#include <boost/timer/timer.hpp>

#define MICROSEC_TO_SEC 0.000001
#define NANOSEC_TO_SEC 0.000000001

class bgstopwatch
{
public:
    // CTOR
    bgstopwatch() { }
    float secondsFromLast()
    {
        boost::timer::cpu_times current = _prev.elapsed();
        _prev.start();
        return current.wall*NANOSEC_TO_SEC;
    }
    float secondsFromStart()
    {
        boost::timer::cpu_times times = timer.elapsed();
        return times.wall*NANOSEC_TO_SEC;
    }
    void restart()
    {
        timer.start();
        _prev.start();
    }
private:
    boost::timer::cpu_timer timer; //starts immediately
    boost::timer::cpu_timer _prev;  // last time an interval was checked
};



 /*
//OLD boost timer code - pre 1.48
#include <boost/timer.hpp>
class bgstopwatch
{
public:
    // CTOR
    bgstopwatch() { }
    float secondsFromLast()
    {
        double current = prevtimer.elapsed();
        prevtimer.restart();
        return current;
    }
    float secondsFromStart()
    {
        double times = bgtimer.elapsed();
        return times;
    }
    void restart()
    {
        bgtimer.restart();
        prevtimer.start();
    }
private:
    boost::timer bgtimer;
    boost::timer prevtimer;
};
 */
