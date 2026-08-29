// Motusy Moto Box — testy kolejki wysylki.
//
// Sedno: numery przejazdow wynikaja z pozycji w historii, a nie z zapisanej
// flagi. Testy pilnuja, zeby to wyprowadzenie sie zgadzalo takze wtedy, gdy
// historia sie przepelni albo serwer odpowie bez sensu.

#include <unity.h>

#include "UploadQueue.h"

using namespace telemetry;

namespace {

/// Przejazd rozpoznawalny po wartosci — przechyl w lewo niesie numer, wiec
/// widac, ktory rekord trafil na ktore miejsce w przesylce.
motion::RideValues rideMarked(float marker) {
    motion::RideValues values;
    values.maxLeanLeftDeg = marker;
    values.maxAccelG = 0.5f;
    return values;
}

/// Symuluje `count` przejazdow: kazdy trafia do historii i dostaje numer.
void archiveRides(UploadQueue& queue, motion::RideHistory& history, int count) {
    for (int i = 1; i <= count; ++i) {
        history.push(rideMarked(static_cast<float>(i)));
        queue.onRideArchived();
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_fresh_queue_has_nothing_to_send() {
    UploadQueue queue;
    motion::RideHistory history;

    TEST_ASSERT_EQUAL_UINT32(0, queue.lastSeq());
    TEST_ASSERT_EQUAL_UINT32(0, queue.pendingCount(history.count()));
}

void test_archived_rides_are_numbered_from_one() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 3);

    TEST_ASSERT_EQUAL_UINT32(3, queue.lastSeq());
    TEST_ASSERT_EQUAL_UINT32(3, queue.pendingCount(history.count()));

    // Pozycja 0 to najnowszy przejazd, czyli najwyzszy numer.
    TEST_ASSERT_EQUAL_UINT32(3, queue.seqAt(0, history.count()));
    TEST_ASSERT_EQUAL_UINT32(1, queue.seqAt(2, history.count()));
    TEST_ASSERT_EQUAL_UINT32(0, queue.seqAt(3, history.count()));
}

/// Przesylka musi isc numerami rosnaco, bo serwer potwierdza jedna liczba.
void test_collect_returns_oldest_first() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 3);

    RideRecord records[kMaxRidesPerPayload];
    const size_t count = queue.collect(history, records, kMaxRidesPerPayload);

    TEST_ASSERT_EQUAL_UINT32(3, count);
    TEST_ASSERT_EQUAL_UINT32(1, records[0].seq);
    TEST_ASSERT_EQUAL_UINT32(2, records[1].seq);
    TEST_ASSERT_EQUAL_UINT32(3, records[2].seq);

    // Numer musi trafic w ten przejazd, ktorym sie podaje.
    TEST_ASSERT_EQUAL_FLOAT(1.0f, records[0].values.maxLeanLeftDeg);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, records[2].values.maxLeanLeftDeg);
}

void test_confirmation_empties_the_queue() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 3);

    TEST_ASSERT_TRUE(queue.confirmSentThrough(3));
    TEST_ASSERT_EQUAL_UINT32(0, queue.pendingCount(history.count()));

    RideRecord records[kMaxRidesPerPayload];
    TEST_ASSERT_EQUAL_UINT32(0, queue.collect(history, records, kMaxRidesPerPayload));
}

void test_partial_confirmation_leaves_the_rest() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 5);

    TEST_ASSERT_TRUE(queue.confirmSentThrough(2));
    TEST_ASSERT_EQUAL_UINT32(3, queue.pendingCount(history.count()));

    RideRecord records[kMaxRidesPerPayload];
    const size_t count = queue.collect(history, records, kMaxRidesPerPayload);

    TEST_ASSERT_EQUAL_UINT32(3, count);
    TEST_ASSERT_EQUAL_UINT32(3, records[0].seq);
    TEST_ASSERT_EQUAL_UINT32(5, records[2].seq);
}

