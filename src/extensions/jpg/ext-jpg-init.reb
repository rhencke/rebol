REBOL [
    Title: {JPG Codec Extension}
    name: 'JPG
    type: 'Extension
    version: 1.0.0
    license: {Apache 2.0}
]

sys/register-codec* 'jpeg [%.jpg %jpeg]
    :identify-jpeg?
    :decode-jpeg
    _ ;-- currently no JPG encoder
