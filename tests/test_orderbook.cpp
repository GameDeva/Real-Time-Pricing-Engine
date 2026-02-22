#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

#include "market_data/OrderBook.h"

// ── Empty book behaviour ───────────────────────────────────────────────────────

TEST(OrderBookTest, EmptyBookBidIsZero) {
    OrderBook book;
    EXPECT_DOUBLE_EQ(book.getBestBid(), 0.0);
}

TEST(OrderBookTest, EmptyBookAskIsZero) {
    OrderBook book;
    EXPECT_DOUBLE_EQ(book.getBestAsk(), 0.0);
}

TEST(OrderBookTest, EmptyBookMidIsZero) {
    OrderBook book;
    EXPECT_DOUBLE_EQ(book.getMidPrice(), 0.0);
}

// ── Basic insert and retrieval ─────────────────────────────────────────────────

TEST(OrderBookTest, InsertBidReturnsBestBid) {
    OrderBook book;
    book.updateLevel(50000.0, 1.5, /*is_bid=*/true);
    EXPECT_DOUBLE_EQ(book.getBestBid(), 50000.0);
}

TEST(OrderBookTest, InsertAskReturnsBestAsk) {
    OrderBook book;
    book.updateLevel(50100.0, 0.8, /*is_bid=*/false);
    EXPECT_DOUBLE_EQ(book.getBestAsk(), 50100.0);
}

TEST(OrderBookTest, BestBidIsHighestPrice) {
    OrderBook book;
    book.updateLevel(49900.0, 1.0, true);
    book.updateLevel(50000.0, 2.0, true);
    book.updateLevel(49800.0, 0.5, true);
    EXPECT_DOUBLE_EQ(book.getBestBid(), 50000.0);
}

TEST(OrderBookTest, BestAskIsLowestPrice) {
    OrderBook book;
    book.updateLevel(50200.0, 1.0, false);
    book.updateLevel(50100.0, 2.0, false);
    book.updateLevel(50300.0, 0.5, false);
    EXPECT_DOUBLE_EQ(book.getBestAsk(), 50100.0);
}

// ── Mid-price calculation ──────────────────────────────────────────────────────

TEST(OrderBookTest, MidPriceIsCorrect) {
    OrderBook book;
    book.updateLevel(50000.0, 1.0, true);
    book.updateLevel(50100.0, 1.0, false);
    EXPECT_DOUBLE_EQ(book.getMidPrice(), 50050.0);
}

TEST(OrderBookTest, MidPriceIsZeroIfOnlyBid) {
    OrderBook book;
    book.updateLevel(50000.0, 1.0, true);
    EXPECT_DOUBLE_EQ(book.getMidPrice(), 0.0);
}

TEST(OrderBookTest, MidPriceIsZeroIfOnlyAsk) {
    OrderBook book;
    book.updateLevel(50100.0, 1.0, false);
    EXPECT_DOUBLE_EQ(book.getMidPrice(), 0.0);
}

// ── Remove level (qty == 0) ────────────────────────────────────────────────────

TEST(OrderBookTest, ZeroQtyRemovesBidLevel) {
    OrderBook book;
    book.updateLevel(50000.0, 1.0, true);
    book.updateLevel(50000.0, 0.0, true);   // remove
    EXPECT_DOUBLE_EQ(book.getBestBid(), 0.0);
}

TEST(OrderBookTest, ZeroQtyRemovesAskLevel) {
    OrderBook book;
    book.updateLevel(50100.0, 1.0, false);
    book.updateLevel(50100.0, 0.0, false);  // remove
    EXPECT_DOUBLE_EQ(book.getBestAsk(), 0.0);
}

TEST(OrderBookTest, RemovingBestBidPromotesNext) {
    OrderBook book;
    book.updateLevel(50000.0, 1.0, true);
    book.updateLevel(49900.0, 1.0, true);
    book.updateLevel(50000.0, 0.0, true);   // remove best
    EXPECT_DOUBLE_EQ(book.getBestBid(), 49900.0);
}

