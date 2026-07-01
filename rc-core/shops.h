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

typedef struct {
    RcShop *shops;
    RcShopStock *stock;
    int shop_count;
    int stock_count;
} RcShopData;

extern RcShop *g_shops;
extern RcShopStock *g_shop_stock;
extern int g_shop_count;
extern int g_shop_stock_count;

int rc_load_shops(const char *path);
void rc_shop_data_init(RcShopData *data);
void rc_shop_data_free(RcShopData *data);
int rc_load_shops_into(const char *path, RcShopData *out);
int rc_shop_data_import_globals(RcShopData *out);
int rc_shops_mirror_to_globals(const RcShopData *data);
void rc_shops_use_data(const RcShopData *data);
void rc_shops_reset_data_if_active(const RcShopData *data);
const RcShop *rc_shop_get(int shop_idx);
const RcShopStock *rc_shop_stock_rows(const RcShop *shop, int *count);
int rc_shop_find_by_name(const char *name);
int rc_shop_has_item(int shop_idx, int item_id);

#endif
