#include <iostream>
#include <algorithm>
#include <sstream>
#include <functional>
#include <source_location>

#include "OrderBook.h"

enum class MsgType
{
    AddOrderRequest      = 0,
    CancelOrderRequest   = 1,
    TradeEvent           = 2,
    OrderFullyFilled     = 3,
    OrderPartiallyFilled = 4,
};

struct SimpleTradeReporter
{
    std::ostream &ostream = std::cout;

    void onTrade( const TradeMsg &msg )
    {
        ostream << "2," << msg.tradeQty << "," << double( msg.tradePrice / 100.0 ) << std::endl;
        printFill( msg.aggressiveOrderFill );
        printFill( msg.restingOrderFill );
    }
    std::ostream &printFill( const TradeMsg::Fill &fill )
    {
        if ( fill.isFull )
        {
            ostream << "3," << fill.orderID << std::endl;
        }
        else
        {
            ostream << "4," << fill.orderID << "," << fill.leaveQty << std::endl;
        }
        return ostream;
    }
};
static_assert( TradeReporter<SimpleTradeReporter>, "SimpleTradeReporter Impl TradeReporter" );

namespace StrUtil
{
void ltrim_str( std::string &s )
{
    s.erase( s.begin(), std::find_if( s.begin(), s.end(), []( char c ) { return !std::isspace( c ); } ) );
}
void rtrim_str( std::string &s )
{
    s.erase( std::find_if( s.rbegin(), s.rend(), []( char c ) { return !std::isspace( c ); } ).base(), s.end() );
}
void trim_str( std::string &s )
{
    ltrim_str( s );
    rtrim_str( s );
}

/// if onReadStr(index, std::string& str) returns void or bool, if return false, exit parsing.
/// Note that str could be empty.
int read_each_str( std::istream &istrm, char delim, std::invocable<int, std::string &> auto &&onReadStr )
{
    using RetType = decltype( onReadStr( std::declval<int>(), std::declval<std::string &>() ) );
    static_assert( std::is_convertible_v<RetType, bool> || std::is_same_v<RetType, void>, "onReadStr returns void or bool" );

    int         seq = 0;
    std::string line;
    while ( std::getline( istrm, line, delim ) )
    {
        trim_str( line );
        if constexpr ( std::is_convertible_v<RetType, bool> )
        {
            if ( not onReadStr( seq++, line ) )
            { // return false to exit
                break;
            }
        }
        else
        {
            onReadStr( seq++, line );
        }
    }
    return seq;
}

std::vector<std::string> split_str( const std::string &s, char delim )
{
    std::vector<std::string> res;
    std::stringstream        ss( s );
    read_each_str( ss, delim, [&]( int, std::string &word ) { res.push_back( std::move( word ) ); } );
    return res;
}
} // namespace StrUtil

