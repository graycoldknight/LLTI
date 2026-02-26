#include "llti/sorted_lookup.h"
#include <gtest/gtest.h>
#include <random>

TEST(SortedLookupTest, FindAllInsertedKeys) {
    llti::SortedLookup<int64_t> table;
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

TEST(SortedLookupTest, MissingKeysReturnNullptr) {
    llti::SortedLookup<int64_t> table;
    std::vector<std::pair<int64_t, int64_t>> entries;
    for (int64_t i = 0; i < 100; ++i) {
        entries.push_back({i * 2, i});
    }
    table.build(std::move(entries));

    // Odd keys should not be found
    for (int64_t i = 0; i < 100; ++i) {
        EXPECT_EQ(table.find(i * 2 + 1), nullptr);
    }
    // Keys beyond range
    EXPECT_EQ(table.find(-1), nullptr);
    EXPECT_EQ(table.find(200), nullptr);
}

TEST(SortedLookupTest, EmptyTable) {
    llti::SortedLookup<int64_t> table;
    table.build({});
    EXPECT_EQ(table.find(0), nullptr);
    EXPECT_EQ(table.find(42), nullptr);
}

TEST(SortedLookupTest, SingleElement) {
    llti::SortedLookup<int64_t> table;
    table.build({{42, 999}});
    ASSERT_NE(table.find(42), nullptr);
    EXPECT_EQ(*table.find(42), 999);
    EXPECT_EQ(table.find(41), nullptr);
    EXPECT_EQ(table.find(43), nullptr);
}

TEST(SortedLookupTest, DuplicateKeys) {
    llti::SortedLookup<int64_t> table;
    // With duplicates, build keeps both; find returns the first match
    table.build({{5, 100}, {5, 200}, {10, 300}});
    auto* val = table.find(5);
    ASSERT_NE(val, nullptr);
    // First occurrence after sort
    EXPECT_EQ(*val, 100);
    ASSERT_NE(table.find(10), nullptr);
    EXPECT_EQ(*table.find(10), 300);
}

TEST(SortedLookupTest, UnsortedInput) {
    llti::SortedLookup<int64_t> table;
    table.build({{50, 5}, {10, 1}, {30, 3}, {20, 2}, {40, 4}});

    for (int64_t i = 1; i <= 5; ++i) {
        auto* val = table.find(i * 10);
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, i);
    }
}

TEST(SortedLookupTest, LargeRandomDataset) {
    constexpr int N = 100000;
    std::mt19937_64 rng(12345);
    std::vector<std::pair<int64_t, int64_t>> entries;
    entries.reserve(N);
    for (int i = 0; i < N; ++i) {
        int64_t key = static_cast<int64_t>(rng());
        entries.push_back({key, key * 2});
    }

    llti::SortedLookup<int64_t> table;
    auto copy = entries;
    table.build(std::move(copy));

    // Verify a sample of inserted keys
    for (int i = 0; i < 1000; ++i) {
        auto [key, expected] = entries[i * (N / 1000)];
        auto* val = table.find(key);
        ASSERT_NE(val, nullptr) << "key=" << key;
        EXPECT_EQ(*val, expected);
    }
}
