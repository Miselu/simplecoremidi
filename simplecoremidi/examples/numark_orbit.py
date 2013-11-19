# Simple example of grabbing MIDI packets from a Numark ORBIT controller, and sending
# noteOn and noteOff messages out to the Numark ORBIT to blink the buttons a random color
# as they are pressed.

# This example uses two Python threads: one for reading from the ORBIT controller, and one
# for writing to the ORBIT controller.  A Queue is used to send messages from one thread to
# the other.  A signal handler allows the program to be quit by hitting ^C.

import sys
import simplecoremidi
import time
import random
import threading
import signal
import Queue

input = simplecoremidi.MIDIInput("Numark ORBIT")
output = simplecoremidi.MIDIOutput("Numark ORBIT")

print "MyInput:", input, input._input
print "MyOutput:", output, output._output

running = True
q = Queue.Queue()

def reader():
    global running
    while running:
        pkts = input.recv()
        print "recv:", pkts
        for p in pkts:
            q.put(p)
        time.sleep(0.2)
    print "reader exiting"

def writer():

    status = 0x90
    note = 36
    while running:
        try:
            pkt = q.get(timeout=0.2)
        except Queue.Empty:
            pkt = None

        if pkt == None:
            continue

        (xstatus, xnote, xvel) = pkt
        print "writer:", xstatus, xnote, xvel

        if xvel == 0:
            x = output.send([0x80, xnote, 0])
        else:
            color = random.randint(0, 128)
            x = output.send([status, xnote, color])

        if note == 52:
            note = 36
        else:
            note = note + 1

        # time.sleep(0.1)

    print "writer exiting"


r = threading.Thread(target=reader)
w = threading.Thread(target=writer)

def sigterm_handler(signum, frame):
    print "Sigterm"
    print >>sys.stderr, "SIGTERM handler.  Shutting Down."
    global running
    running = False

signal.signal(signal.SIGTERM, sigterm_handler) # #9
signal.signal(signal.SIGINT, sigterm_handler)  # ^C

w.start()
print "w started"

r.start()
print "r started"

while running:
    time.sleep(0.5)
    

print "End"
    



