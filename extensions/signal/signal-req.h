EXTERN_C REBDEV Dev_Signal;

struct devreq_posix_signal {
    struct rebol_devreq devreq;
    sigset_t mask;      // signal mask
};

#define ReqPosixSignal(req) \
    cast(struct devreq_posix_signal*, req)
