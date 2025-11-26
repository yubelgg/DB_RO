import os
from simplerpc.marshal import Marshal
from simplerpc.future import Future

LogEntryRPC = Marshal.reg_type('LogEntryRPC', [('term', 'uint64_t'), ('command', 'MarshallDeputy')])

class RaftService(object):
    VOTE = 0x2393abb8
    APPENDENTRIES = 0x587dd9a0
    EMPTYAPPENDENTRIES = 0x63525725

    __input_type_info__ = {
        'Vote': ['uint64_t','ballot_t','siteid_t','ballot_t'],
        'AppendEntries': ['uint64_t','ballot_t','uint64_t','uint64_t','uint64_t','uint64_t','MarshallDeputy','uint64_t'],
        'EmptyAppendEntries': ['uint64_t','ballot_t','uint64_t','uint64_t','uint64_t','uint64_t'],
    }

    __output_type_info__ = {
        'Vote': ['ballot_t','bool_t'],
        'AppendEntries': ['uint64_t','uint64_t','uint64_t'],
        'EmptyAppendEntries': ['uint64_t','uint64_t','uint64_t'],
    }

    def __bind_helper__(self, func):
        def f(*args):
            return getattr(self, func.__name__)(*args)
        return f

    def __reg_to__(self, server):
        server.__reg_func__(RaftService.VOTE, self.__bind_helper__(self.Vote), ['uint64_t','ballot_t','siteid_t','ballot_t'], ['ballot_t','bool_t'])
        server.__reg_func__(RaftService.APPENDENTRIES, self.__bind_helper__(self.AppendEntries), ['uint64_t','ballot_t','uint64_t','uint64_t','uint64_t','uint64_t','MarshallDeputy','uint64_t'], ['uint64_t','uint64_t','uint64_t'])
        server.__reg_func__(RaftService.EMPTYAPPENDENTRIES, self.__bind_helper__(self.EmptyAppendEntries), ['uint64_t','ballot_t','uint64_t','uint64_t','uint64_t','uint64_t'], ['uint64_t','uint64_t','uint64_t'])

    def Vote(__self__, lst_log_idx, lst_log_term, site_id, cur_term):
        raise NotImplementedError('subclass RaftService and implement your own Vote function')

    def AppendEntries(__self__, slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex, cmd, leaderNextLogTerm):
        raise NotImplementedError('subclass RaftService and implement your own AppendEntries function')

    def EmptyAppendEntries(__self__, slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex):
        raise NotImplementedError('subclass RaftService and implement your own EmptyAppendEntries function')

class RaftProxy(object):
    def __init__(self, clnt):
        self.__clnt__ = clnt

    def async_Vote(__self__, lst_log_idx, lst_log_term, site_id, cur_term):
        return __self__.__clnt__.async_call(RaftService.VOTE, [lst_log_idx, lst_log_term, site_id, cur_term], RaftService.__input_type_info__['Vote'], RaftService.__output_type_info__['Vote'])

    def async_AppendEntries(__self__, slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex, cmd, leaderNextLogTerm):
        return __self__.__clnt__.async_call(RaftService.APPENDENTRIES, [slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex, cmd, leaderNextLogTerm], RaftService.__input_type_info__['AppendEntries'], RaftService.__output_type_info__['AppendEntries'])

    def async_EmptyAppendEntries(__self__, slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex):
        return __self__.__clnt__.async_call(RaftService.EMPTYAPPENDENTRIES, [slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex], RaftService.__input_type_info__['EmptyAppendEntries'], RaftService.__output_type_info__['EmptyAppendEntries'])

    def sync_Vote(__self__, lst_log_idx, lst_log_term, site_id, cur_term):
        __result__ = __self__.__clnt__.sync_call(RaftService.VOTE, [lst_log_idx, lst_log_term, site_id, cur_term], RaftService.__input_type_info__['Vote'], RaftService.__output_type_info__['Vote'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_AppendEntries(__self__, slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex, cmd, leaderNextLogTerm):
        __result__ = __self__.__clnt__.sync_call(RaftService.APPENDENTRIES, [slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex, cmd, leaderNextLogTerm], RaftService.__input_type_info__['AppendEntries'], RaftService.__output_type_info__['AppendEntries'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_EmptyAppendEntries(__self__, slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex):
        __result__ = __self__.__clnt__.sync_call(RaftService.EMPTYAPPENDENTRIES, [slot, ballot, leaderCurrentTerm, leaderPrevLogIndex, leaderPrevLogTerm, leaderCommitIndex], RaftService.__input_type_info__['EmptyAppendEntries'], RaftService.__output_type_info__['EmptyAppendEntries'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

