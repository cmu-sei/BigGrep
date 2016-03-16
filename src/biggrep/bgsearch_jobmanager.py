# BigGrep
#
# @license:   GPL and Gov't Purpose, see LICENSE.txt for details
# @copyright: 2013 by Carnegie Mellon University
# @author:    Matt Coates <mc-contact@cert.org>


import logging
import jobdispatch
import bgsearch
import re
import collections

logger = logging.getLogger(__name__)

BgSearchJob = collections.namedtuple('BgSearchJob',['terms','input','duration'])
BgResultJob = collections.namedtuple('BgResultJob',['state','terms','result_tuples','count','duration'])

class BgSearchJobManager(jobdispatch.JobManager):
    job_spec_key="bgsearch"
    def __init__(self,candidate_limit=0,filter_criteria=None,verify=False):
        self.sjobs=collections.deque()
        self.vjobs=collections.deque()
        self.completed_jobs = collections.deque()
        self.candidate_count = 0
        self.candidate_limit = candidate_limit
        self.verified_count = 0
        self.verify_checked_count = 0
        self.searched_count = 0
        self.candidate_limit_reached = False
        self.filter_criteria = filter_criteria
        self.working = True
        self.verify=verify
        self.search_duration = 0.0
        self.verify_duration = 0.0
        self.verify_jobs = 0
    
    def getJob(self,processor):
        if processor.job_spec[BgSearchJobManager.job_spec_key] == "search":
            try:
                return self.sjobs.pop()
            except IndexError:
                pass

        if processor.job_spec[BgSearchJobManager.job_spec_key] == "verify":
            try:
                return self.vjobs.pop()
            except IndexError:
                pass

    def putJob(self,job):
        self.working = True
        if job.state == "searchdone":
            self.search_duration += job.duration
            self.searched_count += 1
            if self.candidate_limit > 0 and self.candidate_count > self.candidate_limit:
                logger.debug("candidate_count > candidate_limit")
                #ensure job processing stops
                self.candidate_limit_reached = True

            filtered_results = []
            for (f,m) in job.result_tuples:
                #metadata filtering
                filtered = True
                if self.filter_criteria and len(self.filter_criteria) >0 :
                    metadata=dict([bgsearch.split_typeify(met) for met in m.split(',') if met])
                    (filtered,missing_keys) = bgsearch.filter_metadata(self.filter_criteria,metadata)
                    missing_keys=[re.sub('[;,]','_',x) for x in missing_keys]
                    if filtered == None:
                        logger.error("Malformed filter criteria supplied.")
                        exit(1)
                    if len(missing_keys) > 0:
                        logger.warning("%s has no %s metadata and could not be filtered using that criteria."%(f,str(missing_keys))) 
                        m+=",FILTER_MISSING_METADATA="+';'.join(missing_keys)
                if filtered:
                    if self.verify and not self.candidate_limit_reached:
                        self.vjobs.append(BgSearchJob(terms=job.terms,input=[(f,m)],duration=0))
                    else:
                        self.completed_jobs.append(BgResultJob(state='complete',terms=job.terms,result_tuples=[(f,m)],count=0,duration=0))
                    self.candidate_count+=1
        if job.state == "verifydone":
            logger.debug("adding job to completed_jobs")
            if job.count > 0:
                self.verified_count += len(job.result_tuples)
                self.verify_checked_count += job.count
                self.completed_jobs.append(job)
                self.verify_duration += job.duration
                self.verify_jobs += 1
        self.working = False

    def addJob(self,searchterms,index_file,verify=False,verbose=False,debug=False):
        self.sjobs.append(BgSearchJob(terms=searchterms,input=index_file,duration=0))

    def addVerifyOnlyJob(self,searchterms,to_verify,verbose=False,debug=False):
        j = BgSearchJob(searchterms,"nothing",verbose,debug)
        j.status = "verify"
        j.to_verify = [to_verify]
        self.vjobs.append(j)


    def getCompletedJob(self):
       try:
           return self.completed_jobs.pop()
       except IndexError:
           return None
#
#class BgVerifyJobManager(object):
#    job_spec_key="bgverify"
#    def __init__(self):
#        self.jobs=[]
#
#    def getJob(self,processor):
#        return self.jobs.pop()
#
#    def putJob(self,job):
#        pass
#
#    def addJob(self,job):
#        self.jobs.append(job)
