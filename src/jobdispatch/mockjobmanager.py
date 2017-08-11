# jobdispatch
#
# @license:   GPL and Govt't Purpose, see LICENSE.txt for details
# @copyright: 2013-2017 by Carnegie Mellon University
# @author:    Matt Coates <mfcoates@cert.org>
import random
import logging

logger = logging.getLogger(__name__)

class MockJob(object):
    pass

class MockManager(object):
    def getJob(self,processor):
        j=MockJob()
        j.value="foo%d"%random.randint(0,10000)
        logger.debug("getting %s"%j.value)
        return j
    def putJob(self,job):
        logger.debug("putting %s"%job.value)

