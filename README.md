# 2-way-set-associative-cache-simulator

compile with: g++ -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror cache.cpp -o cache && ./cache

output:

sets: 256, ways: 2, offset bits: 6, index bits: 8

-- linear walk --
accesses:   2048 (1920 reads, 128 writes)
hits:       1920
misses:     128
evictions:  0
writebacks: 0
hit rate:   93.75%

-- conflict thrash --
accesses:   300 (200 reads, 100 writes)
hits:       0
misses:     300
evictions:  298
writebacks: 99
hit rate:   0.00%

ill update readme later
