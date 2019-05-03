REBOL [
    title: "Hello World server in REBOL 3"
    author: "Andreas Bolka"
    note: {
        Binds REP socket to tcp://*:5555
        Expects "Hello" from client, replies with "World"
    }
]

import %helpers.r3

ctx: zmq-init 1

; Socket to talk to clients.
;
socket: zmq-socket ctx 'rep
zmq-bind socket tcp://*:5555

forever [
    s-recv socket  ; Wait for next request from client
    print "Received Hello"

    wait 1  ; Do some 'work'.

    s-send socket "World"  ; Send reply back to client
]

; We never get here, but if we did, this would be how we end.

zmq-close socket
zmq-term ctx
