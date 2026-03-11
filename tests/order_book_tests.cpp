#include <cassert>
#include <iostream>
#include <vector>

#include "lom/order_book.hpp"

using namespace lom;

static void test_crossing_trade() {
    LimitOrderBook::Config cfg{};
    cfg.max_order_nodes = 1024;
    cfg.snapshot_depth = 10;
    LimitOrderBook book(cfg);

    std::vector<Trade> trades;
    NewOrder o1{};
    o1.order_id = 1;
    o1.user_id = 101;
    o1.side = Side::sell;
    o1.price_ticks = 10100;
    o1.quantity = 50;
    bool ok = book.on_new_order(o1, trades, 100);
    assert(ok);
    assert(trades.empty());

    NewOrder o2{};
    o2.order_id = 2;
    o2.user_id = 202;
    o2.side = Side::buy;
    o2.price_ticks = 10150;
    o2.quantity = 20;
    ok = book.on_new_order(o2, trades, 101);
    assert(ok);
    assert(trades.size() == 1);
    assert(trades.back().quantity == 20);
    assert(trades.back().price_ticks == 10100);
}

static void test_fifo_priority() {
    LimitOrderBook::Config cfg{};
    cfg.max_order_nodes = 1024;
    cfg.snapshot_depth = 10;
    LimitOrderBook book(cfg);
    std::vector<Trade> trades;

    NewOrder o1{};
    o1.order_id = 10;
    o1.user_id = 1000;
    o1.side = Side::buy;
    o1.price_ticks = 9900;
    o1.quantity = 30;
    bool ok = book.on_new_order(o1, trades, 1);
    assert(ok);

    NewOrder o2{};
    o2.order_id = 11;
    o2.user_id = 1001;
    o2.side = Side::buy;
    o2.price_ticks = 9900;
    o2.quantity = 30;
    ok = book.on_new_order(o2, trades, 2);
    assert(ok);

    NewOrder o3{};
    o3.order_id = 12;
    o3.user_id = 2000;
    o3.side = Side::sell;
    o3.price_ticks = 9800;
    o3.quantity = 40;
    ok = book.on_new_order(o3, trades, 3);
    assert(ok);

    assert(trades.size() == 2);
    assert(trades[0].maker_order_id == 10);
    assert(trades[0].quantity == 30);
    assert(trades[1].maker_order_id == 11);
    assert(trades[1].quantity == 10);
}

static void test_cancel_path() {
    LimitOrderBook::Config cfg{};
    cfg.max_order_nodes = 1024;
    cfg.snapshot_depth = 10;
    LimitOrderBook book(cfg);
    std::vector<Trade> trades;

    NewOrder o1{};
    o1.order_id = 100;
    o1.user_id = 501;
    o1.side = Side::sell;
    o1.price_ticks = 10500;
    o1.quantity = 15;
    bool ok = book.on_new_order(o1, trades, 1);
    assert(ok);
    assert(book.active_order_count() == 1);

    CancelOrder c{};
    c.order_id = 100;
    c.user_id = 501;
    ok = book.on_cancel_order(c);
    assert(ok);
    assert(book.active_order_count() == 0);
}

static void test_cancel_rejects_wrong_user() {
    LimitOrderBook::Config cfg{};
    cfg.max_order_nodes = 1024;
    cfg.snapshot_depth = 10;
    LimitOrderBook book(cfg);
    std::vector<Trade> trades;

    NewOrder o1{};
    o1.order_id = 200;
    o1.user_id = 9001;
    o1.side = Side::buy;
    o1.price_ticks = 10000;
    o1.quantity = 10;
    bool ok = book.on_new_order(o1, trades, 1);
    assert(ok);
    assert(book.active_order_count() == 1);

    CancelOrder bad_cancel{};
    bad_cancel.order_id = 200;
    bad_cancel.user_id = 9002;
    ok = book.on_cancel_order(bad_cancel);
    assert(!ok);
    assert(book.active_order_count() == 1);

    CancelOrder admin_cancel{};
    admin_cancel.order_id = 200;
    admin_cancel.user_id = 0;
    ok = book.on_cancel_order(admin_cancel);
    assert(ok);
    assert(book.active_order_count() == 0);
}

static void test_reject_duplicate_order_id() {
    LimitOrderBook::Config cfg{};
    cfg.max_order_nodes = 1024;
    cfg.snapshot_depth = 10;
    LimitOrderBook book(cfg);
    std::vector<Trade> trades;

    NewOrder o1{};
    o1.order_id = 300;
    o1.user_id = 7001;
    o1.side = Side::sell;
    o1.price_ticks = 10100;
    o1.quantity = 12;

    bool ok = book.on_new_order(o1, trades, 1);
    assert(ok);
    assert(book.active_order_count() == 1);

    NewOrder dup = o1;
    dup.user_id = 7002;
    dup.quantity = 9;
    ok = book.on_new_order(dup, trades, 2);
    assert(!ok);
    assert(book.active_order_count() == 1);
}

int main() {
    test_crossing_trade();
    test_fifo_priority();
    test_cancel_path();
    test_cancel_rejects_wrong_user();
    test_reject_duplicate_order_id();
    std::cout << "All order book tests passed.\n";
    return 0;
}
