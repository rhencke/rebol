; datatypes/image.r
(image? make image! 100x100)
(not image? 1)
(image! = type of make image! 0x0)
; minimum
(image? #[image! [0x0 #{}]])
