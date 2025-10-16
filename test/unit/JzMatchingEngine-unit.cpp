#ifndef TEST_CONFIG_IMPLEMENT_MAIN
#define TEST_CONFIG_IMPLEMENT_MAIN
#endif
#include "UnitTest.h"
#include <OrderBook.h>

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