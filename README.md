# HMZ - A high speed huffman prefix free code compressor

Typical compression rates range from 900MB/s up to 1.25GB/s
Typical decompression rates range from 450MB/s up to 2.3GB/s

Included is a utility called hmz with various options to control the
compressor.

Here is sample benchmark output:

```
$ ./hmz -b 5 enwik8
File enwik8: size 100000000 bytes, chunk 32768 bytes
Format 1: --> 63498096,   63.4981%,  1081.8177 MB/s,  1221.0867 MB/s

$ ./hmz -b 5 -x 256 enwik8
File enwik8: size 100000000 bytes, chunk 262144 bytes
Format 1: --> 64095843,   64.0958%,  1186.2223 MB/s,  1584.1023 MB/s

$ ./hmz -b 5 rand
File rand: size 104857600 bytes, chunk 32768 bytes
Format 1: --> 104860800,  100.0031%, 2571.1539 MB/s,  9237.6192 MB/s

$ ./hmz -b 5 zero
File zero: size 104857600 bytes, chunk 32768 bytes
Format 1: --> 19200,      0.0183%,   2653.9881 MB/s,  28255.0148 MB/s
```

The software in this suite has only been tested on Intel CPUs.  No specific
consideration has been made to support big endian systems in which case endian
conversion support would need to be added.
