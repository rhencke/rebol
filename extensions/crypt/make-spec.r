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
        %crypt/bigint/bigint.c

        ; See above remarks on Spectre.  This may be a priority to
        ; address, if bigint is used in INTEGER!.
        ;
        <msc:/wd5045>  ; https://stackoverflow.com/q/50399940
    ]

    %crypt/rsa/rsa.c

    ; If you're using a platform that mbedTLS has been designed for,
    ; you can take the standard settings of what "malloc" and "free"
    ; and "printf" are supposed to be.  (Hopefully it won't actually
    ; use printf in release code...)
    ;
    [%crypt/mbedtls/library/platform.c  #no-c++]
    [%crypt/mbedtls/library/platform_util.c  #no-c++]

    ; The current plan is to embed the bignum implementation into Rebol itself
    ; to power its INTEGER! type (when the integers exceed the cell size).
    ; So it should be shareable across the various crypto that uses it.
    ;
    [%crypt/mbedtls/library/bignum.c  #no-c++]

    ; Generic message digest and cipher abstraction layers (write code to one
    ; C interface, get all the digests and ciphers adapted to it for "free",
    ; as well as be able to list and query which ones were built in by name)
    ;
    [%crypt/mbedtls/library/md.c  #no-c++]
    [%crypt/mbedtls/library/cipher.c  #no-c++]
    [%crypt/mbedtls/library/cipher_wrap.c  #no-c++]

    ; MESSAGE DIGESTS
    ;
    ; !!! RC4, MD5, and SHA1 are all considered weak by modern standards.
    ; But they were in R3-Alpha, and outside of bytes taken up in the EXE
    ; don't cost extra to support (the generic MD wrapper handles them)
    ;
    [%crypt/mbedtls/library/sha256.c  #no-c++]
    [%crypt/mbedtls/library/sha512.c  #no-c++]
    [%crypt/mbedtls/library/ripemd160.c  #no-c++]  ; used by BitCoin :-/
    [%crypt/mbedtls/library/md5.c  #no-c++]  ; !!! weak
    [%crypt/mbedtls/library/sha1.c  #no-c++]  ; !!! weak

    ; BLOCK CIPHERS
    ;
    [%crypt/mbedtls/library/aes.c  #no-c++]
    [%crypt/mbedtls/library/arc4.c  #no-c++]  ; !!! weak

    ; !!! Plain Diffie-Hellman(-Merkel) is considered weaker than the
    ; Elliptic Curve Diffie-Hellman (ECDH).  It was an easier first test case
    ; to replace the %dh.h and %dh.c code, however.  Separate extensions for
    ; each crypto again would be preferable.
    ;
    [%crypt/mbedtls/library/dhm.c  #no-c++]

    %crypt/easy-ecc/ecc.c
]
