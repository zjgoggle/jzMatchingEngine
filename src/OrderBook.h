#include <unordered_map>
#include <vector>
#include <array>
#include <concepts>
#include <list>
#include <algorithm>
#include <iostream>
#include <assert.h>
#include <stdint.h>


#define ASSERT_OP(a, OP, b)                                                                                                                         \
    std::invoke(                                                                                                                                    \
            [](auto twoValues) {                                                                                                                    \
                if (auto &[_left, _right] = twoValues; !(_left OP _right)) {                                                                        \
                    std::cerr << '\n' << __FILE__ << ":" << __LINE__ << ":\n    Failed assertion: " #a " " #OP " " #b;                              \
                    if constexpr (requires(decltype(twoValues) _ab) { std::cerr << _ab.first << _ab.second; })                                      \
                        std::cerr << "\n         Left  value: " << _left << "\n         Right value: " << _right;                                   \
                    std::cerr << std::endl;                                                                                                         \
                    abort();                                                                                                                        \
                }                                                                                                                                   \
            },                                                                                                                                      \
            std::make_pair((a), (b)))

#define ASSERT_EQ(a, b) ASSERT_OP(a, ==, b)
#define ASSERT_LT(a, b) ASSERT_OP(a, <, b)
#define ASSERT_TRUE(a) ASSERT_OP(a, ==, true)

using OrderID    = uint64_t;
using CentPrice  = int;
using FloatPrice = double;
using Qty        = int;

enum class Side {
    Buy,
    Sell,
};

enum class MsgType {
    AddOrderRequest      = 0,
    CancelOrderRequest   = 1,
    TradeEvent           = 2,
    OrderFullyFilled     = 3,
    OrderPartiallyFilled = 4,
    // more types
    PartialCancelRequest = 5,
    ReplaceOrderRequest  = 6,
};

enum class ErrCode {
    DuplicateOrderID,
    UnknownOrderID,
    QtyTooLarge,
    QtyTooSmall,
};

/// TradeMsg is used for TradeReporter to report a trade.
struct TradeMsg {
    struct Fill {
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
concept BookEventReporter = requires(T t, TradeMsg tradeMsg, OrderID orderID, MsgType msgType, ErrCode errCode, const std::string &errMsg) {
    { t.onTrade(tradeMsg) } -> std::same_as<void>;
    { t.onError(orderID, msgType, errCode, errMsg) } -> std::same_as<void>;
    { t.onLog(orderID, msgType, errMsg) } -> std::same_as<void>;
};

namespace internal {
/// @brief OrderInfo contains order info needed by order book.
struct OrderInfo {
    OrderID   orderID{};
    Qty       qty{};
    CentPrice price{};
};

using OrderList           = std::list<OrderInfo>;
using OrderListByPriceMap = std::unordered_map<CentPrice, OrderList>;

/// OrderKey identifies an order in a book.
struct OrderKey {
    Side                          side; // used for cancel request which doesn't have side info.
    OrderList::iterator           iterList;
    OrderListByPriceMap::iterator iterMap;
};

using OrderKeyByOrderIDMap = std::unordered_map<OrderID, internal::OrderKey>;

/// @brief PriceLevel used by min/max heap.
struct PriceLevel {
    CentPrice                     price;
    OrderListByPriceMap::iterator iterMap;
};

/// @brief SideBook maintains all orders by pricess for a side of an instrument.
class SideBook {
    OrderKeyByOrderIDMap             &_orderKeyByOrderIDMap; // shared OrderIDMap by buy&sell books of an instrument.
    Side                              _side;
    OrderListByPriceMap               _levelsByPriceMap;
    std::vector<internal::PriceLevel> _priceQue; // Buy(0): max heap; Sell(1): min heap.
    size_t                            _nOrders{0}, _nPriceLevels{0};

    bool (*compare_price)(const internal::PriceLevel &x, const internal::PriceLevel &y) = nullptr; // used by _priceQue
    bool (*can_match)(internal::PriceLevel thisPrice, CentPrice otherPrice)             = nullptr;

