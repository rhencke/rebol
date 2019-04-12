REBOL [
    Title: "TCP/UDP Networking"
    Name: Network
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}
]

register-network-device

sys/make-scheme [
    title: "TCP Networking"
    name: 'tcp
    actor: get-tcp-actor-handle
    spec: system/standard/port-spec-net
    info: system/standard/net-info ; for C enums
    awake: func [event] [print ['TCP-event event/type] true]
]

sys/make-scheme [
    title: "UDP Networking"
    name: 'udp
    actor: get-udp-actor-handle
    spec: system/standard/port-spec-net
    info: system/standard/net-info ; for C enums
    awake: func [event] [print ['UDP-event event/type] true]
]
