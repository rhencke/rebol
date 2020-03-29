REBOL [
    Title: "Serial Port Extension"
    Name: Serial
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}
]

sys/make-scheme [
    title: "Serial Port"
    name: 'serial
    actor: get-serial-actor-handle
    spec: system/standard/port-spec-serial
    init: func [port <local> path speed] [
        if url? port/spec/ref [
            parse port/spec/ref [
                thru #":" 0 2 slash
                copy path [to slash | end] skip
                copy speed to end
            ]
            attempt [port/spec/speed: to-integer speed]
            port/spec/path: to file! path
        ]
        return
    ]
]
