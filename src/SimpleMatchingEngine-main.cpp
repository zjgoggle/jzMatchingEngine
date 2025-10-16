#include <iostream>
#include <algorithm>
#include <sstream>
#include <functional>
#include <source_location>

#include "OrderBook.h"

#define EXPECT_OR_ERR(expr, err_func, err_msg)                                                                                                      \
    do {                                                                                                                                            \
        if (not(expr)) {                                                                                                                            \
            std::cerr << err_msg << std::endl;                                                                                                      \
            err_func;                                                                                                                               \
        }                                                                                                                                           \
    } while (false)


struct SimpleTradeReporter {
    std::ostream &ostream = std::cout, &estream = std::cerr;

    void onTrade(const TradeMsg &msg) {
        ostream << "2," << msg.tradeQty << "," << double(msg.tradePrice / 100.0) << std::endl;
        printFill(msg.aggressiveOrderFill);
        printFill(msg.restingOrderFill);
    }
    void onError(OrderID orderID, MsgType msgType, ErrCode errCode, const std::string &errMsg) {
        formatError(estream, orderID, msgType, errCode, errMsg);
    }

private:
    std::ostream &printFill(const TradeMsg::Fill &fill) {
        if (fill.isFull) {
            ostream << "3," << fill.orderID << std::endl;
        } else {
            ostream << "4," << fill.orderID << "," << fill.leaveQty << std::endl;
        }
        return ostream;
    }
};
static_assert(BookEventReporter<SimpleTradeReporter>, "SimpleTradeReporter Impl BookEventReporter");

namespace StrUtil {
void ltrim_str(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c) { return !std::isspace(c); }));
}
void rtrim_str(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](char c) { return !std::isspace(c); }).base(), s.end());
}
void trim_str(std::string &s) {
    ltrim_str(s);
    rtrim_str(s);
}

/// if onReadStr(index, std::string& str) returns void or bool, if return false, exit parsing.
/// Note that str could be empty.
int read_each_str(std::istream &istrm, char delim, std::invocable<int, std::string &> auto &&onReadStr) {
    using RetType = decltype(onReadStr(std::declval<int>(), std::declval<std::string &>()));
    static_assert(std::is_convertible_v<RetType, bool> || std::is_same_v<RetType, void>, "onReadStr returns void or bool");

    int         seq = 0;
    std::string line;
    while (std::getline(istrm, line, delim)) {
        trim_str(line);
        if constexpr (std::is_convertible_v<RetType, bool>) {
            if (not onReadStr(seq++, line)) { // return false to exit
                break;
            }
        } else {
            onReadStr(seq++, line);
        }
    }
    return seq;
}

std::vector<std::string> split_str(const std::string &s, char delim) {
    std::vector<std::string> res;
    std::stringstream        ss(s);
    read_each_str(ss, delim, [&](int, std::string &word) { res.push_back(std::move(word)); });
    return res;
}
} // namespace StrUtil

int main_func() {
    SimpleTradeReporter            reporter;
    OrderBook<SimpleTradeReporter> book{reporter};
    StrUtil::read_each_str(std::cin, '\n', [&](int iLine, std::string &line) {
        MsgType msgType;
        int     orderID;
        Side    side;
        int     qty;
        double  price;

        std::stringstream ss(line);
        bool              ok      = true;
        int               nFields = StrUtil::read_each_str(ss, ',', [&](int iField, std::string &field) {
            EXPECT_OR_ERR(!field.empty(), return ok = false, "ERROR: empty fieldNo: " << iField << " in lineNo: " << iLine << " : " << line);
            char       *pEnd;
            const char *pFieldEnd = field.c_str() + field.size();
            if (iField == 0) { // msgType
                if (field == std::to_string(int(MsgType::AddOrderRequest))) {
                    msgType = MsgType::AddOrderRequest; //
                } else if (field == std::to_string(int(MsgType::CancelOrderRequest))) {
                    msgType = MsgType::CancelOrderRequest; //
                } else {
                    EXPECT_OR_ERR(false, return ok = false, "ERROR: invalid MsgType: " << field << " in lineNo: " << iLine << " : " << line);
                }
            } else if (iField == 1) {
                orderID = std::strtol(field.c_str(), &pEnd, 10);
                EXPECT_OR_ERR(pEnd == pFieldEnd, return ok = false, "ERROR: field parse orderID in lineNo: " << iLine << " : " << line);
            } else {
                if (msgType == MsgType::AddOrderRequest) {
                    if (iField == 2) {
                        EXPECT_OR_ERR(field == std::to_string(int(Side::Buy)) || field == std::to_string(int(Side::Sell)),
                                      return ok = false,
                                      "ERROR: invalid side in lineNo: " << iLine << " : " << line);
                        side = Side(std::strtol(field.c_str(), &pEnd, 10));
                    } else if (iField == 3) {
                        qty = std::strtol(field.c_str(), &pEnd, 10);
                        EXPECT_OR_ERR(pEnd == pFieldEnd, return ok = false, "ERROR: field parse qty in lineNo: " << iLine << " : " << line);
                    } else if (iField == 4) {
                        price = std::strtod(field.c_str(), &pEnd);
                        EXPECT_OR_ERR(pEnd == pFieldEnd, return ok = false, "ERROR: field parse price in lineNo: " << iLine << " : " << line);
                    } else {
                        EXPECT_OR_ERR(false,
                                      return ok = false,
                                      "ERROR: read AddOrderRequest(0) too many fieldNo: " << iField << " in lineNo: " << iLine << " : " << line);
                    }
                } else { // CancelOrderRequest
                    EXPECT_OR_ERR(false,
                                  return ok = false,
                                  "ERROR: read CancelOrderRequest(1) too many fieldNo: " << iField << " in lineNo: " << iLine << " : " << line);
                }
            }
            return true;
        });
        if (!ok) return; // ignore this line

        if (msgType == MsgType::AddOrderRequest) {
            EXPECT_OR_ERR(nFields == 5, return, "ERROR: need more fields for AddOrderRequest");
            book.matchAddNewOrder(orderID, side, qty, CentPrice(price * 100));
        } else {
            EXPECT_OR_ERR(nFields == 2, return, "ERROR: need more fields for CancelOrderRequest");
            book.cancelOrder(orderID);
        }
    });
    return 0;
}

