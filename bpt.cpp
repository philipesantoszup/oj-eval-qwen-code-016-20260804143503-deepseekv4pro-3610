#include "bpt.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

// ============ Serialization helpers ============

uint16_t BPTree::read_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

void BPTree::write_u16(uint8_t* p, uint16_t v) {
    std::memcpy(p, &v, 2);
}

uint32_t BPTree::read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void BPTree::write_u32(uint8_t* p, uint32_t v) {
    std::memcpy(p, &v, 4);
}

int32_t BPTree::read_i32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void BPTree::write_i32(uint8_t* p, int32_t v) {
    std::memcpy(p, &v, 4);
}

// ============ Page field accessors ============

bool BPTree::is_leaf(const uint8_t* p) {
    return (p[OFF_FLAGS] & 1) != 0;
}

void BPTree::set_leaf(uint8_t* p, bool v) {
    if (v) p[OFF_FLAGS] |= 1;
    else   p[OFF_FLAGS] &= ~1;
}

uint16_t BPTree::num_entries(const uint8_t* p) {
    return read_u16(p + OFF_NUM);
}

void BPTree::set_num_entries(uint8_t* p, uint16_t n) {
    write_u16(p + OFF_NUM, n);
}

uint32_t BPTree::next_leaf(const uint8_t* p) {
    return read_u32(p + OFF_NEXT_OR_CHILD);
}

void BPTree::set_next_leaf(uint8_t* p, uint32_t v) {
    write_u32(p + OFF_NEXT_OR_CHILD, v);
}

uint32_t BPTree::first_child(const uint8_t* p) {
    return read_u32(p + OFF_NEXT_OR_CHILD);
}

void BPTree::set_first_child(uint8_t* p, uint32_t v) {
    write_u32(p + OFF_NEXT_OR_CHILD, v);
}

// ============ Entry access ============

int BPTree::entry_offset(const uint8_t* page, int pos) {
    int off = OFF_ENTRIES;
    for (int i = 0; i < pos; i++) {
        off += entry_size_at(page, off);
    }
    return off;
}

int BPTree::entry_size_at(const uint8_t* page, int off) {
    int key_len = page[off];
    if (is_leaf(page)) {
        return 1 + key_len + 4;       // len + key + value
    } else {
        return 1 + key_len + 4 + 4;   // len + key + value + child
    }
}

int BPTree::entry_bytes_needed(const std::string& key, bool leaf) {
    if (leaf) return 1 + (int)key.size() + 4;
    else      return 1 + (int)key.size() + 4 + 4;
}

std::string BPTree::get_key(const uint8_t* page, int pos) {
    int off = entry_offset(page, pos);
    int key_len = page[off];
    return std::string(reinterpret_cast<const char*>(page + off + 1), key_len);
}

int32_t BPTree::get_value(const uint8_t* page, int pos) {
    int off = entry_offset(page, pos);
    int key_len = page[off];
    return read_i32(page + off + 1 + key_len);
}

uint32_t BPTree::get_child(const uint8_t* page, int child_idx) {
    if (child_idx == 0) {
        return first_child(page);
    }
    int off = entry_offset(page, child_idx - 1);
    int key_len = page[off];
    // For internal nodes: after key + value comes child pointer
    return read_u32(page + off + 1 + key_len + 4);
}

int BPTree::space_used(const uint8_t* page) {
    int n = num_entries(page);
    if (n == 0) return OFF_ENTRIES;
    int off = entry_offset(page, n - 1);
    return off + entry_size_at(page, off);
}

bool BPTree::can_insert(const uint8_t* page, const std::string& key, bool leaf) {
    int needed = entry_bytes_needed(key, leaf);
    return space_used(page) + needed <= PAGE_SIZE;
}

// ============ Comparison helpers (no string allocation) ============

