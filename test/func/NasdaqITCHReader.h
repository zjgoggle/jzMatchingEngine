#pragma once

#include <iostream>
#include <fstream>
#include <string.h>
#include <assert.h>

#include <boost/pfr.hpp>

// https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf
// MsgHeader { int16_t bodyLen; char msgType };


inline void reverse_bytes(char *d, size_t n) {
    for (auto i = 0U; i < n / 2; ++i) std::swap(d[i], d[n - i - 1]);
}

enum class Subtype {
    Price,
    Timestamp,
};

/// SizedInt read/write N bytes integer
// factorToFloat: convert to float = value / factorToFloat
template<size_t N, bool reverseBytes = true, size_t factorToFloat = 1, int subtype = -1>
struct SizedInt {
    static_assert(N <= 8, "Support upto 64 bytes int only");
    using RoundupInt = std::conditional_t<N <= 4, std::conditional_t<N <= 2, std::conditional_t<N <= 1, int8_t, int16_t>, int32_t>, int64_t>;
    static constexpr size_t     MAX_SIZE        = N;
    static constexpr RoundupInt MAX_VAL         = (1 << (8 * N - 1)) - 1;
    static constexpr size_t     FACTOR_TO_FLOAT = factorToFloat;
    static constexpr int        SUBTYPE         = subtype;

    RoundupInt value;

    void setOriginalFloat(double originalFloat) {
        value = originalFloat * factorToFloat; // trunc: MAYDO: to nearest int
    }

    double toOriginalFloat() const {
        return value / (1.0 * factorToFloat); //
    }

    // read from buffer and return number of bytes read
    size_t read(const char *buf) {
        value = *(const RoundupInt *)(buf);
        if constexpr (reverseBytes) { reverse_bytes((char *)&value, sizeof(RoundupInt)); }
        if constexpr (sizeof(RoundupInt) != N) {
            using Unsigned = std::make_unsigned_t<RoundupInt>; // unsigned to set 0 for highest bits
            value          = Unsigned(value) >> (8 * (sizeof(RoundupInt) - N));
        }
        return N;
    }
};

template<size_t N, char nullChar = ' ', int subtype = -1>
struct SizedStr {
    static constexpr size_t MAX_SIZE = N;
    static constexpr int    SUBTYPE  = subtype;

    std::string value;

    // read from buffer and return number of bytes read
    size_t read(const char *buf) {
        // MAYDO: support align to right
        const char *p = (const char *)memchr(buf, nullChar, MAX_SIZE);
        if (!p) value = std::string_view{buf, MAX_SIZE};
        else value = std::string_view{buf, size_t(p - buf)};
        return N;
    }
};

template<size_t N, char nullChar, int subtype>
std::ostream &operator<<(std::ostream &os, SizedStr<N, nullChar, subtype> const &val) {
    return os << '\"' << val.value << '\"';
}

using ScaledPrice = SizedInt<4, true, 10000, (int)Subtype::Price>;
using TTimestamp  = SizedInt<6, true, 1, (int)Subtype::Timestamp>;

// len=18 format: HH:MM:SS.sssssssss
inline std::string formatTimestamp(int64_t nanosSinceMidnight) {
    std::string res;
    res.resize(18);
    char *buf  = (char *)res.data();
    int   secs = nanosSinceMidnight / 1000000000LL, nanos = nanosSinceMidnight % 1000000000LL;
    sprintf(buf, "%02d:", secs / 3600);
    secs %= 3600;
    sprintf(buf + 3, "%02d:", secs / 60);
    secs %= 60;
    sprintf(buf + 6, "%02d.", secs);
    sprintf(buf + 9, "%09d", nanos);
    return res;
}

template<size_t N, bool reverseBytes, size_t factorToFloat, int subtype>
std::ostream &operator<<(std::ostream &os, SizedInt<N, reverseBytes, factorToFloat, subtype> const &val) {
    if constexpr (subtype == (int)Subtype::Timestamp) {
        return os << formatTimestamp(val.value);
    } else if constexpr (factorToFloat == 1) {
        return os << val.value;
    } else {
        return os << val.toOriginalFloat();
    }
}

