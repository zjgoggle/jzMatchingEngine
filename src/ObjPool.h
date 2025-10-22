#pragma once

#include <vector>
#include <type_traits>
#include <utility>
#include <assert.h>
#include <stdint.h>

//====================================================================================
//                  ObjPool
//====================================================================================

using PooledObjID                                  = int32_t; // starts from 0
inline constexpr PooledObjID INVALID_POOLED_OBJ_ID = -1;

/// Object pool saves objects in a vector. It only retrun an index of the allocated object. The allocated objects may change when new elements are allocated.
/// Users should NOT save the pointer to the object, but saves the PooledObjID instead, which is used to obtain the object.
template<class T>
class ObjPool {
    static constexpr bool IS_TRIVIALLY_ASSGINABLE_DESTRUCTIBLE = std::is_trivially_copy_assignable_v<T> && std::is_trivially_destructible_v<T>;
    static_assert(IS_TRIVIALLY_ASSGINABLE_DESTRUCTIBLE); // MAYDO: support non trivially copy assignable.
    static constexpr size_t ALIGNMENT  = sizeof(PooledObjID);
    static constexpr size_t ALIGN_UP_T = (sizeof(T) + (ALIGNMENT - 1)) & (~(ALIGNMENT - 1));
    static constexpr size_t NODE_SIZE  = std::max(ALIGN_UP_T, sizeof(PooledObjID));

    struct Node {
        char               storage[NODE_SIZE];
        T                 &asObj() { return *reinterpret_cast<T *>(storage); }
        PooledObjID       &asNextFreeID() { return *reinterpret_cast<PooledObjID *>(storage); }
        PooledObjID const &asNextFreeID() const { return *reinterpret_cast<PooledObjID const *>(storage); }
    };

    std::vector<Node> _storage;
    PooledObjID       _freeList = INVALID_POOLED_OBJ_ID;

public:
    struct UniquePtr {
        ObjPool    *pool  = nullptr; // must be valid if objID is valid.
        PooledObjID objID = INVALID_POOLED_OBJ_ID;

    public:
        UniquePtr(ObjPool *pool = nullptr, PooledObjID objID = INVALID_POOLED_OBJ_ID) : pool(pool), objID(objID) {}
        UniquePtr(UniquePtr &&a) : pool(a.pool), objID(a.objID) {
            a.pool  = nullptr;
            a.objID = INVALID_POOLED_OBJ_ID;
        }
        UniquePtr &operator=(UniquePtr &&a) {
            if (pool) pool->destroy(objID);
            new (this) UniquePtr(std::move(a));
            return *this;
        }
        ~UniquePtr() {
            if (pool) {
                pool->destroy(objID);
                pool  = nullptr;
                objID = INVALID_POOLED_OBJ_ID;
            }
        }
        bool operator==(UniquePtr const &a) const = default;

        std::pair<PooledObjID, ObjPool *> release() {
            std::pair<PooledObjID, ObjPool *> res{objID, pool};
            pool  = nullptr;
            objID = INVALID_POOLED_OBJ_ID;
        }
        operator bool() const { return pool && objID != INVALID_POOLED_OBJ_ID; }
        T *get() {
            if (pool && objID != INVALID_POOLED_OBJ_ID) return &pool->at(objID);
            return nullptr;
        }
        T *operator->() { return get(); }
        T &operator*() { return *get(); }
    };

    ~ObjPool() {
        // TODO: destroy all allocated objects
    }
    std::pair<PooledObjID, T *> alloc() {
        if (_freeList == INVALID_POOLED_OBJ_ID) {
            _storage.emplace_back();
            return std::pair<PooledObjID, T *>{PooledObjID(_storage.size() - 1), &_storage.back().asObj()};
        }
        auto res  = _freeList;
        _freeList = _storage[_freeList].asNextFreeID();
        return std::pair<PooledObjID, T *>{PooledObjID(res), &_storage[res].asObj()};
    }

    template<class... Args>
    std::pair<PooledObjID, T *> create(Args &&...args) {
        std::pair<PooledObjID, T *> obj = alloc();
        new (obj.second) T(std::forward<Args>(args)...);
        return obj;
    }
    template<class... Args>
    UniquePtr create_unique(Args &&...args) {
        return UniquePtr{this, create(std::forward<Args>(args)...).first};
    }
    UniquePtr get_unique(PooledObjID idx) {
        if (idx != INVALID_POOLED_OBJ_ID) return UniquePtr(this, idx);
        return UniquePtr{};
    }
    void destroy(PooledObjID idx) {
        assert(idx != INVALID_POOLED_OBJ_ID && idx < _storage.size());
        _storage[idx].asObj().~T();
        _storage[idx].asNextFreeID() = _freeList;
        _freeList                    = idx;
    }