// Compare key at page offset with a search key
// Returns -1 if page_key < search_key, 0 if equal, 1 if page_key > search_key
static int key_compare(const uint8_t* page, int off, const std::string& search_key) {
    int key_len = page[off];
    const char* key_data = reinterpret_cast<const char*>(page + off + 1);
    int min_len = key_len < (int)search_key.size() ? key_len : (int)search_key.size();
    int cmp = std::memcmp(key_data, search_key.data(), min_len);
    if (cmp != 0) return cmp;
    if (key_len < (int)search_key.size()) return -1;
    if (key_len > (int)search_key.size()) return 1;
    return 0;
}

// Check if key at page offset exactly matches search_key
static bool key_matches(const uint8_t* page, int off, const std::string& search_key) {
    int key_len = page[off];
    if (key_len != (int)search_key.size()) return false;
    return std::memcmp(page + off + 1, search_key.data(), key_len) == 0;
}

// ============ Comparison (with string allocation, for non-hot paths) ============

bool BPTree::entry_less(const uint8_t* page, int pos, const std::string& key, int value) {
    int off = entry_offset(page, pos);
    int key_len = page[off];
    std::string k(reinterpret_cast<const char*>(page + off + 1), key_len);
    if (k != key) return k < key;
    int32_t v = read_i32(page + off + 1 + key_len);
    return v < value;
}

int BPTree::find_pos(const uint8_t* page, const std::string& key, int value) {
    // Lower bound: first position where entry >= (key, value)
    int n = num_entries(page);
    int off = OFF_ENTRIES;
    for (int i = 0; i < n; i++) {
        int cmp = key_compare(page, off, key);
        int32_t v = read_i32(page + off + 1 + page[off]);
        if (cmp > 0 || (cmp == 0 && v >= value)) return i;
        off += entry_size_at(page, off);
    }
    return n;
}

int BPTree::find_child_idx(const uint8_t* page, const std::string& key, int value) {
    // Upper bound: first position where entry > (key, value)
    int n = num_entries(page);
    int off = OFF_ENTRIES;
    for (int i = 0; i < n; i++) {
        int cmp = key_compare(page, off, key);
        int32_t v = read_i32(page + off + 1 + page[off]);
        if (cmp > 0 || (cmp == 0 && v > value)) return i;
        off += entry_size_at(page, off);
    }
    return n;
}

int BPTree::find_entry(const uint8_t* page, const std::string& key, int value) {
    int n = num_entries(page);
    int off = OFF_ENTRIES;
    for (int i = 0; i < n; i++) {
        if (key_matches(page, off, key)) {
            int32_t v = read_i32(page + off + 1 + page[off]);
            if (v == value) return i;
        }
        off += entry_size_at(page, off);
    }
    return -1;
}

// ============ Modify page ============

void BPTree::insert_leaf_entry(uint8_t* page, int pos, const std::string& key, int32_t value) {
    int n = num_entries(page);
    int entry_sz = 1 + (int)key.size() + 4;
    int ins_off = (pos < n) ? entry_offset(page, pos) : space_used(page);

    if (pos < n) {
        int end = space_used(page);
        int shift_bytes = end - ins_off;
        if (shift_bytes > 0) {
            std::memmove(page + ins_off + entry_sz, page + ins_off, shift_bytes);
        }
    }

    page[ins_off] = (uint8_t)key.size();
    std::memcpy(page + ins_off + 1, key.data(), key.size());
    write_i32(page + ins_off + 1 + key.size(), value);

    set_num_entries(page, n + 1);
}

void BPTree::insert_internal_entry(uint8_t* page, int pos, const std::string& key, int32_t value, uint32_t child) {
    int n = num_entries(page);
    int entry_sz = 1 + (int)key.size() + 4 + 4;
    int ins_off = (pos < n) ? entry_offset(page, pos) : space_used(page);

    if (pos < n) {
        int end = space_used(page);
        int shift_bytes = end - ins_off;
        if (shift_bytes > 0) {
            std::memmove(page + ins_off + entry_sz, page + ins_off, shift_bytes);
        }
    }

    page[ins_off] = (uint8_t)key.size();
    std::memcpy(page + ins_off + 1, key.data(), key.size());
    write_i32(page + ins_off + 1 + key.size(), value);
    write_u32(page + ins_off + 1 + key.size() + 4, child);

    set_num_entries(page, n + 1);
}

