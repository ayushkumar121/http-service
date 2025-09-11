# HTTP server

## Features
- HTTP 1.1 Compliant server
- Json encoding/decoding
- String functions
- Temp allocator

## Performance
Specifications: Apple M3 Pro
```shell
$ wrk -c 100 http://localhost:8000
Running 10s test @ http://localhost:8000
  2 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   635.72us  104.34us   6.23ms   96.29%
    Req/Sec    78.26k     3.50k   82.08k    92.57%
  1572383 requests in 10.10s, 266.92MB read
Requests/sec: 155678.17
Transfer/sec:     26.43MB
```

## References:
- http://json.org/ for json encoding/decoding
- https://en.wikipedia.org/wiki/HTTP
