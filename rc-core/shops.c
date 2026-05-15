#include "shops.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHOP_MAGIC 0x504F4853u
#define SHOP_VERSION 1u

RcShop *g_shops = NULL;
RcShopStock *g_shop_stock = NULL;
int g_shop_count = 0;
int g_shop_stock_count = 0;

static int read_str8(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    int keep = len < (uint8_t)(cap - 1) ? (int)len : cap - 1;
    if (keep && !rc_read_exact(f, out, 1, (size_t)keep, path, what)) return 0;
    out[keep] = '\0';
    if (len > (uint8_t)keep &&
            !rc_seek(f, (long)(len - (uint8_t)keep), SEEK_CUR, path, what)) {
        return 0;
    }
    return 1;
}

static int push_stock(RcShopStock **rows, int *count, int *cap,
                      RcShopStock row) {
    if (*count == *cap) {
        int next = *cap ? *cap * 2 : 4096;
        RcShopStock *grown = realloc(*rows, (size_t)next * sizeof(**rows));
        if (!grown) return 0;
        *rows = grown;
        *cap = next;
    }
    (*rows)[(*count)++] = row;
    return 1;
}

int rc_load_shops(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || magic != SHOP_MAGIC || version != SHOP_VERSION) {
        rc_asset_close(f);
        return -1;
    }

    RcShop *shops = calloc(count ? count : 1u, sizeof(*shops));
    RcShopStock *stock = NULL;
    int stock_count = 0, stock_cap = 0;
    if (!shops) {
        rc_asset_close(f);
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        RcShop *shop = &shops[i];
        if (!read_str8(f, shop->name, sizeof(shop->name), path, "name")
                || !read_str8(f, shop->owner, sizeof(shop->owner), path, "owner")
                || !read_str8(f, shop->location, sizeof(shop->location), path, "location")
                || !read_str8(f, shop->specialty, sizeof(shop->specialty), path, "specialty")
                || !rc_read_exact(f, &shop->members, sizeof(shop->members), 1,
                                  path, "members")
                || !rc_read_exact(f, &shop->stock_count, sizeof(shop->stock_count),
                                  1, path, "stock count")) {
            free(shops);
            free(stock);
            rc_asset_close(f);
            return -1;
        }
        shop->first_stock = (uint32_t)stock_count;
        for (int s = 0; s < shop->stock_count; s++) {
            RcShopStock row;
            uint32_t item_id;
            if (!rc_read_exact(f, &item_id, sizeof(item_id), 1, path, "item")
                    || !rc_read_exact(f, &row.buy_price, sizeof(row.buy_price),
                                      1, path, "buy")
                    || !rc_read_exact(f, &row.sell_price, sizeof(row.sell_price),
                                      1, path, "sell")
                    || !rc_read_exact(f, &row.base_stock, sizeof(row.base_stock),
                                      1, path, "stock")
                    || !rc_read_exact(f, &row.buy_mult, sizeof(row.buy_mult),
                                      1, path, "buy mult")
                    || !rc_read_exact(f, &row.sell_mult, sizeof(row.sell_mult),
                                      1, path, "sell mult")
                    || !rc_read_exact(f, &row.restock_ticks,
                                      sizeof(row.restock_ticks), 1, path,
                                      "restock")) {
                free(shops);
                free(stock);
                rc_asset_close(f);
                return -1;
            }
            row.item_id = (int)item_id;
            if (!push_stock(&stock, &stock_count, &stock_cap, row)) {
                free(shops);
                free(stock);
                rc_asset_close(f);
                return -1;
            }
        }
    }

    rc_asset_close(f);
    free(g_shops);
    free(g_shop_stock);
    g_shops = shops;
    g_shop_stock = stock;
    g_shop_count = (int)count;
    g_shop_stock_count = stock_count;
    return g_shop_count;
}

const RcShop *rc_shop_get(int shop_idx) {
    if (shop_idx < 0 || shop_idx >= g_shop_count || !g_shops) return NULL;
    return &g_shops[shop_idx];
}

const RcShopStock *rc_shop_stock_rows(const RcShop *shop, int *count) {
    if (!shop || !g_shop_stock) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = (int)shop->stock_count;
    return &g_shop_stock[shop->first_stock];
}

int rc_shop_find_by_name(const char *name) {
    if (!name || !g_shops) return -1;
    for (int i = 0; i < g_shop_count; i++) {
        if (strcmp(g_shops[i].name, name) == 0) return i;
    }
    return -1;
}

int rc_shop_has_item(int shop_idx, int item_id) {
    const RcShop *shop = rc_shop_get(shop_idx);
    int count = 0;
    const RcShopStock *rows = rc_shop_stock_rows(shop, &count);
    for (int i = 0; i < count; i++) {
        if (rows[i].item_id == item_id) return 1;
    }
    return 0;
}
