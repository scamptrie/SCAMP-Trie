#ifndef __SCAMPTRIE_NODE_BLOCK_LIST_H__
#define __SCAMPTRIE_NODE_BLOCK_LIST_H__

#include <cstddef>
#include <vector>

struct ScampTrieNode;

// Blocked sorted pointer list for ScampTrieNode*.
class ScampTrieNodeBlockList {
public:
    static constexpr size_t kBlockSize = 256;

    size_t size() const { return total_size_; }
    bool empty() const { return total_size_ == 0; }
    void clear() {
        blocks_.clear();
        total_size_ = 0;
    }
    void reserve_hint(size_t /*n*/) {}

    ScampTrieNode* at(size_t index) const {
        size_t b = 0, off = 0;
        locate(index, b, off);
        return blocks_[b][off];
    }

    void set_at(size_t index, ScampTrieNode* value) {
        size_t b = 0, off = 0;
        locate(index, b, off);
        blocks_[b][off] = value;
    }

    template <typename Less>
    size_t lower_bound_index(ScampTrieNode* target, Less less) const {
        if (total_size_ == 0) return 0;
        size_t lb = 0, rb = blocks_.size();
        while (lb < rb) {
            size_t mid = (lb + rb) >> 1;
            ScampTrieNode* last = blocks_[mid].back();
            if (less(last, target)) lb = mid + 1;
            else rb = mid;
        }
        if (lb >= blocks_.size()) return total_size_;
        const auto& blk = blocks_[lb];
        size_t l = 0, r = blk.size();
        while (l < r) {
            size_t m = (l + r) >> 1;
            if (less(blk[m], target)) l = m + 1;
            else r = m;
        }
        return prefix_size_before_block(lb) + l;
    }

    template <typename Pred>
    size_t scan_while(size_t start, Pred pred) const {
        size_t cur = start;
        while (cur < total_size_ && pred(at(cur))) ++cur;
        return cur;
    }

    template <typename OutputVec>
    void copy_range_to(size_t l, size_t r, OutputVec& out) const {
        if (r <= l || l >= total_size_) return;
        if (r > total_size_) r = total_size_;
        out.clear();
        out.reserve(r - l);
        for (size_t i = l; i < r; ++i) out.push_back(at(i));
    }

    void assign_from_vector(const std::vector<ScampTrieNode*>& data) {
        clear();
        for (ScampTrieNode* p : data) push_back(p);
    }

    void erase_range(size_t l, size_t r) {
        if (r <= l || l >= total_size_) return;
        if (r > total_size_) r = total_size_;
        const size_t erase_n = r - l;
        for (size_t i = 0; i < erase_n; ++i) erase_at(l);
    }

    void insert_at(size_t index, ScampTrieNode* value) {
        if (index > total_size_) index = total_size_;
        if (blocks_.empty()) blocks_.push_back({});
        if (index == total_size_) {
            push_back(value);
            return;
        }
        size_t b = 0, off = 0;
        locate(index, b, off);
        blocks_[b].insert(blocks_[b].begin() + static_cast<long>(off), value);
        ++total_size_;
        rebalance_from_block(b);
    }

private:
    std::vector<std::vector<ScampTrieNode*>> blocks_;
    size_t total_size_ = 0;

    void push_back(ScampTrieNode* value) {
        if (blocks_.empty() || blocks_.back().size() >= kBlockSize) {
            blocks_.push_back({});
            blocks_.back().reserve(kBlockSize);
        }
        blocks_.back().push_back(value);
        ++total_size_;
    }

    size_t prefix_size_before_block(size_t block_idx) const {
        size_t s = 0;
        for (size_t i = 0; i < block_idx; ++i) s += blocks_[i].size();
        return s;
    }

    void locate(size_t index, size_t& block_idx, size_t& offset) const {
        if (index >= total_size_) {
            block_idx = blocks_.size() - 1;
            offset = blocks_[block_idx].size();
            return;
        }
        size_t acc = 0;
        for (size_t b = 0; b < blocks_.size(); ++b) {
            size_t next = acc + blocks_[b].size();
            if (index < next) {
                block_idx = b;
                offset = index - acc;
                return;
            }
            acc = next;
        }
        block_idx = blocks_.size() - 1;
        offset = blocks_[block_idx].size();
    }

    void erase_at(size_t index) {
        size_t b = 0, off = 0;
        locate(index, b, off);
        auto& blk = blocks_[b];
        blk.erase(blk.begin() + static_cast<long>(off));
        --total_size_;
        if (blk.empty()) {
            blocks_.erase(blocks_.begin() + static_cast<long>(b));
            return;
        }
        while (b + 1 < blocks_.size() && blk.size() < kBlockSize / 2 && !blocks_[b + 1].empty()) {
            blk.push_back(blocks_[b + 1].front());
            blocks_[b + 1].erase(blocks_[b + 1].begin());
            if (blocks_[b + 1].empty()) {
                blocks_.erase(blocks_.begin() + static_cast<long>(b + 1));
                break;
            }
        }
    }

    void rebalance_from_block(size_t b) {
        size_t cur = b;
        while (cur < blocks_.size() && blocks_[cur].size() > kBlockSize) {
            std::vector<ScampTrieNode*> spill;
            auto& blk = blocks_[cur];
            const size_t keep = kBlockSize / 2;
            spill.assign(blk.begin() + static_cast<long>(keep), blk.end());
            blk.erase(blk.begin() + static_cast<long>(keep), blk.end());
            blocks_.insert(blocks_.begin() + static_cast<long>(cur + 1), std::move(spill));
            ++cur;
        }
    }
};

#endif // __SCAMPTRIE_NODE_BLOCK_LIST_H__
