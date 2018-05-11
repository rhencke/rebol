//
//  File: %mod-crypt.c
//  Summary: "Native Functions for cryptography"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The original cryptography additions to Rebol were done by Saphirion, at
// a time prior to Rebol's open sourcing.  They had to go through a brittle,
// incomplete, and difficult to read API for extending the interpreter with
// C code.
//
// This contains a simplification of %host-core.c, written directly to the
// native API.  It also includes the longstanding (but not standard, and not
// particularly secure) ENCLOAK and DECLOAK operations from R3-Alpha.
//

#include "rc4/rc4.h"
#include "rsa/rsa.h" // defines gCryptProv and rng_fd (used in Init/Shutdown)
#include "dh/dh.h"
#include "aes/aes.h"

#ifdef IS_ERROR
#undef IS_ERROR //winerror.h defines this, so undef it to avoid the warning
#endif
#include "sys-core.h"
#include "sys-ext.h"

#include "sha256/sha256.h" // depends on %reb-c.h for u8, u32, u64

#include "tmp-mod-crypt-first.h"

//
//  Init_Crypto: C
//
void Init_Crypto(void)
{
#ifdef TO_WINDOWS
    if (!CryptAcquireContextW(
        &gCryptProv, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT
    )) {
        // !!! There is no good way to return failure here as the
        // routine is designed, and it appears that in some cases
        // a zero initialization worked in the past.  Assert in the
        // debug build but continue silently otherwise.
        assert(FALSE);
        gCryptProv = 0;
    }
#else
    rng_fd = open("/dev/urandom", O_RDONLY);
    if (rng_fd == -1) {
        // We don't crash the release client now, but we will later
        // if they try to generate random numbers
        assert(FALSE);
    }
#endif
}


//
//  Shutdown_Crypto: C
//
void Shutdown_Crypto(void)
{
#ifdef TO_WINDOWS
    if (gCryptProv != 0)
        CryptReleaseContext(gCryptProv, 0);
#else
    if (rng_fd != -1)
        close(rng_fd);
#endif
}


static void cleanup_rc4_ctx(const REBVAL *v)
{
    RC4_CTX *rc4_ctx = VAL_HANDLE_POINTER(RC4_CTX, v);
    FREE(RC4_CTX, rc4_ctx);
}


//
//  rc4: native/export [
//
//  "Encrypt/decrypt data (modifies) using RC4 algorithm."
//
//      return: [handle! logic!]
//          "Returns stream cipher context handle."
//      /key
//          "Provided only for the first time to get stream HANDLE!"
//      crypt-key [binary!]
//          "Crypt key."
//      /stream
//      ctx [handle!]
//          "Stream cipher context."
//      data [binary!]
//          "Data to encrypt/decrypt."
//  ]
//  new-errors: [
//      key-or-stream-required: {Refinement /key or /stream has to be present}
//      invalid-rc4-context: [{Not a RC4 context:} :arg1]
//  ]
//
static REBNATIVE(rc4)
{
    CRYPT_INCLUDE_PARAMS_OF_RC4;

    if (REF(stream)) {
        REBVAL *data = ARG(data);

        if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_rc4_ctx)
            fail (Error(RE_EXT_CRYPT_INVALID_RC4_CONTEXT, ARG(ctx), END));

        RC4_CTX *rc4_ctx = VAL_HANDLE_POINTER(RC4_CTX, ARG(ctx));

        RC4_crypt(
            rc4_ctx,
            VAL_BIN_AT(data), // input "message"
            VAL_BIN_AT(data), // output (same, since it modifies)
            VAL_LEN_AT(data)
        );

        // In %host-core.c this used to fall through to return the first arg,
        // a refinement, which was true in this case.  :-/
        //
        return R_TRUE;
    }

    if (REF(key)) { // Key defined - setup new context
        RC4_CTX *rc4_ctx = ALLOC_ZEROFILL(RC4_CTX);

        RC4_setup(
            rc4_ctx,
            VAL_BIN_AT(ARG(crypt_key)),
            VAL_LEN_AT(ARG(crypt_key))
        );

        Init_Handle_Managed(D_OUT, rc4_ctx, 0, &cleanup_rc4_ctx);
        return R_OUT;
    }

    fail (Error(RE_EXT_CRYPT_KEY_OR_STREAM_REQUIRED, END));
}


