### deb1-read.txt

    Linux chaos 6.19.13+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.19.13-1~bpo13+1 (2026-04-26) x86_64 GNU/Linux
    hmb 1
    hmb 1
    hmb 0
    hmb 1
    bench done

| step | result |
| --- | --- |
| d-bs64k | 0.192591 s, 697 MB/s |
| d-bs1m | 0.0883786 s, 1.5 GB/s |
| d-bs4m | 0.0624641 s, 2.1 GB/s |
| d-chunk0 | 0.164809 s, 1.6 GB/s |
| d-chunk1 | 0.172255 s, 1.6 GB/s |
| d-chunk2 | 0.184739 s, 1.5 GB/s |
| d-chunk3 | 0.180412 s, 1.5 GB/s |
| d-chunk4 | 0.131905 s, 2.0 GB/s |
| d-chunk5 | 0.189376 s, 1.4 GB/s |
| d-chunk6 | 0.192542 s, 1.4 GB/s |
| d-chunk7 | 0.188185 s, 1.4 GB/s |
| d-chunk8 | 0.172803 s, 1.6 GB/s |
| d-chunk9 | 0.169633 s, 1.6 GB/s |
| d-chunk10 | 0.176487 s, 1.5 GB/s |
| d-chunk11 | 0.171232 s, 1.6 GB/s |
| d-chunk12 | 0.140263 s, 1.9 GB/s |
| d-chunk13 | 0.170548 s, 1.6 GB/s |
| d-chunk14 | 0.16727 s, 1.6 GB/s |
| d-chunk15 | 0.171028 s, 1.6 GB/s |
| b-chunk0 | 0.0726046 s, 3.7 GB/s |
| b-chunk1 | 0.150268 s, 1.8 GB/s |
| b-chunk2 | 0.0689858 s, 3.9 GB/s |
| b-chunk3 | 0.072326 s, 3.7 GB/s |
| b-chunk4 | 0.0682394 s, 3.9 GB/s |
| b-chunk5 | 0.111445 s, 2.4 GB/s |
| b-chunk6 | 0.247389 s, 1.1 GB/s |
| b-chunk7 | 0.243334 s, 1.1 GB/s |
| b-chunk8 | 0.242455 s, 1.1 GB/s |
| b-chunk9 | 0.24351 s, 1.1 GB/s |
| b-chunk10 | 0.246525 s, 1.1 GB/s |
| b-chunk11 | 0.218158 s, 1.2 GB/s |
| b-chunk12 | 0.186874 s, 1.4 GB/s |
| b-chunk13 | 0.239661 s, 1.1 GB/s |
| b-chunk14 | 0.246173 s, 1.1 GB/s |
| b-chunk15 | 0.237016 s, 1.1 GB/s |
| f-seq128k-qd1-hmb1 | read 1123.3 MB/s, 8570 IOPS, 116.4 us |
| f-seq128k-qd32-hmb1 | read 3097.1 MB/s, 23628 IOPS, 1354.0 us |
| f-rand4k-qd1-hmb1 | read 36.0 MB/s, 8801 IOPS, 113.2 us |
| f-rand4k-qd32-hmb1 | read 340.1 MB/s, 83033 IOPS, 385.1 us |
| f-seq128k-qd1-hmb0 | read 1241.8 MB/s, 9473 IOPS, 105.3 us |
| f-seq128k-qd32-hmb0 | read 3133.7 MB/s, 23907 IOPS, 1338.2 us |
| f-rand4k-qd1-hmb0 | read 29.8 MB/s, 7272 IOPS, 137.0 us |
| f-rand4k-qd32-hmb0 | read 285.8 MB/s, 69773 IOPS, 458.4 us |

### deb1-write.txt

    Linux chaos 6.19.13+deb13-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.19.13-1~bpo13+1 (2026-04-26) x86_64 GNU/Linux
    hmb 1
    bench done

| step | result |
| --- | --- |
| d-w1m | 0.704148 s, 1.5 GB/s |
| b-w1m | 0.654202 s, 1.6 GB/s |
| f-seqw128k-qd1 | write 1633.3 MB/s, 12461 IOPS, 80.0 us |
| f-seqw128k-qd32 | write 1903.2 MB/s, 14520 IOPS, 2203.6 us |
| f-randw4k-qd1 | write 340.8 MB/s, 83214 IOPS, 11.8 us |
| f-randw4k-qd32 | write 1321.7 MB/s, 322688 IOPS, 99.0 us |
| ext4-w1m | 0.691889 s, 1.6 GB/s |

