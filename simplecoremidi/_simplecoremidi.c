#include <pthread.h>
#include <mach/mach_time.h>
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Python.h>


struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state)
static struct module_state _state;
#endif


#define DEBUG 0

struct _SCMMIDIDestination {
  MIDIEndpointRef midiDestination;
  CFMutableDataRef receivedMidi;
};

struct _SCMMIDIInput {
  MIDIPortRef midiInputPort;
  CFMutableDataRef receivedMidi;
};

struct _SCMMIDIOutput {
  MIDIPortRef midiOutputPort;
  MIDIEndpointRef dest;
};

typedef struct _SCMMIDIDestination* SCMMIDIDestinationRef;
typedef struct _SCMMIDIInput* SCMMIDIInputRef;
typedef struct _SCMMIDIOutput* SCMMIDIOutputRef;

static MIDIClientRef _midiClient;


static MIDIClientRef
SCMGlobalMIDIClient() {
  if (! _midiClient) {
    MIDIClientCreate(CFSTR("simple core midi client"), NULL, NULL,
                     &(_midiClient));
  }
  return _midiClient;
}


void
SCMRecvMIDIProc(const MIDIPacketList* pktList,
                void* readProcRefCon,
                void* srcConnRefCon) {
  SCMMIDIDestinationRef destRef = (SCMMIDIDestinationRef) readProcRefCon;
  int i;
  const MIDIPacket* pkt;

  pkt = &pktList->packet[0];
  for (i = 0; i < pktList->numPackets; i++) {
    CFDataAppendBytes(destRef->receivedMidi, pkt->data, pkt->length);
    pkt = MIDIPacketNext(pkt);
  }
}


void
SCMRecvMIDIInputProc(const MIDIPacketList* pktList,
                     void* readProcRefCon,
                     void* srcConnRefCon) {
  SCMMIDIInputRef inputRef = (SCMMIDIInputRef) readProcRefCon;
  int i;
  const MIDIPacket* pkt;

  pkt = &pktList->packet[0];
  for (i = 0; i < pktList->numPackets; i++) {
    CFDataAppendBytes(inputRef->receivedMidi, pkt->data, pkt->length);
    pkt = MIDIPacketNext(pkt);
  }
}


SCMMIDIDestinationRef
SCMMIDIDestinationCreate(CFStringRef midiDestinationName) {
  SCMMIDIDestinationRef destRef
    = CFAllocatorAllocate(NULL, sizeof(struct _SCMMIDIDestination), 0);
  destRef->receivedMidi = CFDataCreateMutable(NULL, 0);
  MIDIDestinationCreate(SCMGlobalMIDIClient(),
                        midiDestinationName,
                        SCMRecvMIDIProc,
                        destRef,
                        &(destRef->midiDestination));
  return destRef;
}


void
SCMMIDIDestinationDispose(SCMMIDIDestinationRef destRef) {
  MIDIEndpointDispose(destRef->midiDestination);
  CFRelease(destRef->receivedMidi);
  CFAllocatorDeallocate(NULL, destRef);
}


static void MIDIEndpoint_Destructor(PyObject* obj) {
  MIDIEndpointRef midiEndpoint = (MIDIEndpointRef)PyCapsule_GetPointer(obj, NULL);
  MIDIEndpointDispose(midiEndpoint);
}


static void MIDIDest_Destructor(PyObject* obj) {
  SCMMIDIDestinationRef midiDest = (SCMMIDIDestinationRef)PyCapsule_GetPointer(obj, NULL);
  SCMMIDIDestinationDispose(midiDest);
}

/*
 * An SCMMidiInput holds the state connecting an input PortRef to the receiver function array.
 */

SCMMIDIInputRef
SCMMIDIInputCreate(CFStringRef midiInputName, MIDIEndpointRef midiSource) {
  SCMMIDIInputRef inputRef
    = CFAllocatorAllocate(NULL, sizeof(struct _SCMMIDIInput), 0);
  inputRef->receivedMidi = CFDataCreateMutable(NULL, 0);

  MIDIInputPortCreate(SCMGlobalMIDIClient(),
                      midiInputName,
                      SCMRecvMIDIInputProc,
                      inputRef,
                      &(inputRef->midiInputPort));

  int status;
  status = MIDIPortConnectSource(inputRef->midiInputPort, midiSource, (void*)55);
  if (status) {
#if defined(DEBUG) && (DEBUG != 0)
    fprintf(stderr, "Midi Port Connect Failed.\n");
#endif

    MIDIPortDispose(inputRef->midiInputPort);
    CFRelease(inputRef->receivedMidi);
    CFAllocatorDeallocate(NULL, inputRef);

    inputRef = NULL;
  }

  return inputRef;
}

