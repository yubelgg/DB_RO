import os
from simplerpc.marshal import Marshal
from simplerpc.future import Future

class ShardKvService(object):
    PUT = 0x362b501b
    APPEND = 0x5e937191
    GET = 0x400b7569

    __input_type_info__ = {
        'Put': ['uint64_t','std::string','std::string'],
        'Append': ['uint64_t','std::string','std::string'],
        'Get': ['uint64_t','std::string'],
    }

    __output_type_info__ = {
        'Put': ['uint32_t'],
        'Append': ['uint32_t'],
        'Get': ['uint32_t','std::string'],
    }

    def __bind_helper__(self, func):
        def f(*args):
            return getattr(self, func.__name__)(*args)
        return f

    def __reg_to__(self, server):
        server.__reg_func__(ShardKvService.PUT, self.__bind_helper__(self.Put), ['uint64_t','std::string','std::string'], ['uint32_t'])
        server.__reg_func__(ShardKvService.APPEND, self.__bind_helper__(self.Append), ['uint64_t','std::string','std::string'], ['uint32_t'])
        server.__reg_func__(ShardKvService.GET, self.__bind_helper__(self.Get), ['uint64_t','std::string'], ['uint32_t','std::string'])

    def Put(__self__, op_id, key, value):
        raise NotImplementedError('subclass ShardKvService and implement your own Put function')

    def Append(__self__, op_id, key, value):
        raise NotImplementedError('subclass ShardKvService and implement your own Append function')

    def Get(__self__, op_id, key):
        raise NotImplementedError('subclass ShardKvService and implement your own Get function')

class ShardKvProxy(object):
    def __init__(self, clnt):
        self.__clnt__ = clnt

    def async_Put(__self__, op_id, key, value):
        return __self__.__clnt__.async_call(ShardKvService.PUT, [op_id, key, value], ShardKvService.__input_type_info__['Put'], ShardKvService.__output_type_info__['Put'])

    def async_Append(__self__, op_id, key, value):
        return __self__.__clnt__.async_call(ShardKvService.APPEND, [op_id, key, value], ShardKvService.__input_type_info__['Append'], ShardKvService.__output_type_info__['Append'])

    def async_Get(__self__, op_id, key):
        return __self__.__clnt__.async_call(ShardKvService.GET, [op_id, key], ShardKvService.__input_type_info__['Get'], ShardKvService.__output_type_info__['Get'])

    def sync_Put(__self__, op_id, key, value):
        __result__ = __self__.__clnt__.sync_call(ShardKvService.PUT, [op_id, key, value], ShardKvService.__input_type_info__['Put'], ShardKvService.__output_type_info__['Put'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_Append(__self__, op_id, key, value):
        __result__ = __self__.__clnt__.sync_call(ShardKvService.APPEND, [op_id, key, value], ShardKvService.__input_type_info__['Append'], ShardKvService.__output_type_info__['Append'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_Get(__self__, op_id, key):
        __result__ = __self__.__clnt__.sync_call(ShardKvService.GET, [op_id, key], ShardKvService.__input_type_info__['Get'], ShardKvService.__output_type_info__['Get'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

