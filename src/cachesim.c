#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//CacheLine represents a single cache line within a set (valid bit + tag + replacement metadata)
typedef struct {
    int valid;
    unsigned long tag;
    unsigned long age;   //age within the set
} CacheLine;

//Cache stores cache configuration, stats, and the 2D array of cache lines (sets x lines)
typedef struct {
    int sets_num;
    int set_lines;
    int block_size;
    int block_bits;
    int set_bits;
    int policy;         
    CacheLine **lines;

    unsigned long hits;
    unsigned long misses;
    unsigned long reads;
    unsigned long writes;
} Cache;

//Replacement policy identifiers
enum {
    POLICY_FIFO = 0,
    POLICY_LRU  = 1
};

//Returns 1 if x is a power of two, else 0 (used to validate CLI args)
int is_power_of_two(unsigned long x) {
    return x != 0 && (x & (x - 1)) == 0;
}

//Integer log2 for powers of two (counts how many bits needed to represent x)
int log2_int(unsigned long x) {
    int count = 0;
    while (x > 1) {
        x >>= 1;
        count++;
    }
    return count;
}

//Allocates and initializes a cache with the given size/associativity/blocksize/policy
Cache* create_cache(int cache_size, int associativity, int block_size, int policy) {
    Cache *cache = (Cache*)malloc(sizeof(Cache));
    if (!cache) {
        fprintf(stderr, "Error: malloc failed\n");
        exit(1);
    }

    //Derive bit widths and set geometry from inputs
    cache->block_size = block_size;
    cache->block_bits = log2_int(block_size);
    cache->set_lines = associativity;
    cache->sets_num = cache_size / (associativity * block_size);
    cache->set_bits = log2_int(cache->sets_num);
    cache->policy = policy;

    //Initialize stats counters
    cache->hits = 0;
    cache->misses = 0;
    cache->reads = 0;
    cache->writes = 0;

    //Allocate array of sets (each set points to an array of CacheLine)
    cache->lines = (CacheLine**)malloc(cache->sets_num * sizeof(CacheLine*));
    if (!cache->lines) {
        fprintf(stderr, "Error: malloc failed\n");
        exit(1);
    }

    //Allocate each set and initialize lines to invalid
    for (int i = 0; i < cache->sets_num; i++) {
        cache->lines[i] = (CacheLine*)malloc(associativity * sizeof(CacheLine));
        if (!cache->lines[i]) {
            fprintf(stderr, "Error: malloc failed\n");
            exit(1);
        }
        for (int j = 0; j < associativity; j++) {
            cache->lines[i][j].valid = 0;
            cache->lines[i][j].tag = 0;
            cache->lines[i][j].age = 0;
        }
    }

    return cache;
}

//Frees all dynamically allocated memory for a cache
void free_cache(Cache *cache) {
    for (int i = 0; i < cache->sets_num; i++) {
        free(cache->lines[i]);
    }
    free(cache->lines);
    free(cache);
}

//Computes block id by shifting off block offset bits
unsigned long get_block_id(Cache *cache, unsigned long address) {
    return address >> cache->block_bits;
}

//Computes set index from address using block id and set_bits mask
unsigned long get_set_index(Cache *cache, unsigned long address) {
    unsigned long block_id = get_block_id(cache, address);
    unsigned long mask = (cache->set_bits == 0) ? 0 : ((1UL << cache->set_bits) - 1);
    return block_id & mask;
}

//Computes tag by shifting off block offset bits and set index bits
unsigned long get_tag(Cache *cache, unsigned long address) {
    return address >> (cache->block_bits + cache->set_bits);
}


//Searches for a matching valid line in the correct set; returns line index or -1 if miss
int find_line(Cache *cache, unsigned long address, unsigned long *set_idx_out) {
    unsigned long set_idx = get_set_index(cache, address);
    unsigned long tag = get_tag(cache, address);
    *set_idx_out = set_idx;

    for (int i = 0; i < cache->set_lines; i++) {
        if (cache->lines[set_idx][i].valid &&
            cache->lines[set_idx][i].tag == tag) {
            return i;
        }
    }
    return -1;
}

//LRU-mark as most recently used.
//FIFO-do nothing.
void update_lru_on_access(Cache *cache, unsigned long set_idx, int line_idx) {
    //Only update ages for LRU; FIFO ages are handled only on insertion/replacement
    if (cache->policy != POLICY_LRU) {
        return;
    }
    //For LRU: accessed line age=0, others in same set age++
    for (int i = 0; i < cache->set_lines; i++) {
        if (!cache->lines[set_idx][i].valid) continue;
        if (i == line_idx) {
            cache->lines[set_idx][i].age = 0;
        } else {
            cache->lines[set_idx][i].age++;
        }
    }
}


//Loads a block into cache by inserting into an empty line or evicting based on max age
void load_block(Cache *cache, unsigned long address) {
    unsigned long set_idx = get_set_index(cache, address);
    unsigned long tag = get_tag(cache, address);

    int replace_idx = -1;
    unsigned long max_age = 0;

    //Find an invalid line first; otherwise choose the line with the largest age
    for (int i = 0; i < cache->set_lines; i++) {
        if (!cache->lines[set_idx][i].valid) {
            replace_idx = i;
            break;
        }
        if (cache->lines[set_idx][i].age >= max_age) {
            max_age = cache->lines[set_idx][i].age;
            replace_idx = i;
        }
    }

    //Insert/replace the chosen line
    cache->lines[set_idx][replace_idx].valid = 1;
    cache->lines[set_idx][replace_idx].tag = tag;

    //After insertion: new line age=0, others age++ (works for both FIFO+LRU in this implementation)
    for (int i = 0; i < cache->set_lines; i++) {
        if (!cache->lines[set_idx][i].valid) continue;
        if (i == replace_idx) {
            cache->lines[set_idx][i].age = 0;
        } else {
            cache->lines[set_idx][i].age++;
        }
    }
}


