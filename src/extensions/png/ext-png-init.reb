REBOL [
    Title: "PNG Codec Extension"
    name: 'PNG
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

sys/register-codec* 'png %.png
    get in import 'png 'identify-png?
    get in import 'png 'decode-png
    get in import 'png 'encode-png
