# jobdispatch
#
# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2013 by Carnegie Mellon University
# @author:    Matt Coates <mc-contact@cert.org>
import threading
import time
import logging

logger = logging.getLogger(__name__)

class NoWork(Exception):
    pass
class Processor(object):
    #job_spec is a dictionary containing parameters used by different job managers to fetch jobs appropriate for this processor. Key values for this dict can be found in the various job manager classes.
    def __init__(self,(job_spec_key,job_spec_value)):
        self.job_spec={}
        self.job_spec[job_spec_key]=job_spec_value
        self._listlock = threading.Lock()
        self._jobs = []
        self._done = []

    def needsJob(self):
        """returns true if the package can accept a new job"""
        job_needed = True
        with self._listlock:
            if len(self._jobs) > 0:
                job_needed = False
        return job_needed

    def getResults(self):
        """returns array of jobs that are ready to be checked in"""
        returnlist = None
        with self._listlock:
            returnlist = self._done
            self._done = []
        return returnlist

    def addJob(self,job):
        """adds a job for processing"""
        with self._listlock:
            self._jobs.append(job)

    def _startJob(self):
        """pull a job to work on"""
        job = None
        with self._listlock:
            try:
                job = self._jobs.pop()
            except IndexError:
                raise NoWork
        return job

    def _finishedJob(self,job):
        """place a job in the finished list"""
        with self._listlock:
            self._done.append(job)
        logger.debug("_jobs: %d _done: %d"%(len(self._jobs),len(self._done)))

    def do(self):
        """does the actual work"""
        pass

    def clean(self):
        """cleans after a hard failure"""
        pass

class ProcessorEngine(threading.Thread):
    def __init__(self,processor):
        threading.Thread.__init__(self)
        self.processor = processor
        self.live = True
        self.started = False
        self._pause = False

    def run(self):
        logger.debug("starting processor engine thread")
        self.start = True
        while(self.live):
           if self._pause:
               time.sleep(.1)
               continue
           #if self.processor.needsJob():
           #   logger.debug("processor needs job, sleeping")
           #   time.sleep(.01)
           #   continue
           try:
               self.processor.do()
           except NoWork:
#               logger.debug("processor needs job, sleeping")
               time.sleep(.01)
               continue
        self.processor.clean()

    def pause(self):
        self._pause = True

    def resume(self):
        self._pause = False

class MockProcessor(Processor):

    #def __init__(self,(key,value)):
    #    super(Processor).__init__(self,(key,value))
    #    #add to the job_spec dict to support the jobqueue job manager
    #    #job_spec["jqstatuses"]=["status1","status2"]
    #    self.job_spec[key]=value

    def do(self):
        j=self._startJob()
        logger.debug("doing %s"%j.value)
        if int(j.value[3:]) % 9 == 0:
            logger.debug("causing a problem with job %s"%j.value)
            raise Exception
        j.done=True
        self._finishedJob(j)