static void
SCMMIDIInputDispose(PyObject* obj) {
  SCMMIDIInputRef inputRef = (SCMMIDIInputRef)PyCapsule_GetPointer(obj, NULL);

#if defined(DEBUG) && (DEBUG != 0)
  fprintf(stderr, "input disposer\n");
#endif

  MIDIPortDispose(inputRef->midiInputPort);
  CFRelease(inputRef->receivedMidi);
  CFAllocatorDeallocate(NULL, inputRef);
}

/*
 * An SCMMidiOutput holds the state relating a destination to a newly created output port
 */

SCMMIDIOutputRef
SCMMIDIOutputCreate(MIDIEndpointRef midiDest) {

  SCMMIDIOutputRef outputRef  = CFAllocatorAllocate(NULL, sizeof(struct _SCMMIDIOutput), 0);
  int status;

  outputRef->dest = midiDest;

  if ((status = MIDIOutputPortCreate(SCMGlobalMIDIClient(), CFSTR("OuTpUt"), &(outputRef->midiOutputPort))) != 0) {
#if defined(DEBUG) && (DEBUG != 0)
      fprintf(stderr, "Error trying to create MIDI output port: %d\n", status);
      // printf("%s\n", GetMacOSStatusErrorString(status));
#endif
      CFAllocatorDeallocate(NULL, outputRef);
      outputRef = NULL;;
    }

  return outputRef;
}

static void
SCMMIDIOutputDispose(PyObject* obj) {
  SCMMIDIOutputRef outputRef = (SCMMIDIOutputRef)PyCapsule_GetPointer(obj, NULL);

#if defined(DEBUG) && (DEBUG != 0)
  fprintf(stderr, "output disposer\n");
#endif

  MIDIEndpointDispose(outputRef->dest);
  MIDIPortDispose(outputRef->midiOutputPort);
  CFAllocatorDeallocate(NULL, outputRef);
}


static PyObject *
SCMCreateMIDISource(PyObject* self, PyObject* args) {
  MIDIEndpointRef midiSource;
  CFStringRef midiSourceName;
  int status;

  midiSourceName =
    CFStringCreateWithCString(NULL,
#if PY_MAJOR_VERSION >= 3
                              PyUnicode_AsUTF8AndSize(PyTuple_GetItem(args, 0), NULL),
#else
                              PyString_AsString(PyTuple_GetItem(args, 0)),
#endif
                              kCFStringEncodingUTF8);
  status = MIDISourceCreate(SCMGlobalMIDIClient(), midiSourceName, &midiSource);
  CFRelease(midiSourceName);

  if (status == 0) {
      return PyCapsule_New((void*)midiSource, NULL, MIDIEndpoint_Destructor);
  }

  Py_INCREF(Py_None);
  return Py_None;
}


/*
 * Find a midi source, create an input object and return a reference to it.
 */

static PyObject *
SCMFindMIDIInput(PyObject* self, PyObject* args) {
  MIDIEndpointRef midiSource;
  char *midiSourceName = PyString_AsString(PyTuple_GetItem(args, 0));

  long numSrc = MIDIGetNumberOfSources();
  long idxSrc;
  CFStringRef cfstr;
  const char *str;

  for (idxSrc = 0; idxSrc < numSrc; idxSrc++) {
    midiSource = MIDIGetSource(idxSrc);
    MIDIObjectGetStringProperty(midiSource, kMIDIPropertyName, &cfstr);
    str = CFStringGetCStringPtr(cfstr, kCFStringEncodingMacRoman);
    if (strcmp(str, midiSourceName) == 0) {

#if defined(DEBUG) && (DEBUG != 0)
      fprintf(stderr, "Found Input:%s\n", str);
#endif

      SCMMIDIInputRef inputRef = SCMMIDIInputCreate(cfstr, midiSource);
      if (inputRef != NULL) {
        return PyCapsule_New((void*)inputRef, NULL, SCMMIDIInputDispose);
      } else {
        PyErr_SetString(PyExc_IOError, "Unable to create MIDIInput.");
        return NULL;
      }
    }
  }

  Py_INCREF(Py_None);
  return Py_None;
}


/*
 * Find a midi destination by name, create an output object and return a reference to it.
 */

