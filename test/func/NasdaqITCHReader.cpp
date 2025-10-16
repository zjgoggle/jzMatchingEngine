#include "OrderBook.h"
#include "NasdaqITCHReader.h"


template<BookEventReporter TradeReporterT = NUllBookEventReporter> // EventDetailPrinter>
struct OrderBookAndReporter {
    TradeReporterT            tradeReporter;
    OrderBook<TradeReporterT> book{tradeReporter};
    OrderBookAndReporter() : book(tradeReporter) {}
};

// -1 for all
constexpr int64_t INTERESTED_STOCK = 5336; // MU
constexpr int64_t MAX_SEQ_NUM      = -1;   //262173;
// supply order requeest to order book and print order events
void run_with_order_book(const std::string filename, int64_t insterestedStock = INTERESTED_STOCK, int64_t maxSeqNum = MAX_SEQ_NUM) {
    using SymbolID = int32_t;
    std::unordered_map<SymbolID, OrderBookAndReporter<>> bookMap;

    int64_t lastRequestSeq = -1;
    read_nasdaq_itch(filename, [&]<typename T>(size_t seqnum, T &msg) {
        if (insterestedStock >= 0 && insterestedStock != msg.StockLocate.value) return;
        std::cout << "** " << seqnum << ", ";
        printMsg(msg);
        if constexpr (std::is_same_v<T, NasdaqITCH::AddOrder> || std::is_same_v<T, NasdaqITCH::AddOrderWithoutMPID>) {
            if (101395 == msg.OrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            auto &tradeRequest      = bookMap[msg.StockLocate.value].tradeReporter;
            tradeRequest.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            auto &book     = bookMap[msg.StockLocate.value].book;
            bool  ok       = book.matchAddNewOrder(
                    msg.OrderReferenceNumber.value, msg.Side == 'B' ? Side::Buy : Side::Sell, msg.Shares.value, msg.Price.value);
            ASSERT_TRUE(ok);
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderPartialCancel>) {
            if (101395 == msg.OrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            bookMap[msg.StockLocate.value].tradeReporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            auto &book     = bookMap[msg.StockLocate.value].book;
            bool  ok       = book.partialCancelOrder(msg.OrderReferenceNumber.value, msg.CancelledShares.value);
            ASSERT_TRUE(ok);
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderDelete>) {
            if (101395 == msg.OrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            bookMap[msg.StockLocate.value].tradeReporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            auto &book     = bookMap[msg.StockLocate.value].book;
            bool  ok       = book.cancelOrder(msg.OrderReferenceNumber.value);
            // ASSERT_TRUE(ok); // fail for OrderReferenceNumber==101395 because the order was executed in nasdaq
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderReplace>) {
            if (101395 == msg.NewOrderReferenceNumber.value) {
                // printMsg(msg); //
            }
            bookMap[msg.StockLocate.value].tradeReporter.requestSeq = seqnum;

            lastRequestSeq = seqnum;
            auto &book     = bookMap[msg.StockLocate.value].book;
            bool  ok       = book.replaceOrder(msg.OrderReferenceNumber.value, msg.NewOrderReferenceNumber.value, msg.Shares.value, msg.Price.value);
            // ASSERT_TRUE(ok);
        } else if constexpr (std::is_same_v<T, NasdaqITCH::OrderExecutedWithoutPrice> || std::is_same_v<T, NasdaqITCH::OrderExecuted>) {
            // TODO: remove
        } else if constexpr (std::is_same_v<T, NasdaqITCH::Trade> || std::is_same_v<T, NasdaqITCH::CrossTrade>) {
            // std::cout << "** Original trade seqnum: " << seqnum << ", lastRequestSeq: " << lastRequestSeq << ", ";
            // printMsg(msg);
        }
        if (maxSeqNum >= 0 && maxSeqNum <= seqnum) { exit(0); }
    });
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
                  << argv[0] << R"( <Path_to_Nasdaq_ITCH_File> [--orderbook|--dump]"
 "\t\t <Path_to_Nasdaq_ITCH_File> https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/01302020.NASDAQ_ITCH50.gz
 "\t\t --orderbook                Supply requests to orderbook and generate trades. 
 "\t\t --dump                     Dump all order requests and trades from the file.
 )" << std::endl;
        exit(!errstr.empty());
    };
    if (argc != 3 && argc != 2) {
        usage("Wrong number of args.");
    } else if (argc == 3 && argv[2] == std::string("--orderbook")) {
        run_with_order_book(argv[1]); //
    } else if (argc == 2 || argv[2] == std::string("--dump")) {
        run_dump(argv[1]); //
    } else {
        usage("Invalid args.");
    }
    return 0;
}

#ifndef TEST_CONFIG_IMPLEMENT_MAIN
int main(int argc, const char *argv[]) { return main_func(argc, argv); }
#else  //---- define TEST_CONFIG_IMPLEMENT_MAIN to build into a test program that doesn't read external input.

int main() {
    char const *args[] = {"test", "../nasdaq-data/data/01302020.NASDAQ_ITCH50", "--orderbook"};
    main_func(sizeof(args) / sizeof(char *), args);
}
#endif // TEST_CONFIG_IMPLEMENT_MAIN
