REBOL [
    Title: "GIF Codec Extension"
    name: 'GIF
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

sys/register-codec* 'gif %.gif
    :identify-gif?
    :decode-gif
    _ ;-- currently no GIF encoder
