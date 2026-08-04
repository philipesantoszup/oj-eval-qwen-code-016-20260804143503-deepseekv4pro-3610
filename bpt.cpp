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
    return 1 + key_len + 4;
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
    return read_u32(page + off + 1 + key_len);
}

int BPTree::space_used(const uint8_t* page) {
    int n = num_entries(page);
    if (n == 0) return OFF_ENTRIES;
    int off = entry_offset(page, n - 1);
    return off + entry_size_at(page, off);
}

bool BPTree::can_insert(const uint8_t* page, const std::string& key) {
    int needed = 1 + (int)key.size() + 4;
    return space_used(page) + needed <= PAGE_SIZE;
}

// ============ Search within a page ============

int BPTree::find_pos(const uint8_t* page, const std::string& key) {
    int n = num_entries(page);
    int off = OFF_ENTRIES;
    for (int i = 0; i < n; i++) {
        int key_len = page[off];
        std::string k(reinterpret_cast<const char*>(page + off + 1), key_len);
        if (k >= key) return i;
        off += 1 + key_len + 4;
    }
    return n;
}

int BPTree::find_child(const uint8_t* page, const std::string& key) {
    // Returns first position where entry.key > key (upper_bound)
    // For internal nodes: the child index to follow for search key
    int n = num_entries(page);
    int off = OFF_ENTRIES;
    for (int i = 0; i < n; i++) {
        int key_len = page[off];
        std::string k(reinterpret_cast<const char*>(page + off + 1), key_len);
        if (k > key) return i;
        off += 1 + key_len + 4;
    }
    return n;
}

int BPTree::find_entry(const uint8_t* page, const std::string& key, int value) {
    int n = num_entries(page);
    int off = OFF_ENTRIES;
    for (int i = 0; i < n; i++) {
        int key_len = page[off];
        std::string k(reinterpret_cast<const char*>(page + off + 1), key_len);
        if (k == key) {
            int32_t v = read_i32(page + off + 1 + key_len);
            if (v == value) return i;
        }
        off += 1 + key_len + 4;
    }
    return -1;
}

// ============ Modify page ============

void BPTree::insert_entry(uint8_t* page, int pos, const std::string& key, int32_t val_or_child, bool leaf) {
    int n = num_entries(page);
    int ins_off = (pos < n) ? entry_offset(page, pos) : space_used(page);
    int entry_sz = 1 + (int)key.size() + 4;

    // Shift existing entries right
    if (pos < n) {
        int end = space_used(page);
        int shift_bytes = end - ins_off;
        if (shift_bytes > 0) {
            std::memmove(page + ins_off + entry_sz, page + ins_off, shift_bytes);
        }
    }

    // Write new entry
    page[ins_off] = (uint8_t)key.size();
    std::memcpy(page + ins_off + 1, key.data(), key.size());
    write_i32(page + ins_off + 1 + key.size(), val_or_child);

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
    // Zero out the freed area at the end
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
    write_header();
    // Initialize in cache
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
        // New file or corrupted — initialize
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
        if (!file_) {
            return;
        }
        // Initialize header
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
        // Empty tree — create root leaf
        uint32_t new_page = alloc_page();
        uint8_t* page = get_page(new_page);
        set_leaf(page, true);
        set_num_entries(page, 0);
        set_next_leaf(page, 0);

        insert_entry(page, 0, key, value, true);
        mark_dirty(new_page);

        root_page_ = new_page;
        first_leaf_ = new_page;
        total_entries_ = 1;
        write_header();
        return;
    }

    SplitResult sr = insert_rec(root_page_, key, value);
    if (sr.split) {
        // Root split — create new root
        uint32_t new_root = alloc_page();
        uint8_t* root = get_page(new_root);
        set_leaf(root, false);
        set_num_entries(root, 0);
        set_first_child(root, root_page_);

        insert_entry(root, 0, sr.key, (int32_t)sr.new_page, false);
        mark_dirty(new_root);

        root_page_ = new_root;
        write_header();
    }
}

