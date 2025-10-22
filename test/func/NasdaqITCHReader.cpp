#include "OrderBook.h"
#include "NasdaqITCHReader.h"
#include <chrono>

#define JZPROFILER_DEFAULT_SAMPLES 500000
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

struct MsgStats {
    JzProfiler *prof      = nullptr;
    int64_t     countMsgs = 0, maxDurationTicks = 0;

    float maxDruationNanos() const { return maxDurationTicks / prof->_params.ticksPerNano; }
};

static MsgStats statsAddOrder{.prof = JZ_PROF_GLOBAL(BookAddOrder)};
static MsgStats statsCancel{.prof = JZ_PROF_GLOBAL(BookCancel)};
static MsgStats statsPartialCancel{.prof = JZ_PROF_GLOBAL(BookPartialCancel)};
static MsgStats statsReplace{.prof = JZ_PROF_GLOBAL(BookReplace)};
static MsgStats statsCancelExected{.prof = JZ_PROF_GLOBAL(BookCancelExecuted)};

// -1 for all
constexpr int64_t INTERESTED_STOCK = 5336; // MU
// supply order requeest to order book and print order events
void run_with_order_book(const std::string filename, int64_t insterestedStock = INTERESTED_STOCK, int64_t maxtMsgs = -1) {
    using SymbolID = int32_t;
    std::unordered_map<SymbolID, OrderBookAndReporter<>> bookMap;

    int64_t       lastRequestSeq = -1;
    static size_t count          = 0;
    int64_t       timeStart      = getSteadyNanos();
    read_nasdaq_itch(filename, [&]<typename T>(size_t seqnum, T &msg, std::string_view) {
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
            // std::cout << ‘NasdaqITCH::PrintMsg’{msg} << std::endl;
        }
        if constexpr (std::is_same_v<T, NasdaqITCH::AddOrder> || std::is_same_v<T, NasdaqITCH::AddOrderWithoutMPID>) {
            ++statsAddOrder.countMsgs;
            constexpr int64_t insteretest_seqnum = -1; // 18023725;
            statsAddOrder.prof->startRecord();
            if (101395 == msg.OrderReferenceNumber.value) {
                // ‘NasdaqITCH::PrintMsg’(msg); //
            }
            if (insteretest_seqnum == seqnum) {
                std::cout << " -- Found it: " << NasdaqITCH::PrintMsg{msg} << std::endl; //
            }
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            bool ok        = book.matchAddNewOrder(
                    msg.OrderReferenceNumber.value, msg.Side == 'B' ? Side::Buy : Side::Sell, msg.Shares.value, msg.Price.value);
            ASSERT_TRUE(ok);
            auto dur = statsAddOrder.prof->stopRecord();
            if (seqnum == insteretest_seqnum) { //16632450) {
                std::cout << print_time() << " " << statsAddOrder.countMsgs << " ** " << seqnum
                          << ", --- Found interested seqnum NewOrderDuration(nanos): " << dur / statsAddOrder.prof->_params.ticksPerNano
                          << std::endl;
                // exit(0);
            }
            if (dur > statsAddOrder.maxDurationTicks && statsAddOrder.countMsgs > 100) {
                statsAddOrder.maxDurationTicks = dur; //
                std::cout << print_time() << " " << statsAddOrder.countMsgs << " ** " << seqnum
                          << ", update maxNewOrderDuration (nanos): " << statsAddOrder.maxDruationNanos() << std::endl;
            }
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderPartialCancel>) {
            ++statsPartialCancel.countMsgs;
            statsPartialCancel.prof->startRecord();
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            bool ok        = book.partialCancelOrder(msg.OrderReferenceNumber.value, msg.CancelledShares.value);
            ASSERT_TRUE(ok);
            statsPartialCancel.prof->stopRecord();
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderDelete>) {
            ++statsCancel.countMsgs;
            statsCancel.prof->startRecord();
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            bool ok        = book.cancelOrder(msg.OrderReferenceNumber.value);
            ASSERT_TRUE(ok); // fail for OrderReferenceNumber==101395 because the order was executed in nasdaq
            if (auto dur = statsCancel.prof->stopRecord(); dur > statsCancel.maxDurationTicks && statsAddOrder.countMsgs > 100) {
                statsCancel.maxDurationTicks = dur; //
                std::cout << print_time() << " " << statsCancel.countMsgs << " ** " << seqnum
                          << ", update statsCancel (nanos): " << statsCancel.maxDruationNanos() << std::endl;
            }
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderReplace>) {
            ++statsReplace.countMsgs;
            statsReplace.prof->startRecord();
            reporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            auto &book     = bookMap[msg.StockLocate.value].book;
            bool  ok       = book.replaceOrder(msg.OrderReferenceNumber.value, msg.NewOrderReferenceNumber.value, msg.Shares.value, msg.Price.value);
            ASSERT_TRUE(ok);
            statsReplace.prof->stopRecord();
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderExecutedWithoutPrice> || std::is_same_v<T, NasdaqITCH::OrderExecuted>) {
            ++statsCancelExected.countMsgs;
            statsCancelExected.prof->startRecord();
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
            // Nasdaq only publishes resting orders (in-book orders) and executions. Traded aggressive orders are not in data file.
            // So we manually remove from order book when receiving exectuion msg.
            if (!hasBookTrade) {
                // std::cerr << "[Warn] Failed to find execution in orderbook. partial cancel it. ";
                bool ok = book.partialCancelOrder(orderID, msg.Executedshares.value);
                ASSERT_TRUE(ok);
            }
            if (auto dur = statsCancelExected.prof->stopRecord(); dur > statsCancelExected.maxDurationTicks && statsAddOrder.countMsgs > 100) {
                statsCancelExected.maxDurationTicks = dur; //
                std::cout << print_time() << " " << statsCancelExected.countMsgs << " ** " << seqnum
                          << ", update statsCancelExected (nanos): " << statsCancelExected.maxDruationNanos() << std::endl;
            }
        } else if constexpr (std::is_same_v<T, NasdaqITCH::Trade> || std::is_same_v<T, NasdaqITCH::CrossTrade>) {
            // std::cout << "** Original trade seqnum: " << seqnum << ", lastRequestSeq: " << lastRequestSeq << ". ";
        }
        if (maxtMsgs >= 0 && maxtMsgs <= count) {
            std::cout << print_time() << " reached maxtMsgs: " << maxtMsgs << std::endl;
            exit(0);
        }
    });
    int64_t durationNanos = timeStart - getSteadyNanos();
    std::cout << print_time() << " " << count << " End. Nanos/msg: " << durationNanos / count << std::endl;
}

