; functions/string/decloak.r
[#48 (
    a: gzip "a"
    b: encloak a "a"
    equal? a decloak b "a"
)]