//
//  rsa: native/export [
//
//  "Encrypt/decrypt data using the RSA algorithm."
//
//      data [binary!]
//      key-object [object!]
//      /decrypt
//         "Decrypts the data (default is to encrypt)"
//      /private
//         "Uses an RSA private key (default is a public key)"
//  ]
//  new-errors: [
//      invalid-key-field: [{Unrecognized field in the key object:} :arg1]
//      invalid-key-data: [{Invalid data in the key object:} :arg1 {for} :arg2]
//      invalid-key: [{No valid key in the object:} :obj]
//      decryption-failure: [{Failed to decrypt:} :arg1]
//      encryption-failure: [{Failed to encrypt:} :arg1]
//  ]
//
static REBNATIVE(rsa)
{
    CRYPT_INCLUDE_PARAMS_OF_RSA;

    REBVAL *obj = ARG(key_object);

    // N and E are required
    //
    REBVAL *n = rebRun("ensure binary! pick", obj, "'n", END);
    REBVAL *e = rebRun("ensure binary! pick", obj, "'e", END);

    RSA_CTX *rsa_ctx = NULL;

    REBINT binary_len;
    if (REF(private)) {
        REBVAL *d = rebRun("ensure binary! pick", obj, "'d", END);

        if (not d)
            fail ("No d returned BLANK, can we assume error for cleanup?");

        REBVAL *p = rebRun("ensure binary! pick", obj, "'p", END);
        REBVAL *q = rebRun("ensure binary! pick", obj, "'q", END);
        REBVAL *dp = rebRun("ensure binary! pick", obj, "'dp", END);
        REBVAL *dq = rebRun("ensure binary! pick", obj, "'dq", END);
        REBVAL *qinv = rebRun("ensure binary! pick", obj, "'qinv", END);

        // !!! Because BINARY! is not locked in memory or safe from GC, the
        // libRebol API doesn't allow direct pointer access.  Use the
        // internal VAL_BIN_AT for now, but consider if a temporary locking
        // should be possible...locked until released.
        //
        binary_len = rebUnbox("length of", d, END);
        RSA_priv_key_new(
            &rsa_ctx
            ,
            VAL_BIN_AT(n)
            , rebUnbox("length of", n, END)
            ,
            VAL_BIN_AT(e)
            , rebUnbox("length of", e, END)
            ,
            VAL_BIN_AT(d)
            , binary_len // taken as `length of d` above
            ,
            p ? VAL_BIN_AT(p) : NULL
            , p ? rebUnbox("length of", p, END) : 0
            ,
            q ? VAL_BIN_AT(q) : NULL
            , q ? rebUnbox("length of", q, END) : 0
            ,
            dp ? VAL_BIN_AT(dp) : NULL
            , dp ? rebUnbox("length of", dp, END) : 0
            ,
            dq ? VAL_BIN_AT(dq) : NULL
            , dp ? rebUnbox("length of", dq, END) : 0
            ,
            qinv ? VAL_BIN_AT(qinv) : NULL
            , qinv ? rebUnbox("length of", qinv, END) : 0
        );

        rebRelease(d);
        rebRelease(p);
        rebRelease(q);
        rebRelease(dp);
        rebRelease(dq);
        rebRelease(qinv);
    }
    else {
        binary_len = rebUnbox("length of", n, END);
        RSA_pub_key_new(
            &rsa_ctx
            ,
            VAL_BIN_AT(n)
            , binary_len // taken as `length of n` above
            ,
            VAL_BIN_AT(e)
            , rebUnbox("length of", e, END)
        );
    }

    rebRelease(n);
    rebRelease(e);

    // !!! See notes above about direct binary access via libRebol
    //
    REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
    REBINT data_len = rebUnbox("length of", ARG(data), END);

    BI_CTX *bi_ctx = rsa_ctx->bi_ctx;
    bigint *data_bi = bi_import(bi_ctx, dataBuffer, data_len);

    // Buffer suitable for recapturing as a BINARY! for either the encrypted
    // or decrypted data
    //
    REBYTE *crypted = cast(REBYTE*, rebMalloc(binary_len));

    if (REF(decrypt)) {
        int result = RSA_decrypt(
            rsa_ctx,
            dataBuffer,
            crypted,
            binary_len,
            REF(private) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted);
            fail (Error(RE_EXT_CRYPT_DECRYPTION_FAILURE, ARG(data), END));
        }

        assert(result == binary_len); // was this true?
    }
    else {
        int result = RSA_encrypt(
            rsa_ctx,
            dataBuffer,
            data_len,
            crypted,
            REF(private) ? 1 : 0
        );

        if (result == -1) {
            bi_free(rsa_ctx->bi_ctx, data_bi);
            RSA_free(rsa_ctx);

            rebFree(crypted);
            fail (Error(RE_EXT_CRYPT_ENCRYPTION_FAILURE, ARG(data), END));
        }

        // !!! any invariant here?
    }

    bi_free(rsa_ctx->bi_ctx, data_bi);
    RSA_free(rsa_ctx);

    REBVAL *binary = rebRepossess(crypted, binary_len);
    Move_Value(D_OUT, binary);
    rebRelease(binary);

    return R_OUT;
}