void BPTree::remove_entry(uint8_t* page, int pos) {
    int n = num_entries(page);
    int off = entry_offset(page, pos);
    int sz = entry_size_at(page, off);
    int end = space_used(page);
    int remaining = end - (off + sz);
    if (remaining > 0) {
        std::memmove(page + off, page + off + sz, remaining);
    }
    std::memset(page + off + remaining, 0, sz);
    set_num_entries(page, n - 1);
}

// ============ Page cache ============

uint8_t* BPTree::get_page(uint32_t page_num) {
    auto it = cache_.find(page_num);
    if (it != cache_.end()) {
        return it->second.data;
    }
    CachedPage cp;
    cp.dirty = false;
    std::memset(cp.data, 0, PAGE_SIZE);
    if (std::fseek(file_, (long)page_num * PAGE_SIZE, SEEK_SET) == 0) {
        std::fread(cp.data, 1, PAGE_SIZE, file_);
    }
    auto [new_it, _] = cache_.emplace(page_num, cp);
    return new_it->second.data;
}

void BPTree::mark_dirty(uint32_t page_num) {
    auto it = cache_.find(page_num);
    if (it != cache_.end()) {
        it->second.dirty = true;
    }
}

uint32_t BPTree::alloc_page() {
    uint32_t page = next_free_++;
    uint8_t* p = get_page(page);
    std::memset(p, 0, PAGE_SIZE);
    mark_dirty(page);
    return page;
}

// ============ Header I/O ============

void BPTree::read_header() {
    uint8_t* p = get_page(0);
    uint32_t magic = read_u32(p + OFF_MAGIC);
    if (magic != MAGIC) {
        std::memset(p, 0, PAGE_SIZE);
        write_u32(p + OFF_MAGIC, MAGIC);
        root_page_ = 0;
        first_leaf_ = 0;
        next_free_ = 1;
        total_entries_ = 0;
        write_header();
        mark_dirty(0);
        return;
    }
    root_page_ = read_u32(p + OFF_ROOT);
    total_entries_ = (int)read_u32(p + OFF_TOTAL);
    next_free_ = read_u32(p + OFF_NEXT_FREE);
    first_leaf_ = read_u32(p + OFF_FIRST_LEAF);
}

void BPTree::write_header() {
    uint8_t* p = get_page(0);
    write_u32(p + OFF_MAGIC, MAGIC);
    write_u32(p + OFF_ROOT, root_page_);
    write_u32(p + OFF_TOTAL, (uint32_t)total_entries_);
    write_u32(p + OFF_NEXT_FREE, next_free_);
    write_u32(p + OFF_FIRST_LEAF, first_leaf_);
    mark_dirty(0);
}

void BPTree::flush_dirty() {
    write_header();  // Sync header before writing
    for (auto& [num, cp] : cache_) {
        if (cp.dirty) {
            std::fseek(file_, (long)num * PAGE_SIZE, SEEK_SET);
            std::fwrite(cp.data, 1, PAGE_SIZE, file_);
        }
    }
    std::fflush(file_);
}

// ============ Constructor / Destructor ============

BPTree::BPTree()
    : file_(nullptr), root_page_(0), first_leaf_(0), next_free_(1), total_entries_(0)
{
    file_ = std::fopen(DATA_FILE, "r+b");
    if (!file_) {
        file_ = std::fopen(DATA_FILE, "w+b");
        if (!file_) return;
        uint8_t* p = get_page(0);
        std::memset(p, 0, PAGE_SIZE);
        write_u32(p + OFF_MAGIC, MAGIC);
        root_page_ = 0;
        first_leaf_ = 0;
        next_free_ = 1;
        total_entries_ = 0;
        write_header();
        mark_dirty(0);
        flush_dirty();
    } else {
        read_header();
    }
}

BPTree::~BPTree() {
    if (file_) {
        flush_dirty();
        std::fclose(file_);
        file_ = nullptr;
    }
}

// ============ Public operations ============

