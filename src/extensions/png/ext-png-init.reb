REBOL [
    Title: "PNG Codec Extension"
    name: 'PNG
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

sys/register-codec* 'png %.png
    :identify-png?
    :decode-png
    :encode-png
