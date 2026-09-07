#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

struct CacheLine {
    uint64_t tag = 0;
    uint64_t last_used = 0;
    bool valid = false;
    bool dirty = false;
};

struct CacheStats {
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;
    uint64_t writebacks = 0;
};

struct MemoryRef {
    uint64_t address = 0;
    bool write = false;
};

static inline bool is_power_of_two(uint32_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static uint32_t log2_exact(uint32_t n) {
    if (!is_power_of_two(n)) {
        throw std::invalid_argument("Value must be a nonzero power of 2.");
    }
    uint32_t bits = 0;
    while (n > 1) {
        n >>= 1;
        ++bits;
    }
    return bits;
}

class Cache {
public:
    Cache(uint32_t total_bytes, uint32_t block_bytes, uint32_t associativity)
        : ways(associativity),
          clock(0) {
        
        if (!is_power_of_two(total_bytes) || 
            !is_power_of_two(block_bytes) || 
            !is_power_of_two(associativity)) {
            throw std::invalid_argument("Cache parameters must be powers of 2.");
        }

        const uint64_t set_bytes = static_cast<uint64_t>(block_bytes) * associativity;
        if (total_bytes < set_bytes) {
            throw std::invalid_argument("total_bytes must be >= block_bytes * associativity.");
        }

        num_sets = static_cast<uint32_t>(total_bytes / set_bytes);
        offset_bits = log2_exact(block_bytes);
        index_bits = log2_exact(num_sets);

        if (offset_bits + index_bits > 64) {
            throw std::invalid_argument("Offset + index bits exceed 64-bit addressing.");
        }

        // Prevent UB on shift by >= 64 bits
        if (index_bits >= 64) {
            index_mask = ~static_cast<uint64_t>(0);
        } else {
            index_mask = (static_cast<uint64_t>(1) << index_bits) - 1;
        }

        lines.resize(static_cast<size_t>(num_sets) * ways);
    }

    // Returns true on a hit.
    bool access(uint64_t address, bool write) {
        ++clock;
        if (write) {
            ++stats.writes;
        } else {
            ++stats.reads;
        }

        const uint32_t set = set_index(address);
        const uint64_t tag = tag_of(address);
        const size_t base = static_cast<size_t>(set) * ways;

        for (uint32_t w = 0; w < ways; ++w) {
            CacheLine &line = lines[base + w];
            if (line.valid && line.tag == tag) {
                line.last_used = clock;
                if (write) {
                    line.dirty = true;
                }
                ++stats.hits;
                return true;
            }
        }

        ++stats.misses;
        fill(base, tag, write);
        return false;
    }

    const CacheStats &get_stats() const { return stats; }

    void print_config() const {
        std::cout << "sets: " << num_sets << ", ways: " << ways
                  << ", offset bits: " << offset_bits
                  << ", index bits: " << index_bits << "\n";
    }

    void print_stats() const {
        const uint64_t total = stats.hits + stats.misses;
        std::cout << "accesses:   " << total << " (" << stats.reads
                  << " reads, " << stats.writes << " writes)\n"
                  << "hits:       " << stats.hits << "\n"
                  << "misses:     " << stats.misses << "\n"
                  << "evictions:  " << stats.evictions << "\n"
                  << "writebacks: " << stats.writebacks << "\n";
        if (total > 0) {
            const double rate = 100.0 * static_cast<double>(stats.hits) / static_cast<double>(total);
            std::cout << "hit rate:   " << std::fixed << std::setprecision(2)
                      << rate << "%\n";
        }
    }

private:
    uint32_t set_index(uint64_t address) const {
        return static_cast<uint32_t>((address >> offset_bits) & index_mask);
    }

    uint64_t tag_of(uint64_t address) const {
        if (offset_bits + index_bits >= 64) {
            return 0;
        }
        return address >> (offset_bits + index_bits);
    }

    void fill(size_t base, uint64_t tag, bool write) {
size_t victim = base;
bool found_free = false;

// First look for any empty slot
for (uint32_t w = 0; w < ways; ++w) {
    if (!lines[base + w].valid) {
        victim = base + w;
        found_free = true;
        break;
    }
}

// If all slots are occupied, find the true LRU line
if (!found_free) {
    for (uint32_t w = 1; w < ways; ++w) {
        if (lines[base + w].last_used < lines[victim].last_used) {
            victim = base + w;
        }
    }
    ++stats.evictions;
    if (lines[victim].dirty) {
        ++stats.writebacks;
    }
}

        CacheLine &line = lines[victim];
        line.tag = tag;
        line.valid = true;
        line.dirty = write;
        line.last_used = clock;
    }

    uint32_t ways;
    uint32_t num_sets;
    uint32_t offset_bits;
    uint32_t index_bits;
    uint64_t index_mask;
    uint64_t clock;
    std::vector<CacheLine> lines;
    CacheStats stats;
};

static void run_trace(Cache &cache, const std::vector<MemoryRef> &trace) {
    for (const auto &ref : trace) {
        cache.access(ref.address, ref.write);
    }
}

int main() {
    try {
        const uint32_t k_cache_bytes = 32 * 1024;
        const uint32_t k_block_bytes = 64;
        const uint32_t k_ways = 2;

        Cache sequential(k_cache_bytes, k_block_bytes, k_ways);
        sequential.print_config();

        // Walk 8 KB linearly: one compulsory miss per block, the rest hit
        std::vector<MemoryRef> linear;
        for (uint64_t addr = 0; addr < 8192; addr += 4) {
            MemoryRef ref;
            ref.address = addr;
            ref.write = (addr % 64 == 0);
            linear.push_back(ref);
        }
        run_trace(sequential, linear);

        std::cout << "\n-- linear walk --\n";
        sequential.print_stats();

        // Three addresses that map to the same set: sets * block size = 16 KB apart.
        Cache thrash(k_cache_bytes, k_block_bytes, k_ways);
        std::vector<MemoryRef> conflict;
        for (int pass = 0; pass < 100; ++pass) {
            for (uint64_t i = 0; i < 3; ++i) {
                MemoryRef ref;
                ref.address = i * 16384;
                ref.write = (i == 1);
                conflict.push_back(ref);
            }
        }
        run_trace(thrash, conflict);

        std::cout << "\n-- conflict thrash --\n";
        thrash.print_stats();

    } catch (const std::exception &ex) {
        std::cerr << "Cache configuration error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
