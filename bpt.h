#ifndef BPT_H
#define BPT_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <climits>

constexpr int PAGE_SIZE = 4096;
constexpr int MAX_KEY_LEN = 64;
constexpr const char* DATA_FILE = "bpt.dat";

class BPTree {
public:
    BPTree();
    ~BPTree();

    bool is_open() const { return file_ != nullptr; }

    void insert(const std::string& key, int value);
    bool remove(const std::string& key, int value);
    std::vector<int> find(const std::string& key);

    int size() const { return total_entries_; }

private:
    struct CachedPage {
        uint8_t data[PAGE_SIZE];
        bool dirty;
    };

    FILE* file_;
    uint32_t root_page_;
    uint32_t first_leaf_;
    uint32_t next_free_;
    int total_entries_;

    std::unordered_map<uint32_t, CachedPage> cache_;

    // Header offsets in page 0
    static constexpr int OFF_MAGIC      = 0;
    static constexpr int OFF_ROOT       = 4;
    static constexpr int OFF_TOTAL      = 8;
    static constexpr int OFF_NEXT_FREE  = 12;
    static constexpr int OFF_FIRST_LEAF = 16;

    // Node header offsets
    static constexpr int OFF_FLAGS          = 0;
    static constexpr int OFF_NUM            = 1;
    static constexpr int OFF_NEXT_OR_CHILD  = 3;
    static constexpr int OFF_ENTRIES        = 7;

    static constexpr uint32_t MAGIC = 0x42505421;

    // Thresholds for rebalancing
    static constexpr int MIN_ENTRIES_LEAF = 20;
    static constexpr int MIN_KEYS_INTERNAL = 20;

    // Page cache
    uint8_t* get_page(uint32_t page_num);
    void mark_dirty(uint32_t page_num);
    uint32_t alloc_page();

    // Header I/O
    void read_header();
    void write_header();
    void flush_dirty();

    // Serialization helpers
    static uint16_t read_u16(const uint8_t* p);
    static void write_u16(uint8_t* p, uint16_t v);
    static uint32_t read_u32(const uint8_t* p);
    static void write_u32(uint8_t* p, uint32_t v);
    static int32_t read_i32(const uint8_t* p);
    static void write_i32(uint8_t* p, int32_t v);

    // Page field accessors
    static bool is_leaf(const uint8_t* p);
    static void set_leaf(uint8_t* p, bool v);
    static uint16_t num_entries(const uint8_t* p);
    static void set_num_entries(uint8_t* p, uint16_t n);
    static uint32_t next_leaf(const uint8_t* p);
    static void set_next_leaf(uint8_t* p, uint32_t v);
    static uint32_t first_child(const uint8_t* p);
    static void set_first_child(uint8_t* p, uint32_t v);

    // Entry access (position-based)
    static int entry_offset(const uint8_t* page, int pos);
    static int entry_size_at(const uint8_t* page, int off);

    static std::string get_key(const uint8_t* page, int pos);
    static int32_t get_value(const uint8_t* page, int pos);
    static uint32_t get_child(const uint8_t* page, int child_idx);

    static int space_used(const uint8_t* page);
    static int entry_bytes_needed(const std::string& key, bool leaf);
    static bool can_insert(const uint8_t* page, const std::string& key, bool leaf);

    // Comparison: (key, value) pairs
    // Returns true if entry at pos has (key, value) < (search_key, search_value)
    static bool entry_less(const uint8_t* page, int pos, const std::string& key, int value);
    // Returns first position where entry >= (key, value) — lower_bound
    static int find_pos(const uint8_t* page, const std::string& key, int value);
    // Returns first position where entry > (key, value) — upper_bound
    static int find_child_idx(const uint8_t* page, const std::string& key, int value);
    // Find exact (key, value) match in leaf, returns position or -1
    static int find_entry(const uint8_t* page, const std::string& key, int value);

    // Modify page
    // Insert entry into leaf: (key, value)
    static void insert_leaf_entry(uint8_t* page, int pos, const std::string& key, int32_t value);
    // Insert entry into internal: (key, value, child)
    static void insert_internal_entry(uint8_t* page, int pos, const std::string& key, int32_t value, uint32_t child);
    // Remove entry at position
    static void remove_entry(uint8_t* page, int pos);

    // Recursive operations
    struct SplitResult {
        bool split;
        std::string key;
        int32_t value;    // separator value
        uint32_t new_page;
    };

    SplitResult insert_rec(uint32_t page_num, const std::string& key, int value);
    bool delete_rec(uint32_t page_num, const std::string& key, int value);
    void rebalance(uint32_t parent_num, int child_idx);
};

#endif