//Prefetches the next sequential block (block_id+1) if not already present
void prefetch_next(Cache *cache, unsigned long address) {
    unsigned long block_id = get_block_id(cache, address);
    unsigned long next_address = (block_id + 1) << cache->block_bits;

    unsigned long set_idx;
    int line_idx = find_line(cache, next_address, &set_idx);

    //On prefetch miss: count a memory read and load the prefetched block
    if (line_idx == -1) {
        cache->reads++;
        load_block(cache, next_address);
    }
}

//Simulates a read access; on miss loads block and optionally prefetches next
void simulate_read(Cache *cache, unsigned long address, int prefetch) {
    unsigned long set_idx;
    int line_idx = find_line(cache, address, &set_idx);

    if (line_idx != -1) {
        //Cache hit
        cache->hits++;
        update_lru_on_access(cache, set_idx, line_idx);
    } else {
        //Cache miss: memory read for demand fetch
        cache->misses++;
        cache->reads++;
        load_block(cache, address);
        if (prefetch) {
            prefetch_next(cache, address);
        }
    }
}

//Simulates a write access (write-through, write-allocate style behavior in this code path)
void simulate_write(Cache *cache, unsigned long address, int prefetch) {
    unsigned long set_idx;
    int line_idx = find_line(cache, address, &set_idx);

    if (line_idx != -1) {
        //Cache hit: count write and update replacement metadata for LRU
        cache->hits++;
        cache->writes++;
        update_lru_on_access(cache, set_idx, line_idx);
    } else {
        //Cache miss: fetch block (read), then perform the write; optionally prefetch next
        cache->misses++;
        cache->reads++;
        load_block(cache, address);
        cache->writes++;
        if (prefetch) {
            prefetch_next(cache, address);
        }
    }
}

//Prints the required output stats for one simulation run
void print_stats(Cache *cache, int prefetch) {
    printf("Prefetch %d\n", prefetch);
    printf("Memory reads: %lu\n", cache->reads);
    printf("Memory writes: %lu\n", cache->writes);
    printf("Cache hits: %lu\n", cache->hits);
    printf("Cache misses: %lu\n", cache->misses);
}

int main(int argc, char *argv[]) {
    //Expect exactly 5 arguments after program name
    if (argc != 6) {
        fprintf(stderr,
                "Usage: %s <cache_size> <associativity> <policy> <block_size> <trace_file>\n",
                argv[0]);
        return 1;
    }

    //Parse CLI inputs
    int cache_size = atoi(argv[1]);
    char *assoc_str = argv[2];
    char *policy_str = argv[3];
    int block_size = atoi(argv[4]);
    char *trace_file = argv[5];

    //Validate power-of-two requirements
    if (!is_power_of_two(cache_size) || !is_power_of_two(block_size)) {
        fprintf(stderr, "Error: Cache size and block size must be powers of 2\n");
        return 1;
    }

    //Parse replacement policy
    int policy;
    if (strcmp(policy_str, "fifo") == 0) {
        policy = POLICY_FIFO;
    } else if (strcmp(policy_str, "lru") == 0) {
        policy = POLICY_LRU;
    } else {
        fprintf(stderr, "Error: Invalid replacement policy\n");
        return 1;
    }

    //Parse associativity format: direct | assoc | assoc:n
    int associativity;
    if (strcmp(assoc_str, "direct") == 0) {
        associativity = 1;
    } else if (strcmp(assoc_str, "assoc") == 0) {
        //one set, all lines in that set
        associativity = cache_size / block_size;
    } else if (strncmp(assoc_str, "assoc:", 6) == 0) {
        associativity = atoi(assoc_str + 6);
        if (!is_power_of_two(associativity)) {
            fprintf(stderr, "Error: Associativity must be a power of 2\n");
            return 1;
        }
    } else {
        fprintf(stderr, "Error: Invalid associativity\n");
        return 1;
    }

    //Run two simulations: no prefetch and with prefetch (same trace)
    Cache *cache_no_prefetch = create_cache(cache_size, associativity, block_size, policy);
    Cache *cache_prefetch    = create_cache(cache_size, associativity, block_size, policy);

    //Open trace file
    FILE *fp = fopen(trace_file, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open trace file %s\n", trace_file);
        free_cache(cache_no_prefetch);
        free_cache(cache_prefetch);
        return 1;
    }

    //Read trace line-by-line; stop at #eof
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "#eof", 4) == 0) break;

        unsigned long pc, address;
        char op;
        //Expected trace format: "<pc>: <R/W> <address>"
        if (sscanf(line, "%lx: %c %lx", &pc, &op, &address) == 3) {
            if (op == 'R') {
                simulate_read(cache_no_prefetch, address, 0);
                simulate_read(cache_prefetch, address, 1);
            } else if (op == 'W') {
                simulate_write(cache_no_prefetch, address, 0);
                simulate_write(cache_prefetch, address, 1);
            }
        }
    }
    fclose(fp);

    //Print results for both runs
    print_stats(cache_no_prefetch, 0);
    print_stats(cache_prefetch, 1);

    //Cleanup
    free_cache(cache_no_prefetch);
    free_cache(cache_prefetch);
    return 0;
}
