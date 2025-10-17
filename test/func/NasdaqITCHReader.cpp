#include "OrderBook.h"
#include "NasdaqITCHReader.h"
#include <chrono>

#define JZPROFILER_DEFAULT_SAMPLES 100000
#include "JzProfiler.h"

template<BookEventReporter TradeReporterT = EventDetailPrinter> // NUllBookEventReporter> // EventDetailPrinter>
struct OrderBookAndReporter {
    TradeReporterT            tradeReporter;
    OrderBook<TradeReporterT> book{tradeReporter};
    OrderBookAndReporter() : book(tradeReporter) {}
};

template<size_t subsecondDigits = 6, bool bUTCTime = false>
const char *print_time(char *buffer = nullptr, timespec ts = {-1, -1}, const char *timeFmt = "%Y%m%d-%T") {
    static const unsigned BUFFERSIZE = 32;
    static char           localbuf[BUFFERSIZE];
    static const char    *subsecFmt[3]    = {".%03ld", ".%06ld", ".%09ld"};
    static const long     subsecFactor[3] = {1000000LL, 1000L, 1L};
    static_assert(subsecondDigits == 0 || subsecondDigits == 3 || subsecondDigits == 6 || subsecondDigits == 9);

    char *buf = buffer ? buffer : localbuf;
    if (ts.tv_nsec == -1) timespec_get(&ts, TIME_UTC); //clock_gettime(CLOCK_REALTIME, &ts);
    time_t timet = ts.tv_sec;
    tm     atime;
#if defined(_WIN32) || defined(_WINDOWS_)
    bUTCTime ? gmtime_s(&atime, &timet) : localtime_s(&atime, &timet);
#else
    bUTCTime ? gmtime_r(&timet, &atime) : localtime_r(&timet, &atime);
#endif
    size_t nbytes = strftime(buf, BUFFERSIZE, timeFmt, &atime); // YYYYMMDD-HH:MM:SS
    if (subsecondDigits > 0 && subsecondDigits < 10) {
        unsigned idx = subsecondDigits / 3 - 1;
        int      ret = snprintf(buf + nbytes, BUFFERSIZE - nbytes, subsecFmt[idx], long(ts.tv_nsec / subsecFactor[idx]));
        if (ret > 0) nbytes += ret;
    }
    buf[nbytes] = '\0';
    return buf;
}

static JzProfiler *profBookAddOrder       = JZ_PROF_GLOBAL(BookAddOrder);
static JzProfiler *profBookCancel         = JZ_PROF_GLOBAL(BookCancel);
static JzProfiler *profBookPartialCancel  = JZ_PROF_GLOBAL(BookPartialCancel);
static JzProfiler *profBookReplace        = JZ_PROF_GLOBAL(BookReplace);
static JzProfiler *profBookCancelExecuted = JZ_PROF_GLOBAL(BookCancelExecuted);

