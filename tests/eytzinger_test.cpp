#include "llti/eytzinger_lookup.h"
#include <gtest/gtest.h>
#include <random>

TEST(EytzingerLookupTest, FindAllInsertedKeys) {
    llti::EytzingerLookup<int64_t> table;
    std::vector<std::pair<int64_t, int64_t>> entries;
    for (int64_t i = 0; i < 1000; ++i) {
        entries.push_back({i * 3, i * 100});
    }
    table.build(std::move(entries));

    for (int64_t i = 0; i < 1000; ++i) {
        auto* val = table.find(i * 3);
        ASSERT_NE(val, nullptr) << "key=" << i * 3;
        EXPECT_EQ(*val, i * 100);
    }
}

TEST(EytzingerLookupTest, MissingKeysReturnNullptr) {
    llti::EytzingerLookup<int64_t> table;
    std::vector<std::pair<int64_t, int64_t>> entries;
    for (int64_t i = 0; i < 100; ++i) {
        entries.push_back({i * 2, i});
    }
    table.build(std::move(entries));

    for (int64_t i = 0; i < 100; ++i) {
        EXPECT_EQ(table.find(i * 2 + 1), nullptr);
    }
    EXPECT_EQ(table.find(-1), nullptr);
    EXPECT_EQ(table.find(200), nullptr);
}

TEST(EytzingerLookupTest, EmptyTable) {
    llti::EytzingerLookup<int64_t> table;
    table.build({});
    EXPECT_EQ(table.find(0), nullptr);
    EXPECT_EQ(table.find(42), nullptr);
}

TEST(EytzingerLookupTest, SingleElement) {
    llti::EytzingerLookup<int64_t> table;
    table.build({{42, 999}});
    ASSERT_NE(table.find(42), nullptr);
    EXPECT_EQ(*table.find(42), 999);
    EXPECT_EQ(table.find(41), nullptr);
    EXPECT_EQ(table.find(43), nullptr);
}

TEST(EytzingerLookupTest, DuplicateKeys) {
    llti::EytzingerLookup<int64_t> table;
    table.build({{5, 100}, {5, 200}, {10, 300}});
    auto* val = table.find(5);
    ASSERT_NE(val, nullptr);
    // Should find one of the duplicate entries
    EXPECT_TRUE(*val == 100 || *val == 200);
    ASSERT_NE(table.find(10), nullptr);
    EXPECT_EQ(*table.find(10), 300);
}

TEST(EytzingerLookupTest, UnsortedInput) {
    llti::EytzingerLookup<int64_t> table;
    table.build({{50, 5}, {10, 1}, {30, 3}, {20, 2}, {40, 4}});

    for (int64_t i = 1; i <= 5; ++i) {
        auto* val = table.find(i * 10);
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, i);
    }
}

TEST(EytzingerLookupTest, PowerOfTwoSize) {
    // Eytzinger layout is cleanest when n = 2^k - 1
    llti::EytzingerLookup<int64_t> table;
    std::vector<std::pair<int64_t, int64_t>> entries;
    for (int64_t i = 0; i < 1023; ++i) {
        entries.push_back({i, i * 7});
    }
    table.build(std::move(entries));

    for (int64_t i = 0; i < 1023; ++i) {
        auto* val = table.find(i);
        ASSERT_NE(val, nullptr) << "key=" << i;
        EXPECT_EQ(*val, i * 7);
    }
    EXPECT_EQ(table.find(1023), nullptr);
}

TEST(EytzingerLookupTest, NonPowerOfTwoSize) {
    // Non-perfect tree sizes stress the edge cases
    for (int sz : {2, 3, 6, 7, 10, 15, 16, 17, 100, 127, 128, 255, 500}) {
        llti::EytzingerLookup<int64_t> table;
        std::vector<std::pair<int64_t, int64_t>> entries;
        for (int64_t i = 0; i < sz; ++i) {
            entries.push_back({i * 10, i});
        }
        table.build(std::move(entries));

        for (int64_t i = 0; i < sz; ++i) {
            auto* val = table.find(i * 10);
            ASSERT_NE(val, nullptr) << "sz=" << sz << " key=" << i * 10;
            EXPECT_EQ(*val, i) << "sz=" << sz;
        }
        // Missing key
        EXPECT_EQ(table.find(sz * 10), nullptr) << "sz=" << sz;
    }
}

TEST(EytzingerLookupTest, LargeRandomDataset) {
    constexpr int N = 100000;
    std::mt19937_64 rng(12345);
    std::vector<std::pair<int64_t, int64_t>> entries;
    entries.reserve(N);
    for (int i = 0; i < N; ++i) {
        int64_t key = static_cast<int64_t>(rng());
        entries.push_back({key, key * 2});
    }

    llti::EytzingerLookup<int64_t> table;
    auto copy = entries;
    table.build(std::move(copy));

    // Verify a sample of inserted keys
    // Need sorted+deduped to get correct expected values since duplicates
    // may exist in random data
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end(),
                              [](const auto& a, const auto& b) { return a.first == b.first; }),
                  entries.end());

    for (size_t i = 0; i < 1000 && i < entries.size(); ++i) {
        size_t idx = i * (entries.size() / 1000);
        auto [key, expected] = entries[idx];
        auto* val = table.find(key);
        ASSERT_NE(val, nullptr) << "key=" << key;
        EXPECT_EQ(*val, expected);
    }
}
