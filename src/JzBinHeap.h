#pragma once

#include <queue>


struct JzBinHeap {
    template<class VecIter, class Value, class LessThan>
    static void adjust_up_heap(VecIter itBegin, int len, int iHole, Value targetVal, LessThan &&lessThan) {
        for (int iParent = (iHole - 1) / 2; iHole > 0 && lessThan(*(itBegin + iParent), targetVal); iParent = (iHole - 1) / 2) {
            *(itBegin + iHole) = std::move(*(itBegin + iParent));
            iHole              = iParent;
        }
        *(itBegin + iHole) = std::move(targetVal);
    }

    template<class VecIter, class Value, class LessThan>
    static void adjust_down_heap(VecIter itBegin, int len, int iHole, Value targetVal, LessThan &&lessThan) {
        for (;;) {
            int  iLeft  = 2 * iHole + 1;
            auto itLeft = itBegin + iLeft;
            if (int iRight = iLeft + 1; iRight < len) {
                auto itMax = itLeft + (int)lessThan(*itLeft, *(itBegin + iRight));
                if (lessThan(targetVal, *itMax)) {
                    *(itBegin + iHole) = std::move(*itMax);
                    iHole              = itMax - itBegin;
                    continue;
                } // else max child is larger than target.
            } else if (iLeft < len) {
                if (lessThan(targetVal, *itLeft)) {
                    *(itBegin + iHole) = std::move(*itLeft);
                    iHole              = iLeft;
                    continue;
                } // else max child is larger than target.
            } // else no child
            break;
        }
        *(itBegin + iHole) = std::move(targetVal);
    }

    /// Assign *iHole = targetVal. Then adjust heap.
    /// Similar t std::pop_heap, but the itTarget could be any element in [itBegin, itEnd)
    template<class VecIter, class Value, class LessThan>
    static void adjust_heap(VecIter itBegin, int len, int iHole, Value targetVal, LessThan &&lessThan) {
        int iParent = (iHole - 1) / 2;
        if (iHole == 0 || !lessThan(*(itBegin + iParent), targetVal)) { // if no parent or parent >= target, move down iHole
            adjust_up_heap(itBegin, len, iHole, std::move(targetVal), std::forward<LessThan>(lessThan));
        } else { // has parent and less than target. move up iHole
            adjust_up_heap(itBegin, len, iHole, std::move(targetVal), std::forward<LessThan>(lessThan));
        }
    }

    // move the itTarget to end and make [itBegin, itEnd-1) still a heap.
    template<class VecIter, class LessThan>
    static void remove_heap(VecIter itBegin, size_t len, size_t iTarget, LessThan &&lessThan) {
        assert(iTarget < len);
        --len;
        auto val         = std::move(*(itBegin + len));
        *(itBegin + len) = std::move(*(itBegin + iTarget));
        adjust_heap(itBegin, len, iTarget, std::move(val), std::forward<LessThan>(lessThan));
    }

    template<class It>
    using PointeeT = typename std::iterator_traits<It>::value_type;

    template<class VecIter, class LessThan = std::less<PointeeT<VecIter>>>
    static void push_heap(VecIter itBegin, VecIter itEnd, LessThan &&lessThan = {}) {
        if (itBegin == itEnd) return;
        adjust_up_heap(itBegin, itEnd - itBegin, itEnd - itBegin - 1, std::move(*(itEnd - 1)), std::forward<LessThan>(lessThan));
    }

    template<class VecIter, class LessThan = std::less<PointeeT<VecIter>>>
    static void pop_heap(VecIter itBegin, VecIter itEnd, LessThan &&lessThan = {}) {
        if (itBegin == itEnd) return;
        auto lastval = std::move(*--itEnd);
        *itEnd       = std::move(*itBegin);
        adjust_down_heap(itBegin, itEnd - itBegin, 0, std::move(lastval), std::forward<LessThan>(lessThan));
    }

    /// visit heap from top. exit visiting when visitor(val) return false. if visitor returns void, visit all elements.
    template<class VecIter, class Visitor, class LessThan = std::less<PointeeT<VecIter>>>
    static void visit_heap(VecIter itBegin, VecIter itEnd, Visitor &&visitor, LessThan &&lessThan = {}) {
        using RetType = std::invoke_result_t<Visitor, PointeeT<VecIter>>;
        static_assert(std::is_convertible_v<RetType, bool> || std::is_same_v<RetType, void>, "void or convertible to bool");

        if (itBegin == itEnd) return;
        int  len       = itEnd - itBegin;
        auto compareAt = [&](int posX, int posY) { return lessThan(*(itBegin + posX), *(itBegin + posY)); };
        std::priority_queue<int, std::vector<int>, decltype(compareAt)> que(compareAt); // saves index to visit
        que.push(0);                                                                    // root
        while (!que.empty()) {
            int pos = que.top();
            que.pop();
            visitor(*(itBegin + pos));
            if (int iLeft = 2 * pos + 1; iLeft < len) {
                que.push(iLeft);
                if (int iRight = iLeft + 1; iRight < len) que.push(iRight);
            }
        }
    }

    /// find targetVal and return the index. if not found, return -1.
    template<class VecIter, class Value, class LessThan = std::less<PointeeT<VecIter>>>
    static int find_heap(VecIter itBegin, size_t len, Value targetVal, LessThan &&lessThan = {}) {
        if (len == 0) return -1;
        auto dfs = [&](auto &me, int pos) -> int {
            if (not lessThan(targetVal, *(itBegin + pos))) {               // find the first targetVal >= val.
                if (not lessThan(*(itBegin + pos), targetVal)) return pos; // return if val >= targetVal
                else return -1;                                            // if val > targatVal
            } // else val < targetVal, try children
            if (int iLeft = 2 * pos + 1; iLeft < len) {
                if (auto ret = me(me, iLeft); ret >= 0) return ret; // found on left
                else if (int iRight = iLeft + 1; iRight < len) return me(me, iRight);
            }
            return -1;
        };
        return dfs(dfs, 0);
    }
};
