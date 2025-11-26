import os
from simplerpc.marshal import Marshal
from simplerpc.future import Future

class ShardMasterService(object):
    JOIN = 0x5a360926
    LEAVE = 0x4beb5f48
    MOVE = 0x3ab92f7a
    QUERY = 0x53b42396

    __input_type_info__ = {
        'Join': ['std::map<uint32_t, std::vector<uint32_t>>'],
        'Leave': ['std::vector<uint32_t>'],
        'Move': ['int32_t','uint32_t'],
        'Query': ['int32_t'],
    }

    __output_type_info__ = {
        'Join': ['uint32_t'],
        'Leave': ['uint32_t'],
        'Move': ['uint32_t'],
        'Query': ['uint32_t','ShardConfig'],
    }

    def __bind_helper__(self, func):
        def f(*args):
            return getattr(self, func.__name__)(*args)
        return f

    def __reg_to__(self, server):
        server.__reg_func__(ShardMasterService.JOIN, self.__bind_helper__(self.Join), ['std::map<uint32_t, std::vector<uint32_t>>'], ['uint32_t'])
        server.__reg_func__(ShardMasterService.LEAVE, self.__bind_helper__(self.Leave), ['std::vector<uint32_t>'], ['uint32_t'])
        server.__reg_func__(ShardMasterService.MOVE, self.__bind_helper__(self.Move), ['int32_t','uint32_t'], ['uint32_t'])
        server.__reg_func__(ShardMasterService.QUERY, self.__bind_helper__(self.Query), ['int32_t'], ['uint32_t','ShardConfig'])

    def Join(__self__, gid_server_map):
        raise NotImplementedError('subclass ShardMasterService and implement your own Join function')

    def Leave(__self__, gids):
        raise NotImplementedError('subclass ShardMasterService and implement your own Leave function')

    def Move(__self__, shard, gid):
        raise NotImplementedError('subclass ShardMasterService and implement your own Move function')

    def Query(__self__, config_no):
        raise NotImplementedError('subclass ShardMasterService and implement your own Query function')

class ShardMasterProxy(object):
    def __init__(self, clnt):
        self.__clnt__ = clnt

    def async_Join(__self__, gid_server_map):
        return __self__.__clnt__.async_call(ShardMasterService.JOIN, [gid_server_map], ShardMasterService.__input_type_info__['Join'], ShardMasterService.__output_type_info__['Join'])

    def async_Leave(__self__, gids):
        return __self__.__clnt__.async_call(ShardMasterService.LEAVE, [gids], ShardMasterService.__input_type_info__['Leave'], ShardMasterService.__output_type_info__['Leave'])

    def async_Move(__self__, shard, gid):
        return __self__.__clnt__.async_call(ShardMasterService.MOVE, [shard, gid], ShardMasterService.__input_type_info__['Move'], ShardMasterService.__output_type_info__['Move'])

    def async_Query(__self__, config_no):
        return __self__.__clnt__.async_call(ShardMasterService.QUERY, [config_no], ShardMasterService.__input_type_info__['Query'], ShardMasterService.__output_type_info__['Query'])

    def sync_Join(__self__, gid_server_map):
        __result__ = __self__.__clnt__.sync_call(ShardMasterService.JOIN, [gid_server_map], ShardMasterService.__input_type_info__['Join'], ShardMasterService.__output_type_info__['Join'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_Leave(__self__, gids):
        __result__ = __self__.__clnt__.sync_call(ShardMasterService.LEAVE, [gids], ShardMasterService.__input_type_info__['Leave'], ShardMasterService.__output_type_info__['Leave'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_Move(__self__, shard, gid):
        __result__ = __self__.__clnt__.sync_call(ShardMasterService.MOVE, [shard, gid], ShardMasterService.__input_type_info__['Move'], ShardMasterService.__output_type_info__['Move'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_Query(__self__, config_no):
        __result__ = __self__.__clnt__.sync_call(ShardMasterService.QUERY, [config_no], ShardMasterService.__input_type_info__['Query'], ShardMasterService.__output_type_info__['Query'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

