# automatically generated by the FlatBuffers compiler, do not modify

# namespace: models

import flatbuffers
from flatbuffers.compat import import_numpy
np = import_numpy()

class HelloRequest(object):
    __slots__ = ['_tab']

    @classmethod
    def GetRootAs(cls, buf, offset=0):
        n = flatbuffers.encode.Get(flatbuffers.packer.uoffset, buf, offset)
        x = HelloRequest()
        x.Init(buf, n + offset)
        return x

    @classmethod
    def GetRootAsHelloRequest(cls, buf, offset=0):
        """This method is deprecated. Please switch to GetRootAs."""
        return cls.GetRootAs(buf, offset)
    # HelloRequest
    def Init(self, buf, pos):
        self._tab = flatbuffers.table.Table(buf, pos)

    # HelloRequest
    def Name(self):
        o = flatbuffers.number_types.UOffsetTFlags.py_type(self._tab.Offset(4))
        if o != 0:
            return self._tab.String(o + self._tab.Pos)
        return None

def HelloRequestStart(builder): builder.StartObject(1)
def Start(builder):
    return HelloRequestStart(builder)
def HelloRequestAddName(builder, name): builder.PrependUOffsetTRelativeSlot(0, flatbuffers.number_types.UOffsetTFlags.py_type(name), 0)
def AddName(builder, name):
    return HelloRequestAddName(builder, name)
def HelloRequestEnd(builder): return builder.EndObject()
def End(builder):
    return HelloRequestEnd(builder)