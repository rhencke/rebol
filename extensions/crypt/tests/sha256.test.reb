; SHA256 small smoke test
; (Implementation comes from mbedTLS which tests the algorithm itself)

[
    (pairs: [
        ; Edge case: empty string or binary
        ;
        {} #{e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855}
        #{} #{e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855}
     
        ; Simple non-empty cases
        ;
        "Rebol" #{C8537DEDCA2810F48C80008DBCBDA9AC2FA60382C7F073118DDDEDEEEE65FF47}
        #{1020BFDBFD0304} #{165825199DB849EAFE254E3339FD651748EBF845CAD94C238424EAF344647F98} 
    ] true)

    ; plain hash test
    (
        for-each [data hash] pairs [
            if hash != sha256 data [
                fail ["bad sha256 for" mold data]
            ]
        ]
        true
    )

    ; non-series head test
    (
        for-each [data hash] pairs [
            longer: join either binary? data [#{57911240}] ["MetÆ"] data
            if hash != sha256 skip longer 4 [
                fail ["bad sha256 for skip 4 of" mold longer]
            ]
        ]
        true
    )
]
