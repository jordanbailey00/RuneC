#ifndef RC_SHOPS_H
#define RC_SHOPS_H

#include "types.h"

typedef struct {
    char name[64];
    RcInvSlot stock[40];
    int stock_count;
    bool general_store;
} RcShop;

extern RcShop g_shops[RC_MAX_SHOPS];
extern int g_shop_count;

int  rc_shop_buy(RcWorld *world, int shop_idx, int stock_slot, int quantity);
int  rc_shop_sell(RcWorld *world, int shop_idx, int inv_slot, int quantity);
void rc_shop_restock_tick(void);

#endif