//
//  dh-generate-key: native/export [
//
//  "Update DH object with new DH private/public key pair."
//
//      return: "No result, object's PRIV-KEY and PUB-KEY members updated"
//          [<opt>]
//      obj [object!]
//         "(modified) Diffie-Hellman object, with generator(g) / modulus(p)"
//  ]
//
static REBNATIVE(dh_generate_key)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_GENERATE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    REBVAL *obj = ARG(obj);

    // !!! This used to ensure that all other fields, besides SELF, were blank
    //
    REBVAL *g = rebRun("ensure binary! pick", obj, "'g", END); // generator
    REBVAL *p = rebRun("ensure binary! pick", obj, "'p", END); // modulus

    dh_ctx.g = VAL_BIN_AT(g);
    dh_ctx.glen = rebUnbox("length of", g, END);

    dh_ctx.p = VAL_BIN_AT(p);
    dh_ctx.len = rebUnbox("length of", p, END);

    // Generate the private and public keys into memory that can be
    // rebRepossess()'d as the memory backing a BINARY! series
    //
    dh_ctx.x = cast(REBYTE*, rebMalloc(dh_ctx.len)); // x => private key
    memset(dh_ctx.x, 0, dh_ctx.len);
    dh_ctx.gx = cast(REBYTE*, rebMalloc(dh_ctx.len)); // gx => public key
    memset(dh_ctx.gx, 0, dh_ctx.len);

    DH_generate_key(&dh_ctx);

    rebRelease(g);
    rebRelease(p);

    REBVAL *priv = rebRepossess(dh_ctx.x, dh_ctx.len);
    REBVAL *pub = rebRepossess(dh_ctx.gx, dh_ctx.len);

    rebElide("poke", obj, "'priv-key", priv, END);
    rebElide("poke", obj, "'pub-key", pub, END);

    rebRelease(priv);
    rebRelease(pub);

    return R_OUT;
}


//
//  dh-compute-key: native/export [
//
//  "Computes key from a private/public key pair and the peer's public key."
//
//      return: [binary!]
//          "Negotiated key"
//      obj [object!]
//          "The Diffie-Hellman key object"
//      public-key [binary!]
//          "Peer's public key"
//  ]
//
static REBNATIVE(dh_compute_key)
{
    CRYPT_INCLUDE_PARAMS_OF_DH_COMPUTE_KEY;

    DH_CTX dh_ctx;
    memset(&dh_ctx, 0, sizeof(dh_ctx));

    REBVAL *obj = ARG(obj);

    // !!! used to ensure object only had other fields SELF, PUB-KEY, G
    // otherwise gave Error(RE_EXT_CRYPT_INVALID_KEY_FIELD)

    REBVAL *p = rebRun("ensure binary! pick", obj, "'p", END);
    REBVAL *priv_key = rebRun("ensure binary! pick", obj, "'priv-key", END);

    dh_ctx.p = VAL_BIN_AT(p);
    dh_ctx.len = rebUnbox("length of", p, END);

    dh_ctx.x = VAL_BIN_AT(priv_key);
    // !!! No length check here, should there be?

    dh_ctx.gy = VAL_BIN_AT(ARG(public_key));
    // !!! No length check here, should there be?

    dh_ctx.k = cast(REBYTE*, rebMalloc(dh_ctx.len));
    memset(dh_ctx.k, 0, dh_ctx.len);

    DH_compute_key(&dh_ctx);

    REBVAL *binary = rebRepossess(dh_ctx.k, dh_ctx.len);
    Move_Value(D_OUT, binary);
    rebRelease(binary);

    return R_OUT;
}


static void cleanup_aes_ctx(const REBVAL *v)
{
    AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, v);
    FREE(AES_CTX, aes_ctx);
}


