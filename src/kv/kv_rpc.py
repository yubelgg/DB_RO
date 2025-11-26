import os
from simplerpc.marshal import Marshal
from simplerpc.future import Future

class KvService(object):
    PUT = 0x290f6531
    APPEND = 0x345937c6
    GET = 0x4b3e2e6a

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
        server.__reg_func__(KvService.PUT, self.__bind_helper__(self.Put), ['uint64_t','std::string','std::string'], ['uint32_t'])
        server.__reg_func__(KvService.APPEND, self.__bind_helper__(self.Append), ['uint64_t','std::string','std::string'], ['uint32_t'])
        server.__reg_func__(KvService.GET, self.__bind_helper__(self.Get), ['uint64_t','std::string'], ['uint32_t','std::string'])

    def Put(__self__, op_id, key, value):
        raise NotImplementedError('subclass KvService and implement your own Put function')

    def Append(__self__, op_id, key, value):
        raise NotImplementedError('subclass KvService and implement your own Append function')

    def Get(__self__, op_id, key):
        raise NotImplementedError('subclass KvService and implement your own Get function')

class KvProxy(object):
    def __init__(self, clnt):
        self.__clnt__ = clnt

    def async_Put(__self__, op_id, key, value):
        return __self__.__clnt__.async_call(KvService.PUT, [op_id, key, value], KvService.__input_type_info__['Put'], KvService.__output_type_info__['Put'])

    def async_Append(__self__, op_id, key, value):
        return __self__.__clnt__.async_call(KvService.APPEND, [op_id, key, value], KvService.__input_type_info__['Append'], KvService.__output_type_info__['Append'])

    def async_Get(__self__, op_id, key):
        return __self__.__clnt__.async_call(KvService.GET, [op_id, key], KvService.__input_type_info__['Get'], KvService.__output_type_info__['Get'])

    def sync_Put(__self__, op_id, key, value):
        __result__ = __self__.__clnt__.sync_call(KvService.PUT, [op_id, key, value], KvService.__input_type_info__['Put'], KvService.__output_type_info__['Put'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_Append(__self__, op_id, key, value):
        __result__ = __self__.__clnt__.sync_call(KvService.APPEND, [op_id, key, value], KvService.__input_type_info__['Append'], KvService.__output_type_info__['Append'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

    def sync_Get(__self__, op_id, key):
        __result__ = __self__.__clnt__.sync_call(KvService.GET, [op_id, key], KvService.__input_type_info__['Get'], KvService.__output_type_info__['Get'])
        if __result__[0] != 0:
            raise Exception("RPC returned non-zero error code %d: %s" % (__result__[0], os.strerror(__result__[0])))
        if len(__result__[1]) == 1:
            return __result__[1][0]
        elif len(__result__[1]) > 1:
            return __result__[1]

