#include <unordered_map>
#include <vector>
#include <array>
#include <concepts>
#include <list>
#include <assert.h>

#define EXPECT_OR_ERR( expr, err_func, err_msg )                                                                                                    \
    do                                                                                                                                              \
    {                                                                                                                                               \
        if ( not( expr ) )                                                                                                                          \
        {                                                                                                                                           \
            std::cerr << err_msg << std::endl;                                                                                                      \
            err_func;                                                                                                                               \
        }                                                                                                                                           \
    } while ( false )

enum class Side
{
    Buy,
    Sell,
};

using OrderID    = int;
using CentPrice  = int;
using FloatPrice = double;
using Qty        = int;

/// TradeMsg is used for TradeReporter to report a trade.
struct TradeMsg
{
    struct Fill
    {
        bool    isFull;
        OrderID orderID;
        Qty     leaveQty{}; // not used if isFull
    };

    Qty       tradeQty;
    CentPrice tradePrice;
    Fill      aggressiveOrderFill;
    Fill      restingOrderFill;
};

template<class T>
concept TradeReporter = requires( T t, TradeMsg tradeMsg ) {
    { t.onTrade( tradeMsg ) } -> std::same_as<void>;
};

namespace internal
{
/// @brief OrderInfo contains order info needed by order book.
struct OrderInfo
{
    OrderID   orderID{};
    Qty       qty{};
    CentPrice price{};
};

using OrderList           = std::list<OrderInfo>;
using OrderListByPriceMap = std::unordered_map<CentPrice, OrderList>;

/// OrderKey identifies an order in a book.
struct OrderKey
{
    Side                          side; // used for cancel request which doesn't have side info.
    OrderList::iterator           iterList;
    OrderListByPriceMap::iterator iterMap;
};

using OrderKeyByOrderIDMap = std::unordered_map<OrderID, internal::OrderKey>;

/// @brief PriceLevel used by min/max heap.
struct PriceLevel
{
    CentPrice                     price;
    OrderListByPriceMap::iterator iterMap;
};

/// @brief SideBook maintains all orders by pricess for a side of an instrument.
class SideBook
{
    OrderKeyByOrderIDMap             &_orderKeyByOrderIDMap; // shared OrderIDMap by buy&sell books of an instrument.
    Side                              _side;
    OrderListByPriceMap               _levelsByPriceMap;
    std::vector<internal::PriceLevel> _priceQue; // Buy(0): max heap; Sell(1): min heap.

    bool ( *compare_price )( const internal::PriceLevel &x, const internal::PriceLevel &y ) = nullptr; // used by _priceQue
    bool ( *can_match )( internal::PriceLevel thisPrice, CentPrice otherPrice )             = nullptr;

    static bool compare_price_buy( const internal::PriceLevel &x, const internal::PriceLevel &y )
    {
        return x.price < y.price; // Buy: max heap
    }
    static bool compare_price_sell( const internal::PriceLevel &x, const internal::PriceLevel &y )
    {
        return x.price > y.price; // Sell: min heap
    }
    static bool can_match_buy( internal::PriceLevel thisPrice, CentPrice otherPrice )
    {
        return thisPrice.price >= otherPrice; // buy >= sell
    }
    static bool can_match_sell( internal::PriceLevel thisPrice, CentPrice otherPrice )
    {
        return thisPrice.price <= otherPrice;
    }


public:
    SideBook( std::unordered_map<OrderID, OrderKey> &orderKeyByOrderIDMap, Side side, size_t reserveOrders, size_t reservePriceLevelsPerSide )
        : _orderKeyByOrderIDMap( orderKeyByOrderIDMap ), _side( side )
    {
        if ( side == Side::Buy )
        {
            compare_price = &compare_price_buy;
            can_match     = &can_match_buy;
        }
        else
        {
            compare_price = &compare_price_sell;
            can_match     = &can_match_sell;
        }
        _levelsByPriceMap.reserve( reservePriceLevelsPerSide );
        _priceQue.reserve( reservePriceLevelsPerSide );
    }

    /// Add order to book.
    void addNewOrder( OrderID orderID, Qty qty, CentPrice price )
    {
        //- use the hashmap to find the price level.
        auto [iterMap, inserted] = _levelsByPriceMap.try_emplace( price );
        if ( inserted ) // it's a new level. add to queue
        {
            _priceQue.push_back( PriceLevel{ price, iterMap } );
            std::push_heap( _priceQue.begin(), _priceQue.end(), *compare_price );
        }
        //- add order to orderlist
        OrderList &orderList = iterMap->second;
        orderList.push_back( OrderInfo{ .orderID = orderID, .qty = qty, .price = price } );
        bool ok = _orderKeyByOrderIDMap.try_emplace( orderID, OrderKey{ .side = _side, .iterList = --orderList.end(), .iterMap = iterMap } ).second;
        assert( ok && "Logic Error: orderID has been checked before calling addNewOrder" );
    }