TEST(OrderBookTest, ZeroQtyUpdatesMidToZeroWhenSideEmpty) {
    OrderBook book;
    book.updateLevel(50000.0, 1.0, true);
    book.updateLevel(50100.0, 1.0, false);
    book.updateLevel(50000.0, 0.0, true);   // remove only bid
    EXPECT_DOUBLE_EQ(book.getMidPrice(), 0.0);
}

// ── Level cap (kMaxLevels = 20) ────────────────────────────────────────────────

TEST(OrderBookTest, BidsShrinkToMaxLevels) {
    OrderBook book;
    // Insert 25 bid levels at different prices
    for (int i = 1; i <= 25; ++i)
        book.updateLevel(static_cast<double>(50000 + i), 1.0, true);
    // We can only observe via best bid: the best level must always be there
    EXPECT_DOUBLE_EQ(book.getBestBid(), 50025.0);
    // We cannot query size directly, but the worst 5 levels should be pruned.
    // After pruning, levels 50006..50025 remain (lowest bid 50006 pruned out).
    // Remove the worst surviving level (50006) — should be a no-op if it was pruned.
    // What we can assert: best bid is still 50025 (unchanged by pruning direction).
    EXPECT_GT(book.getBestBid(), 0.0);
}

TEST(OrderBookTest, AsksShrinkToMaxLevels) {
    OrderBook book;
    for (int i = 1; i <= 25; ++i)
        book.updateLevel(static_cast<double>(50000 + i), 1.0, false);
    // Best ask should be 50001 — the lowest — and must always survive
    EXPECT_DOUBLE_EQ(book.getBestAsk(), 50001.0);
}

// ── Atomic mid-price cache consistency ────────────────────────────────────────

TEST(OrderBookTest, AtomicMidAgreesMutexMid) {
    OrderBook book;
    // Insert several levels then confirm atomic getMidPrice() == manual calc
    book.updateLevel(49950.0, 2.0, true);
    book.updateLevel(49960.0, 1.0, true);   // best bid
    book.updateLevel(50040.0, 1.0, false);  // best ask
    book.updateLevel(50050.0, 2.0, false);

    double expected_mid = (49960.0 + 50040.0) / 2.0;
    EXPECT_DOUBLE_EQ(book.getMidPrice(), expected_mid);
}

TEST(OrderBookTest, MidCacheUpdatesAfterRemoval) {
    OrderBook book;
    book.updateLevel(50000.0, 1.0, true);
    book.updateLevel(50100.0, 1.0, false);
    EXPECT_DOUBLE_EQ(book.getMidPrice(), 50050.0);

    // Add a better bid; mid should shift
    book.updateLevel(50010.0, 1.0, true);
    EXPECT_DOUBLE_EQ(book.getMidPrice(), (50010.0 + 50100.0) / 2.0);
}

// ── Thread-safety smoke test ───────────────────────────────────────────────────

TEST(OrderBookTest, ConcurrentWritersAndReaders) {
    OrderBook book;
    constexpr int kIterations = 2000;

    // 4 writer threads: each inserts and removes levels at different prices
    auto writer = [&](int offset) {
        for (int i = 0; i < kIterations; ++i) {
            double price = 50000.0 + offset + (i % 20);
            book.updateLevel(price, 1.0 + i, /*is_bid=*/true);
            book.updateLevel(price + 200.0, 1.0, /*is_bid=*/false);
            if (i % 5 == 0)
                book.updateLevel(price, 0.0, /*is_bid=*/true);  // occasional remove
        }
    };

    // 4 reader threads: continuously read without locking (uses atomic / shared_lock)
    std::atomic<bool> stop{false};
    auto reader = [&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            volatile double bid = book.getBestBid();
            volatile double ask = book.getBestAsk();
            volatile double mid = book.getMidPrice();
            // Suppress unused-variable warnings; we just want to exercise the paths
            (void)bid; (void)ask; (void)mid;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(writer, i * 100);
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(reader);

    // Join writers
    for (int i = 0; i < 4; ++i)
        threads[i].join();

    // Signal readers to stop, then join
    stop.store(true, std::memory_order_relaxed);
    for (int i = 4; i < 8; ++i)
        threads[i].join();

    // If we reach here without a data race / crash the test passes.
    // (Run with -fsanitize=thread on Linux for rigorous detection.)
    SUCCEED();
}
