REBOL []

name: 'Console
source: %console/mod-console.c
depends: [
    %console/p-console.c
]
includes: [
    %prep/extensions/console ;for %tmp-ext-console-init.inc
]
libraries: switch system-config/os-base [
    default [
        []
    ]
]
