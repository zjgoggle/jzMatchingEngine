#ifndef TEST_CONFIG_IMPLEMENT_MAIN
#define TEST_CONFIG_IMPLEMENT_MAIN
#endif
#include "UnitTest.h"
#include <OrderBook.h>

#include <ObjPool.h>
#include <JzBinHeap.h>

#include <random>
#include <algorithm>


struct StockInfo : public PooledIntrusiveListNode {
    StockInfo(int aID) : id(aID) {}
    int id;
};

TEST_CASE("PooledIntrusiveList") {
    ObjPool<StockInfo>             pool;
    PooledIntrusiveList<StockInfo> alist(pool);

    auto it1 = alist.emplace_back(1);
    CHECK_EQ(it1->id, 1);
    CHECK_EQ(alist.size(), 1);

    auto it2 = alist.emplace_back(2);
    CHECK_EQ(it2->id, 2);
    CHECK_EQ(alist.size(), 2);

    CHECK_EQ(it1, alist.begin());
    CHECK_EQ(++it2, alist.end());

    ++it1, --it2, --it2; // it1 points to 2; it2 points 1;
    CHECK_EQ(it1->id, 2);
    CHECK_EQ(it2->id, 1);

    CHECK_EQ(pool.count_allocated(), 2); // head, tail, plus others
    CHECK_EQ(pool.count_free(), 0);

    alist.erase(it1); // erase 2 in [1, 2]
    CHECK(pool.is_free(it1.objID));
    CHECK(pool.is_allocated(it2.objID));
    CHECK_EQ(alist.size(), 1);
    CHECK_EQ(alist.front().id, 1);
    CHECK_EQ(alist.back().id, 1);

    alist.erase(it2); // it's empty now
    CHECK(pool.is_free(it2.objID));
    CHECK(alist.empty());
    CHECK_EQ(pool.count_free(), 2);
}

TEST_CASE("JzBinHeap") {
    std::vector<int> heap;
    int              num_items = 7;
    heap.reserve(num_items);

    std::random_device rd;
    std::mt19937       randgen(rd());

    // std::vector<int> input {1, 3, 6, 2, 4, 7, 5}; // {7, 6, 5, 4, 3, 2, 1};
    // for (int i = 0; i < num_items; ++i) {
    //     heap.push_back(input[i]);
    //     JzBinHeap::push_heap(heap.begin(), heap.end());
    //     ASSERT_TRUE(std::is_heap(heap.begin(), heap.end()));
    // }
    // std::vector<int> origin = input;

    std::vector<int> origin = {7, 6, 5, 4, 3, 2, 1};
    for (int i = 0; i < num_items * 10; ++i) {
        std::shuffle(origin.begin(), origin.end(), randgen);
        heap.clear();
        for (int j = 0; j < num_items; ++j) {
            heap.push_back(origin[j]);
            JzBinHeap::push_heap(heap.begin(), heap.end());
            CHECK(std::is_heap(heap.begin(), heap.end())); // << PrintVec{heap} << " Origin: " << PrintVec{origin};
        }
        std::vector<int> sorted;
        JzBinHeap::visit_heap(heap.begin(), heap.end(), [&](int val) { sorted.push_back(val); });
        CHECK(std::is_sorted(sorted.begin(), sorted.end(), std::greater<int>{})); // << PrintVec{heap} << " Origin: " << PrintVec{origin};
    }

    {
        std::vector<int> sorted;
        JzBinHeap::visit_heap(heap.begin(), heap.end(), [&](int val) { sorted.push_back(val); });
        CHECK(std::is_sorted(sorted.begin(), sorted.end(), std::greater<int>{})); // << PrintVec{heap} << " Origin: " << PrintVec{origin};
    }
    CHECK(std::is_heap(heap.begin(), heap.end()));
    CHECK_EQ(0, JzBinHeap::find_heap(heap.begin(), heap.size(), heap[0]));                 // << PrintVec{heap} << " Origin: " << PrintVec{origin};
    CHECK_EQ(1, JzBinHeap::find_heap(heap.begin(), heap.size(), heap[1]));                 //<< PrintVec{heap} << " Origin: " << PrintVec{origin};
    CHECK_EQ(2, JzBinHeap::find_heap(heap.begin(), heap.size(), heap[2]));                 // << PrintVec{heap} << " Origin: " << PrintVec{origin};
    CHECK_EQ(num_items - 1, JzBinHeap::find_heap(heap.begin(), heap.size(), heap.back())); // << PrintVec{heap} << "Origin: " << PrintVec{origin};
    CHECK_EQ(-1, JzBinHeap::find_heap(heap.begin(), heap.size(), -2));                     //<< PrintVec{heap} << " Origin: " << PrintVec{origin};
}

TEST_CASE("OrderBook-Match") {
    EventDetailPrinter            reporter;
    OrderBook<EventDetailPrinter> orderBook{reporter};
    orderBook.matchAddNewOrder(OrderID{1}, Side::Buy, Qty{100}, CentPrice{3000});
    orderBook.matchAddNewOrder(OrderID{2}, Side::Buy, Qty{200}, CentPrice{3000});
    orderBook.matchAddNewOrder(OrderID{3}, Side::Buy, Qty{300}, CentPrice{1000});

    CHECK_EQ(2, orderBook.countOrdersAtPrice(Side::Buy, CentPrice{3000}));
    CHECK_EQ(1, orderBook.countOrdersAtPrice(Side::Buy, CentPrice{1000}));
    CHECK_EQ(2, orderBook.countPriceLevels(Side::Buy));
    CHECK_EQ(3, orderBook.countOrders(Side::Buy));
    CHECK_EQ(0, orderBook.countOrdersAtPrice(Side::Sell, CentPrice{2000}));

    // Order 1: fully fill, 100; Order 2: partial fill, 100; Order 4: full fill, 200
    orderBook.matchAddNewOrder(OrderID{4}, Side::Sell, Qty{200}, CentPrice{2000});

    CHECK_EQ(2, orderBook.countPriceLevels(Side::Buy));
    CHECK_EQ(1, orderBook.countOrdersAtPrice(Side::Buy, CentPrice{3000}));
    CHECK_EQ(2, orderBook.countOrders(Side::Buy));
    CHECK_EQ(0, orderBook.countOrders(Side::Sell));

    // cancel order 2: 100.
    orderBook.cancelOrder(OrderID{2});
    CHECK_EQ(1, orderBook.countOrders(Side::Buy));

    // match Order 3: full fill, 300; Order 5, partial fill, 100
    orderBook.matchAddNewOrder(OrderID{5}, Side::Sell, Qty{400}, CentPrice{1000});

    CHECK_EQ(0, orderBook.countOrders(Side::Buy));
    CHECK_EQ(0, orderBook.countPriceLevels(Side::Buy));
    CHECK_EQ(1, orderBook.countOrders(Side::Sell));
    CHECK_EQ(1, orderBook.countPriceLevels(Side::Sell));
}