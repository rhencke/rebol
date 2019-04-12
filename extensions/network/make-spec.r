REBOL []

name: 'Network
source: %network/mod-network.c
includes: [
    %prep/extensions/network
]

depends: [
    %network/dev-net.c
]