void BPTree::insert(const std::string& key, int value) {
    if (!file_) return;

    if (root_page_ == 0) {
        uint32_t new_page = alloc_page();
        uint8_t* page = get_page(new_page);
        set_leaf(page, true);
        set_num_entries(page, 0);
        set_next_leaf(page, 0);
        insert_leaf_entry(page, 0, key, value);
        mark_dirty(new_page);

        root_page_ = new_page;
        first_leaf_ = new_page;
        total_entries_ = 1;
        return;
    }

    SplitResult sr = insert_rec(root_page_, key, value);
    if (sr.split) {
        uint32_t new_root = alloc_page();
        uint8_t* root = get_page(new_root);
        set_leaf(root, false);
        set_num_entries(root, 0);
        set_first_child(root, root_page_);
        insert_internal_entry(root, 0, sr.key, sr.value, sr.new_page);
        mark_dirty(new_root);

        root_page_ = new_root;
    }
}

BPTree::SplitResult BPTree::insert_rec(uint32_t page_num, const std::string& key, int value) {
    uint8_t* page = get_page(page_num);

    if (is_leaf(page)) {
        // Check for duplicate
        if (find_entry(page, key, value) >= 0) {
            return {false, "", 0, 0};
        }

        if (!can_insert(page, key, true)) {
            // Split leaf
            int n = num_entries(page);
            struct Entry {
                std::string key;
                int value;
            };
            std::vector<Entry> entries;
            entries.reserve(n + 1);
            // Sequential traversal to avoid O(n^2) entry_offset calls
            int off = OFF_ENTRIES;
            for (int i = 0; i < n; i++) {
                int key_len = page[off];
                entries.push_back({
                    std::string(reinterpret_cast<const char*>(page + off + 1), key_len),
                    read_i32(page + off + 1 + key_len)
                });
                off += entry_size_at(page, off);
            }
            auto it = std::lower_bound(entries.begin(), entries.end(), Entry{key, value},
                [](const Entry& a, const Entry& b) {
                    if (a.key != b.key) return a.key < b.key;
                    return a.value < b.value;
                });
            entries.insert(it, {key, value});

            int total = (int)entries.size();
            int mid = total / 2;

            // Save old next_leaf
            uint32_t old_next_leaf = next_leaf(page);

            // Rebuild left page
            std::memset(page, 0, PAGE_SIZE);
            set_leaf(page, true);
            set_num_entries(page, 0);
            for (int i = 0; i < mid; i++) {
                insert_leaf_entry(page, i, entries[i].key, entries[i].value);
            }

            // Build right page
            uint32_t right_page = alloc_page();
            uint8_t* right = get_page(right_page);
            set_leaf(right, true);
            set_num_entries(right, 0);
            set_next_leaf(right, old_next_leaf);
            for (int i = mid; i < total; i++) {
                insert_leaf_entry(right, i - mid, entries[i].key, entries[i].value);
            }

            set_next_leaf(page, right_page);

            mark_dirty(page_num);
            mark_dirty(right_page);
            total_entries_++;
            return {true, entries[mid].key, entries[mid].value, right_page};
        }

        // Normal insertion
        int pos = find_pos(page, key, value);
        insert_leaf_entry(page, pos, key, value);
        mark_dirty(page_num);
        total_entries_++;
        return {false, "", 0, 0};
    }

    // Internal node
    int child_idx = find_child_idx(page, key, value);
    uint32_t child_page = get_child(page, child_idx);

    SplitResult sr = insert_rec(child_page, key, value);

    if (sr.split) {
        if (!can_insert(page, sr.key, false)) {
            // Split internal node
            int n = num_entries(page);
            uint32_t child0 = first_child(page);

            struct KV {
                std::string key;
                int32_t value;
                uint32_t child;
            };
            std::vector<KV> kvs;
            kvs.reserve(n + 1);
            // Sequential traversal
            int off2 = OFF_ENTRIES;
            for (int i = 0; i < n; i++) {
                int key_len = page[off2];
                kvs.push_back({
                    std::string(reinterpret_cast<const char*>(page + off2 + 1), key_len),
                    read_i32(page + off2 + 1 + key_len),
                    read_u32(page + off2 + 1 + key_len + 4)
                });
                off2 += entry_size_at(page, off2);
            }

            auto it = std::lower_bound(kvs.begin(), kvs.end(), sr.key,
                [](const KV& kv, const std::string& k) { return kv.key < k; });
            // Insert maintaining sorted order by (key, value)
            while (it != kvs.end() && it->key == sr.key && it->value < sr.value) ++it;
            kvs.insert(it, {sr.key, sr.value, sr.new_page});

            int total = (int)kvs.size();
            int mid = total / 2;

            std::string median_key = kvs[mid].key;
            int32_t median_value = kvs[mid].value;

            // Rebuild left page
            std::memset(page, 0, PAGE_SIZE);
            set_leaf(page, false);
            set_num_entries(page, 0);
            set_first_child(page, child0);
            for (int i = 0; i < mid; i++) {
                insert_internal_entry(page, i, kvs[i].key, kvs[i].value, kvs[i].child);
            }

            // Build right page
            uint32_t right_page = alloc_page();
            uint8_t* right = get_page(right_page);
            set_leaf(right, false);
            set_num_entries(right, 0);
            set_first_child(right, kvs[mid].child);
            for (int i = mid + 1; i < total; i++) {
                insert_internal_entry(right, i - mid - 1, kvs[i].key, kvs[i].value, kvs[i].child);
            }

            mark_dirty(page_num);
            mark_dirty(right_page);

            return {true, median_key, median_value, right_page};
        }

        // Normal insertion into internal node
        int ins_pos = find_pos(page, sr.key, sr.value);
        insert_internal_entry(page, ins_pos, sr.key, sr.value, sr.new_page);
        mark_dirty(page_num);
        return {false, "", 0, 0};
    }

    return {false, "", 0, 0};
}