#ifndef TEST_CONFIG_IMPLEMENT_MAIN
int main() { main_func(); }
#else  //---- define TEST_CONFIG_IMPLEMENT_MAIN to build into a test program that doesn't read external input.
/// @return output of std::cout.
template<class Func>
std::string runWithRedirectedIO(std::string input, Func func) {
    std::stringstream std_input, std_output;
    std::streambuf   *_oldCout = std::cout.rdbuf(std_output.rdbuf()), *_oldCin = std::cin.rdbuf(std_input.rdbuf());
    std_input << input;
    func();
    std::cout.rdbuf(_oldCout), std::cin.rdbuf(_oldCin);
    std::string s = std_output.str();
    return s;
}

auto test = [](std::string input, std::string expected, std::source_location loc = std::source_location::current()) {
    std::cout << loc.file_name() << ":" << loc.line() << "  test started. " << std::endl;
    std::string s = runWithRedirectedIO(input, main_func);
    std::cout << loc.file_name() << ":" << loc.line() << "  test ended. " << std::endl;
    ASSERT_EQ(StrUtil::split_str(s, '\n'), StrUtil::split_str(expected, '\n'));
};

int main() {
    // NewOrder, OrderID{1}, Side::Buy, Qty{100}, CentPrice{3000}
    // NewOrder, OrderID{2}, Side::Buy, Qty{200}, CentPrice{3000}
    // NewOrder, OrderID{3}, Side::Buy, Qty{300}, CentPrice{1000}

    // NewOrder, OrderID{4}, Side::Sell, Qty{200}, CentPrice{2000}
    //-- Trade, Qty{100}, Price{30}
    //-- PartFill, OrderID{4}, LeaveQty{100}
    //-- FullFill, OrderID{1}
    //-- Trade, Qty{100}, Price{30}
    //-- FullFill, OrderID{4}
    //-- PartFill, OrderID{2}, LeaveQty{100}

    // CancelOrder, OrderID{2}

    // NewOrder, OrderID{5}, Side::Sell, Qty{400}, CentPrice{1000}
    //-- Trade, Qty{300}, Price{10}
    //-- PartFill, OrderID{5}, LeaveQty{100}
    //-- FullFill, OrderID{3}

    test(R"(0,1,0,100,30
0,2,0,200,30
0,3,0,300,10
0,4,1,200,20
1,2
0,5,1,400,10)",
         R"(2,100,30
4,4,100
3,1
2,100,30
3,4
4,2,100
2,300,10
4,5,100
3,3)");

    /*
Input:
0,1000000,1,1,1075
0,1000001,0,9,1000
0,1000002,0,30,975
0,1000003,1,10,1050
0,1000004,0,10,950
BADMESSAGE // An erroneous input
0,1000005,1,2,1025
0,1000006,0,1,1000
1,1000004 // remove order
0,1000007,1,5,1025 // Original standing order book from Details
0,1000008,0,3,1050 // Matches! Triggers trades

Output:
2,2,1025 // Trade for 2 at 1025
4,1000008,1 // order partially filled, remaining open quantity 1
3,1000005 // order fully filled
2,1,1025 // Trade for 1 at 1025
3,1000008 // order fully filled
4,1000007,4 // order quantity reduced for partial fill
*/
    test(R"(0,1000000,1,1,1075
0,1000001,0,9,1000
0,1000002,0,30,975
0,1000003,1,10,1050
0,1000004,0,10,950
BADMESSAGE
0,1000005,1,2,1025
0,1000006,0,1,1000
1,1000004
0,1000007,1,5,1025
0,1000008,0,3,1050
)",
         R"(2,2,1025
4,1000008,1
3,1000005
2,1,1025
3,1000008
4,1000007,4)");
}
#endif // TEST_CONFIG_IMPLEMENT_MAIN