    static bool compare_price_buy(const internal::PriceLevel &x, const internal::PriceLevel &y) {
        return x.price < y.price; // Buy: max heap
    }
    static bool compare_price_sell(const internal::PriceLevel &x, const internal::PriceLevel &y) {
        return x.price > y.price; // Sell: min heap
    }
    static bool can_match_buy(internal::PriceLevel thisPrice, CentPrice otherPrice) {
        return thisPrice.price >= otherPrice; // buy >= sell
    }
    static bool can_match_sell(internal::PriceLevel thisPrice, CentPrice otherPrice) { return thisPrice.price <= otherPrice; }


public:
    SideBook(std::unordered_map<OrderID, OrderKey> &orderKeyByOrderIDMap, Side side, size_t reserveOrders, size_t reservePriceLevelsPerSide)
        : _orderKeyByOrderIDMap(orderKeyByOrderIDMap), _side(side) {
        if (side == Side::Buy) {
            compare_price = &compare_price_buy;
            can_match     = &can_match_buy;
        } else {
            compare_price = &compare_price_sell;
            can_match     = &can_match_sell;
        }
        _levelsByPriceMap.reserve(reservePriceLevelsPerSide);
        _priceQue.reserve(reservePriceLevelsPerSide);
    }

    /// Add order to book.
    void addNewOrder(OrderID orderID, Qty qty, CentPrice price) {
        //- use the hashmap to find the price level.
        auto [iterMap, inserted] = _levelsByPriceMap.try_emplace(price);
        if (inserted) // it's a new level. add to queue
        {
            _priceQue.push_back(PriceLevel{price, iterMap});
            std::push_heap(_priceQue.begin(), _priceQue.end(), *compare_price);
            ++_nPriceLevels;
        }
        //- add order to orderlist
        OrderList &orderList = iterMap->second;
        orderList.push_back(OrderInfo{.orderID = orderID, .qty = qty, .price = price});
        bool ok = _orderKeyByOrderIDMap.try_emplace(orderID, OrderKey{.side = _side, .iterList = --orderList.end(), .iterMap = iterMap}).second;
        assert(ok && "Logic Error: orderID has been checked before calling addNewOrder");
        ++_nOrders;
    }

    /// @return remaining qty after match
    Qty tryMatchOtherSide(OrderID orderID, Qty qty, CentPrice price, BookEventReporter auto &&tradeReporter) {
        while (qty && !_priceQue.empty() && (*can_match)(_priceQue.front(), price)) {
            if (_priceQue.front().iterMap->second.empty()) {
                removeTopEmptyPriceLevel();
            } else {
                internal::PriceLevel &thisLevel = _priceQue.front();
                internal::OrderList  &orderList = thisLevel.iterMap->second;
                internal::OrderInfo  &orderInfo = orderList.front();

                Qty matchQty = std::min(qty, orderInfo.qty);
                qty -= matchQty;
                orderInfo.qty -= matchQty;

                if (qty == 0) {
                    // aggressiveOrder fully filled.
                    if (orderInfo.qty == 0) {
                        // restingOrder fully filled
                        tradeReporter.onTrade(TradeMsg{.tradeQty            = matchQty,
                                                       .tradePrice          = thisLevel.price,
                                                       .aggressiveOrderFill = TradeMsg::Fill{.isFull = true, .orderID = orderID},
                                                       .restingOrderFill    = TradeMsg::Fill{.isFull = true, .orderID = orderInfo.orderID}});
                        removeOrderFromBookTop(orderList, orderInfo);
                    } else {
                        // restingOrder partially filled
                        tradeReporter.onTrade(TradeMsg{
                                .tradeQty            = matchQty,
                                .tradePrice          = thisLevel.price,
                                .aggressiveOrderFill = TradeMsg::Fill{.isFull = true, .orderID = orderID},
                                .restingOrderFill    = TradeMsg::Fill{.isFull = false, .orderID = orderInfo.orderID, .leaveQty = orderInfo.qty}});
                    }
                } else {
                    // aggressiveOrder partially fill, restingOrder fully fill
                    tradeReporter.onTrade(TradeMsg{.tradeQty            = matchQty,
                                                   .tradePrice          = thisLevel.price,
                                                   .aggressiveOrderFill = TradeMsg::Fill{.isFull = false, .orderID = orderID, .leaveQty = qty},
                                                   .restingOrderFill    = TradeMsg::Fill{.isFull = true, .orderID = orderInfo.orderID}});
                    removeOrderFromBookTop(orderList, orderInfo);
                }
            }
        }

        return qty;
    }

    void cancelOrder(OrderKeyByOrderIDMap::iterator iterKey) {
        OrderList &orderList = iterKey->second.iterMap->second;
        orderList.erase(iterKey->second.iterList);
        _orderKeyByOrderIDMap.erase(iterKey);
        --_nOrders;
        if (orderList.empty()) {
            --_nPriceLevels;
            while (!_priceQue.empty() && _priceQue.front().iterMap->second.empty()) { removeTopEmptyPriceLevel(); }
            // if it's not the top level, leave the empty level in book.
        }
    }

