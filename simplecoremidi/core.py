from . import _simplecoremidi as cfuncs


class MIDIInput(object):
    def __init__(self, input_name=None):
        if not input_name:
            self._input = None
        else:
            self._input = cfuncs.find_input(input_name)

    def recv_raw(self):
        if self._input:
            raw = cfuncs.recv_midi_from_input(self._input)
            return raw
        else:
            return ()

    def recv(self):
        if self._input:
            raw = cfuncs.recv_midi_from_input(self._input)
            result = [ ]
            i = 0
            while (i < len(raw)):
                pktlen = raw[i]
                pktdata = raw[i+1:i+1+pktlen]
                result.append(pktdata)
                i += (pktlen + 1)
            return result
        else:
            return ()


class MIDIOutput(object):
    def __init__(self, output_name=None):
        if not output_name:
            self._output = None
        else:
            self._output = cfuncs.find_output(output_name)

    def send(self, midi_data):
        assert isinstance(midi_data, tuple) or isinstance(midi_data, list)
        return cfuncs.send_midi_to_output(self._output, midi_data)


class MIDISource(object):
    def __init__(self, source_name=None):
        if not source_name:
            source_name = "unnamed source"
        self._source = cfuncs.create_source(source_name)

    def send(self, midi_data):
        assert isinstance(midi_data, tuple) or isinstance(midi_data, list)
        return cfuncs.send_midi(self._source, midi_data)


class MIDIDestination(object):
    def __init__(self, dest_name=None):
        if not dest_name:
            dest_name = "unnamed destination"
        self._dest = cfuncs.create_destination(dest_name)

    def recv_raw(self):
        if self._dest:
            raw = cfuncs.recv_midi(self._dest)
            return raw
        else:
            return ()

    def recv(self):
        if self._dest:
            raw = cfuncs.recv_midi(self._dest)
            result = [ ]
            i = 0
            while (i < len(raw)):
                pktlen = raw[i]
                pktdata = raw[i+1:i+1+pktlen]
                result.append(pktdata)
                i += (pktlen + 1)
            return result
        else:
            return ()


_global_midi_source = None
def _get_global_midi_source():
    global _global_midi_source
    if _global_midi_source is None:
        _global_midi_source = MIDISource("simple core midi source")
    return _global_midi_source


_global_midi_dest = None
def _get_global_midi_dest():
    global _global_midi_dest
    if _global_midi_dest is None:
        _global_midi_dest = MIDIDestination("simple core midi destination")
    return _global_midi_dest


def send_midi(midi_data):
    return _get_global_midi_source().send(midi_data)


def recv_midi():
    return _get_global_midi_dest().recv()
