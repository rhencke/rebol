EXTERN_C REBYTE *MD5(const REBYTE *data, REBLEN data_len, REBYTE *md);
EXTERN_C void MD5_Init(void *c);
EXTERN_C void MD5_Update(void *c, const REBYTE *data, REBLEN len);
EXTERN_C void MD5_Final(REBYTE *md, void *c);
EXTERN_C int MD5_CtxSize(void);

