REBOL [
    Title: "BMP Codec Extension"
    name: 'BMP
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

sys/register-codec* 'bmp %.bmp
    get in import 'bmp 'identify-bmp?
    get in import 'bmp 'decode-bmp
    get in import 'bmp 'encode-bmp