BPTree::SplitResult BPTree::insert_rec(uint32_t page_num, const std::string& key, int value) {
    uint8_t* page = get_page(page_num);

    if (is_leaf(page)) {
        // Check for duplicate
        if (find_entry(page, key, value) >= 0) {
            return {false, "", 0};
        }

        if (!can_insert(page, key)) {
            // Split leaf with insertion
            int n = num_entries(page);

            // Collect all entries + new entry
            struct Entry {
                std::string key;
                int value;
            };
            std::vector<Entry> entries;
            entries.reserve(n + 1);
            for (int i = 0; i < n; i++) {
                entries.push_back({get_key(page, i), get_value(page, i)});
            }

            // Insert new entry in sorted order
            auto it = std::lower_bound(entries.begin(), entries.end(), Entry{key, value},
                [](const Entry& a, const Entry& b) {
                    if (a.key != b.key) return a.key < b.key;
                    return a.value < b.value;
                });
            entries.insert(it, {key, value});

            int total = (int)entries.size();
            int mid = total / 2;

            // Avoid splitting within same-key group to prevent duplicate keys
            // in internal nodes. Move all entries with the median key to left.
            while (mid < total && entries[mid].key == entries[mid-1].key) {
                mid++;
            }
            if (mid == total) {
                // All remaining entries share the same key — must split anyway
                mid = total / 2;
            }

            // Save original next_leaf before clearing
            uint32_t old_next_leaf = next_leaf(page);

            // Rebuild left page with first half
            std::memset(page, 0, PAGE_SIZE);
            set_leaf(page, true);
            set_num_entries(page, 0);

            for (int i = 0; i < mid; i++) {
                insert_entry(page, i, entries[i].key, entries[i].value, true);
            }

            // Build right page with second half
            uint32_t right_page = alloc_page();
            uint8_t* right = get_page(right_page);
            set_leaf(right, true);
            set_num_entries(right, 0);
            set_next_leaf(right, old_next_leaf);

            for (int i = mid; i < total; i++) {
                insert_entry(right, i - mid, entries[i].key, entries[i].value, true);
            }

            // Update linked list
            set_next_leaf(page, right_page);

            mark_dirty(page_num);
            mark_dirty(right_page);
            total_entries_++;
            write_header();

            return {true, entries[mid].key, right_page};
        }

        // Normal insertion
        int pos = find_pos(page, key);
        // Advance past same-key entries with smaller values
        while (pos < num_entries(page) && get_key(page, pos) == key && get_value(page, pos) < value) {
            pos++;
        }
        insert_entry(page, pos, key, value, true);
        mark_dirty(page_num);
        total_entries_++;
        write_header();
        return {false, "", 0};
    }

    // Internal node
    int child_idx = find_child(page, key);
    uint32_t child_page = get_child(page, child_idx);

    SplitResult sr = insert_rec(child_page, key, value);

    if (sr.split) {
        if (!can_insert(page, sr.key)) {
            // Split internal node with insertion
            int n = num_entries(page);
            uint32_t child0 = first_child(page);

            struct KV {
                std::string key;
                uint32_t child;
            };
            std::vector<KV> kvs;
            kvs.reserve(n + 1);
            for (int i = 0; i < n; i++) {
                kvs.push_back({get_key(page, i), get_child(page, i + 1)});
            }

            // Insert new (key, child) in sorted order
            auto it = std::lower_bound(kvs.begin(), kvs.end(), sr.key,
                [](const KV& kv, const std::string& k) { return kv.key < k; });
            kvs.insert(it, {sr.key, sr.new_page});

            int total = (int)kvs.size();
            int mid = total / 2;

            // Avoid splitting within same-key group to prevent duplicate keys in parent
            while (mid < total && kvs[mid].key == kvs[mid-1].key) {
                mid++;
            }
            if (mid == total) {
                mid = total / 2;
            }

            std::string median_key = kvs[mid].key;

            // Rebuild left page
            std::memset(page, 0, PAGE_SIZE);
            set_leaf(page, false);
            set_num_entries(page, 0);
            set_first_child(page, child0);

            for (int i = 0; i < mid; i++) {
                insert_entry(page, i, kvs[i].key, (int32_t)kvs[i].child, false);
            }

            // Build right page
            uint32_t right_page = alloc_page();
            uint8_t* right = get_page(right_page);
            set_leaf(right, false);
            set_num_entries(right, 0);
            set_first_child(right, kvs[mid].child);

            for (int i = mid + 1; i < total; i++) {
                insert_entry(right, i - mid - 1, kvs[i].key, (int32_t)kvs[i].child, false);
            }

            mark_dirty(page_num);
            mark_dirty(right_page);

            return {true, median_key, right_page};
        }

        // Normal insertion into internal node
        int ins_pos = find_pos(page, sr.key);
        insert_entry(page, ins_pos, sr.key, (int32_t)sr.new_page, false);
        mark_dirty(page_num);
        return {false, "", 0};
    }

    return {false, "", 0};
}