//
//  aes: native/export [
//
//  "Encrypt/decrypt data using AES algorithm."
//
//      return: [handle! binary! logic!]
//          "Stream cipher context handle or encrypted/decrypted data."
//      /key
//          "Provided only for the first time to get stream HANDLE!"
//      crypt-key [binary!]
//          "Crypt key."
//      iv [binary! blank!]
//          "Optional initialization vector."
//      /stream
//      ctx [handle!]
//          "Stream cipher context."
//      data [binary!]
//          "Data to encrypt/decrypt."
//      /decrypt
//          "Use the crypt-key for decryption (default is to encrypt)"
//  ]
//  new-errors: [
//      invalid-aes-context: [{Not a AES context:} :arg1]
//      invalid-aes-key-length: [{AES key length has to be 16 or 32:} :arg1]
//  ]
//
static REBNATIVE(aes)
{
    CRYPT_INCLUDE_PARAMS_OF_AES;

    if (REF(stream)) {
        if (VAL_HANDLE_CLEANER(ARG(ctx)) != cleanup_aes_ctx)
            fail (Error(RE_EXT_CRYPT_INVALID_AES_CONTEXT, ARG(ctx), END));

        AES_CTX *aes_ctx = VAL_HANDLE_POINTER(AES_CTX, ARG(ctx));

        REBYTE *dataBuffer = VAL_BIN_AT(ARG(data));
        REBINT len = VAL_LEN_AT(ARG(data));

        if (len == 0)
            return R_BLANK;

        REBINT pad_len = (((len - 1) >> 4) << 4) + AES_BLOCKSIZE;

        REBYTE *pad_data;
        if (len < pad_len) {
            //
            //  make new data input with zero-padding
            //
            pad_data = ALLOC_N(REBYTE, pad_len);
            memset(pad_data, 0, pad_len);
            memcpy(pad_data, dataBuffer, len);
            dataBuffer = pad_data;
        }
        else
            pad_data = NULL;

        REBSER *binaryOut = Make_Binary(pad_len);
        memset(BIN_HEAD(binaryOut), 0, pad_len);

        if (aes_ctx->key_mode == AES_MODE_DECRYPT)
            AES_cbc_decrypt(
                aes_ctx,
                cast(const uint8_t*, dataBuffer),
                BIN_HEAD(binaryOut),
                pad_len
            );
        else
            AES_cbc_encrypt(
                aes_ctx,
                cast(const uint8_t*, dataBuffer),
                BIN_HEAD(binaryOut),
                pad_len
            );

        if (pad_data)
            FREE_N(REBYTE, pad_len, pad_data);

        SET_SERIES_LEN(binaryOut, pad_len);
        Init_Binary(D_OUT, binaryOut);
        return R_OUT;
    }

    if (REF(key)) {
        uint8_t iv[AES_IV_SIZE];

        if (IS_BINARY(ARG(iv))) {
            if (VAL_LEN_AT(ARG(iv)) < AES_IV_SIZE)
                return R_BLANK;

            memcpy(iv, VAL_BIN_AT(ARG(iv)), AES_IV_SIZE);
        }
        else {
            assert(IS_BLANK(ARG(iv)));
            memset(iv, 0, AES_IV_SIZE);
        }

        //key defined - setup new context

        REBINT len = VAL_LEN_AT(ARG(crypt_key)) << 3;
        if (len != 128 and len != 256) {
            DECLARE_LOCAL (i);
            Init_Integer(i, len);
            fail (Error(RE_EXT_CRYPT_INVALID_AES_KEY_LENGTH, i, END));
        }

        AES_CTX *aes_ctx = ALLOC_ZEROFILL(AES_CTX);

        AES_set_key(
            aes_ctx,
            cast(const uint8_t *, VAL_BIN_AT(ARG(crypt_key))),
            cast(const uint8_t *, iv),
            (len == 128) ? AES_MODE_128 : AES_MODE_256
        );

        if (REF(decrypt))
            AES_convert_key(aes_ctx);

        Init_Handle_Managed(D_OUT, aes_ctx, 0, &cleanup_aes_ctx);
        return R_OUT;
    }

    fail (Error(RE_EXT_CRYPT_KEY_OR_STREAM_REQUIRED, END));
}


