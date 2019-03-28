REBOL [
    Title: "PNG Codec Extension"
    Name: PNG
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}
]

sys/register-codec* 'png %.png
    :identify-png?
    :decode-png
    :encode-png