    void reserve(size_t n) { _storage.reserve(n); }

    void dealloc(PooledObjID idx) {
        assert(idx != INVALID_POOLED_OBJ_ID && idx < _storage.size());
        _storage[idx].asNextFreeID() = _freeList;
        _freeList                    = idx;
    }

    T &at(PooledObjID idx) {
        assert(idx != INVALID_POOLED_OBJ_ID && idx < _storage.size());
        return _storage[idx].asObj();
    }
    /// @return true if the obj is in the pool and not free.
    bool is_allocated(PooledObjID idx) const { return idx >= 0 && idx < _storage.size() && !is_free(idx); }
    /// @return true if the obj is in the pool and free.
    bool is_free(PooledObjID idx) const {
        for (auto i = _freeList; i != INVALID_POOLED_OBJ_ID; i = _storage[i].asNextFreeID()) {
            if (i == idx) return true;
        }
        return false;
    }
    size_t count_free() const {
        size_t n = 0;
        for (auto i = _freeList; i != INVALID_POOLED_OBJ_ID; i = _storage[i].asNextFreeID()) ++n;
        return n;
    }
    size_t count_total() const { return _storage.size(); }
    size_t count_allocated() const { return count_total() - count_free(); }
};

template<class T>
concept LikePooledListNode = requires(T t) {
    { t._currObjID } -> std::convertible_to<PooledObjID>;
    { t._prevObjID } -> std::convertible_to<PooledObjID>;
    { t._nextObjID } -> std::convertible_to<PooledObjID>;
};

struct PooledIntrusiveListNode {
    explicit PooledIntrusiveListNode(PooledObjID objID = INVALID_POOLED_OBJ_ID) : _currObjID(objID) {}
    PooledIntrusiveListNode &operator=(const PooledIntrusiveListNode &) = default;


    // add not-in-list nextNodeInPool into listNode which is already in list.
    template<class T>
    static void add_next(ObjPool<T> &pool, LikePooledListNode auto &listNode, LikePooledListNode auto &nextNodeInPool) {
        assert(listNode._currObjID != INVALID_POOLED_OBJ_ID);
        assert(nextNodeInPool._currObjID != INVALID_POOLED_OBJ_ID);
        if (listNode._nextObjID != INVALID_POOLED_OBJ_ID) { pool.at(listNode._nextObjID)._prevObjID = nextNodeInPool._currObjID; }
        nextNodeInPool._nextObjID = listNode._nextObjID;
        nextNodeInPool._prevObjID = listNode._currObjID;
        listNode._nextObjID       = nextNodeInPool._currObjID;
    }

    template<class T>
    static void add_prev(ObjPool<T> &pool, LikePooledListNode auto &listNode, LikePooledListNode auto &prevNodeInPool) {
        assert(listNode._currObjID != INVALID_POOLED_OBJ_ID);
        assert(prevNodeInPool._currObjID != INVALID_POOLED_OBJ_ID);
        if (listNode._prevObjID != INVALID_POOLED_OBJ_ID) { pool.at(listNode._prevObjID)._nextObjID = prevNodeInPool._currObjID; }
        prevNodeInPool._nextObjID = listNode._currObjID;
        prevNodeInPool._prevObjID = listNode._prevObjID;
        listNode._prevObjID       = prevNodeInPool._currObjID;
    }

    // remove from list
    template<class T>
    static void remove_frome_list(ObjPool<T> &pool, LikePooledListNode auto &node) {
        assert(node._currObjID != INVALID_POOLED_OBJ_ID);
        if (node._nextObjID != INVALID_POOLED_OBJ_ID) { // has Next
            pool.at(node._nextObjID)._prevObjID = node._prevObjID;
        }
        if (node._prevObjID != INVALID_POOLED_OBJ_ID) { // has prev
            pool.at(node._prevObjID)._nextObjID = node._nextObjID;
        }
        node._prevObjID = node._nextObjID = INVALID_POOLED_OBJ_ID;
    }

    PooledObjID _currObjID = INVALID_POOLED_OBJ_ID;
    PooledObjID _prevObjID = INVALID_POOLED_OBJ_ID, _nextObjID = INVALID_POOLED_OBJ_ID;
};