// -1 for all
constexpr int64_t INTERESTED_STOCK = 5336; // MU
constexpr int64_t MAX_SEQ_NUM      = -1;   //262173;
// supply order requeest to order book and print order events
void run_with_order_book(const std::string filename, int64_t insterestedStock = INTERESTED_STOCK, int64_t maxSeqNum = MAX_SEQ_NUM) {
    using SymbolID = int32_t;
    std::unordered_map<SymbolID, OrderBookAndReporter<>> bookMap;

    int64_t       lastRequestSeq = -1;
    static size_t count          = 0;
    int64_t       timeStart      = std::chrono::system_clock().now().time_since_epoch().count();
    read_nasdaq_itch(filename, [&]<typename T>(size_t seqnum, T &msg) {
        if (insterestedStock >= 0 && insterestedStock != msg.StockLocate.value) return;
        auto &reporter = bookMap[msg.StockLocate.value].tradeReporter;
        auto &book     = bookMap[msg.StockLocate.value].book;
        if (++count % 100000 == 0) {
            auto topBuy = book.getTopPriceAndOrders(Side::Buy), topSell = book.getTopPriceAndOrders(Side::Sell);
            std::cout << print_time() << " " << count << " ** " << seqnum << ", Buy-nOrders: " << book.countOrders(Side::Buy)
                      << ", nPrices: " << book.countPriceLevels(Side::Buy) << ", QueSize: " << book.getPriceQueueSize(Side::Buy)
                      << ", TopPrice: " << topBuy.first << ", TopOrders: " << topBuy.second << " -- Sell-nOrders: " << book.countOrders(Side::Sell)
                      << ", nPrices: " << book.countPriceLevels(Side::Sell) << ", QueSize: " << book.getPriceQueueSize(Side::Sell)
                      << ", TopPrice: " << topBuy.first << ", TopOrders: " << topBuy.second << std::endl;
            // printMsg(msg);
        }
        if constexpr (std::is_same_v<T, NasdaqITCH::AddOrder> || std::is_same_v<T, NasdaqITCH::AddOrderWithoutMPID>) {
            profBookAddOrder->startRecord();
            if (101395 == msg.OrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            bool ok        = book.matchAddNewOrder(
                    msg.OrderReferenceNumber.value, msg.Side == 'B' ? Side::Buy : Side::Sell, msg.Shares.value, msg.Price.value);
            ASSERT_TRUE(ok);
            profBookAddOrder->stopRecord();
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderPartialCancel>) {
            profBookPartialCancel->startRecord();
            if (101395 == msg.OrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            bool ok        = book.partialCancelOrder(msg.OrderReferenceNumber.value, msg.CancelledShares.value);
            ASSERT_TRUE(ok);
            profBookPartialCancel->stopRecord();
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderDelete>) {
            profBookCancel->startRecord();
            if (101395 == msg.OrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            bool ok        = book.cancelOrder(msg.OrderReferenceNumber.value);
            ASSERT_TRUE(ok); // fail for OrderReferenceNumber==101395 because the order was executed in nasdaq
            profBookCancel->stopRecord();
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderReplace>) {
            profBookReplace->startRecord();
            if (101395 == msg.NewOrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            auto &book     = bookMap[msg.StockLocate.value].book;
            bool  ok       = book.replaceOrder(msg.OrderReferenceNumber.value, msg.NewOrderReferenceNumber.value, msg.Shares.value, msg.Price.value);
            ASSERT_TRUE(ok);
            profBookReplace->stopRecord();
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderExecutedWithoutPrice> || std::is_same_v<T, NasdaqITCH::OrderExecuted>) {
            profBookCancelExecuted->startRecord();
            // check if execution was also generated by OrderBook. If not (caused by dirty data), mannually remove qty.
            bool hasBookTrade = false;
            auto orderID      = msg.OrderReferenceNumber.value;
            for (TradeMsg const &trade : bookMap[msg.StockLocate.value].tradeReporter.lastTrades) {
                if (orderID == trade.aggressiveOrderFill.orderID || orderID == trade.restingOrderFill.orderID) {
                    ASSERT_EQ(trade.tradeQty, msg.Executedshares.value);
                    hasBookTrade = true;
                    break;
                }
            }
            if (!hasBookTrade) { // it's dirty data. manually remove from order book.
                // std::cerr << "[Warn] Failed to find execution in orderbook. partial cancel it. ";
                // printMsg(msg);
                bool ok = book.partialCancelOrder(orderID, msg.Executedshares.value);
                ASSERT_TRUE(ok);
            }
            profBookCancelExecuted->stopRecord();
        } else if constexpr (std::is_same_v<T, NasdaqITCH::Trade> || std::is_same_v<T, NasdaqITCH::CrossTrade>) {
            // std::cout << "** Original trade seqnum: " << seqnum << ", lastRequestSeq: " << lastRequestSeq << ". ";
            // printMsg(msg);
        }
        if (maxSeqNum >= 0 && maxSeqNum <= seqnum) { exit(0); }
    });
    int64_t durationNanos = timeStart - std::chrono::system_clock().now().time_since_epoch().count();
    std::cout << print_time() << " " << count << " End. Nanos/msg: " << durationNanos / count << std::endl;
}

void run_dump(const std::string filename, int64_t insterestedStock = -1, int64_t maxSeqNum = -1) {
    read_nasdaq_itch(filename, [&]<typename T>(size_t seqnum, T &msg) {
        if (insterestedStock >= 0 && insterestedStock != msg.StockLocate.value) return;
        std::cout << seqnum << ", ";
        printMsg(msg);
        if (maxSeqNum >= 0 && maxSeqNum <= seqnum) { exit(0); }
    });
}

int main_func(int argc, const char *argv[]) {
    auto usage = [&](std::string errstr) {
        if (!errstr.empty()) { std::cerr << "Arg Error: " << errstr << std::endl; }
        std::cerr << "Usage:\n"
                  << argv[0] << R"( -f <Path_to_Nasdaq_ITCH_File> [--orderbook|--dump] [--oid orderID]"
    <Path_to_Nasdaq_ITCH_File> E.g. https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/01302020.NASDAQ_ITCH50.gz
    --orderbook              Supply requests to orderbook and generate trades. 
     --dump                   Default. Dump all order requests and trades from the file.
    <orderID>                  Interested orderID. -1 for all.
    --help|-h
 )" << std::endl;
        exit(!errstr.empty());
    };

    bool        bOrderBook        = false;
    int64_t     interestedOrderID = -1;
    std::string dataFile;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::string("-f")) {
            dataFile = argv[++i];
        } else if (argv[i] == std::string("--orderbook")) {
            bOrderBook = true;
        } else if (argv[i] == std::string("--dump")) {
            bOrderBook = false;
        } else if (argv[i] == std::string("--oid")) {
            interestedOrderID = std::strtoll(argv[++i], nullptr, 10);
        } else if (argv[i] == std::string("--help") || argv[i] == std::string("--h")) {
            usage("");
        } else {
            usage("Unknown arg: " + std::string(argv[i]));
        }
    }
    if (dataFile.empty()) { usage("No data file specified."); }
    if (bOrderBook) {
        run_with_order_book(dataFile, interestedOrderID); //
    } else {
        run_dump(dataFile, interestedOrderID); //
    }
    return 0;
}

#ifndef TEST_CONFIG_IMPLEMENT_MAIN
int main(int argc, const char *argv[]) { return main_func(argc, argv); }
#else  //---- define TEST_CONFIG_IMPLEMENT_MAIN to build into a test program that doesn't read external input.

int main() {
    // 5336: MU , 13: APPL
    char const *args[] = {"test", "-f", "../nasdaq-data/data/01302020.NASDAQ_ITCH50", "--orderbook", "--oid", "13"};
    main_func(sizeof(args) / sizeof(char *), args);
}
#endif // TEST_CONFIG_IMPLEMENT_MAIN
