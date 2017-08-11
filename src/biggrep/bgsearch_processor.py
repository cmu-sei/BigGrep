# BigGrep
#
# @license:   GPL and Govt't Purpose, see LICENSE.txt for details
# @copyright: 2013-2017 by Carnegie Mellon University
# @author:    Matt Coates <mc-help@cert.org>
import jobdispatch
import subprocess
import logging
import signal
import bgsearch
import bgsearch_jobmanager

logger = logging.getLogger(__name__)

#(search,[search,term],path/to/index.bgi)
class BgSearchProcessor(jobdispatch.Processor):

    def do(self):
        j=self._startJob()
        (_,results,duration,num_files)=bgsearch.parse(j.terms,j.input,logger,self.verbose,self.debug, self.metrics)
        self._finishedJob(bgsearch_jobmanager.BgResultJob(state='searchdone',terms=j.terms,result_tuples=results,count=0,duration=duration,num_files=num_files,input=j.input))
