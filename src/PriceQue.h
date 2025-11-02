#include <queue>
#include <set>

template<class T, class Compare>
struct PriceLevelQue {
    static constexpr bool has_erase    = false;
    static constexpr bool has_iterator = false;

    PriceLevelQue(size_t nreserve, Compare comp) : _comp(std::move(comp)) { _data.reserve(nreserve); }

    void push(T val) {
        _data.push_back(std::move(val));
        std::push_heap(_data.begin(), _data.end(), _comp);
    }
    bool     empty() const { return _data.empty(); }
    size_t   size() const { return _data.size(); }
    T const &top() const {
        assert(!empty());
        return _data.front();
    }
    void pop() {
        assert(!empty());
        std::pop_heap(_data.begin(), _data.end(), _comp);
        _data.pop_back();
    }

private:
    std::vector<T> _data;
    Compare        _comp;
};

template<class T, class Compare>
struct PriceLevelTree {
    static constexpr bool has_erase    = true;
    static constexpr bool has_iterator = true;

    using iterator = typename std::set<T, Compare>::const_reverse_iterator;

    PriceLevelTree(size_t nreserve, Compare comp) : _data(std::move(comp)) {}
    void   push(T val) { _data.insert(std::move(val)); }
    bool   empty() const { return _data.empty(); }
    size_t size() const { return _data.size(); }
    // return the one with max value
    T const &top() const {
        assert(!empty());
        return *_data.rbegin();
    }
    void pop() {
        assert(!empty());
        _data.erase(--_data.end());
    }
    void erase(const T &val) { _data.erase(val); }
    // return the pointer to the max/top value
    auto begin() const { return _data.rbegin(); }
    auto end() const { return _data.rend(); }

private:
    std::set<T, Compare> _data;
};