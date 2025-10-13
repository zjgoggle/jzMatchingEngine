#ifndef TEST_CONFIG_IMPLEMENT_MAIN
#define TEST_CONFIG_IMPLEMENT_MAIN
#endif
#include "UnitTest.h"
#include "../src/OrderBook.h"

TEST_CASE( "OrderBook" )
{
    OrderBook          orderBook{};
    TradeDetailPrinter tradeReporter;
    orderBook.matchAddNewOrder( OrderID{ 1 }, Side::Buy, Qty{ 100 }, CentPrice{ 3000 }, tradeReporter );
    orderBook.matchAddNewOrder( OrderID{ 2 }, Side::Buy, Qty{ 200 }, CentPrice{ 3000 }, tradeReporter );
    orderBook.matchAddNewOrder( OrderID{ 3 }, Side::Buy, Qty{ 300 }, CentPrice{ 1000 }, tradeReporter );

    // Order 1: fully fill, 100; Order 2: partial fill, 100; Order 4: full fill, 200
    orderBook.matchAddNewOrder( OrderID{ 4 }, Side::Sell, Qty{ 200 }, CentPrice{ 2000 }, tradeReporter );

    // cancel order 2: 100.
    orderBook.cancelOrder( OrderID{ 2 } );

    // match Order 3: full fill, 300; Order 5, partial fill, 100
    orderBook.matchAddNewOrder( OrderID{ 5 }, Side::Sell, Qty{ 400 }, CentPrice{ 1000 }, tradeReporter );
}