//
//  sha256: native/export [
//
//  {Calculate a SHA256 hash value from binary data.}
//
//      return: [binary!]
//          {32-byte binary hash}
//      data [binary! string!]
//          {Data to hash, STRING! will be converted to UTF-8}
//  ]
//
REBNATIVE(sha256)
{
    CRYPT_INCLUDE_PARAMS_OF_SHA256;

    REBVAL *data = ARG(data);

    REBYTE *bp;
    REBSIZ size;
    if (IS_STRING(data)) {
        REBSIZ offset;
        REBSER *temp = Temp_UTF8_At_Managed(
            &offset, &size, data, VAL_LEN_AT(data)
        );
        bp = BIN_AT(temp, offset);
    }
    else {
        assert(IS_BINARY(data));

        bp = VAL_BIN_AT(data);
        size = VAL_LEN_AT(data);
    }

    SHA256_CTX ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, bp, size);

    REBSER *buf = Make_Binary(SHA256_BLOCK_SIZE);
    sha256_final(&ctx, BIN_HEAD(buf));
    TERM_BIN_LEN(buf, SHA256_BLOCK_SIZE);

    Init_Binary(D_OUT, buf);
    return R_OUT;
}


/*
#define SEED_LEN 10
static REBYTE seed_str[SEED_LEN] = {
    249, 52, 217, 38, 207, 59, 216, 52, 222, 61 // xor "Sassenrath" #{AA55..}
};
//      kp = seed_str; // Any seed constant.
//      klen = SEED_LEN;
*/

//
//  Cloak: C
//
// Simple data scrambler. Quality depends on the key length.
// Result is made in place (data string).
//
// The key (kp) is passed as a REBVAL or REBYTE (when klen is !0).
//
static void Cloak(
    REBOOL decode,
    REBYTE *cp,
    REBCNT dlen,
    const REBVAL *key,
    REBOOL as_is
){
    REBYTE src[20];
    REBYTE dst[20];

    if (dlen == 0)
        return;

    REBYTE *kp;
    REBSIZ klen;

    switch (VAL_TYPE(key)) {
    case REB_BINARY:
        kp = VAL_BIN_AT(key);
        klen = VAL_LEN_AT(key);
        break;

    case REB_STRING: {
        REBSIZ offset;
        REBSER *temp = Temp_UTF8_At_Managed(
            &offset, &klen, key, VAL_LEN_AT(key)
        );
        kp = BIN_AT(temp, offset);
        break; }

    case REB_INTEGER:
        INT_TO_STR(VAL_INT64(key), dst);
        kp = dst;
        klen = LEN_BYTES(dst);
        as_is = FALSE;
        break;

    default:
        panic ("Invalid key type passed to Cloak()");
    }

    if (klen == 0)
        fail (key);

    REBCNT i;

    if (!as_is) {
        for (i = 0; i < 20; i++)
            src[i] = kp[i % klen];

        SHA1(src, 20, dst);
        klen = 20;
        kp = dst;
    }

    if (decode) {
        for (i = dlen - 1; i > 0; i--)
            cp[i] ^= cp[i - 1] ^ kp[i % klen];
    }

    // Change starting byte based all other bytes.

    REBCNT n = 0xa5;
    for (i = 1; i < dlen; i++)
        n += cp[i];

    cp[0] ^= cast(REBYTE, n);

    if (!decode) {
        for (i = 1; i < dlen; i++)
            cp[i] ^= cp[i - 1] ^ kp[i % klen];
    }
}


//
//  decloak: native/export [
//
//  {Decodes a binary string scrambled previously by encloak.}
//
//      data [binary!]
//          "Binary series to descramble (modified)"
//      key [string! binary! integer!]
//          "Encryption key or pass phrase"
//      /with
//          "Use a string! key as-is (do not generate hash)"
//  ]
//
static REBNATIVE(decloak)
{
    CRYPT_INCLUDE_PARAMS_OF_DECLOAK;

    Cloak(
        TRUE,
        VAL_BIN_AT(ARG(data)),
        VAL_LEN_AT(ARG(data)),
        ARG(key),
        REF(with)
    );

    Move_Value(D_OUT, ARG(data));
    return R_OUT;
}


//
//  encloak: native/export [
//
//  "Scrambles a binary string based on a key."
//
//      data [binary!]
//          "Binary series to scramble (modified)"
//      key [string! binary! integer!]
//          "Encryption key or pass phrase"
//      /with
//          "Use a string! key as-is (do not generate hash)"
//  ]
//
static REBNATIVE(encloak)
{
    CRYPT_INCLUDE_PARAMS_OF_ENCLOAK;

    Cloak(
        FALSE,
        VAL_BIN_AT(ARG(data)),
        VAL_LEN_AT(ARG(data)),
        ARG(key),
        REF(with)
    );

    Move_Value(D_OUT, ARG(data));
    return R_OUT;
}

#include "tmp-mod-crypt-last.h"
