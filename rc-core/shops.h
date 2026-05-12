#ifndef RC_SHOPS_H
#define RC_SHOPS_H

#include <stdint.h>

typedef struct {
    char name[64];
    char owner[64];
    char location[64];
    char specialty[64];
    uint32_t first_stock;
    uint16_t stock_count;
    uint8_t members;
} RcShop;

typedef struct {
    int item_id;
    uint32_t buy_price;
    uint32_t sell_price;
    uint16_t base_stock;
    uint16_t buy_mult;
    uint16_t sell_mult;
    uint16_t restock_ticks;
} RcShopStock;

extern RcShop *g_shops;
extern RcShopStock *g_shop_stock;
extern int g_shop_count;
extern int g_shop_stock_count;

int rc_load_shops(const char *path);
const RcShop *rc_shop_get(int shop_idx);
const RcShopStock *rc_shop_stock_rows(const RcShop *shop, int *count);
int rc_shop_find_by_name(const char *name);
int rc_shop_has_item(int shop_idx, int item_id);

#endif