    size_t countOrders() const { return _nOrders; }
    size_t countPriceLevels() const { return _nPriceLevels; }
    /// PriceQueueSize >= PriceLevels. There may be empty price levels in queue.
    size_t getPriceQueueSize() const { return _priceQue.size(); }
    size_t countOrdersAtPrice(CentPrice price) const {
        if (auto it = _levelsByPriceMap.find(price); it != _levelsByPriceMap.end()) return it->second.size();
        return 0;
    }

private:
    void removeTopEmptyPriceLevel() {
        assert(!_priceQue.empty());
        _levelsByPriceMap.erase(_priceQue.front().iterMap);
        std::pop_heap(_priceQue.begin(), _priceQue.end(), *compare_price);
        _priceQue.pop_back();
    }

    void removeOrderFromBookTop(internal::OrderList &orderList, internal::OrderInfo &orderInfo) {
        _orderKeyByOrderIDMap.erase(orderInfo.orderID);
        orderList.pop_front();
        --_nOrders;
        if (orderList.empty()) {
            --_nPriceLevels;
            removeTopEmptyPriceLevel();
        }
    }
};
} // namespace internal


/// @brief OrderBook manages all orders for an instrument.
template<BookEventReporter BookEventReporterT>
class OrderBook {
    BookEventReporterT               &_eventReporter;
    internal::OrderKeyByOrderIDMap    _orderKeyByOrderIDMap; // elements are added/deleted in internal::Book.
    std::array<internal::SideBook, 2> _books;                // buy & sell books
public:
    explicit OrderBook(BookEventReporterT &reporter, size_t reserveOrders = 100000, size_t reservePriceLevelsPerSide = 1000)
        : _eventReporter(reporter),
          _books{internal::SideBook{_orderKeyByOrderIDMap, Side::Buy, reserveOrders, reservePriceLevelsPerSide},
                 internal::SideBook{_orderKeyByOrderIDMap, Side::Sell, reserveOrders, reservePriceLevelsPerSide}} {}

    /// try matching the new order. If there's remaining qty, add to order book.
    /// @param tradeReporter  reports trade events and executions if there are matches.
    /// @return false when duplicate orderID
    bool matchAddNewOrder(OrderID orderID, Side side, Qty qty, CentPrice price) {
        if (_orderKeyByOrderIDMap.contains(orderID)) {
            _eventReporter.onError(orderID, MsgType::AddOrderRequest, ErrCode::DuplicateOrderID, "");
            return false;
        }

        int otherSide = (int(side) + 1) % 2;

        qty = _books[otherSide].tryMatchOtherSide(orderID, qty, price, _eventReporter);
        if (qty) { // add to book if there are remainings
            _books[int(side)].addNewOrder(orderID, qty, price);
        }
        return true;
    }

    /// cancel a client order
    /// @return false when orderID is not found.
    bool cancelOrder(OrderID orderID) {
        if (auto it = _orderKeyByOrderIDMap.find(orderID); it != _orderKeyByOrderIDMap.end()) { // SideBook erases it.
            _books[int(it->second.side)].cancelOrder(it);
        } else {
            _eventReporter.onError(orderID, MsgType::CancelOrderRequest, ErrCode::UnknownOrderID, "");
            return false;
        }
        return true;
    }

    /// partial cancel (reduce qty and priority doesn't change).
    /// @return false if orderID is not found or cancelledQty > orderQty.
    /// @note if cancelledQty > orderQty, it's a cancelOrder
    bool partialCancelOrder(OrderID orderID, Qty cancelledQty) {
        if (auto it = _orderKeyByOrderIDMap.find(orderID); it != _orderKeyByOrderIDMap.end()) { // SideBook erases it.
            internal::OrderInfo &orderInfo = *it->second.iterList;
            if (orderInfo.qty < cancelledQty) {
                _eventReporter.onError(orderID, MsgType::PartialCancelRequest, ErrCode::QtyTooLarge, "");
                return false;
            }
            orderInfo.qty -= cancelledQty;
            if (orderInfo.qty <= 0) {
                _books[int(it->second.side)].cancelOrder(it); // cancel
            }
        } else {
            _eventReporter.onError(orderID, MsgType::PartialCancelRequest, ErrCode::UnknownOrderID, "");
            return false;
        }
        return true;
    }

