# jobdispatch
#
# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2013 by Carnegie Mellon University
# @author:    Matt Coates <mc-contact@cert.org>
import threading
import time
import logging
from processor import ProcessorEngine

logger = logging.getLogger(__name__)

class JobDispatcher(threading.Thread):
    def __init__(self,jobmanager):
        threading.Thread.__init__(self)
        self.processor_threads = []
        self.jobs = []
        self.live = True
        self.jobmanager=jobmanager

    def signal_children_exit(self):
        #we've been asked to stop doing work, signal processing threads
        for p in self.processor_threads:
            p.live=False

    def run(self):
        logger.debug("Starting job dispatcher")
        while(self.live):
            self.nowork = True
            self.dispatch_process_watch()
            if self.nowork:
                time.sleep(.01)
        self.signal_children_exit()
        #keep an eye on processor threads until they're all stopped, clean up work as you go
        notready=True
        while(notready):
            notready=False
            for p in self.processor_threads:
                self.processResults(p)
                if p.isAlive():
                    notready=True
                    break
            time.sleep(.01)

    def dispatchWork(self):
        #iterate through processor threads, find one that's available
        for (i,p) in enumerate(self.processor_threads):
            if p.processor.needsJob() and p.isAlive():
                logger.debug("processor %d needs work"%i)
                job = None
                job = self.jobmanager.getJob(p.processor)
                if not job:
                    #no jobs available, move on to next thread
                    logger.debug("No jobs located for processor %d"%i)
                    continue
                #self.jobs.append(job)
                #Hand job to available thread
                p.processor.addJob(job)
                logger.debug("job added to processor %d"%i)
                self.nowork = False

    def processResults(self):
        #pull completed jobs from processor threads, and let the job manager put them back
        #logger.debug("processResults")
        for p in self.processor_threads:
            for r in p.processor.getResults():
                logger.debug("got result job")
                self.jobmanager.putJob(r)

    def processResults(self,p):
        for r in p.processor.getResults():
            logger.debug("got result job")
            self.jobmanager.putJob(r)

    def addProcessorEngine(self,pt):
        #add a child of ProcessorEngine to the job dispatcher and start the thread
        self.processor_threads.append(pt)
        pt.start()

    def addProcessor(self,p):
        pe=ProcessorEngine(p)
        self.addProcessorEngine(pe)
        return pe

    def watchdog(self):
        #monitor threads, and replace dead ones
        for p in self.processor_threads:
            if p.started and p.live and not p.isAlive():
                logger.error("A processor thread failed, restarting thread")
                self.addProcessor(p.processor)
                p.live = False
    def dispatch_process_watch(self):
        #iterate through processor threads, find one that's available
        for (i,p) in enumerate(self.processor_threads):
            if p.processor.needsJob() and p.isAlive():
                logger.debug("processor %d needs work"%i)
                job = None
                job = self.jobmanager.getJob(p.processor)
                if job:
                    #self.jobs.append(job)
                    #Hand job to available thread
                    p.processor.addJob(job)
                    logger.debug("job added to processor %d"%i)
                    self.nowork = False
                else:
                    #no jobs available, move on to next thread
                    logger.debug("No jobs located for processor %d"%i)
            #pull completed jobs from processor threads, and let the job manager put them back
            self.processResults(p)
            #monitor threads, and replace dead ones
            if p.started and p.live and not p.isAlive():
                logger.error("A processor thread failed, restarting thread")
                self.addProcessor(p.processor)
                p.live = False