static PyObject *
SCMFindMIDIOutput(PyObject* self, PyObject* args) {
  MIDIEndpointRef midiDest;
  char *midiDestName = PyString_AsString(PyTuple_GetItem(args, 0));

  long numDest = MIDIGetNumberOfDestinations();
  long idxDest;
  CFStringRef cfstr;
  const char *str;

  for (idxDest = 0; idxDest < numDest; idxDest++) {
    midiDest = MIDIGetDestination(idxDest);
    MIDIObjectGetStringProperty(midiDest, kMIDIPropertyName, &cfstr);
    str = CFStringGetCStringPtr(cfstr, kCFStringEncodingMacRoman);
    if (strcmp(str, midiDestName) == 0) {

#if defined(DEBUG) && (DEBUG != 0)
      fprintf(stderr, "Found Output:%s\n", str);
#endif

      SCMMIDIOutputRef outputRef = SCMMIDIOutputCreate(midiDest);
      if (outputRef != NULL) {
          return PyCapsule_New((void*)outputRef, NULL, SCMMIDIOutputDispose);
      } else {
        PyErr_SetString(PyExc_IOError, "Unable to create MIDIOutput.");
        return NULL;
      }
    }
  }

  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject*
SCMSendMidi(PyObject* self, PyObject* args) {
  MIDIEndpointRef midiSource;
  PyObject* midiData;
  Py_ssize_t nBytes;
  char pktListBuf[1024+100];
  MIDIPacketList* pktList = (MIDIPacketList*) pktListBuf;
  MIDIPacket* pkt;
  Byte midiDataToSend[1024];
  UInt64 now;
  int i;

  midiSource = (MIDIEndpointRef) PyCapsule_GetPointer(PyTuple_GetItem(args, 0), NULL);

  midiData = PyTuple_GetItem(args, 1);
  nBytes = PySequence_Size(midiData);

  for (i = 0; i < nBytes; i++) {
    PyObject* midiByte;

    midiByte = PySequence_GetItem(midiData, i);
#if PY_MAJOR_VERSION >= 3
    midiDataToSend[i] = PyLong_AsLong(midiByte);
#else
    midiDataToSend[i] = PyInt_AsLong(midiByte);
#endif
  }

  now = mach_absolute_time();
  pkt = MIDIPacketListInit(pktList);
  pkt = MIDIPacketListAdd(pktList, 1024+100, pkt, now, nBytes, midiDataToSend);

  if (pkt == NULL || MIDIReceived(midiSource, pktList)) {
#if defined(DEBUG) && (DEBUG != 0)
    fprintf(stderr, "failed to send the midi.\n");
#endif

    PyErr_SetString(PyExc_IOError, "Unable to send MIDI data.");
    return NULL;
  }

  Py_INCREF(Py_None);
  return Py_None;
}


static PyObject*
SCMSendMidiToOutput(PyObject* self, PyObject* args) {
  SCMMIDIOutputRef scmOutput;

  PyObject* midiData;
  Py_ssize_t nBytes;
  char pktListBuf[1024+100];
  MIDIPacketList* pktList = (MIDIPacketList*) pktListBuf;
  MIDIPacket* pkt;
  Byte midiDataToSend[1024];
  UInt64 now;
  int i;

  scmOutput = (SCMMIDIOutputRef) PyCapsule_GetPointer(PyTuple_GetItem(args, 0), NULL);

  if (scmOutput != NULL) {
      midiData = PyTuple_GetItem(args, 1);
      nBytes = PySequence_Size(midiData);

      for (i = 0; i < nBytes; i++) {
        PyObject* midiByte;

        midiByte = PySequence_GetItem(midiData, i);
#if PY_MAJOR_VERSION >= 3
        midiDataToSend[i] = PyLong_AsLong(midiByte);
#else
        midiDataToSend[i] = PyInt_AsLong(midiByte);
#endif
      }

      now = mach_absolute_time();
      pkt = MIDIPacketListInit(pktList);
      pkt = MIDIPacketListAdd(pktList, 1024+100, pkt, now, nBytes, midiDataToSend);

#if defined(DEBUG) && (DEBUG != 0)
      fprintf(stderr, "sending...\n");
#endif

      if (pkt == NULL || MIDISend(scmOutput->midiOutputPort, scmOutput->dest, pktList)) {
#if defined(DEBUG) && (DEBUG != 0)
        fprintf(stderr, "failed to send the midi.\n");
#endif

        PyErr_SetString(PyExc_IOError, "Unable to send MIDI data.");
        return NULL;
      }

      Py_INCREF(Py_None);
      return Py_None;
  }

  PyErr_SetString(PyExc_IOError, "Invalid MIDIOutput.");
  return NULL;
}


static PyObject*
SCMCreateMIDIDestination(PyObject* self, PyObject* args) {
  SCMMIDIDestinationRef destRef;
  CFStringRef midiDestinationName;

  midiDestinationName =
    CFStringCreateWithCString(NULL,
#if PY_MAJOR_VERSION >= 3
                              PyUnicode_AsUTF8AndSize(PyTuple_GetItem(args, 0), NULL),
#else
                              PyString_AsString(PyTuple_GetItem(args, 0)),
#endif
                              kCFStringEncodingUTF8);
  destRef = SCMMIDIDestinationCreate(midiDestinationName);
  CFRelease(midiDestinationName);
  return PyCapsule_New(destRef, NULL, MIDIDest_Destructor);
}


static PyObject*
SCMRecvMidi(PyObject* self, PyObject* args) {
  PyObject* receivedMidiT;
  UInt8* bytePtr;
  int i;
  CFIndex numBytes;
  SCMMIDIDestinationRef destRef
    = (SCMMIDIDestinationRef) PyCapsule_GetPointer(PyTuple_GetItem(args, 0), NULL);

  if (destRef != NULL) {
      numBytes = CFDataGetLength(destRef->receivedMidi);

      receivedMidiT = PyTuple_New(numBytes);
      bytePtr = CFDataGetMutableBytePtr(destRef->receivedMidi);
      for (i = 0; i < numBytes; i++, bytePtr++) {
#if PY_MAJOR_VERSION >= 3
        PyObject* midiByte = PyLong_FromLong(*bytePtr);
#else
        PyObject* midiByte = PyInt_FromLong(*bytePtr);
#endif
        PyTuple_SetItem(receivedMidiT, i, midiByte);
      }

      CFDataDeleteBytes(destRef->receivedMidi, CFRangeMake(0, numBytes));
      return receivedMidiT;
  }

  PyErr_SetString(PyExc_IOError, "Invalid MIDIDestination.");
  return NULL;
}

/*
 * this method is used with a SCMMidiInputRef to get bytes from its buffer
 */

static PyObject*
SCMRecvMidiFromInput(PyObject* self, PyObject* args) {
  PyObject* receivedMidiT;
  UInt8* bytePtr;
  int i;
  CFIndex numBytes;
  SCMMIDIInputRef inputRef
    = (SCMMIDIInputRef) PyCapsule_GetPointer(PyTuple_GetItem(args, 0), NULL);

  if (inputRef != NULL) {
      numBytes = CFDataGetLength(inputRef->receivedMidi);

      receivedMidiT = PyTuple_New(numBytes);
      bytePtr = CFDataGetMutableBytePtr(inputRef->receivedMidi);
      for (i = 0; i < numBytes; i++, bytePtr++) {
#if PY_MAJOR_VERSION >= 3
        PyObject* midiByte = PyLong_FromLong(*bytePtr);
#else
        PyObject* midiByte = PyInt_FromLong(*bytePtr);
#endif
        PyTuple_SetItem(receivedMidiT, i, midiByte);
      }

      CFDataDeleteBytes(inputRef->receivedMidi, CFRangeMake(0, numBytes));
      return receivedMidiT;
  }

  PyErr_SetString(PyExc_IOError, "Invalid MIDIInput.");
  return NULL;
}

#if defined(DEBUG) && (DEBUG != 0)
static void print_midi_source_info()
{
  int i;
  long n;
  int status;
  MIDIEndpointRef source;

  n = MIDIGetNumberOfSources();
  fprintf(stderr, "MIDIGetNumberOfSources:%ld\n", n);
  for (i = 0; i < n; i++) {
    source = MIDIGetSource(i);
    if (source) {
      CFStringRef cfprop;
      const char *prop;
      status = MIDIObjectGetStringProperty(source, kMIDIPropertyName, &cfprop);
      if (cfprop) {
        prop = CFStringGetCStringPtr(cfprop, kCFStringEncodingMacRoman);
        fprintf(stderr, "DEBUG: MIDISourcePropertyName:%s\n", prop);
      }
      else {
        fprintf(stderr, "DEBUG: no name\n");
      }
    }
  }
}

static void print_midi_destination_info()
{
  int i;
  long n;
  int status;
  MIDIEndpointRef dest;

  n = MIDIGetNumberOfDestinations();
  fprintf(stderr, "MIDIGetNumberOfDestinations:%ld\n", n);
  for (i = 0; i < n; i++) {
    dest = MIDIGetDestination(i);
    if (dest) {
      CFStringRef cfprop;
      const char *prop;
      status = MIDIObjectGetStringProperty(dest, kMIDIPropertyName, &cfprop);
      if (cfprop) {
        prop = CFStringGetCStringPtr(cfprop, kCFStringEncodingMacRoman);
        fprintf(stderr, "DEBUG: MIDIDestinationPropertyName:%s\n", prop);
      }
      else {
        fprintf(stderr, "DEBUG: no name\n");
      }
    }
  }
}

static void print_midi_device_info()
{
  int status;
  CFStringRef cfstr;
  const char *str;

  long numDevices = MIDIGetNumberOfDevices();
  long idxDev = 0;
  MIDIDeviceRef dev;
  for (idxDev = 0; idxDev < numDevices; idxDev++) {
    dev = MIDIGetDevice(idxDev);

    status = MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &cfstr);
    str = CFStringGetCStringPtr(cfstr, kCFStringEncodingMacRoman);
    fprintf(stderr, "  Found Device %ld: %s\n", idxDev, str);

    long numEnt = MIDIDeviceGetNumberOfEntities(dev);
    long idxEnt = 0;
    MIDIEntityRef ent;
    for (idxEnt = 0; idxEnt < numEnt; idxEnt++) {
      ent = MIDIDeviceGetEntity(dev, idxEnt);

      status = MIDIObjectGetStringProperty(ent, kMIDIPropertyName, &cfstr);
      str = CFStringGetCStringPtr(cfstr, kCFStringEncodingMacRoman);
      fprintf(stderr, "    Found Entity: %ld: %s\n", idxEnt, str);

      long numDest;
      long idxDest;
      long numSrc;
      long idxSrc;

      MIDIEndpointRef dest;
      MIDIEndpointRef src;

      numDest = MIDIEntityGetNumberOfDestinations(ent);
      for (idxDest = 0; idxDest < numDest; idxDest++) {
        dest = MIDIEntityGetDestination(ent, idxDest);

        status = MIDIObjectGetStringProperty(dest, kMIDIPropertyName, &cfstr);
        str = CFStringGetCStringPtr(cfstr, kCFStringEncodingMacRoman);
        fprintf(stderr, "      Found Dest: %ld: %s\n", idxDest, str);
      }

      numSrc = MIDIEntityGetNumberOfSources(ent);
      for (idxSrc = 0; idxSrc < numSrc; idxSrc++) {
        src = MIDIEntityGetSource(ent, idxSrc);

        status = MIDIObjectGetStringProperty(src, kMIDIPropertyName, &cfstr);
        str = CFStringGetCStringPtr(cfstr, kCFStringEncodingMacRoman);
        fprintf(stderr, "      Found Source: %ld: %s\n", idxSrc, str);
      }
    }
  }
}
#endif

