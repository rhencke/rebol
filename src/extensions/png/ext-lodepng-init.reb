REBOL [
    Title: "PNG Codec Extension"
    name: 'PNG
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

sys/register-codec* 'png %.png
    get in import 'lodepng 'identify-png?
    get in import 'lodepng 'decode-png
    get in import 'lodepng 'encode-png