/// Powtorzone potwierdzenie zdarzy sie przy kazdej ponownej wysylce tego
/// samego przejazdu — nie moze niczego zmieniac ani zmuszac do zapisu w NVS.
void test_repeated_confirmation_changes_nothing() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 3);

    TEST_ASSERT_TRUE(queue.confirmSentThrough(2));
    TEST_ASSERT_FALSE(queue.confirmSentThrough(2));
    TEST_ASSERT_FALSE(queue.confirmSentThrough(1));
    TEST_ASSERT_EQUAL_UINT32(2, queue.sentThrough());
}

/// Serwer nie moze potwierdzic czegos, czego urzadzenie nie wyslalo —
/// przyjecie takiej liczby cicho skasowaloby zalegle przejazdy.
void test_confirmation_from_the_future_is_ignored() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 3);

    TEST_ASSERT_FALSE(queue.confirmSentThrough(9));
    TEST_ASSERT_EQUAL_UINT32(0, queue.sentThrough());
    TEST_ASSERT_EQUAL_UINT32(3, queue.pendingCount(history.count()));
}

void test_restore_clamps_broken_state() {
    UploadQueue queue;
    queue.restore(4, 11);

    TEST_ASSERT_EQUAL_UINT32(4, queue.lastSeq());
    TEST_ASSERT_EQUAL_UINT32(4, queue.sentThrough());
}

void test_numbering_survives_restore() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 4);
    queue.confirmSentThrough(2);

    UploadQueue afterReboot;
    afterReboot.restore(queue.lastSeq(), queue.sentThrough());
    afterReboot.onRideArchived();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, afterReboot.lastSeq(),
                                     "Numery nie moga sie cofac po restarcie");
    TEST_ASSERT_EQUAL_UINT32(2, afterReboot.sentThrough());
}

/// Dwanascie przejazdow bez zasiegu: historia trzyma dziesiec, dwa najstarsze
/// przepadaja. Kolejka ma o tym wiedziec, a nie obiecywac dwunastu.
void test_overflowing_history_drops_oldest() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 12);

    TEST_ASSERT_EQUAL_UINT32(12, queue.lastSeq());
    TEST_ASSERT_EQUAL_UINT32(motion::RideHistory::kCapacity,
                             queue.pendingCount(history.count()));

    RideRecord records[kMaxRidesPerPayload];
    const size_t count = queue.collect(history, records, kMaxRidesPerPayload);

    TEST_ASSERT_EQUAL_UINT32(motion::RideHistory::kCapacity, count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, records[0].seq,
                                     "Najstarszy zachowany przejazd ma numer 3");
    TEST_ASSERT_EQUAL_UINT32(12, records[count - 1].seq);
}

/// Przy ciasnym buforze bierzemy najstarsze i BEZ PRZERW — inaczej jedna
/// liczba potwierdzenia nie opisalaby tego, co faktycznie doszlo.
void test_limited_batch_takes_oldest_without_gaps() {
    UploadQueue queue;
    motion::RideHistory history;
    archiveRides(queue, history, 5);

    RideRecord records[3];
    const size_t count = queue.collect(history, records, 3);

    TEST_ASSERT_EQUAL_UINT32(3, count);
    TEST_ASSERT_EQUAL_UINT32(1, records[0].seq);
    TEST_ASSERT_EQUAL_UINT32(2, records[1].seq);
    TEST_ASSERT_EQUAL_UINT32(3, records[2].seq);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fresh_queue_has_nothing_to_send);
    RUN_TEST(test_archived_rides_are_numbered_from_one);
    RUN_TEST(test_collect_returns_oldest_first);
    RUN_TEST(test_confirmation_empties_the_queue);
    RUN_TEST(test_partial_confirmation_leaves_the_rest);
    RUN_TEST(test_repeated_confirmation_changes_nothing);
    RUN_TEST(test_confirmation_from_the_future_is_ignored);
    RUN_TEST(test_restore_clamps_broken_state);
    RUN_TEST(test_numbering_survives_restore);
    RUN_TEST(test_overflowing_history_drops_oldest);
    RUN_TEST(test_limited_batch_takes_oldest_without_gaps);
    return UNITY_END();
}