//------------------------------------------------------------------
//       Order Messages
//------------------------------------------------------------------
namespace NasdaqITCH {
enum class MsgType : char {
    SystemEvent = 'S',
    // Stock
    StockDirectory                = 'R',
    StockTradingAction            = 'H',
    RegSHORestricted              = 'Y',
    MarketParticipantPosition     = 'L',
    MWCBDeclineLevel              = 'V',
    MWCBStatus                    = 'W',
    QuotingPeriodUpdate           = 'K',
    LimitUpLimitDownAuctionCollar = 'J',
    OperationalHalt               = 'h',
    // Order
    AddOrderWithoutMPID       = 'A',
    AddOrder                  = 'F',
    OrderExecutedWithoutPrice = 'E',
    OrderExecuted             = 'C',
    OrderPartialCancel        = 'X',
    OrderDelete               = 'D',
    OrderReplace              = 'U',
    // Trade
    Trade       = 'P',
    CrossTrade  = 'Q',
    BrokenTrade = 'B',
    //
    NetOrderImbalanceIndicator      = 'I',
    RetailPriceImprovementIndicator = 'N',
    DirectListingWithCapitalRaise   = 'O',
};

// pfr doens't support inheritation, so we use macro
// COMMON_ORDER_FIELDS size: 19
#define COMMON_ORDER_FIELDS()                                                                                                                       \
    char        MessageType;                                                                                                                        \
    SizedInt<2> StockLocate;                                                                                                                        \
    SizedInt<2> TrackingNumber;                                                                                                                     \
    TTimestamp  Timestamp;                                                                                                                          \
    SizedInt<8> OrderReferenceNumber;

struct AddOrderWithoutMPID {
    static constexpr char NAME[]   = "AddOrderWithoutMPID";
    static constexpr char MSG_TYPE = (char)MsgType::AddOrderWithoutMPID; // 'A';
    COMMON_ORDER_FIELDS()

    char        Side; // 'B': buy, 'S': sell
    SizedInt<4> Shares;
    SizedStr<8> Stock; // symbol
    ScaledPrice Price; // price.getOriginalFloat() to dollar
};

struct AddOrder {
    static constexpr char NAME[]   = "AddOrder";
    static constexpr char MSG_TYPE = (char)MsgType::AddOrder; // 'F';
    COMMON_ORDER_FIELDS()

    char        Side; // 'B': buy, 'S': sell
    SizedInt<4> Shares;
    SizedStr<8> Stock; // symbol
    ScaledPrice Price; // price.getOriginalFloat() to dollar

    SizedStr<4> Attribution; // MPID
};

struct OrderExecutedWithoutPrice {
    static constexpr char NAME[]   = "OrderExecutedWithoutPrice";
    static constexpr char MSG_TYPE = (char)MsgType::OrderExecutedWithoutPrice; // 'E';
    COMMON_ORDER_FIELDS()
    SizedInt<4> Executedshares;
    SizedInt<8> MatchNumber;
};

struct OrderExecuted {
    static constexpr char NAME[]   = "OrderExecuted";
    static constexpr char MSG_TYPE = (char)MsgType::OrderExecuted; // 'C';
    COMMON_ORDER_FIELDS()
    SizedInt<4> Executedshares;
    SizedInt<8> MatchNumber;

    char        Printable; // Y, N
    ScaledPrice ExecutionPrice;
};

struct OrderPartialCancel {
    static constexpr char NAME[]   = "OrderPartialCancel";
    static constexpr char MSG_TYPE = (char)MsgType::OrderPartialCancel; // 'X';
    COMMON_ORDER_FIELDS()
    SizedInt<4> CancelledShares;
};

struct OrderDelete {
    static constexpr char NAME[]   = "OrderDelete";
    static constexpr char MSG_TYPE = (char)MsgType::OrderDelete; // 'D';
    COMMON_ORDER_FIELDS()
};

struct OrderReplace {
    static constexpr char NAME[]   = "OrderReplace";
    static constexpr char MSG_TYPE = (char)MsgType::OrderReplace; // 'U';
    COMMON_ORDER_FIELDS()
    SizedInt<8> NewOrderReferenceNumber;
    SizedInt<4> Shares;
    ScaledPrice Price; // price.getOriginalFloat() to dollar
};

//------------------------------------------------------------------
//       Trade Messages
//------------------------------------------------------------------

struct Trade {
    static constexpr char NAME[]   = "Trade";
    static constexpr char MSG_TYPE = (char)MsgType::Trade; // 'P';
    COMMON_ORDER_FIELDS()
    char        Side; // always B
    SizedInt<4> Shares;
    SizedStr<8> Stock; // symbol
    ScaledPrice Price; // price.getOriginalFloat() to dollar
    SizedInt<8> MatchNumber;
};

struct CrossTrade {
    static constexpr char NAME[]   = "CrossTrade";
    static constexpr char MSG_TYPE = (char)MsgType::CrossTrade; // 'Q';
    char                  MessageType;
    SizedInt<2>           StockLocate; // symbolID
    SizedInt<2>           TrackingNumber;
    TTimestamp            Timestamp;