void run_print(const std::string filename, int64_t insterestedStock = -1, int64_t maxtMsgs = -1) {
    int64_t count = 0;
    read_nasdaq_itch(filename, [&]<typename T>(size_t seqnum, T &msg, std::string_view) {
        if (insterestedStock >= 0 && insterestedStock != msg.StockLocate.value) return;
        ++count;
        std::cout << count << ", seqnum: " << seqnum << ", " << NasdaqITCH::PrintMsg{msg} << std::endl;
        if (maxtMsgs >= 0 && maxtMsgs <= count) { exit(0); }
    });
}

void run_chop(const std::string filename, const std::string &outfilename, int64_t insterestedStock = -1, int64_t maxtMsgs = -1) {
    std::ofstream outfile(outfilename.c_str(), std::ios::out | std::ios::binary);
    if (!outfile.is_open()) {
        std::cout << "ERROR: failed to open out file: " << outfilename << std::endl;
        exit(1);
    }
    int64_t count = 0, nNewOrder = 0, nReplace = 0, nPartialCancel = 0, nCancel = 0, nExecution = 0, nTrade = 0;
    auto    printSumary = [&]() -> std::ostream    &{
        return std::cout << print_time() << " wrote msgs: " << count << ", nNewOrder: " << nNewOrder << ", nReplace: " << nReplace
                         << ", nPartialCancel: " << nPartialCancel << ", nCancel: " << nCancel << ", nExecution: " << nExecution;
    };
    auto writeMsg = [&](size_t seqnum, std::string_view buf) {
        outfile.write(buf.data(), buf.size());
        ++count;
        if (maxtMsgs >= 0 && maxtMsgs <= count) {
            outfile.flush();
            printSumary() << "\n\tSuccessfully wrote " << count << " messages to file: " << outfilename << std::endl;
            exit(0);
        } else if (count % 500000 == 0) {
            printSumary() << ", seqnum: " << seqnum << std::endl;
        }
    };
    read_nasdaq_itch(filename, [&]<typename T>(size_t seqnum, T &msg, std::string_view buf) {
        if (insterestedStock >= 0 && insterestedStock != msg.StockLocate.value) return;
        switch (NasdaqITCH::MsgType(T::MSG_TYPE)) {
            case NasdaqITCH::MsgType::AddOrderWithoutMPID:
            case NasdaqITCH::MsgType::AddOrder:
                ++nNewOrder;
                writeMsg(seqnum, buf);
                break;
            case NasdaqITCH::MsgType::OrderPartialCancel:
                ++nPartialCancel;
                writeMsg(seqnum, buf);
                break;
            case NasdaqITCH::MsgType::OrderDelete:
                ++nCancel;
                writeMsg(seqnum, buf);
                break;
            case NasdaqITCH::MsgType::OrderReplace:
                ++nReplace;
                writeMsg(seqnum, buf);
                break;
            case NasdaqITCH::MsgType::OrderExecutedWithoutPrice:
            case NasdaqITCH::MsgType::OrderExecuted:
                ++nExecution;
                writeMsg(seqnum, buf);
                break;
            case NasdaqITCH::MsgType::Trade:
            case NasdaqITCH::MsgType::CrossTrade:
            case NasdaqITCH::MsgType::BrokenTrade: // ignored
                break;
        }
    });
    printSumary() << "\n\tSuccessfully wrote all " << count << " messages to file: " << outfilename << std::endl;
}

