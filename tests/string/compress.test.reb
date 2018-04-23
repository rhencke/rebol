; functions/string/compress.r
[#1679
    (#{666F6F} = inflate deflate "foo")
]

(#{666F6F} = zinflate zdeflate "foo")

(#{666F6F} = gunzip gzip "foo")

(
    zip (zipped: copy #{}) [%abc.txt "abc" %def.txt "def"]
    unzip (unzipped: copy []) zipped
    unzipped = [%abc.txt #{616263} %def.txt #{646566}]
)