template<LikePooledListNode T>
struct PooledIntrusiveList {
    struct iterator {
        PooledObjID          objID;
        PooledIntrusiveList *_owner = nullptr;

        using value_type = T;

        bool operator==(iterator const &a) const = default;
        bool operator!=(iterator const &a) const { return !operator==(a); }

        T &operator*() {
            assert(_owner);
            assert(objID != INVALID_POOLED_OBJ_ID);
            return _owner->getObj(objID);
        }
        T        *operator->() { return &operator*(); }
        iterator &operator++() {
            objID = _owner->nextObjID(objID);
            return *this;
        }
        iterator operator++(int) {
            iterator res = *this;
            operator++();
            return res;
        }
        iterator &operator--() {
            objID = _owner->prevObjID(objID);
            return *this;
        }
        iterator operator--(int) {
            iterator res = *this;
            operator--();
            return res;
        }
    };

    explicit PooledIntrusiveList(ObjPool<T> &pool) : _pool(pool) {}

    bool   empty() const { return _size == 0; }
    size_t size() const { return _size; }

    iterator begin() { return {_first, this}; }
    iterator end() { return {INVALID_POOLED_OBJ_ID, this}; }

    T &front() {
        assert(_size && _first != INVALID_POOLED_OBJ_ID);
        return _pool.at(_first);
    }
    T &back() {
        assert(_size && _last != INVALID_POOLED_OBJ_ID);
        return _pool.at(_last);
    }

    void erase(PooledObjID objID) {
        assert(objID != INVALID_POOLED_OBJ_ID);
        if (objID == INVALID_POOLED_OBJ_ID) return;
        T &obj = _pool.at(objID);
        if (_last == objID) { _last = obj._prevObjID; }
        if (_first == objID) { _first = obj._nextObjID; }
        PooledIntrusiveListNode::remove_frome_list(_pool, obj);
        obj.~T();
        _pool.dealloc(objID);
        --_size;
    }
    void erase(iterator pos) { erase(pos.objID); }
    void pop_front() {
        assert(!empty());
        erase(_first);
    }
    void pop_back() {
        assert(!empty());
        erase(_last);
    }

    template<class... Args>
    iterator emplace_front(Args &&...args) {
        auto [id, pObj]  = _pool.create(std::forward<Args>(args)...);
        pObj->_currObjID = id;
        if (empty()) {
            _first = _last = id;
        } else {
            PooledIntrusiveListNode::add_next(_pool, _pool.at(_first), *pObj);
            _first = id;
        }
        ++_size;
        return {id, this};
    }

    template<class... Args>
    iterator emplace_back(Args &&...args) {
        auto [id, pObj]  = _pool.create(std::forward<Args>(args)...);
        pObj->_currObjID = id;
        if (empty()) {
            _first = _last = id;
        } else {
            PooledIntrusiveListNode::add_next(_pool, _pool.at(_last), *pObj);
            _last = id;
        }
        ++_size;
        return {id, this};
    }

    template<class... Args>
    iterator emplace_at(iterator pos, Args &&...args) {
        if (pos.objID == _first) return emplace_front(std::forward<Args>(args)...);
        if (pos.objID == INVALID_POOLED_OBJ_ID) return emplace_back(std::forward<Args>(args)...);
        auto [id, pObj]  = _pool.create(std::forward<Args>(args)...);
        pObj->_currObjID = id;
        PooledIntrusiveListNode::add_prev(_pool, _pool.at(pos.objID), *pObj);
        ++_size;
        return {id, this};
    }

    T &getObj(PooledObjID objID) {
        assert(objID != INVALID_POOLED_OBJ_ID);
        return _pool.at(objID);
    }

    // if it's the end, return objID
    PooledObjID nextObjID(PooledObjID objID) const {
        if (objID == INVALID_POOLED_OBJ_ID) return objID;
        return _pool.at(objID)._nextObjID;
    }
    // if it's first, return the end. if it's the end, return last.
    PooledObjID prevObjID(PooledObjID objID) const {
        if (objID == INVALID_POOLED_OBJ_ID) return _last;
        return _pool.at(objID)._prevObjID;
    }

private:
    ObjPool<T> &_pool;
    PooledObjID _first{INVALID_POOLED_OBJ_ID}, _last{INVALID_POOLED_OBJ_ID};
    size_t      _size{0};
};