bool BPTree::remove(const std::string& key, int value) {
    if (!file_ || root_page_ == 0) return false;

    int old_total = total_entries_;
    delete_rec(root_page_, key, value);
    bool found = (total_entries_ < old_total);

    // Check if root is now empty
    if (found && root_page_ != 0) {
        uint8_t* root = get_page(root_page_);
        if (!is_leaf(root) && num_entries(root) == 0) {
            // Root has no keys — child becomes new root
            uint32_t new_root = first_child(root);
            root_page_ = new_root;
            write_header();
        } else if (is_leaf(root) && num_entries(root) == 0) {
            // Tree is now empty
            root_page_ = 0;
            first_leaf_ = 0;
            write_header();
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
        write_header();

        // Return true if underfull (and not root)
        return (page_num != root_page_) && (num_entries(page) < MIN_ENTRIES_LEAF);
    }

    // Internal node
    int child_idx = find_child(page, key);
    uint32_t child_page = get_child(page, child_idx);

    bool needs_rebalance = delete_rec(child_page, key, value);

    if (needs_rebalance) {
        rebalance(page_num, child_idx);
    }

    // Check if this node is underfull
    page = get_page(page_num);  // re-read after potential changes
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
            // Borrow last entry from left sibling
            int last_pos = num_entries(left) - 1;
            std::string borrowed_key = get_key(left, last_pos);
            int32_t borrowed_val = get_value(left, last_pos);

            remove_entry(left, last_pos);
            mark_dirty(left_page);

            // Insert at front of child
            insert_entry(child, 0, borrowed_key, borrowed_val, true);
            mark_dirty(child_page);

            // Update parent separator
            // The separator between left and child should be the first key of child
            std::string new_sep = get_key(child, 0);
            // Update key at position child_idx-1 in parent
            int key_off = entry_offset(parent, child_idx - 1);
            // We need to replace the key. Since keys are variable-length, this is tricky.
            // For simplicity, we remove and re-insert the entry.
            uint32_t ch = get_child(parent, child_idx);
            remove_entry(parent, child_idx - 1);
            insert_entry(parent, child_idx - 1, new_sep, (int32_t)ch, false);
            mark_dirty(parent_num);
            return;
        }

        if (!child_is_leaf && num_entries(left) > MIN_KEYS_INTERNAL) {
            // Borrow last entry from left internal sibling
            int last_pos = num_entries(left) - 1;
            std::string borrowed_key = get_key(left, last_pos);
            uint32_t borrowed_child = get_child(left, last_pos + 1);

            // The separator in parent becomes the new first key of child
            std::string parent_sep = get_key(parent, child_idx - 1);

            remove_entry(left, last_pos);
            mark_dirty(left_page);

            // Insert at front of child: (parent_sep, child's old first_child)
            uint32_t child_first = first_child(child);
            // Shift everything in child right by 1
            // Actually, insert_entry handles this
            // But we need to set child's first_child to borrowed_child
            // And insert (parent_sep, child_first) at position 0

            // Build new child content
            int child_n = num_entries(child);
            uint32_t orig_first = first_child(child);

            struct KV {
                std::string key;
                uint32_t ch;
            };
            std::vector<KV> kvs;
            kvs.reserve(child_n + 1);
            kvs.push_back({parent_sep, orig_first});
            for (int i = 0; i < child_n; i++) {
                kvs.push_back({get_key(child, i), get_child(child, i + 1)});
            }

            std::memset(child, 0, PAGE_SIZE);
            set_leaf(child, false);
            set_num_entries(child, 0);
            set_first_child(child, borrowed_child);

            for (int i = 0; i < (int)kvs.size(); i++) {
                insert_entry(child, i, kvs[i].key, (int32_t)kvs[i].ch, false);
            }
            mark_dirty(child_page);

            // Update parent separator
            uint32_t ch_p = get_child(parent, child_idx);
            remove_entry(parent, child_idx - 1);
            insert_entry(parent, child_idx - 1, borrowed_key, (int32_t)ch_p, false);
            mark_dirty(parent_num);
            return;
        }
    }

    // Try borrow from right sibling
    if (child_idx < n) {
        uint32_t right_page = get_child(parent, child_idx + 1);
        uint8_t* right = get_page(right_page);

        if (child_is_leaf && num_entries(right) > MIN_ENTRIES_LEAF) {
            // Borrow first entry from right sibling
            std::string borrowed_key = get_key(right, 0);
            int32_t borrowed_val = get_value(right, 0);

            remove_entry(right, 0);
            mark_dirty(right_page);

            // Insert at end of child
            int child_n = num_entries(child);
            insert_entry(child, child_n, borrowed_key, borrowed_val, true);
            mark_dirty(child_page);

            // Update parent separator for right sibling
            std::string new_sep = get_key(right, 0);
            uint32_t ch = get_child(parent, child_idx + 1);
            remove_entry(parent, child_idx);
            insert_entry(parent, child_idx, new_sep, (int32_t)ch, false);
            mark_dirty(parent_num);
            return;
        }

        if (!child_is_leaf && num_entries(right) > MIN_KEYS_INTERNAL) {
            // Borrow first entry from right internal sibling
            std::string borrowed_key = get_key(right, 0);
            uint32_t right_first = first_child(right);
            uint32_t borrowed_child = get_child(right, 1);

            // The separator in parent becomes the new last key of child
            std::string parent_sep = get_key(parent, child_idx);

            remove_entry(right, 0);
            // Update right's first_child to borrowed_child
            set_first_child(right, borrowed_child);
            mark_dirty(right_page);

            // Insert at end of child: (parent_sep, right_first)
            int child_n = num_entries(child);
            insert_entry(child, child_n, parent_sep, (int32_t)right_first, false);
            mark_dirty(child_page);

            // Update parent separator
            uint32_t ch = get_child(parent, child_idx + 1);
            remove_entry(parent, child_idx);
            insert_entry(parent, child_idx, borrowed_key, (int32_t)ch, false);
            mark_dirty(parent_num);
            return;
        }
    }

    // Merge with left sibling
    if (child_idx > 0) {
        uint32_t left_page = get_child(parent, child_idx - 1);
        uint8_t* left = get_page(left_page);

        if (child_is_leaf) {
            // Move all entries from child to left
            int child_n = num_entries(child);
            int left_n = num_entries(left);
            for (int i = 0; i < child_n; i++) {
                insert_entry(left, left_n + i, get_key(child, i), get_value(child, i), true);
            }
            set_next_leaf(left, next_leaf(child));
            mark_dirty(left_page);
        } else {
            // Internal merge: separator from parent becomes a key in left
            std::string parent_sep = get_key(parent, child_idx - 1);
            uint32_t child_first = first_child(child);

            int left_n = num_entries(left);
            insert_entry(left, left_n, parent_sep, (int32_t)child_first, false);

            int child_n = num_entries(child);
            for (int i = 0; i < child_n; i++) {
                insert_entry(left, left_n + 1 + i, get_key(child, i), (int32_t)get_child(child, i + 1), false);
            }
            mark_dirty(left_page);
        }

        // Remove separator from parent
        remove_entry(parent, child_idx - 1);
        mark_dirty(parent_num);

        // If this was the first leaf, update first_leaf_
        if (child_is_leaf && child_page == first_leaf_) {
            first_leaf_ = left_page;
            write_header();
        }
        return;
    }

    // Merge with right sibling
    if (child_idx < n) {
        uint32_t right_page = get_child(parent, child_idx + 1);
        uint8_t* right = get_page(right_page);

        if (child_is_leaf) {
            // Move all entries from right to child
            int child_n = num_entries(child);
            int right_n = num_entries(right);
            for (int i = 0; i < right_n; i++) {
                insert_entry(child, child_n + i, get_key(right, i), get_value(right, i), true);
            }
            set_next_leaf(child, next_leaf(right));
            mark_dirty(child_page);
        } else {
            // Internal merge: separator from parent becomes a key in child
            std::string parent_sep = get_key(parent, child_idx);
            uint32_t right_first = first_child(right);

            int child_n = num_entries(child);
            insert_entry(child, child_n, parent_sep, (int32_t)right_first, false);

            int right_n = num_entries(right);
            for (int i = 0; i < right_n; i++) {
                insert_entry(child, child_n + 1 + i, get_key(right, i), (int32_t)get_child(right, i + 1), false);
            }
            mark_dirty(child_page);
        }

        // Remove separator from parent
        remove_entry(parent, child_idx);
        mark_dirty(parent_num);

        // If right was the first leaf, update first_leaf_
        if (child_is_leaf && right_page == first_leaf_) {
            first_leaf_ = child_page;
            write_header();
        }
        return;
    }
}

