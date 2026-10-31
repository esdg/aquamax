#include <unity.h>
#include <cstdio>
#include "events/EventJournal.h"

using namespace aquamax;
char temporaryPath[L_tmpnam];
void setUp() { TEST_ASSERT_NOT_NULL(std::tmpnam(temporaryPath)); }
void tearDown() { std::remove(temporaryPath); }

void test_journal_persists_wraps_and_deletes_only_selected_entry() {
    EventJournal journal(temporaryPath);
    TEST_ASSERT_TRUE(journal.begin());
    for (unsigned index = 0; index < 103; ++index) TEST_ASSERT_TRUE(journal.add(index, JournalKind::note, "Water change"));
    TEST_ASSERT_EQUAL_UINT32(100, journal.size());
    TEST_ASSERT_EQUAL_UINT32(103, journal.recent(0)->id);
    TEST_ASSERT_EQUAL_UINT32(4, journal.recent(99)->id);
    TEST_ASSERT_TRUE(journal.remove(50));
    TEST_ASSERT_FALSE(journal.remove(50));
    EventJournal restored(temporaryPath);
    TEST_ASSERT_TRUE(restored.begin());
    TEST_ASSERT_EQUAL_UINT32(99, restored.size());
    TEST_ASSERT_EQUAL_UINT32(103, restored.recent(0)->id);
    TEST_ASSERT_TRUE(restored.add(104, JournalKind::recovery, "Recovered"));
    TEST_ASSERT_EQUAL_UINT32(104, restored.recent(0)->id);
}

void test_journal_validation_and_corruption() {
    TEST_ASSERT_FALSE(validJournalText(""));
    TEST_ASSERT_TRUE(validJournalText("first\nsecond"));
    TEST_ASSERT_FALSE(validJournalText("bad\x01line"));
    TEST_ASSERT_TRUE(validJournalText("Changement d'eau"));
    TEST_ASSERT_FALSE(validJournalText("\xc0\x80"));
    EventJournal journal(temporaryPath);
    TEST_ASSERT_TRUE(journal.begin());
    TEST_ASSERT_TRUE(journal.add(0, JournalKind::alert, "Probe disconnected"));
    std::FILE* file = std::fopen(temporaryPath, "r+b");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_INT(0, std::fputc(0, file));
    TEST_ASSERT_EQUAL_INT(0, std::fclose(file));
    EventJournal restored(temporaryPath);
    TEST_ASSERT_TRUE(restored.begin());
    TEST_ASSERT_EQUAL_UINT32(0, restored.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_journal_persists_wraps_and_deletes_only_selected_entry);
    RUN_TEST(test_journal_validation_and_corruption);
    return UNITY_END();
}