    /// @return remaining qty after match
    Qty tryMatchOtherSide( OrderID orderID, Qty qty, CentPrice price, TradeReporter auto &&tradeReporter )
    {
        while ( qty && !_priceQue.empty() && ( *can_match )( _priceQue.front(), price ) )
        {
            if ( _priceQue.front().iterMap->second.empty() )
            {
                removeTopEmptyPriceLevel();
            }
            else
            {
                internal::PriceLevel &thisLevel = _priceQue.front();
                internal::OrderList  &orderList = thisLevel.iterMap->second;
                internal::OrderInfo  &orderInfo = orderList.front();

                Qty matchQty = std::min( qty, orderInfo.qty );
                qty -= matchQty;
                orderInfo.qty -= matchQty;

                if ( qty == 0 ) // aggressiveOrder fully filled.
                {
                    if ( orderInfo.qty == 0 ) // restingOrder fully filled
                    {
                        tradeReporter.onTrade( TradeMsg{ .tradeQty            = matchQty,
                                                         .tradePrice          = thisLevel.price,
                                                         .aggressiveOrderFill = TradeMsg::Fill{ .isFull = true, .orderID = orderID },
                                                         .restingOrderFill    = TradeMsg::Fill{ .isFull = true, .orderID = orderInfo.orderID } } );
                        removeOrderFromBookTop( orderList, orderInfo );
                    }
                    else // restingOrder partially filled
                    {
                        tradeReporter.onTrade( TradeMsg{
                                .tradeQty            = matchQty,
                                .tradePrice          = thisLevel.price,
                                .aggressiveOrderFill = TradeMsg::Fill{ .isFull = true, .orderID = orderID },
                                .restingOrderFill = TradeMsg::Fill{ .isFull = false, .orderID = orderInfo.orderID, .leaveQty = orderInfo.qty } } );
                    }
                }
                else // aggressiveOrder partially fill, restingOrder fully fill
                {
                    tradeReporter.onTrade( TradeMsg{ .tradeQty            = matchQty,
                                                     .tradePrice          = thisLevel.price,
                                                     .aggressiveOrderFill = TradeMsg::Fill{ .isFull = false, .orderID = orderID, .leaveQty = qty },
                                                     .restingOrderFill    = TradeMsg::Fill{ .isFull = true, .orderID = orderInfo.orderID } } );
                    removeOrderFromBookTop( orderList, orderInfo );
                }
            }
        }

        return qty;
    }

    void cancelOrder( OrderKeyByOrderIDMap::iterator iterKey )
    {
        OrderList &orderList = iterKey->second.iterMap->second;
        orderList.erase( iterKey->second.iterList );
        _orderKeyByOrderIDMap.erase( iterKey );
        if ( orderList.empty() )
        {
            while ( !_priceQue.empty() && _priceQue.front().iterMap->second.empty() )
            {
                removeTopEmptyPriceLevel();
            }
            // if it's not the top level, leave the empty level in book.
        }
    }

private:
    void removeTopEmptyPriceLevel()
    {
        assert( !_priceQue.empty() );
        _levelsByPriceMap.erase( _priceQue.front().iterMap );
        std::pop_heap( _priceQue.begin(), _priceQue.end(), *compare_price );
        _priceQue.pop_back();
    }

    void removeOrderFromBookTop( internal::OrderList &orderList, internal::OrderInfo &orderInfo )
    {
        _orderKeyByOrderIDMap.erase( orderInfo.orderID );
        orderList.pop_front();
        if ( orderList.empty() )
        {
            removeTopEmptyPriceLevel();
        }
    }
};
} // namespace internal


/// @brief OrderBook manages all orders for an instrument.
class OrderBook
{
    internal::OrderKeyByOrderIDMap    _orderKeyByOrderIDMap; // elements are added/deleted in internal::Book.
    std::array<internal::SideBook, 2> _books;                // buy & sell books

public:
    explicit OrderBook( size_t reserveOrders = 100000, size_t reservePriceLevelsPerSide = 1000 )
        : _books{ internal::SideBook{ _orderKeyByOrderIDMap, Side::Buy, reserveOrders, reservePriceLevelsPerSide },
                  internal::SideBook{ _orderKeyByOrderIDMap, Side::Sell, reserveOrders, reservePriceLevelsPerSide } }
    {
    }

    /// try matching the new order. If there's remaining qty, add to order book.
    /// @param tradeReporter  reports trade events and executions if there are matches.
    /// @return false when duplicate orderID
    void matchAddNewOrder( OrderID orderID, Side side, Qty qty, CentPrice price, TradeReporter auto &&tradeReporter )
    {
        EXPECT_OR_ERR( !_orderKeyByOrderIDMap.contains( orderID ), return, "ERROR. Failed to add duplicate orderID: " << orderID );

        int otherSide = ( int( side ) + 1 ) % 2;

        qty = _books[otherSide].tryMatchOtherSide( orderID, qty, price, tradeReporter );
        if ( qty )
        { // add to book if there are remainings
            _books[int( side )].addNewOrder( orderID, qty, price );
        }
    }

    /// cancel a client order
    void cancelOrder( OrderID orderID )
    {
        if ( auto it = _orderKeyByOrderIDMap.find( orderID ); it != _orderKeyByOrderIDMap.end() )
        { // SideBook erases it.
            _books[int( it->second.side )].cancelOrder( it );
        }
        else
        {
            EXPECT_OR_ERR( false, return, "ERROR. Failed to cancel unknown orderID: " << orderID );
        }
    }
};

struct TradeDetailPrinter
{
    std::ostream &ostream = std::cout;

    void onTrade( const TradeMsg &msg )
    {
        ostream << "Trade qty: " << msg.tradeQty << ", price: " << double( msg.tradePrice / 100.0 ) << ", Aggressive ";
        printFill( msg.aggressiveOrderFill ) << ", Resting ";
        printFill( msg.restingOrderFill ) << std::endl;
    }
    std::ostream &printFill( const TradeMsg::Fill &fill )
    {
        if ( fill.isFull )
        {
            ostream << "FullFill orderID: " << fill.orderID;
        }
        else
        {
            ostream << "PartFill orderID: " << fill.orderID << ", leaveQty: " << fill.leaveQty;
        }
        return ostream;
    }
};
static_assert( TradeReporter<TradeDetailPrinter>, "TradeDetailPrinter Impl TradeReporter" );
