#include "shops.h"

RcShop g_shops[RC_MAX_SHOPS];
int g_shop_count = 0;

int rc_shop_buy(RcWorld *world, int shop_idx, int stock_slot, int quantity) {
    // TODO: implement buy logic
    (void)world; (void)shop_idx; (void)stock_slot; (void)quantity;
    return -1;
}

int rc_shop_sell(RcWorld *world, int shop_idx, int inv_slot, int quantity) {
    // TODO: implement sell logic
    (void)world; (void)shop_idx; (void)inv_slot; (void)quantity;
    return -1;
}

void rc_shop_restock_tick(void) {
    // TODO: gradual restock
}