bool BPTree::remove(const std::string& key, int value) {
    if (!file_ || root_page_ == 0) return false;

    int old_total = total_entries_;
    delete_rec(root_page_, key, value);
    bool found = (total_entries_ < old_total);

    if (found && root_page_ != 0) {
        uint8_t* root = get_page(root_page_);
        if (!is_leaf(root) && num_entries(root) == 0) {
            uint32_t new_root = first_child(root);
            root_page_ = new_root;
        } else if (is_leaf(root) && num_entries(root) == 0) {
            root_page_ = 0;
            first_leaf_ = 0;
        }
    }

    return found;
}

bool BPTree::delete_rec(uint32_t page_num, const std::string& key, int value) {
    uint8_t* page = get_page(page_num);

    if (is_leaf(page)) {
        int pos = find_entry(page, key, value);
        if (pos < 0) return false;

        remove_entry(page, pos);
        mark_dirty(page_num);
        total_entries_--;
        return (page_num != root_page_) && (num_entries(page) < MIN_ENTRIES_LEAF);
    }

    // Internal node
    int child_idx = find_child_idx(page, key, value);
    uint32_t child_page = get_child(page, child_idx);

    bool needs_rebalance = delete_rec(child_page, key, value);

    if (needs_rebalance) {
        rebalance(page_num, child_idx);
    }

    page = get_page(page_num);
    return (page_num != root_page_) && (num_entries(page) < MIN_KEYS_INTERNAL);
}

