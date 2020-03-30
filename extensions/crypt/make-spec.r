REBOL []

name: 'Crypt
loadable: no ;tls depends on this, so it has to be builtin
source: %crypt/mod-crypt.c
includes: reduce [
    ;
    ; Added so `#include "bigint/bigint.h` can be found by %rsa.h
    ; and `#include "rsa/rsa.h" can be found by %dh.c
    ;
    repo-dir/extensions/crypt
    repo-dir/extensions/crypt/mbedtls/include  ; w/subdir %mbedtls
    %prep/extensions/crypt ;for %tmp-extensions-crypt-init.inc
]
definitions: [
    {MBEDTLS_CONFIG_FILE="mbedtls-rebol-config.h"}
]
depends: [
    [
        %crypt/aes/aes.c

        ; May 2018 update to MSVC 2017 added warnings about Spectre
        ; mitigation.  The JPG code contains a lot of code that would
        ; trigger slowdown.  It is not a priority to rewrite, given
        ; that some other vetted 3rd party JPG code should be used.
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
    ]

    [
        %crypt/bigint/bigint.c

        ; See above remarks on Spectre.  This may be a priority to
        ; address, if bigint is used in INTEGER!.
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
    ]

    %crypt/dh/dh.c
    %crypt/rsa/rsa.c

    ; If you're using a platform that mbedTLS has been designed for,
    ; you can take the standard settings of what "malloc" and "free"
    ; and "printf" are supposed to be.  (Hopefully it won't actually
    ; use printf in release code...) 
    ;
    [%crypt/mbedtls/library/platform.c  #no-c++]
    [%crypt/mbedtls/library/platform_util.c  #no-c++]

    [%crypt/mbedtls/library/sha256.c  #no-c++]

    ; !!! RC4 is a weak cipher and no longer used, but was included as
    ; part of Saphirion's cipher suites in the original TLS.  Should
    ; be a separate extension you can exclude...but keeping for now.
    ;
    [%crypt/mbedtls/library/arc4.c  #no-c++]

    %crypt/easy-ecc/ecc.c

    [%crypt/md5/u-md5.c <implicit-fallthru>]

    [
        %crypt/sha1/u-sha1.c
        <implicit-fallthru>
        <no-hidden-local>
    ]
]