int main_func(int argc, const char *argv[]) {
    auto usage = [&](std::string errstr) {
        if (!errstr.empty()) { std::cerr << "Arg Error: " << errstr << std::endl; }
        std::cerr << "Usage:\n"
                  << argv[0]
                  << R"( -f <Path_to_Nasdaq_ITCH_File> [--orderbook|--print | --chop <destChopFile>] [--stockid stockID] [--msgs <maxtMsgs>]"
    <Path_to_Nasdaq_ITCH_File> E.g. https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/01302020.NASDAQ_ITCH50.gz
    <destChopFile>            Chop binary file and write to destChopFile. <stock> and <maxSeqNum> applies.
    --orderbook               Supply requests to orderbook and generate trades. 
    --print                   Default. Print all order requests and trades from the file.
    <stock>                   Interested stockID. Default -1 for all.
    <countMsgs>               The max number of messages for above actions. Default -1 for all.
    --help|-h
 )" << std::endl;
        exit(!errstr.empty());
    };

    bool        bOrderBook        = false;
    int64_t     interestedStockID = -1, maxtMsgs = -1;
    std::string dataFile, chopFile;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == std::string("-f")) {
            dataFile = argv[++i];
        } else if (argv[i] == std::string("--orderbook")) {
            bOrderBook = true;
        } else if (argv[i] == std::string("--print")) {
            bOrderBook = false;
        } else if (argv[i] == std::string("--chop")) {
            chopFile = argv[++i];
        } else if (argv[i] == std::string("--stockid")) {
            interestedStockID = std::strtoll(argv[++i], nullptr, 10);
        } else if (argv[i] == std::string("--msgs")) {
            maxtMsgs = std::strtoll(argv[++i], nullptr, 10);
        } else if (argv[i] == std::string("--help") || argv[i] == std::string("--h")) {
            usage("");
        } else {
            usage("Unknown arg: " + std::string(argv[i]));
        }
    }
    if (dataFile.empty()) { usage("No data file specified."); }

    if (!chopFile.empty()) {
        run_chop(dataFile, chopFile, interestedStockID, maxtMsgs);
    } else if (bOrderBook) {
        run_with_order_book(dataFile, interestedStockID, maxtMsgs); //
    } else {
        run_print(dataFile, interestedStockID, maxtMsgs); //
    }
    return 0;
}

#ifndef TEST_CONFIG_IMPLEMENT_MAIN
int main(int argc, const char *argv[]) { return main_func(argc, argv); }
#else  //---- define TEST_CONFIG_IMPLEMENT_MAIN to build into a test program that doesn't read external input.

int main() {
    // 5336: MU , 13: APPL
    char const *args[] = {"test", "-f", "../nasdaq-data/data/01302020.NASDAQ_ITCH50", "--orderbook", "--stockid", "13"};
    main_func(sizeof(args) / sizeof(char *), args);
}
#endif // TEST_CONFIG_IMPLEMENT_MAIN