    SizedInt<4> Shares;
    SizedStr<8> Stock; // symbol
    ScaledPrice Price; // price.getOriginalFloat() to dollar
    SizedInt<8> MatchNumber;
};

struct BrokenTrade {
    static constexpr char NAME[]   = "BrokenTrade";
    static constexpr char MSG_TYPE = (char)MsgType::BrokenTrade; // 'Q';
    char                  MessageType;
    SizedInt<2>           StockLocate; // symbolID
    SizedInt<2>           TrackingNumber;
    TTimestamp            Timestamp;
    SizedInt<8>           MatchNumber;
};

//------------------------------------------------------------------
//       printMsg readMsg
//------------------------------------------------------------------

template<class Msg>
void printMsg(const Msg &msg, std::ostream &os = std::cout) {
    os << "Msg: " << Msg::NAME;
    boost::pfr::for_each_field_with_name(msg, [&]<typename T>(std::string_view name, const T &field) {
        os << ", " << name << ": " << field; //
    });
    os << std::endl;
}

template<class Msg>
size_t serializedMsgSize(const Msg &msg) {
    size_t nbytes = 0;
    boost::pfr::for_each_field_with_name(msg, [&]<typename T>(std::string_view name, const T &field) {
        if constexpr (std::is_same_v<T, char>) {
            ++nbytes;
        } else {
            nbytes += T::MAX_SIZE;
        }
    });
    return nbytes;
}

/// @return bytes read (== serializedMsgSize(msg)).
template<class Msg>
size_t readMsg(Msg &msg, const char *buf, size_t buflen) {
    size_t offset = 0;
    boost::pfr::for_each_field_with_name(msg, [&]<typename T>(std::string_view name, T &field) {
        if constexpr (std::is_same_v<T, char>) {
            field = buf[offset++];
        } else {
            field.read(buf + offset);
            offset += T::MAX_SIZE;
        }
    });
    return offset;
}

} // namespace NasdaqITCH

// call onMessage(size_t seqnum, Msg&) for each message.
template<class OnMessage>
void read_nasdaq_itch(const std::string &filename, OnMessage &&onMessage) {
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        exit(1);
    }
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    constexpr size_t MAX_BODY_LEN = 256;
    char             buf[MAX_BODY_LEN];
    size_t           offset = 0;
    for (size_t nMsg = 0; file.read(buf, 2); ++nMsg) {
        offset += 2;
        SizedInt<2> bodyLen; // packet body len == msgLen
        bodyLen.read(buf);
        assert(bodyLen.value < MAX_BODY_LEN);
        if (!file.read(buf, bodyLen.value)) {
            std::cerr << "Failed to read msg body len: " << bodyLen.value << std::endl;
            exit(1);
        }
        offset += bodyLen.value;

        auto readAndPrintMsg = [&]<typename Msg>(Msg msg) {
            ASSERT_EQ(bodyLen.value, readMsg(msg, buf, bodyLen.value));
            onMessage(nMsg, msg);
        };
        switch (NasdaqITCH::MsgType(buf[0])) {
            case NasdaqITCH::MsgType::SystemEvent:
            // Stock
            case NasdaqITCH::MsgType::StockDirectory:
            case NasdaqITCH::MsgType::StockTradingAction:
            case NasdaqITCH::MsgType::RegSHORestricted:
            case NasdaqITCH::MsgType::MarketParticipantPosition:
            case NasdaqITCH::MsgType::MWCBDeclineLevel:
            case NasdaqITCH::MsgType::MWCBStatus:
            case NasdaqITCH::MsgType::QuotingPeriodUpdate:
            case NasdaqITCH::MsgType::LimitUpLimitDownAuctionCollar:
            case NasdaqITCH::MsgType::OperationalHalt: {
                break;
            }
            // Order
            case NasdaqITCH::MsgType::AddOrderWithoutMPID: {
                readAndPrintMsg(NasdaqITCH::AddOrderWithoutMPID{});
                break;
            }
            case NasdaqITCH::MsgType::AddOrder: {
                readAndPrintMsg(NasdaqITCH::AddOrder{});
                break;
            }
            case NasdaqITCH::MsgType::OrderPartialCancel: {
                readAndPrintMsg(NasdaqITCH::OrderPartialCancel{});
                break;
            }
            case NasdaqITCH::MsgType::OrderDelete: {
                readAndPrintMsg(NasdaqITCH::OrderDelete{});
                break;
            }
            case NasdaqITCH::MsgType::OrderReplace: {
                readAndPrintMsg(NasdaqITCH::OrderReplace{});
                break;
            }
            case NasdaqITCH::MsgType::OrderExecutedWithoutPrice: {
                readAndPrintMsg(NasdaqITCH::OrderExecutedWithoutPrice{});
                break;
            }
            case NasdaqITCH::MsgType::OrderExecuted: {
                readAndPrintMsg(NasdaqITCH::OrderExecuted{});
                break;
            }
            // Trade
            case NasdaqITCH::MsgType::Trade: {
                readAndPrintMsg(NasdaqITCH::Trade{});
                break;
            }
            case NasdaqITCH::MsgType::CrossTrade: {
                readAndPrintMsg(NasdaqITCH::CrossTrade{});
                break;
            }
            case NasdaqITCH::MsgType::BrokenTrade: {
                readAndPrintMsg(NasdaqITCH::BrokenTrade{});
                break;
            }
            //
            case NasdaqITCH::MsgType::NetOrderImbalanceIndicator:
            case NasdaqITCH::MsgType::RetailPriceImprovementIndicator:
            case NasdaqITCH::MsgType::DirectListingWithCapitalRaise: //
                break;
            default: //
                std::cerr << "Undefined msg type: " << buf[0] << std::endl;
                exit(1);
        }
    }
}