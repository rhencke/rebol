EXTERN_C REBDEV Dev_Serial;

struct devreq_serial {
    struct rebol_devreq devreq;
    REBVAL *path;           //device path string (in OS local format)
    void *prior_attr;       // termios: retain previous settings to revert on close
    int32_t baud;           // baud rate of serial port
    uint8_t data_bits;      // 5, 6, 7 or 8
    uint8_t parity;         // odd, even, mark or space
    uint8_t stop_bits;      // 1 or 2
    uint8_t flow_control;   // hardware or software
};

inline static struct devreq_serial *ReqSerial(REBREQ *req) {
    assert(Req(req)->device == &Dev_Serial);
    return cast(struct devreq_serial*, Req(req));
}