void BPTree::rebalance(uint32_t parent_num, int child_idx) {
    uint8_t* parent = get_page(parent_num);
    int n = num_entries(parent);

    uint32_t child_page = get_child(parent, child_idx);
    uint8_t* child = get_page(child_page);
    bool child_is_leaf = is_leaf(child);

    // Try borrow from left sibling
    if (child_idx > 0) {
        uint32_t left_page = get_child(parent, child_idx - 1);
        uint8_t* left = get_page(left_page);

        if (child_is_leaf && num_entries(left) > MIN_ENTRIES_LEAF) {
            int last_pos = num_entries(left) - 1;
            std::string borrowed_key = get_key(left, last_pos);
            int32_t borrowed_val = get_value(left, last_pos);

            remove_entry(left, last_pos);
            mark_dirty(left_page);

            insert_leaf_entry(child, 0, borrowed_key, borrowed_val);
            mark_dirty(child_page);

            // Update parent separator: new separator is first entry of child
            std::string new_sep_key = get_key(child, 0);
            int32_t new_sep_val = get_value(child, 0);
            uint32_t ch_p = get_child(parent, child_idx);
            remove_entry(parent, child_idx - 1);
            insert_internal_entry(parent, child_idx - 1, new_sep_key, new_sep_val, ch_p);
            mark_dirty(parent_num);
            return;
        }

        if (!child_is_leaf && num_entries(left) > MIN_KEYS_INTERNAL) {
            int last_pos = num_entries(left) - 1;
            std::string borrowed_key = get_key(left, last_pos);
            int32_t borrowed_val = get_value(left, last_pos);
            uint32_t borrowed_child = get_child(left, last_pos + 1);

            // Parent separator becomes new first key of child
            std::string parent_sep_key = get_key(parent, child_idx - 1);
            int32_t parent_sep_val = get_value(parent, child_idx - 1);

            remove_entry(left, last_pos);
            mark_dirty(left_page);

            // Rebuild child: new first_child = borrowed_child, then insert (parent_sep, old_first_child)
            uint32_t orig_first = first_child(child);
            int child_n = num_entries(child);

            struct KV {
                std::string key;
                int32_t value;
                uint32_t ch;
            };
            std::vector<KV> kvs;
            kvs.reserve(child_n + 1);
            kvs.push_back({parent_sep_key, parent_sep_val, orig_first});
            for (int i = 0; i < child_n; i++) {
                kvs.push_back({get_key(child, i), get_value(child, i), get_child(child, i + 1)});
            }

            std::memset(child, 0, PAGE_SIZE);
            set_leaf(child, false);
            set_num_entries(child, 0);
            set_first_child(child, borrowed_child);
            for (int i = 0; i < (int)kvs.size(); i++) {
                insert_internal_entry(child, i, kvs[i].key, kvs[i].value, kvs[i].ch);
            }
            mark_dirty(child_page);

            // Update parent separator
            uint32_t ch_p = get_child(parent, child_idx);
            remove_entry(parent, child_idx - 1);
            insert_internal_entry(parent, child_idx - 1, borrowed_key, borrowed_val, ch_p);
            mark_dirty(parent_num);
            return;
        }
    }

    // Try borrow from right sibling
    if (child_idx < n) {
        uint32_t right_page = get_child(parent, child_idx + 1);
        uint8_t* right = get_page(right_page);

        if (child_is_leaf && num_entries(right) > MIN_ENTRIES_LEAF) {
            std::string borrowed_key = get_key(right, 0);
            int32_t borrowed_val = get_value(right, 0);

            remove_entry(right, 0);
            mark_dirty(right_page);

            int child_n = num_entries(child);
            insert_leaf_entry(child, child_n, borrowed_key, borrowed_val);
            mark_dirty(child_page);

            // Update parent separator for right sibling
            std::string new_sep_key = get_key(right, 0);
            int32_t new_sep_val = get_value(right, 0);
            uint32_t ch_p = get_child(parent, child_idx + 1);
            remove_entry(parent, child_idx);
            insert_internal_entry(parent, child_idx, new_sep_key, new_sep_val, ch_p);
            mark_dirty(parent_num);
            return;
        }

        if (!child_is_leaf && num_entries(right) > MIN_KEYS_INTERNAL) {
            std::string borrowed_key = get_key(right, 0);
            int32_t borrowed_val = get_value(right, 0);
            uint32_t right_first = first_child(right);
            uint32_t borrowed_child = get_child(right, 1);

            std::string parent_sep_key = get_key(parent, child_idx);
            int32_t parent_sep_val = get_value(parent, child_idx);

            remove_entry(right, 0);
            set_first_child(right, borrowed_child);
            mark_dirty(right_page);

            int child_n = num_entries(child);
            insert_internal_entry(child, child_n, parent_sep_key, parent_sep_val, right_first);
            mark_dirty(child_page);

            uint32_t ch_p = get_child(parent, child_idx + 1);
            remove_entry(parent, child_idx);
            insert_internal_entry(parent, child_idx, borrowed_key, borrowed_val, ch_p);
            mark_dirty(parent_num);
            return;
        }
    }

    // Merge with left sibling
    if (child_idx > 0) {
        uint32_t left_page = get_child(parent, child_idx - 1);
        uint8_t* left = get_page(left_page);

        if (child_is_leaf) {
            int child_n = num_entries(child);
            int left_n = num_entries(left);
            for (int i = 0; i < child_n; i++) {
                insert_leaf_entry(left, left_n + i, get_key(child, i), get_value(child, i));
            }
            set_next_leaf(left, next_leaf(child));
            mark_dirty(left_page);
        } else {
            std::string parent_sep_key = get_key(parent, child_idx - 1);
            int32_t parent_sep_val = get_value(parent, child_idx - 1);
            uint32_t child_first = first_child(child);

            int left_n = num_entries(left);
            insert_internal_entry(left, left_n, parent_sep_key, parent_sep_val, child_first);

            int child_n = num_entries(child);
            for (int i = 0; i < child_n; i++) {
                insert_internal_entry(left, left_n + 1 + i, get_key(child, i), get_value(child, i), get_child(child, i + 1));
            }
            mark_dirty(left_page);
        }

        remove_entry(parent, child_idx - 1);
        mark_dirty(parent_num);

        if (child_is_leaf && child_page == first_leaf_) {
            first_leaf_ = left_page;
        }
        return;
    }

    // Merge with right sibling
    if (child_idx < n) {
        uint32_t right_page = get_child(parent, child_idx + 1);
        uint8_t* right = get_page(right_page);

        if (child_is_leaf) {
            int child_n = num_entries(child);
            int right_n = num_entries(right);
            for (int i = 0; i < right_n; i++) {
                insert_leaf_entry(child, child_n + i, get_key(right, i), get_value(right, i));
            }
            set_next_leaf(child, next_leaf(right));
            mark_dirty(child_page);
        } else {
            std::string parent_sep_key = get_key(parent, child_idx);
            int32_t parent_sep_val = get_value(parent, child_idx);
            uint32_t right_first = first_child(right);

            int child_n = num_entries(child);
            insert_internal_entry(child, child_n, parent_sep_key, parent_sep_val, right_first);

            int right_n = num_entries(right);
            for (int i = 0; i < right_n; i++) {
                insert_internal_entry(child, child_n + 1 + i, get_key(right, i), get_value(right, i), get_child(right, i + 1));
            }
            mark_dirty(child_page);
        }

        remove_entry(parent, child_idx);
        mark_dirty(parent_num);

        if (child_is_leaf && right_page == first_leaf_) {
            first_leaf_ = child_page;
        }
        return;
    }
}

std::vector<int> BPTree::find(const std::string& key) {
    std::vector<int> results;
    if (!file_ || root_page_ == 0) return results;

    // Traverse to leaf using (key, INT_MIN) — this finds the first entry with this key
    uint32_t page_num = root_page_;
    while (true) {
        uint8_t* page = get_page(page_num);
        if (is_leaf(page)) {
            // Scan this leaf and following leaves for matching entries
            while (page_num != 0) {
                page = get_page(page_num);
                int n = num_entries(page);
                int off = OFF_ENTRIES;
                int last_off = OFF_ENTRIES;
                bool found_any = false;
                for (int i = 0; i < n; i++) {
                    if (key_matches(page, off, key)) {
                        int32_t v = read_i32(page + off + 1 + page[off]);
                        results.push_back(v);
                        found_any = true;
                    } else if (found_any) {
                        return results;
                    }
                    last_off = off;
                    off += entry_size_at(page, off);
                }
                if (found_any && n > 0) {
                    if (!key_matches(page, last_off, key)) {
                        return results;
                    }
                }
                page_num = next_leaf(page);
            }
            return results;
        }
        // Internal node: find child
        int child_idx = find_child_idx(page, key, INT_MIN);
        page_num = get_child(page, child_idx);
    }
}