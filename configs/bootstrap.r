REBOL [
    File: %bootstrap.r
]

os-id: 0.4.40

toolset: [
    gcc {../../r3withtcc --do "c99" --}
    ld {../../r3withtcc --do "c99" --}
]