std::vector<int> BPTree::find(const std::string& key) {
    std::vector<int> results;
    if (!file_ || root_page_ == 0) return results;

    // Traverse to leaf
    uint32_t page_num = root_page_;
    while (true) {
        uint8_t* page = get_page(page_num);
        if (is_leaf(page)) {
            // Search this leaf and following leaves for matching entries
            while (page_num != 0) {
                page = get_page(page_num);
                int n = num_entries(page);
                int off = OFF_ENTRIES;
                bool found_any = false;
                for (int i = 0; i < n; i++) {
                    int key_len = page[off];
                    std::string k(reinterpret_cast<const char*>(page + off + 1), key_len);
                    if (k == key) {
                        int32_t v = read_i32(page + off + 1 + key_len);
                        results.push_back(v);
                        found_any = true;
                    } else if (found_any) {
                        // Since entries are sorted, once we see a different key after finding matches, we're done
                        return results;
                    }
                    off += 1 + key_len + 4;
                }
                if (found_any && n > 0) {
                    // Check if the last entry in this leaf has our key
                    // If so, we need to continue to next leaf
                    int last_off = entry_offset(page, n - 1);
                    int last_key_len = page[last_off];
                    std::string last_k(reinterpret_cast<const char*>(page + last_off + 1), last_key_len);
                    if (last_k != key) {
                        return results;
                    }
                }
                page_num = next_leaf(page);
            }
            return results;
        }
        // Internal node: find child
        int child_idx = find_child(page, key);
        page_num = get_child(page, child_idx);
    }
}