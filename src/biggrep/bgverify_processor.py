# BigGrep
#
# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2013 by Carnegie Mellon University
# @author:    Matt Coates <mc-contact@cert.org>
import jobdispatch
import logging
import bgsearch
import re
import bgsearch_jobmanager

logger = logging.getLogger(__name__)


#(verify,[search,term],path/to/candidate)
class BgVerifyProcessor(jobdispatch.Processor):
    def do(self):
        j=self._startJob()
        meta=dict(j.input)
        (vbsize,v_results,duration)=bgsearch.verify(j.terms,meta.keys(),logger,self.verbose,self.debug)
        self._finishedJob(bgsearch_jobmanager.BgResultJob(state='verifydone',terms=j.terms,result_tuples=[(f,meta[f]) for (f,_) in v_results],count=vbsize,duration=duration))

class VerifyYaraProcessor(jobdispatch.Processor):
    def do(self):
        j=self._startJob()
        meta=dict(j.input)
        (vbsize,v_results,duration)=bgsearch.verify_yara_cli(self.yara_file,meta.keys(),logger,self.verbose,self.debug)
        results=[]
        for (f,y_matches) in v_results:
            metadata_string = ';'.join(re.sub('[;,]','_',str(x)) for x in y_matches)
            m=meta[f]+",YARA_MATCHES="+metadata_string
            results.append((f,m))

        self._finishedJob(bgsearch_jobmanager.BgResultJob(state='verifydone',terms=j.terms,result_tuples=results,count=vbsize,duration=duration))
