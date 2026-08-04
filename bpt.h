#ifndef BPT_H
#define BPT_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <cstring>

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

    // Based on 64-byte max key: floor((4096-7)/(1+64+4)) = 59
    static constexpr int MAX_ENTRIES = 59;
    static constexpr int MIN_ENTRIES_LEAF = 29;
    static constexpr int MIN_KEYS_INTERNAL = 29;

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

    // Page field accessors (on buffer)
    static bool is_leaf(const uint8_t* p);
    static void set_leaf(uint8_t* p, bool v);
    static uint16_t num_entries(const uint8_t* p);
    static void set_num_entries(uint8_t* p, uint16_t n);
    static uint32_t next_leaf(const uint8_t* p);
    static void set_next_leaf(uint8_t* p, uint32_t v);
    static uint32_t first_child(const uint8_t* p);
    static void set_first_child(uint8_t* p, uint32_t v);

    // Entry access
    static int find_pos(const uint8_t* page, const std::string& key);
    static int find_child(const uint8_t* page, const std::string& key);
    static int find_entry(const uint8_t* page, const std::string& key, int value);

    static int entry_offset(const uint8_t* page, int pos);
    static int entry_size_at(const uint8_t* page, int off);

    static std::string get_key(const uint8_t* page, int pos);
    static int32_t get_value(const uint8_t* page, int pos);
    static uint32_t get_child(const uint8_t* page, int child_idx);

    static int space_used(const uint8_t* page);
    static bool can_insert(const uint8_t* page, const std::string& key);

    // Modify page
    static void insert_entry(uint8_t* page, int pos, const std::string& key, int32_t val_or_child, bool leaf);
    static void remove_entry(uint8_t* page, int pos);

    // Recursive operations
    struct SplitResult {
        bool split;
        std::string key;
        uint32_t new_page;
    };

    SplitResult insert_rec(uint32_t page_num, const std::string& key, int value);
    bool delete_rec(uint32_t page_num, const std::string& key, int value);
    void rebalance(uint32_t parent_num, int child_idx);
};

#endif