static PyMethodDef SimpleCoreMidiMethods[] = {
  {"send_midi", SCMSendMidi, METH_VARARGS, "Send midi data tuple via source."},
  {"recv_midi", SCMRecvMidi, METH_VARARGS, "Receive midi data tuple."},
  {"create_source", SCMCreateMIDISource, METH_VARARGS, "Create a new MIDI source."},
  {"create_destination", SCMCreateMIDIDestination, METH_VARARGS, "Create a new MIDI destination."},

  {"find_input", SCMFindMIDIInput, METH_VARARGS, "Find a MIDI source by name."},
  {"find_output", SCMFindMIDIOutput, METH_VARARGS, "Find a MIDI destination by name."},
  {"recv_midi_from_input", SCMRecvMidiFromInput, METH_VARARGS, "Receive midi data tuple from an input."},
  {"send_midi_to_output", SCMSendMidiToOutput, METH_VARARGS, "Send midi data tuple to an output."},
  {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3

static int simplecoremidi_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int simplecoremidi_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}


static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "simplecoremidi",
        NULL,
        sizeof(struct module_state),
        SimpleCoreMidiMethods,
        NULL,
        simplecoremidi_traverse,
        simplecoremidi_clear,
        NULL
};

#define INITERROR return NULL

PyObject *
PyInit__simplecoremidi(void)

#else
#define INITERROR return
void
init_simplecoremidi(void)
#endif
{

#if defined(DEBUG) && (DEBUG != 0)
    print_midi_source_info();
    print_midi_destination_info();
    print_midi_device_info();
#endif

#if PY_MAJOR_VERSION >= 3
    PyObject *module = PyModule_Create(&moduledef);
#else
    PyObject *module = Py_InitModule("_simplecoremidi", SimpleCoreMidiMethods);
#endif
    if (module == NULL)
      INITERROR;
    struct module_state *st = GETSTATE(module);

    st->error = PyErr_NewException("simplecoremidi.Error", NULL, NULL);
    if (st->error == NULL) {
        Py_DECREF(module);
        INITERROR;
    }
#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