    /// Replace order with new qty & price
    /// @return false if originalOrderID is not found or newOrderID is duplicate
    bool replaceOrder(OrderID originalOrderID, OrderID newOrderID, Qty qty, CentPrice price) {
        if (newOrderID == originalOrderID || _orderKeyByOrderIDMap.contains(newOrderID)) {
            _eventReporter.onError(
                    newOrderID, MsgType::ReplaceOrderRequest, ErrCode::DuplicateOrderID, "originalOrderID: " + std::to_string(originalOrderID));
            return false;
        }
        Side side;
        if (auto it = _orderKeyByOrderIDMap.find(originalOrderID); it != _orderKeyByOrderIDMap.end()) { // SideBook erases it.
            side = it->second.side;
        }
        if (!cancelOrder(originalOrderID)) return false;
        return matchAddNewOrder(newOrderID, side, qty, price);
    }

    size_t countOrders(Side side) const { return _books[int(side)].countOrders(); }
    size_t countPriceLevels(Side side) const { return _books[int(side)].countPriceLevels(); }
    size_t countOrdersAtPrice(Side side, CentPrice price) const { return _books[int(side)].countOrdersAtPrice(price); }
};

inline std::string msgTypeToStr(MsgType msgType) {
    switch (msgType) {
        case MsgType::AddOrderRequest: return "AddOrderRequest";
        case MsgType::CancelOrderRequest: return "CancelOrderRequest";
        case MsgType::PartialCancelRequest: return "PartialCancelRequest";
        case MsgType::OrderFullyFilled: return "OrderFullyFilled";
        case MsgType::OrderPartiallyFilled: return "OrderPartiallyFilled";
        case MsgType::TradeEvent: return "TradeEvent";
        default: return "UnkownMsgType";
    }
}

inline std::ostream &formatError(std::ostream &ostream, OrderID orderID, MsgType msgType, ErrCode errCode, const std::string &errMsg) {
    std::string errStr;
    switch (errCode) {
        case ErrCode::DuplicateOrderID: errStr = "DuplicateOrderID"; break;
        case ErrCode::UnknownOrderID: errStr = "UnknownOrderID"; break;
        case ErrCode::QtyTooLarge: errStr = "QtyTooLarge"; break;
        case ErrCode::QtyTooSmall: errStr = "QtyTooSmall"; break;
    }
    return ostream << "[Error] " << errStr << ", orderID: " << orderID << ", msgType: " << msgTypeToStr(msgType) << ". " << errMsg << std::endl;
}

struct EventDetailPrinter {
    std::ostream         &ostream = std::cout, &estream = std::cerr;
    int64_t               requestSeq = -1;
    std::vector<TradeMsg> lastTrades; // save trades for last aggressive order.

    void onTrade(const TradeMsg &msg) {
        if (!lastTrades.empty() && lastTrades.back().aggressiveOrderFill.orderID != msg.aggressiveOrderFill.orderID) {
            lastTrades.clear(); // it's a new aggressive order
        }
        lastTrades.push_back(msg);
        if (requestSeq >= 0) ostream << "requestSeq: " << requestSeq << ", ";
        ostream << "Trade qty: " << msg.tradeQty << ", price: " << double(msg.tradePrice / 100.0) << ", Aggressive ";
        printFill(msg.aggressiveOrderFill) << ", Resting ";
        printFill(msg.restingOrderFill) << std::endl;
    }
    void onError(OrderID orderID, MsgType msgType, ErrCode errCode, const std::string &errMsg) {
        if (errCode == ErrCode::UnknownOrderID) {
            // ignore due to dirty input
            return;
        }
        formatError(estream, orderID, msgType, errCode, errMsg);
    }
    void onLog(OrderID orderID, MsgType msgType, const std::string &msg) {
        ostream << "[Info] orderID: " << orderID << ", msgType: " << msgTypeToStr(msgType) << ". " << msg << std::endl;
    }
    std::ostream &printFill(const TradeMsg::Fill &fill) {
        if (fill.isFull) {
            ostream << "FullFill orderID: " << fill.orderID;
        } else {
            ostream << "PartFill orderID: " << fill.orderID << ", leaveQty: " << fill.leaveQty;
        }
        return ostream;
    }
};

struct NUllBookEventReporter {
    int64_t requestSeq = -1;
    void    onTrade(const TradeMsg &msg) {}
    void    onError(OrderID orderID, MsgType msgType, ErrCode errCode, const std::string &errMsg) {}
    void    onLog(OrderID orderID, MsgType msgType, const std::string &msg) {}
};

static_assert(BookEventReporter<NUllBookEventReporter>, "NUllBookEventReporter Impl BookEventReporter");
static_assert(BookEventReporter<EventDetailPrinter>, "EventDetailPrinter Impl BookEventReporter");