int main_func()
{
    SimpleTradeReporter reporter;
    OrderBook           book{};
    StrUtil::read_each_str(
            std::cin,
            '\n',
            [&]( int iLine, std::string &line )
            {
                MsgType msgType;
                int     orderID;
                Side    side;
                int     qty;
                double  price;

                std::stringstream ss( line );
                bool              ok      = true;
                int               nFields = StrUtil::read_each_str(
                        ss,
                        ',',
                        [&]( int iField, std::string &field )
                        {
                            EXPECT_OR_ERR( !field.empty(),
                                           return ok = false,
                                           "ERROR: empty fieldNo: " << iField << " in lineNo: " << iLine << " : " << line );
                            char       *pEnd;
                            const char *pFieldEnd = field.c_str() + field.size();
                            if ( iField == 0 )
                            { // msgType
                                if ( field == std::to_string( int( MsgType::AddOrderRequest ) ) )
                                {
                                    msgType = MsgType::AddOrderRequest; //
                                }
                                else if ( field == std::to_string( int( MsgType::CancelOrderRequest ) ) )
                                {
                                    msgType = MsgType::CancelOrderRequest; //
                                }
                                else
                                {
                                    EXPECT_OR_ERR( false,
                                                   return ok = false,
                                                   "ERROR: invalid fieldNo: " << iField << ": " << field << " in lineNo: " << iLine << " : "
                                                                              << line );
                                }
                            }
                            else if ( iField == 1 )
                            {
                                orderID = std::strtol( field.c_str(), &pEnd, 10 );
                                EXPECT_OR_ERR(
                                        pEnd == pFieldEnd, return ok = false, "ERROR: field parse orderID in lineNo: " << iLine << " : " << line );
                            }
                            else
                            {
                                if ( msgType == MsgType::AddOrderRequest )
                                {
                                    if ( iField == 2 )
                                    {
                                        EXPECT_OR_ERR( field == std::to_string( int( Side::Buy ) ) || field == std::to_string( int( Side::Sell ) ),
                                                       return ok = false,
                                                       "ERROR: invalid side in lineNo: " << iLine << " : " << line );
                                        side = Side( std::strtol( field.c_str(), &pEnd, 10 ) );
                                    }
                                    else if ( iField == 3 )
                                    {
                                        qty = std::strtol( field.c_str(), &pEnd, 10 );
                                        EXPECT_OR_ERR( pEnd == pFieldEnd,
                                                       return ok = false,
                                                       "ERROR: field parse qty in lineNo: " << iLine << " : " << line );
                                    }
                                    else if ( iField == 4 )
                                    {
                                        price = std::strtod( field.c_str(), &pEnd );
                                        EXPECT_OR_ERR( pEnd == pFieldEnd,
                                                       return ok = false,
                                                       "ERROR: field parse price in lineNo: " << iLine << " : " << line );
                                    }
                                    else
                                    {
                                        EXPECT_OR_ERR( false,
                                                       return ok = false,
                                                       "ERROR: read AddOrderRequest(0) too many fieldNo: " << iField << " in lineNo: " << iLine
                                                                                                           << " : " << line );
                                    }
                                }
                                else
                                { // CancelOrderRequest
                                    EXPECT_OR_ERR( false,
                                                   return ok = false,
                                                   "ERROR: read CancelOrderRequest(1) too many fieldNo: " << iField << " in lineNo: " << iLine
                                                                                                          << " : " << line );
                                }
                            }
                            return true;
                        } );
                if ( !ok )
                    return; // ignore this line

                if ( msgType == MsgType::AddOrderRequest )
                {
                    EXPECT_OR_ERR( nFields == 5, return, "ERROR: need more fields for AddOrderRequest" );
                    book.matchAddNewOrder( orderID, side, qty, CentPrice( price * 100 ), reporter );
                }
                else
                {
                    EXPECT_OR_ERR( nFields == 2, return, "ERROR: need more fields for AddOrderRequest" );
                    book.cancelOrder( orderID );
                }
            } );
    return 0;
}

#ifndef TEST_CONFIG_IMPLEMENT_MAIN
int main()
{
    main_func();
}
#else //---- define TEST_CONFIG_IMPLEMENT_MAIN to build into a test program that doesn't read external input.
/// @return output of std::cout.
template<class Func>
std::string runWithRedirectedIO( std::string input, Func func )
{
    std::stringstream std_input, std_output;
    std::streambuf   *_oldCout = std::cout.rdbuf( std_output.rdbuf() ), *_oldCin = std::cin.rdbuf( std_input.rdbuf() );
    std_input << input;
    func();
    std::cout.rdbuf( _oldCout ), std::cin.rdbuf( _oldCin );
    std::string s = std_output.str();
    return s;
}

#define ASSERT_OP( a, OP, b )                                                                                                                       \
    std::invoke(                                                                                                                                    \
            []( auto twoValues )                                                                                                                    \
            {                                                                                                                                       \
                if ( auto &[_left, _right] = twoValues; !( _left OP _right ) )                                                                      \
                {                                                                                                                                   \
                    std::cerr << '\n' << __FILE__ << ":" << __LINE__ << ":\n    Failed assertion: " #a " " #OP " " #b;                              \
                    if constexpr ( requires( decltype( twoValues ) _ab ) { std::cerr << _ab.first << _ab.second; } )                                \
                        std::cerr << "\n         Left  value: " << _left << "\n         Right value: " << _right;                                   \
                    std::cerr << std::endl;                                                                                                         \
                    abort();                                                                                                                        \
                }                                                                                                                                   \
            },                                                                                                                                      \
            std::make_pair( ( a ), ( b ) ) )

#define ASSERT_EQ( a, b ) ASSERT_OP( a, ==, b )

auto test = []( std::string input, std::string expected, std::source_location loc = std::source_location::current() )
{
    std::cout << loc.file_name() << ":" << loc.line() << "  test started. " << std::endl;
    std::string s = runWithRedirectedIO( input, main_func );
    std::cout << loc.file_name() << ":" << loc.line() << "  test ended. " << std::endl;
    ASSERT_EQ( StrUtil::split_str( s, '\n' ), StrUtil::split_str( expected, '\n' ) );
};

int main()
{
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

    test( R"(0,1,0,100,30
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
3,3)" );
}
#endif
